// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherSubsystem.h"
#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherCloudPaint.h"
#include "DreamWeatherMapRenderer.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSettings.h"
#include "DreamWeatherShelter.h"
#include "DreamWeatherZone.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Misc/App.h"
#include "TextureResource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherSubsystem)

DECLARE_CYCLE_STAT(TEXT("DreamWeather Update"), STAT_DreamWeatherUpdate, STATGROUP_Game);

namespace DreamWeather::Subsystem
{
	static const FName SnapshotKey(TEXT("DreamWeather"));
	/** Forced weather sits above ordinary modifiers. */
	constexpr int32 ForcedPriority = 1000;
}

UDreamWeatherSubsystem* UDreamWeatherSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
}

bool UDreamWeatherSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FallbackSelector = NewObject<UDreamWeatherSelector_Fixed>(this, TEXT("FallbackSelector"), RF_Transient);
	GlobalValues = FDreamWeatherChannels::Defaults();
	ViewSample.Values = GlobalValues;
	MapRenderer = MakeShared<FDreamWeatherMapRenderer, ESPMode::ThreadSafe>();

	if (UDreamWorldSubsystem* Clock = Collection.InitializeDependency<UDreamWorldSubsystem>())
	{
		ClockSubsystem = Clock;
		PreTickHandle = Clock->OnWorldPreTick.AddUObject(this, &UDreamWeatherSubsystem::PreTick);
		CaptureHandle = Clock->OnCaptureSnapshot.AddWeakLambda(this, [this](FDreamWorldSnapshot& Snapshot) { CaptureSnapshot(Snapshot); });
		ApplyHandle = Clock->OnApplySnapshot.AddWeakLambda(this, [this](const FDreamWorldSnapshot& Snapshot) { ApplySnapshot(Snapshot); });
	}
}

void UDreamWeatherSubsystem::Deinitialize()
{
	if (UDreamWorldSubsystem* Clock = ClockSubsystem.Get())
	{
		Clock->OnWorldPreTick.Remove(PreTickHandle);
		Clock->OnCaptureSnapshot.Remove(CaptureHandle);
		Clock->OnApplySnapshot.Remove(ApplyHandle);
	}
	WeatherActors.Reset();
	Influences.Reset();
	Modifiers.Reset();
	MapRenderer.Reset();
	Super::Deinitialize();
}

// -------------------------------------------------------------------------------------------------
// What runs
// -------------------------------------------------------------------------------------------------

void UDreamWeatherSubsystem::RegisterWeatherActor(ADreamWeather* Actor)
{
	WeatherActors.RemoveAll([](const TWeakObjectPtr<ADreamWeather>& A) { return !A.IsValid(); });
	if (Actor && !WeatherActors.Contains(Actor))
	{
		WeatherActors.Add(Actor);
		if (WeatherActors.Num() > 1)
		{
			UE_LOG(LogDreamWeather, Warning, TEXT("%s: more than one Dream Weather in the level; %s is in charge."), *GetNameSafe(GetWorld()), *GetNameSafe(WeatherActors[0].Get()));
		}
		bEventsPrimed = false;
	}
}

void UDreamWeatherSubsystem::UnregisterWeatherActor(ADreamWeather* Actor)
{
	WeatherActors.RemoveAll([Actor](const TWeakObjectPtr<ADreamWeather>& A) { return !A.IsValid() || A.Get() == Actor; });
	bEventsPrimed = false;
}

void UDreamWeatherSubsystem::RegisterInfluence(UObject* Influence)
{
	Influences.RemoveAll([](const TWeakObjectPtr<UObject>& I) { return !I.IsValid(); });
	if (Influence && Cast<IDreamWeatherInfluence>(Influence) && !Influences.Contains(Influence))
	{
		Influences.Add(Influence);
	}
}

void UDreamWeatherSubsystem::UnregisterInfluence(UObject* Influence)
{
	Influences.RemoveAll([Influence](const TWeakObjectPtr<UObject>& I) { return !I.IsValid() || I.Get() == Influence; });
}

void UDreamWeatherSubsystem::RegisterCloudPaint(UDreamCloudPaintComponent* Paint)
{
	CloudPaints.RemoveAll([Paint](const TWeakObjectPtr<UDreamCloudPaintComponent>& P) { return !P.IsValid() || P.Get() == Paint; });
	if (Paint)
	{
		CloudPaints.Add(Paint);
	}
	bMapRedrawPending = true;
}

void UDreamWeatherSubsystem::UnregisterCloudPaint(UDreamCloudPaintComponent* Paint)
{
	CloudPaints.RemoveAll([Paint](const TWeakObjectPtr<UDreamCloudPaintComponent>& P) { return !P.IsValid() || P.Get() == Paint; });
	bMapRedrawPending = true;
}

static TAutoConsoleVariable<int32> CVarDreamWeatherCloudPaint(
	TEXT("DreamWeather.CloudPaint"), 1,
	TEXT("1 = the weather uses the level's cloud painting (ADreamCloudPaint), 0 = it ignores it (to compare)."),
	ECVF_Default);

UDreamCloudPaintComponent* UDreamWeatherSubsystem::GetActiveCloudPaint() const
{
	if (CVarDreamWeatherCloudPaint.GetValueOnGameThread() == 0)
	{
		return nullptr;
	}
	for (int32 i = CloudPaints.Num() - 1; i >= 0; --i)
	{
		UDreamCloudPaintComponent* Paint = CloudPaints[i].Get();
		if (Paint && Paint->IsUsable())
		{
			return Paint;
		}
	}
	return nullptr;
}

void UDreamWeatherSubsystem::RegisterShelter(UObject* Shelter)
{
	Shelters.RemoveAll([](const TWeakObjectPtr<UObject>& S) { return !S.IsValid(); });
	if (Shelter && Cast<IDreamWeatherShelterProvider>(Shelter) && !Shelters.Contains(Shelter))
	{
		Shelters.Add(Shelter);
	}
}

void UDreamWeatherSubsystem::UnregisterShelter(UObject* Shelter)
{
	Shelters.RemoveAll([Shelter](const TWeakObjectPtr<UObject>& S) { return !S.IsValid() || S.Get() == Shelter; });
}

void UDreamWeatherSubsystem::RegisterLightningRod(UDreamLightningRodComponent* Rod)
{
	Rods.RemoveAll([](const TWeakObjectPtr<UDreamLightningRodComponent>& R) { return !R.IsValid(); });
	if (Rod && !Rods.Contains(Rod))
	{
		Rods.Add(Rod);
	}
}

void UDreamWeatherSubsystem::UnregisterLightningRod(UDreamLightningRodComponent* Rod)
{
	Rods.RemoveAll([Rod](const TWeakObjectPtr<UDreamLightningRodComponent>& R) { return !R.IsValid() || R.Get() == Rod; });
}

float UDreamWeatherSubsystem::ExposureAt(const FVector& Position) const
{
	float Shelter = 0.0f;
	for (const TWeakObjectPtr<UObject>& Weak : Shelters)
	{
		if (const IDreamWeatherShelterProvider* S = Cast<IDreamWeatherShelterProvider>(Weak.Get()))
		{
			Shelter = FMath::Max(Shelter, S->GetShelterAt(Position));
		}
	}

	// The wind's baked heights are the level's top surfaces: a roof above the point covers it, and with wind the
	// rain also comes in under the eaves on the windward side.
	float Open = 1.0f;
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	const UWorld* World = GetWorld();
	const UDreamWindSubsystem* Wind = World && S->bUseWindTerrainHeights ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	const FDreamWindTerrainCpuData* Terrain = Wind ? Wind->GetFrameData().TerrainCpu.Get() : nullptr;
	// The runtime terrain map's CPU grid where it has swept (its heights are the level as it stands now), else the baked map.
	const FDreamWindTerrainGridData* Grid = Wind ? Wind->GetFrameData().TerrainGrid.Get() : nullptr;
	const auto GridHeight = [Grid](double X, double Y)
	{
		return Grid ? Grid->HeightAt(X, Y) : FDreamWindTerrainGridData::NotSwept;
	};
	const auto Known = [](float Height) { return Height > FDreamWindTerrainGridData::NotSwept * 0.75f; };
	if (Known(GridHeight(Position.X, Position.Y)) || (Terrain && Terrain->IsInside(Position.X, Position.Y)))
	{
		const auto CoveredAt = [Terrain, &Position, S, &GridHeight, &Known](double X, double Y)
		{
			const float FromGrid = GridHeight(X, Y);
			if (Known(FromGrid))
			{
				return double(FromGrid) > Position.Z + S->RoofClearance;
			}
			return Terrain && Terrain->IsInside(X, Y) && double(Terrain->HeightBase) + Terrain->HeightBilinear(X, Y, 0) > Position.Z + S->RoofClearance;
		};
		if (CoveredAt(Position.X, Position.Y))
		{
			Open = 0.0f;
		}
		else
		{
			// Where the rain hitting this point came from three metres up.
			const FVector WindVelocity = Wind->SampleWindVelocity(Position);
			const FVector2D Slant = -FVector2D(WindVelocity.X, WindVelocity.Y) / FMath::Max(100.0f, S->RainFallSpeed) * 300.0;
			if (CoveredAt(Position.X + Slant.X, Position.Y + Slant.Y))
			{
				Open = 0.4f;
			}
		}
	}
	return (1.0f - Shelter) * Open;
}

float UDreamWeatherSubsystem::GetExposureAt(FVector Position) const
{
	return ExposureAt(Position);
}

bool UDreamWeatherSubsystem::IsActive() const
{
	for (const TWeakObjectPtr<ADreamWeather>& Actor : WeatherActors)
	{
		if (Actor.IsValid())
		{
			return true;
		}
	}
	return UDreamWeatherSettings::Get()->bRunWithoutWeatherActor;
}

const UDreamWeatherSelector* UDreamWeatherSubsystem::GetSelector() const
{
	const UWorld* World = GetWorld();
	for (const TWeakObjectPtr<ADreamWeather>& Weak : WeatherActors)
	{
		const ADreamWeather* Actor = Weak.Get();
		if (!Actor)
		{
			continue;
		}
		if (World && !World->IsGameWorld() && Actor->EditorPreview && FallbackSelector)
		{
			FallbackSelector->Preset = Actor->EditorPreview;
			return FallbackSelector;
		}
		if (Actor->Selector)
		{
			return Actor->Selector;
		}
	}
	if (UDreamWeatherSettings::Get()->bRunWithoutWeatherActor && FallbackSelector)
	{
		FallbackSelector->Preset = DefaultPreset;
		return FallbackSelector;
	}
	return nullptr;
}

double UDreamWeatherSubsystem::NowSeconds() const
{
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	return Clock ? Clock->GetWorldSeconds() : 0.0;
}

int32 UDreamWeatherSubsystem::Seed() const
{
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	return Clock ? Clock->GetSeed() : 0;
}

// -------------------------------------------------------------------------------------------------
// Evaluation
// -------------------------------------------------------------------------------------------------

FDreamWeatherValues UDreamWeatherSubsystem::EvaluateSchedule(double WorldSeconds) const
{
	FDreamWeatherValues Out = FDreamWeatherChannels::Defaults();
	const UDreamWeatherSelector* Selector = GetSelector();
	if (!Selector)
	{
		return Out;
	}
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	const int32 WorldSeed = Seed();
	for (int32 i = 0; i < Defs.Num(); ++i)
	{
		const FDreamWeatherChannelDef& Def = Defs[i];
		// A channel with a lead lives that much ahead of the clock: it is already easing toward the next weather.
		const double T = WorldSeconds + double(Def.LeadMinutes) * 60.0;
		const FDreamWeatherSlot Slot = Selector->GetSlot(T, WorldSeed);
		const float From = Slot.Previous ? Slot.Previous->GetValue(i) : Def.Default;
		const float To = Slot.Current ? Slot.Current->GetValue(i) : Def.Default;
		const double Duration = FMath::Min(double(Def.TransitionMinutes) * 60.0, Slot.Length);
		const double Alpha = Duration > 0.0 ? FMath::Clamp((T - Slot.Start) / Duration, 0.0, 1.0) : 1.0;
		Out.Values[i] = FMath::Lerp(From, To, FMath::SmoothStep(0.0f, 1.0f, float(Alpha)));
	}
	return Out;
}

void UDreamWeatherSubsystem::AdvanceModifiers(float DeltaTime)
{
	for (FModifier& M : Modifiers)
	{
		const float Rate = M.FadeSeconds > 0.0f ? FMath::Max(M.Desc.Weight, 0.001f) / M.FadeSeconds : 1.0e9f;
		M.Current = FMath::FInterpConstantTo(M.Current, M.Target, FMath::Max(DeltaTime, 0.0f), Rate);
	}
	// A forced weather being replaced goes once its replacement is fully in; if the replacement is taken away
	// first, it fades out with it.
	for (FModifier& M : Modifiers)
	{
		if (M.SupersededBy == 0)
		{
			continue;
		}
		const FModifier* By = Modifiers.FindByPredicate([&M](const FModifier& Other) { return Other.Id == M.SupersededBy; });
		if (!By || By->bRemoving)
		{
			M.SupersededBy = 0;
			M.bRemoving = true;
			M.Target = 0.0f;
			M.FadeSeconds = By ? By->FadeSeconds : 0.0f;
		}
		else if (By->Current >= By->Target - 1.0e-4f)
		{
			M.SupersededBy = 0;
			M.bRemoving = true;
			M.Target = 0.0f;
			M.Current = 0.0f;
		}
	}
	Modifiers.RemoveAll([](const FModifier& M) { return M.bRemoving && M.Current <= 0.0f; });
}

void UDreamWeatherSubsystem::ApplyModifiers(FDreamWeatherValues& InOut) const
{
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	// Modifiers are kept sorted by priority, then by the order they were pushed.
	for (const FModifier& M : Modifiers)
	{
		if (M.Current <= 0.0f)
		{
			continue;
		}
		for (int32 i = 0; i < InOut.Values.Num() && i < Defs.Num(); ++i)
		{
			const float* Own = M.Desc.Channels.Find(Defs[i].Channel);
			if (!Own && !M.Desc.Preset)
			{
				continue;
			}
			const float Value = Own ? *Own : M.Desc.Preset->GetValue(i);
			InOut.Values[i] = FMath::Clamp(FDreamWeatherChannels::Combine(M.Desc.Blend, InOut.Values[i], Value, M.Current), Defs[i].Min, Defs[i].Max);
		}
	}
}

TArray<IDreamWeatherInfluence*> UDreamWeatherSubsystem::SortedInfluences() const
{
	TArray<IDreamWeatherInfluence*> Out;
	for (const TWeakObjectPtr<UObject>& Weak : Influences)
	{
		if (IDreamWeatherInfluence* Influence = Cast<IDreamWeatherInfluence>(Weak.Get()))
		{
			Out.Add(Influence);
		}
	}
	// Lowest priority first, so the highest applies last and wins; equal priorities by a stable id, never by
	// registration order (which differs between runs).
	Out.StableSort([](const IDreamWeatherInfluence& A, const IDreamWeatherInfluence& B)
	{
		const int32 PA = A.GetWeatherInfluencePriority();
		const int32 PB = B.GetWeatherInfluencePriority();
		return PA != PB ? PA < PB : A.GetWeatherInfluenceStableId() < B.GetWeatherInfluenceStableId();
	});
	return Out;
}

void UDreamWeatherSubsystem::ApplyInfluences(const FVector& Position, FDreamWeatherValues& InOut, double WorldSeconds) const
{
	for (IDreamWeatherInfluence* Influence : SortedInfluences())
	{
		Influence->ApplyWeatherInfluenceAt(Position, WorldSeconds, InOut);
	}
	// The cloud painting over the zones (the map draws it after them too).
	if (const UDreamCloudPaintComponent* Paint = GetActiveCloudPaint())
	{
		const int32 Cover = FDreamWeatherChannels::CloudCoverage;
		if (InOut.Values.IsValidIndex(Cover))
		{
			Paint->ApplyTo(Position, InOut.Values[Cover]);
		}
	}
}

float UDreamWeatherSubsystem::TemperatureFor(const FVector& Position, float Offset, float Coverage, double Seconds) const
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	const FDateTime Date = DreamWorld::Celestial::ToDateTime(Seconds);
	const double Hours = DreamWorld::Celestial::HoursOfDay(Seconds);
	const bool bSouth = Clock && Clock->GetCelestialSettings().Mode == EDreamCelestialMode::Realistic && Clock->GetCelestialSettings().Latitude < 0.0;
	// Coldest in mid-January (mid-July in the south); warmest at WarmestHour, less of a swing under cloud.
	const double Seasonal = -FMath::Cos(2.0 * UE_DOUBLE_PI * (Date.GetDayOfYear() - 15) / 365.25) * (bSouth ? -1.0 : 1.0);
	const double Diurnal = FMath::Cos(2.0 * UE_DOUBLE_PI * (Hours - S->WarmestHour) / 24.0);
	const double Height = (Position.Z - S->BaseHeight) / 10000.0;
	// A climate selector brings its own means and swings.
	float Mean = S->MeanTemperature, SeasonalAmplitude = S->SeasonalAmplitude, DiurnalAmplitude = S->DiurnalAmplitude;
	if (const UDreamWeatherSelector* Selector = GetSelector())
	{
		Selector->GetClimateTemperature(Mean, SeasonalAmplitude, DiurnalAmplitude);
	}
	return float(Mean + SeasonalAmplitude * Seasonal + DiurnalAmplitude * Diurnal * (1.0 - 0.6 * Coverage)
		- S->LapseRatePer100m * Height + Offset);
}

FDreamWeatherSample UDreamWeatherSubsystem::MakeSample(const FVector& Position, const FDreamWeatherValues& Values, double WorldSeconds) const
{
	using C = FDreamWeatherChannels;
	using namespace DreamWeather::Tags;
	FDreamWeatherSample S;
	S.Values = Values;
	S.CloudCoverage = Values.Get(C::CloudCoverage);
	S.CloudDarkness = Values.Get(C::CloudDarkness);
	S.Precipitation = Values.Get(C::Precipitation);
	S.Fog = Values.Get(C::Fog);
	S.Dust = Values.Get(C::Dust);
	S.Wind = Values.Get(C::Wind);
	S.Lightning = Values.Get(C::Lightning);
	S.Wetness = Values.Get(C::Wetness);
	S.SnowCover = Values.Get(C::SnowCover);
	S.Temperature = TemperatureFor(Position, Values.Get(C::Temperature), S.CloudCoverage, WorldSeconds);
	// What falls turns to snow near freezing, whatever the weather says (a mountain top snows in the rain), all but the
	// supercooled part of the rain: freezing rain stays rain below freezing.
	S.FreezingRain = FMath::Clamp(Values.Get(C::FreezingRain), 0.0f, 1.0f);
	S.SnowRatio = FMath::Max(Values.Get(C::SnowRatio), (1.0f - FMath::SmoothStep(-1.0f, 2.0f, S.Temperature)) * (1.0f - S.FreezingRain));
	// The precipitation's kinds: its hail share first (at any temperature), the sleet's of what is left, the rest rain
	// and snow by the snow ratio.
	const float HailShare = FMath::Clamp(Values.Get(C::Hail), 0.0f, 1.0f);
	const float SleetShare = FMath::Clamp(Values.Get(C::Sleet), 0.0f, 1.0f) * (1.0f - HailShare);
	const float Left = 1.0f - HailShare - SleetShare;
	S.Hail = S.Precipitation * HailShare;
	S.Sleet = S.Precipitation * SleetShare;
	S.Rain = S.Precipitation * Left * (1.0f - S.SnowRatio);
	S.Snow = S.Precipitation * Left * S.SnowRatio;
	S.Exposure = ExposureAt(Position);
	S.bSheltered = S.Exposure < 0.5f;
	S.RainHere = S.Rain * S.Exposure;
	S.SnowHere = S.Snow * S.Exposure;
	S.HailHere = S.Hail * S.Exposure;
	S.SleetHere = S.Sleet * S.Exposure;

	if (S.CloudCoverage < 0.3f)
	{
		S.Tags.AddTag(State_Clear);
	}
	else if (S.CloudCoverage < 0.75f)
	{
		S.Tags.AddTag(State_Cloudy);
	}
	else
	{
		S.Tags.AddTag(State_Overcast);
	}
	if (S.Rain > 0.1f)
	{
		S.Tags.AddTag(S.Rain > 0.6f ? State_Rain_Heavy : State_Rain);
	}
	if (S.Snow > 0.1f)
	{
		S.Tags.AddTag(S.Snow > 0.6f ? State_Snow_Heavy : State_Snow);
	}
	if (S.Hail > 0.05f)
	{
		S.Tags.AddTag(State_Hail);
	}
	if (S.Sleet > 0.1f)
	{
		S.Tags.AddTag(State_Sleet);
	}
	if (S.Lightning > 0.2f)
	{
		S.Tags.AddTag(State_Storm);
	}
	if (S.Fog > 0.4f)
	{
		S.Tags.AddTag(State_Fog);
	}
	if (S.Dust > 0.4f)
	{
		S.Tags.AddTag(State_Dust);
	}
	return S;
}

FDreamWeatherSample UDreamWeatherSubsystem::SampleWeather(FVector Position) const
{
	return SampleWeatherAt(Position, GlobalValues, NowSeconds());
}

FDreamWeatherSample UDreamWeatherSubsystem::SampleWeatherAt(FVector Position, const FDreamWeatherValues& Global, double WorldSeconds) const
{
	FDreamWeatherValues Values = Global;
	if (Values.Values.Num() != FDreamWeatherChannels::Num())
	{
		Values = FDreamWeatherChannels::Defaults();
	}
	ApplyInfluences(Position, Values, WorldSeconds);
	FDreamWeatherChannels::ApplyCloudGates(Values);
	return MakeSample(Position, Values, WorldSeconds);
}

float UDreamWeatherSubsystem::GetChannelAt(FVector Position, FGameplayTag Channel) const
{
	const int32 Index = FDreamWeatherChannels::IndexOf(Channel);
	if (Index == INDEX_NONE)
	{
		return 0.0f;
	}
	FDreamWeatherValues Values = GlobalValues;
	ApplyInfluences(Position, Values, NowSeconds());
	FDreamWeatherChannels::ApplyCloudGates(Values);
	return Values.Get(Index);
}

float UDreamWeatherSubsystem::GetTemperatureAt(FVector Position) const
{
	return SampleWeather(Position).Temperature;
}

FText UDreamWeatherSubsystem::GetCurrentWeatherDisplayName() const
{
	const UDreamWeatherPreset* Preset = GetCurrentWeather();
	if (!Preset)
	{
		return FText::GetEmpty();
	}
	return Preset->DisplayName.IsEmpty() ? FText::FromName(Preset->GetFName()) : Preset->DisplayName;
}

UDreamWeatherPreset* UDreamWeatherSubsystem::GetForcedWeather() const
{
	if (ForcedModifierId == 0)
	{
		return nullptr;
	}
	for (const FModifier& M : Modifiers)
	{
		if (M.Id == ForcedModifierId && !M.bRemoving)
		{
			return M.Desc.Preset;
		}
	}
	return nullptr;
}

UDreamWeatherPreset* UDreamWeatherSubsystem::GetWeatherShown() const
{
	UDreamWeatherPreset* Forced = GetForcedWeather();
	return Forced ? Forced : GetCurrentWeather();
}

UDreamWeatherPreset* UDreamWeatherSubsystem::GetCurrentWeather() const
{
	const UDreamWeatherSelector* Selector = GetSelector();
	return Selector ? const_cast<UDreamWeatherPreset*>(Selector->GetSlot(NowSeconds(), Seed()).Current) : nullptr;
}

TArray<FDreamWeatherForecastEntry> UDreamWeatherSubsystem::GetForecast(float Hours, float StepHours) const
{
	TArray<FDreamWeatherForecastEntry> Out;
	const UDreamWeatherSelector* Selector = GetSelector();
	if (!Selector)
	{
		return Out;
	}
	const double Start = NowSeconds();
	const double Step = FMath::Max(0.25, double(StepHours)) * 3600.0;
	for (double T = Start; T <= Start + double(Hours) * 3600.0 + 1.0; T += Step)
	{
		FDreamWeatherForecastEntry& E = Out.AddDefaulted_GetRef();
		E.Time = DreamWorld::Celestial::ToDateTime(T);
		E.Preset = const_cast<UDreamWeatherPreset*>(Selector->GetSlot(T, Seed()).Current);
	}
	return Out;
}

// -------------------------------------------------------------------------------------------------
// Modifiers
// -------------------------------------------------------------------------------------------------

FDreamWeatherModifierHandle UDreamWeatherSubsystem::PushModifier(const FDreamWeatherModifierDesc& Desc)
{
	FModifier M;
	M.Id = NextModifierId++;
	M.Desc = Desc;
	M.Desc.Weight = FMath::Clamp(Desc.Weight, 0.0f, 1.0f);
	M.Target = M.Desc.Weight;
	M.FadeSeconds = FMath::Max(0.0f, Desc.BlendInSeconds);
	M.Current = M.FadeSeconds > 0.0f ? 0.0f : M.Target;
	// Sorted insert: by priority, then by id (push order).
	int32 Index = Modifiers.Num();
	while (Index > 0 && Modifiers[Index - 1].Desc.Priority > M.Desc.Priority)
	{
		--Index;
	}
	Modifiers.Insert(M, Index);
	FDreamWeatherModifierHandle Handle;
	Handle.Id = M.Id;
	return Handle;
}

void UDreamWeatherSubsystem::RemoveModifier(FDreamWeatherModifierHandle Handle, float BlendOutSeconds)
{
	for (FModifier& M : Modifiers)
	{
		if (M.Id == Handle.Id)
		{
			M.bRemoving = true;
			M.Target = 0.0f;
			M.FadeSeconds = FMath::Max(0.0f, BlendOutSeconds);
		}
	}
	AdvanceModifiers(0.0f);
}

void UDreamWeatherSubsystem::SetModifierWeight(FDreamWeatherModifierHandle Handle, float Weight, float Seconds)
{
	for (FModifier& M : Modifiers)
	{
		if (M.Id == Handle.Id && !M.bRemoving)
		{
			M.Desc.Weight = FMath::Clamp(Weight, 0.0f, 1.0f);
			M.Target = M.Desc.Weight;
			M.FadeSeconds = FMath::Max(0.0f, Seconds);
		}
	}
}

bool UDreamWeatherSubsystem::UpdateModifier(FDreamWeatherModifierHandle Handle, const FDreamWeatherModifierDesc& Desc)
{
	const int32 Index = Modifiers.IndexOfByPredicate([&Handle](const FModifier& M) { return M.Id == Handle.Id && !M.bRemoving; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	FModifier M = Modifiers[Index];
	const float Weight = M.Desc.Weight;
	const float BlendIn = M.Desc.BlendInSeconds;
	M.Desc = Desc;
	M.Desc.Weight = Weight;
	M.Desc.BlendInSeconds = BlendIn;
	if (M.Desc.Priority == Modifiers[Index].Desc.Priority)
	{
		Modifiers[Index] = MoveTemp(M);
		return true;
	}
	// A new priority: back into the sorted order (after the others of its priority, by id).
	Modifiers.RemoveAt(Index);
	int32 Insert = Modifiers.Num();
	while (Insert > 0 && (Modifiers[Insert - 1].Desc.Priority > M.Desc.Priority
		|| (Modifiers[Insert - 1].Desc.Priority == M.Desc.Priority && Modifiers[Insert - 1].Id > M.Id)))
	{
		--Insert;
	}
	Modifiers.Insert(MoveTemp(M), Insert);
	return true;
}

bool UDreamWeatherSubsystem::HasModifier(FDreamWeatherModifierHandle Handle) const
{
	return Modifiers.ContainsByPredicate([&Handle](const FModifier& M) { return M.Id == Handle.Id && !M.bRemoving; });
}

void UDreamWeatherSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);
	// (The modifiers are not reflected: a preset only a modifier holds, as DreamWeather.Set loads one, would be collected
	// under it.)
	for (FModifier& M : CastChecked<UDreamWeatherSubsystem>(InThis)->Modifiers)
	{
		Collector.AddReferencedObject(M.Desc.Preset);
	}
}

FDreamWeatherModifierHandle UDreamWeatherSubsystem::ForceWeather(UDreamWeatherPreset* Preset, float TransitionSeconds)
{
	// Fading the old forcing out while the new one fades in would let the schedule show through halfway (a scheduled
	// storm raining while forced clear turns to forced overcast): the old one holds until the new one is in.
	const int32 Previous = ForcedModifierId;
	const bool bHandOver = Previous != 0 && TransitionSeconds > 0.0f;
	if (!bHandOver)
	{
		ClearForcedWeather(TransitionSeconds);
	}
	FDreamWeatherModifierDesc Desc;
	Desc.Source = TEXT("ForceWeather");
	Desc.Preset = Preset;
	Desc.Blend = EDreamWeatherBlend::Override;
	Desc.Priority = DreamWeather::Subsystem::ForcedPriority;
	Desc.BlendInSeconds = TransitionSeconds;
	const FDreamWeatherModifierHandle Handle = PushModifier(Desc);
	if (bHandOver)
	{
		for (FModifier& M : Modifiers)
		{
			if (M.Id == Previous && !M.bRemoving)
			{
				M.SupersededBy = Handle.Id;
			}
		}
	}
	ForcedModifierId = Handle.Id;
	return Handle;
}

void UDreamWeatherSubsystem::ClearForcedWeather(float TransitionSeconds)
{
	if (ForcedModifierId != 0)
	{
		FDreamWeatherModifierHandle Handle;
		Handle.Id = ForcedModifierId;
		RemoveModifier(Handle, TransitionSeconds);
		ForcedModifierId = 0;
	}
}

// -------------------------------------------------------------------------------------------------
// Frame
// -------------------------------------------------------------------------------------------------

void UDreamWeatherSubsystem::PreTick(UDreamWorldSubsystem& Clock, float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWeatherUpdate);
	const bool bActive = IsActive();
	if (!bActive)
	{
		if (bWasActive)
		{
			// Hand the sky back to its own style and the materials a neutral weather.
			Clock.SetEnvironment(FDreamWorldEnvironment());
			Clock.SetVectorParameter(DreamWeather::Names::MPC_WeatherMap, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
			OnWeatherStopped.Broadcast();
		}
		bWasActive = false;
		bEventsPrimed = false;
		return;
	}
	bWasActive = true;

	if (!DefaultPreset && UDreamWeatherSettings::Get()->bRunWithoutWeatherActor)
	{
		DefaultPreset = Cast<UDreamWeatherPreset>(UDreamWeatherSettings::Get()->DefaultPreset.TryLoad());
	}

	AdvanceModifiers(DeltaTime);
	GlobalValues = EvaluateSchedule(Clock.GetWorldSeconds());
	ApplyModifiers(GlobalValues);

	ViewLocation = Clock.HasViewLocation() ? Clock.GetViewLocation() : FVector::ZeroVector;
	ViewForward = Clock.HasViewRotation() ? Clock.GetViewRotation().Vector() : FVector::ZeroVector;
	ViewSample = SampleWeather(ViewLocation);

	ProcessLightning(DeltaTime);

	FDreamWorldEnvironment Env;
	Env.bActive = true;
	Env.LightningFlash = Flash;
	Env.LightningDirection = FlashDirection;
	Env.CloudCoverage = ViewSample.CloudCoverage;
	Env.CloudDarkness = ViewSample.CloudDarkness;
	Env.Precipitation = ViewSample.Precipitation;
	Env.SnowRatio = ViewSample.SnowRatio;
	Env.Fog = ViewSample.Fog;
	Env.Dust = ViewSample.Dust;
	Env.Wind = ViewSample.Wind;
	Env.Lightning = ViewSample.Lightning;
	Env.Temperature = ViewSample.Temperature;
	GatherSkyModifiers(Env.SkyModifiers);
	Clock.SetEnvironment(Env);

	using namespace DreamWeather::Names;
	Clock.SetVectorParameter(MPC_Weather0, FLinearColor(ViewSample.CloudCoverage, ViewSample.CloudDarkness, ViewSample.Precipitation, ViewSample.SnowRatio));
	Clock.SetVectorParameter(MPC_Weather1, FLinearColor(ViewSample.Fog, ViewSample.Dust, ViewSample.Wind, ViewSample.Lightning));
	Clock.SetVectorParameter(MPC_Flash, FLinearColor(float(FlashDirection.X), float(FlashDirection.Y), float(FlashDirection.Z), Flash));

	UpdateWind(ViewSample);
	UpdateWeatherMap(Clock);
	OnWeatherFrame.Broadcast(Clock, *this, ViewSample, DeltaTime);
	FireEvents(ViewSample);
}

void UDreamWeatherSubsystem::GatherSkyModifiers(TArray<FDreamWorldWeightedObject>& Out) const
{
	Out.Reset();
	TArray<TPair<const UDreamWeatherPreset*, float>, TInlineAllocator<8>> Weights;
	auto Layer = [&Weights](const UDreamWeatherPreset* Preset, EDreamWeatherBlend Blend, float Weight)
	{
		Weight = FMath::Clamp(Weight, 0.0f, 1.0f);
		if (!Preset || Weight <= 0.0f)
		{
			return;
		}
		if (Blend == EDreamWeatherBlend::Override)
		{
			for (TPair<const UDreamWeatherPreset*, float>& Under : Weights)
			{
				Under.Value *= 1.0f - Weight;
			}
		}
		TPair<const UDreamWeatherPreset*, float>* Same = Weights.FindByPredicate([Preset](const TPair<const UDreamWeatherPreset*, float>& P) { return P.Key == Preset; });
		if (Same)
		{
			Same->Value = FMath::Min(Same->Value + Weight, 1.0f);
		}
		else
		{
			Weights.Emplace(Preset, Weight);
		}
	};

	// The schedule, as the cloud cover eases across its slots (the channel the sky shows most).
	if (const UDreamWeatherSelector* Selector = GetSelector())
	{
		const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
		const FDreamWeatherChannelDef* Def = Defs.IsValidIndex(FDreamWeatherChannels::CloudCoverage) ? &Defs[FDreamWeatherChannels::CloudCoverage] : nullptr;
		const double T = NowSeconds() + (Def ? double(Def->LeadMinutes) * 60.0 : 0.0);
		const FDreamWeatherSlot Slot = Selector->GetSlot(T, Seed());
		const double Duration = FMath::Min(Def ? double(Def->TransitionMinutes) * 60.0 : 0.0, Slot.Length);
		const double Alpha = Duration > 0.0 ? FMath::Clamp((T - Slot.Start) / Duration, 0.0, 1.0) : 1.0;
		Layer(Slot.Previous, EDreamWeatherBlend::Override, 1.0f);
		Layer(Slot.Current, EDreamWeatherBlend::Override, FMath::SmoothStep(0.0f, 1.0f, float(Alpha)));
	}
	// Forced weather and pushed presets, in the order they apply (a pushed set of channels brings no preset).
	for (const FModifier& M : Modifiers)
	{
		Layer(M.Desc.Preset, M.Desc.Blend, M.Current);
	}
	// The zones (and any influence that brings a preset) at the view.
	for (IDreamWeatherInfluence* Influence : SortedInfluences())
	{
		float Weight = 0.0f;
		EDreamWeatherBlend Blend = EDreamWeatherBlend::Override;
		if (const UDreamWeatherPreset* Preset = Influence->GetWeatherInfluencePreset(ViewLocation, Weight, Blend))
		{
			Layer(Preset, Blend, Weight);
		}
	}

	// (The presets hold their modifiers; the sky gets weak pointers to them.)
	for (const TPair<const UDreamWeatherPreset*, float>& P : Weights)
	{
		UObject* Modifier = P.Key->SkyModifier;
		if (!Modifier || P.Value <= 1.0e-4f)
		{
			continue;
		}
		FDreamWorldWeightedObject* Same = Out.FindByPredicate([Modifier](const FDreamWorldWeightedObject& E) { return E.Object.Get() == Modifier; });
		if (Same)
		{
			Same->Weight = FMath::Min(Same->Weight + P.Value, 1.0f);
		}
		else
		{
			FDreamWorldWeightedObject& Added = Out.AddDefaulted_GetRef();
			Added.Object = Modifier;
			Added.Weight = P.Value;
		}
	}
}

void UDreamWeatherSubsystem::UpdateWind(const FDreamWeatherSample& View)
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	UWorld* World = GetWorld();
	UDreamWindSubsystem* Wind = World && S->bDriveWind ? World->GetSubsystem<UDreamWindSubsystem>() : nullptr;
	if (!Wind)
	{
		return;
	}
	const float Speed = FMath::Lerp(float(S->WindSpeedRange.X), float(S->WindSpeedRange.Y), View.Wind);
	const float Gust = FMath::Lerp(float(S->WindGustRange.X), float(S->WindGustRange.Y), View.Wind);
	if (FMath::Abs(Speed - LastWindSpeed) > FMath::Max(1.0f, 0.01f * Speed) || FMath::Abs(Gust - LastWindGust) > 0.01f)
	{
		FDreamWindGlobalWind Global = Wind->GetGlobalWind();
		Global.Speed = Speed;
		Global.GustStrength = Gust;
		Wind->SetGlobalWind(Global, 0.0f);
		LastWindSpeed = Speed;
		LastWindGust = Gust;
	}
}

float UDreamWeatherSubsystem::FlashEnvelope(float Seconds, float Duration, int32 Strokes, const float* StrokeTimes, const float* StrokeLevels)
{
	if (Seconds < 0.0f || Seconds >= Duration || Duration <= 0.0f)
	{
		return 0.0f;
	}
	// Each stroke is a sharp spike that dies in a tenth of a second; under them, an afterglow fades over the
	// whole flash (the lit cloud).
	constexpr float StrokeDecay = 0.07f;
	float Level = 0.25f * FMath::Square(1.0f - Seconds / Duration);
	for (int32 i = 0; i < FMath::Min(Strokes, 4); ++i)
	{
		const float Since = Seconds - StrokeTimes[i];
		if (Since >= 0.0f)
		{
			Level = FMath::Max(Level, StrokeLevels[i] * FMath::Exp(-Since / StrokeDecay));
		}
	}
	return FMath::Clamp(Level, 0.0f, 1.0f);
}

void UDreamWeatherSubsystem::ProcessLightning(float DeltaTime)
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();

	// Strikes are drawn in quarter-second buckets of real time from the world seed: the same seed and the same
	// frames give the same storm. A pause or a long hitch does not unload a backlog of strikes at once.
	constexpr double Bucket = 0.25;
	LightningClock += FMath::Max(DeltaTime, 0.0f);
	auto UpdateFlash = [this] { UpdateFlashNow(); };
	const int64 Now = int64(FMath::FloorToDouble(LightningClock / Bucket));
	int64 First = LastLightningBucket + 1;
	if (LastLightningBucket < 0 || Now - LastLightningBucket > 40)
	{
		First = Now;
	}
	LastLightningBucket = Now;
	if (S->StrikesPerMinute <= 0.0f)
	{
		UpdateFlash();
		return;
	}

	// The storm may be off to one side: the strongest lightning around the view decides how often it strikes.
	using C = FDreamWeatherChannels;
	float Regional = ViewSample.Lightning;
	for (int32 i = 0; i < 8; ++i)
	{
		const double Angle = UE_DOUBLE_PI * 0.25 * i;
		const FVector Around = ViewLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * double(S->StrikeRange.Y) * 0.5;
		Regional = FMath::Max(Regional, GetChannelAt(Around, DreamWeather::Tags::Channel_Lightning) * 0.9f);
	}
	if (Regional <= 0.001f)
	{
		UpdateFlash();
		return;
	}

	const int32 WorldSeed = Seed() ^ 0x5bd1e995;
	for (int64 B = First; B <= Now; ++B)
	{
		const float Chance = Regional * S->StrikesPerMinute / 60.0f * float(Bucket);
		if (UDreamWeatherSelector::Hash01(WorldSeed, B, 11u) >= Chance)
		{
			continue;
		}
		// Somewhere around the view where the lightning is strong (a few tries; the storm wins), more often in
		// the half of the sky the view faces.
		const FVector2D Facing = FVector2D(ViewForward.X, ViewForward.Y).GetSafeNormal();
		const bool bInFront = !Facing.IsZero() && UDreamWeatherSelector::Hash01(WorldSeed, B, 70u) < S->StrikeViewBias;
		const double FacingAngle = FMath::Atan2(Facing.Y, Facing.X);
		bool bPlaced = false;
		FVector Location = ViewLocation;
		for (uint32 Try = 0; Try < 4 && !bPlaced; ++Try)
		{
			const double Turn = UDreamWeatherSelector::Hash01(WorldSeed, B, 20u + Try);
			const double Angle = bInFront ? FacingAngle + (Turn - 0.5) * UE_DOUBLE_PI : 2.0 * UE_DOUBLE_PI * Turn;
			const double Reach = FMath::Lerp(S->StrikeRange.X, S->StrikeRange.Y, FMath::Sqrt(double(UDreamWeatherSelector::Hash01(WorldSeed, B, 30u + Try))));
			const FVector Candidate = ViewLocation + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * Reach;
			const float Local = GetChannelAt(Candidate, DreamWeather::Tags::Channel_Lightning);
			if (UDreamWeatherSelector::Hash01(WorldSeed, B, 40u + Try) < Local / Regional)
			{
				Location = Candidate;
				bPlaced = true;
			}
		}
		if (!bPlaced)
		{
			continue;
		}
		StrikeAt(Location, FMath::Lerp(0.6f, 1.0f, UDreamWeatherSelector::Hash01(WorldSeed, B, 60u)), WorldSeed, B);
	}
	UpdateFlashNow();
}

void UDreamWeatherSubsystem::UpdateFlashNow()
{
	Flash = FlashPeak * FlashEnvelope(float(LightningClock - FlashStart), FlashLength, FlashStrokeCount, FlashStrokeTimes, FlashStrokeLevels);
}

void UDreamWeatherSubsystem::StrikeLightning(FVector Location, float Intensity)
{
	// Scripted strikes draw their flash shapes from their own sequence, apart from the storm's.
	StrikeAt(Location, FMath::Clamp(Intensity, 0.0f, 1.0f), Seed() ^ 0x2c1b3c6d, (int64(1) << 40) + int64(++ScriptedStrikes));
}

void UDreamWeatherSubsystem::StrikeAt(const FVector& Location, float Intensity, int32 WorldSeed, int64 Key)
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	FDreamLightningStrike Strike;
	Strike.Location = Location;
	Strike.WorldSeconds = NowSeconds();
	// A rod within reach may draw it (the nearest that does).
	UDreamLightningRodComponent* Struck = nullptr;
	double Nearest = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<UDreamLightningRodComponent>& Weak : Rods)
	{
		UDreamLightningRodComponent* Rod = Weak.Get();
		if (!Rod)
		{
			continue;
		}
		const double D = FVector::Dist2D(Rod->GetComponentLocation(), Location);
		if (D <= Rod->AttractRadius && D < Nearest && UDreamWeatherSelector::Hash01(WorldSeed, Key, 50u) < Rod->AttractChance)
		{
			Nearest = D;
			Struck = Rod;
		}
	}
	if (Struck)
	{
		Strike.Location = Struck->GetComponentLocation();
		Strike.Target = Struck;
	}
	Strike.Intensity = Intensity;
	Strike.Distance = float(FVector::Dist2D(Strike.Location, ViewLocation));
	Strike.ThunderDelay = Strike.Distance / 100000.0f * S->ThunderSecondsPerKm;

	// The flash lights the clouds over the strike; nearer strikes flash brighter. A stronger strike takes over
	// from a flash still in progress.
	const float Nearness = 1.0f - 0.7f * FMath::Clamp(Strike.Distance / FMath::Max(float(S->StrikeRange.Y), 1.0f), 0.0f, 1.0f);
	UpdateFlashNow();
	if (Strike.Intensity * Nearness >= Flash)
	{
		FlashStart = LightningClock;
		FlashPeak = Strike.Intensity * Nearness;
		FlashDirection = (Strike.Location + FVector(0.0, 0.0, 150000.0) - ViewLocation).GetSafeNormal();
		FlashLength = float(FMath::Lerp(S->FlashDuration.X, S->FlashDuration.Y, double(UDreamWeatherSelector::Hash01(WorldSeed, Key, 70u))));
		const int32 MinStrokes = FMath::Clamp(S->FlashStrokes.X, 1, 4);
		const int32 MaxStrokes = FMath::Clamp(S->FlashStrokes.Y, MinStrokes, 4);
		FlashStrokeCount = MinStrokes + FMath::Min(MaxStrokes - MinStrokes, int32(UDreamWeatherSelector::Hash01(WorldSeed, Key, 71u) * float(MaxStrokes - MinStrokes + 1)));
		// The first stroke is the brightest; the rest stutter after it within the first half of the flash.
		float At = 0.0f;
		for (int32 k = 0; k < FlashStrokeCount; ++k)
		{
			if (k > 0)
			{
				At = FMath::Min(At + FMath::Lerp(0.06f, 0.22f, UDreamWeatherSelector::Hash01(WorldSeed, Key, 80u + uint32(k))), FlashLength * 0.5f);
			}
			FlashStrokeTimes[k] = At;
			FlashStrokeLevels[k] = k == 0 ? 1.0f : FMath::Lerp(0.4f, 0.9f, UDreamWeatherSelector::Hash01(WorldSeed, Key, 90u + uint32(k)));
		}
		UpdateFlashNow();
	}
	++StrikeCount;
	LastStrike = Strike;
	OnLightningStrike.Broadcast(Strike);
	if (Struck)
	{
		Struck->OnStruck.Broadcast(Strike);
	}
}

uint32 UDreamWeatherSubsystem::GetWeatherMapDrawCount() const
{
	return MapRenderer.IsValid() ? MapRenderer->GetDrawCount() : 0;
}

void UDreamWeatherSubsystem::UpdateWeatherMap(UDreamWorldSubsystem& Clock)
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	UWorld* World = GetWorld();

	// The map's records are made whether or not anything can draw them (the CPU reference uses them too).
	MapZones.Reset();
	TArray<FVector4f> PolygonPoints;
	const double WorldSeconds = Clock.GetWorldSeconds();
	for (IDreamWeatherInfluence* Influence : SortedInfluences())
	{
		const UDreamWeatherZoneComponent* Zone = Cast<UDreamWeatherZoneComponent>(Influence);
		FVector4f Z0, Z1, Z2;
		if (Zone && Zone->MakeMapRecordAt(WorldSeconds, Z0, Z1, Z2, PolygonPoints))
		{
			MapZones.Add(Z0);
			MapZones.Add(Z1);
			MapZones.Add(Z2);
		}
	}
	// Too many: keep the highest priorities, which are last.
	const int32 MaxFloats = FMath::Max(1, S->MaxMapZones) * 3;
	if (MapZones.Num() > MaxFloats)
	{
		MapZones.RemoveAt(0, MapZones.Num() - MaxFloats);
	}
	// The polygons' points after the records.
	MapZoneCount = MapZones.Num() / 3;
	UDreamWeatherZoneComponent::AppendPolygonPoints(MapZones, 3, PolygonPoints);

	const bool bCanRender = S->bWeatherMap && FApp::CanEverRender() && World && World->Scene != nullptr && !IsRunningDedicatedServer();
	bool bRedrawAll = false;
	if (bCanRender && !MapWriter.Acquire(World, bRedrawAll))
	{
		// Another world has the shared map (play in editor): leave it, and where this world's materials look, alone.
		return;
	}
	// (Back from another world: what the map holds is that world's weather around its view; draw it afresh.)
	bMapRedrawPending |= bRedrawAll;
	const double Now = FPlatformTime::Seconds();
	if (bCanRender && !WeatherMap && Now - LastMapResolve > 5.0)
	{
		LastMapResolve = Now;
		WeatherMap = Cast<UTextureRenderTarget2D>(S->WeatherMapTexture.TryLoad());
		if (WeatherMap && (!WeatherMap->bSupportsUAV || WeatherMap->GetFormat() != PF_FloatRGBA))
		{
			// The compute pass writes the map, so its resource needs a UAV.
			WeatherMap->bSupportsUAV = true;
			WeatherMap->InitCustomFormat(FMath::Max(16, WeatherMap->SizeX), FMath::Max(16, WeatherMap->SizeY), PF_FloatRGBA, true);
			WeatherMap->UpdateResourceImmediate(false);
		}
	}
	if (!bCanRender || !WeatherMap)
	{
		Clock.SetVectorParameter(DreamWeather::Names::MPC_WeatherMap, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
		return;
	}

	const int32 Resolution = FMath::Max(1, WeatherMap->SizeX);
	const double Extent = FMath::Max(10000.0, double(S->WeatherMapExtent));
	const double Texel = Extent / Resolution;
	MapCenter = FVector2D(FMath::GridSnap(ViewLocation.X, Texel), FMath::GridSnap(ViewLocation.Y, Texel));

	using C = FDreamWeatherChannels;
	const FVector4f Global(GlobalValues.Get(C::CloudCoverage), GlobalValues.Get(C::CloudDarkness), GlobalValues.Get(C::Precipitation), 0.0f);
	// The cloud painting, its texture brought up to date.
	UDreamCloudPaintComponent* Paint = GetActiveCloudPaint();
	UTexture2D* PaintTexture = Paint ? Paint->Paint->GetTexture() : nullptr;
	const FVector4f PaintRect = PaintTexture ? Paint->GetFootprintRecord() : FVector4f::Zero();
	const float PaintStrength = PaintTexture ? Paint->Strength : 0.0f;
	const uint32 PaintRevision = PaintTexture ? Paint->Paint->GetRevision() : 0;
	const bool bPaintChanged = PaintRect != LastMapPaintRect || PaintStrength != LastMapPaintStrength || PaintRevision != LastMapPaintRevision;
	const bool bChanged = bMapRedrawPending || bPaintChanged || MapCenter != LastMapCenter || !Global.Equals(LastMapGlobal, 1.0e-3f) || MapZones != LastMapZones;
	if (bChanged && (bMapRedrawPending || Now - LastMapDraw >= S->WeatherMapInterval))
	{
		FDreamWeatherMapRenderer::FUpdateArgs Args;
		Args.Target = WeatherMap->GameThread_GetRenderTargetResource();
		Args.Resolution = FIntPoint(WeatherMap->SizeX, WeatherMap->SizeY);
		Args.Center = FVector2f(MapCenter);
		Args.Extent = float(Extent);
		Args.Global = Global;
		Args.PrecipitationGate = FVector2f(S->PrecipitationCloudCover);
		Args.Zones = MapZones;
		Args.NumZones = MapZoneCount;
		Args.PaintResource = PaintTexture ? PaintTexture->GetResource() : nullptr;
		Args.PaintRect = PaintRect;
		Args.PaintStrength = PaintStrength;
		if (Args.Target)
		{
			LastMapPaintRect = PaintRect;
			LastMapPaintStrength = PaintStrength;
			LastMapPaintRevision = PaintRevision;
			MapRenderer->EnqueueUpdate(MoveTemp(Args));
			bMapRedrawPending = false;
			LastMapDraw = Now;
			LastMapCenter = MapCenter;
			LastMapGlobal = Global;
			LastMapZones = MapZones;
		}
	}
	// The centre the map holds (the last one drawn, not where the view is now: between redraws the view moves on, and a
	// material reading the map around the live camera would find the weather shifted until the next redraw, the clouds
	// jumping each time it lands). As minus the centre: materials add it to a world position.
	const bool bDrawn = LastMapCenter.X < 1.0e29;
	Clock.SetVectorParameter(DreamWeather::Names::MPC_WeatherMap, bDrawn
		? FLinearColor(float(-LastMapCenter.X), float(-LastMapCenter.Y), float(1.0 / Extent), 1.0f) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
}

void UDreamWeatherSubsystem::FireEvents(const FDreamWeatherSample& View)
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	UDreamWeatherPreset* Current = GetCurrentWeather();
	if (!bEventsPrimed)
	{
		// The first frame (or after a level change) reports the state as it is, not as a change.
		bEventsPrimed = true;
		LastWeather = Current;
		bPrecipitating = View.Precipitation > S->PrecipitationStart;
		LastTags = View.Tags;
		return;
	}
	if (Current != LastWeather.Get())
	{
		UDreamWeatherPreset* Previous = LastWeather.Get();
		LastWeather = Current;
		OnWeatherChanged.Broadcast(Previous, Current);
	}
	if (!bPrecipitating && View.Precipitation > S->PrecipitationStart)
	{
		bPrecipitating = true;
		OnPrecipitationStarted.Broadcast();
	}
	else if (bPrecipitating && View.Precipitation < S->PrecipitationStop)
	{
		bPrecipitating = false;
		OnPrecipitationStopped.Broadcast();
	}
	if (View.Tags != LastTags)
	{
		LastTags = View.Tags;
		OnWeatherTagsChanged.Broadcast(LastTags);
	}
}

// -------------------------------------------------------------------------------------------------
// Snapshots
// -------------------------------------------------------------------------------------------------

void UDreamWeatherSubsystem::CaptureSnapshot(FDreamWorldSnapshot& Snapshot) const
{
	// The schedule is a pure function of the time and seed the snapshot already holds; only the modifiers
	// (scripted weather) need saving.
	FDreamWeatherSnapshotData Data;
	Data.NextModifierId = NextModifierId;
	for (const FModifier& M : Modifiers)
	{
		FDreamWeatherModifierState& State = Data.Modifiers.AddDefaulted_GetRef();
		State.Id = M.Id;
		State.Desc = M.Desc;
		State.Current = M.Current;
		State.Target = M.Target;
		State.FadeSeconds = M.FadeSeconds;
		State.bRemoving = M.bRemoving;
		State.SupersededBy = M.SupersededBy;
	}
	FString Json;
	if (FJsonObjectConverter::UStructToJsonObjectString(Data, Json))
	{
		Snapshot.Extensions.Add(DreamWeather::Subsystem::SnapshotKey, Json);
	}
}

void UDreamWeatherSubsystem::ApplySnapshot(const FDreamWorldSnapshot& Snapshot)
{
	const FString* Json = Snapshot.Extensions.Find(DreamWeather::Subsystem::SnapshotKey);
	FDreamWeatherSnapshotData Data;
	if (!Json || !FJsonObjectConverter::JsonObjectStringToUStruct(*Json, &Data))
	{
		return;
	}
	Modifiers.Reset();
	ForcedModifierId = 0;
	for (const FDreamWeatherModifierState& State : Data.Modifiers)
	{
		FModifier M;
		M.Id = State.Id;
		M.Desc = State.Desc;
		M.Current = State.Current;
		M.Target = State.Target;
		M.FadeSeconds = State.FadeSeconds;
		M.bRemoving = State.bRemoving;
		M.SupersededBy = State.SupersededBy;
		if (M.Desc.Priority == DreamWeather::Subsystem::ForcedPriority && M.Desc.Source == TEXT("ForceWeather") && !M.bRemoving && M.SupersededBy == 0)
		{
			ForcedModifierId = M.Id;
		}
		Modifiers.Add(M);
	}
	Modifiers.StableSort([](const FModifier& A, const FModifier& B) { return A.Desc.Priority != B.Desc.Priority ? A.Desc.Priority < B.Desc.Priority : A.Id < B.Id; });
	NextModifierId = FMath::Max(Data.NextModifierId, 1);
	bEventsPrimed = false;
}
