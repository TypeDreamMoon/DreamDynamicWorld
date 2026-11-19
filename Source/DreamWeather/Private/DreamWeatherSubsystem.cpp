// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherSubsystem.h"
#include "DreamWeather.h"
#include "DreamWeatherChannels.h"
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
	if (Terrain && Terrain->IsInside(Position.X, Position.Y))
	{
		const auto CoveredAt = [Terrain, &Position, S](double X, double Y)
		{
			return Terrain->IsInside(X, Y) && double(Terrain->HeightBase) + Terrain->HeightBilinear(X, Y, 0) > Position.Z + S->RoofClearance;
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

void UDreamWeatherSubsystem::ApplyInfluences(const FVector& Position, FDreamWeatherValues& InOut) const
{
	for (IDreamWeatherInfluence* Influence : SortedInfluences())
	{
		Influence->ApplyWeatherInfluence(Position, InOut);
	}
}

float UDreamWeatherSubsystem::TemperatureFor(const FVector& Position, float Offset, float Coverage) const
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	const UDreamWorldSubsystem* Clock = ClockSubsystem.Get();
	const double Seconds = NowSeconds();
	const FDateTime Date = DreamWorld::Celestial::ToDateTime(Seconds);
	const double Hours = DreamWorld::Celestial::HoursOfDay(Seconds);
	const bool bSouth = Clock && Clock->GetCelestialSettings().Mode == EDreamCelestialMode::Realistic && Clock->GetCelestialSettings().Latitude < 0.0;
	// Coldest in mid-January (mid-July in the south); warmest at WarmestHour, less of a swing under cloud.
	const double Seasonal = -FMath::Cos(2.0 * UE_DOUBLE_PI * (Date.GetDayOfYear() - 15) / 365.25) * (bSouth ? -1.0 : 1.0);
	const double Diurnal = FMath::Cos(2.0 * UE_DOUBLE_PI * (Hours - S->WarmestHour) / 24.0);
	const double Height = (Position.Z - S->BaseHeight) / 10000.0;
	return float(S->MeanTemperature + S->SeasonalAmplitude * Seasonal + S->DiurnalAmplitude * Diurnal * (1.0 - 0.6 * Coverage)
		- S->LapseRatePer100m * Height + Offset);
}

FDreamWeatherSample UDreamWeatherSubsystem::MakeSample(const FVector& Position, const FDreamWeatherValues& Values) const
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
	S.Temperature = TemperatureFor(Position, Values.Get(C::Temperature), S.CloudCoverage);
	// What falls turns to snow near freezing, whatever the weather says (a mountain top snows in the rain).
	S.SnowRatio = FMath::Max(Values.Get(C::SnowRatio), 1.0f - FMath::SmoothStep(-1.0f, 2.0f, S.Temperature));
	S.Rain = S.Precipitation * (1.0f - S.SnowRatio);
	S.Snow = S.Precipitation * S.SnowRatio;
	S.Exposure = ExposureAt(Position);
	S.bSheltered = S.Exposure < 0.5f;
	S.RainHere = S.Rain * S.Exposure;
	S.SnowHere = S.Snow * S.Exposure;

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
	FDreamWeatherValues Values = GlobalValues;
	if (Values.Values.Num() != FDreamWeatherChannels::Num())
	{
		Values = FDreamWeatherChannels::Defaults();
	}
	ApplyInfluences(Position, Values);
	return MakeSample(Position, Values);
}

float UDreamWeatherSubsystem::GetChannelAt(FVector Position, FGameplayTag Channel) const
{
	const int32 Index = FDreamWeatherChannels::IndexOf(Channel);
	if (Index == INDEX_NONE)
	{
		return 0.0f;
	}
	FDreamWeatherValues Values = GlobalValues;
	ApplyInfluences(Position, Values);
	return Values.Get(Index);
}

float UDreamWeatherSubsystem::GetTemperatureAt(FVector Position) const
{
	return SampleWeather(Position).Temperature;
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

FDreamWeatherModifierHandle UDreamWeatherSubsystem::ForceWeather(UDreamWeatherPreset* Preset, float TransitionSeconds)
{
	ClearForcedWeather(TransitionSeconds);
	FDreamWeatherModifierDesc Desc;
	Desc.Source = TEXT("ForceWeather");
	Desc.Preset = Preset;
	Desc.Blend = EDreamWeatherBlend::Override;
	Desc.Priority = DreamWeather::Subsystem::ForcedPriority;
	Desc.BlendInSeconds = TransitionSeconds;
	const FDreamWeatherModifierHandle Handle = PushModifier(Desc);
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
	for (IDreamWeatherInfluence* Influence : SortedInfluences())
	{
		const UDreamWeatherZoneComponent* Zone = Cast<UDreamWeatherZoneComponent>(Influence);
		FVector4f Z0, Z1, Z2;
		if (Zone && Zone->MakeMapRecord(Z0, Z1, Z2))
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

	const bool bCanRender = S->bWeatherMap && FApp::CanEverRender() && World && World->Scene != nullptr && !IsRunningDedicatedServer();
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
	const bool bChanged = MapCenter != LastMapCenter || !Global.Equals(LastMapGlobal, 1.0e-3f) || MapZones != LastMapZones;
	if (bChanged && Now - LastMapDraw >= S->WeatherMapInterval)
	{
		FDreamWeatherMapRenderer::FUpdateArgs Args;
		Args.Target = WeatherMap->GameThread_GetRenderTargetResource();
		Args.Resolution = FIntPoint(WeatherMap->SizeX, WeatherMap->SizeY);
		Args.Center = FVector2f(MapCenter);
		Args.Extent = float(Extent);
		Args.Global = Global;
		Args.Zones = MapZones;
		if (Args.Target)
		{
			MapRenderer->EnqueueUpdate(MoveTemp(Args));
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
		if (M.Desc.Priority == DreamWeather::Subsystem::ForcedPriority && M.Desc.Source == TEXT("ForceWeather") && !M.bRemoving)
		{
			ForcedModifierId = M.Id;
		}
		Modifiers.Add(M);
	}
	Modifiers.StableSort([](const FModifier& A, const FModifier& B) { return A.Desc.Priority != B.Desc.Priority ? A.Desc.Priority < B.Desc.Priority : A.Id < B.Id; });
	NextModifierId = FMath::Max(Data.NextModifierId, 1);
	bEventsPrimed = false;
}
