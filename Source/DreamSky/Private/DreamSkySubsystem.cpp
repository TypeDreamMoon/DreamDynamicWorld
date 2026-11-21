// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkySubsystem.h"
#include "DreamSky.h"
#include "DreamSkyCloudRenderer.h"
#include "DreamSkyModifier.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamWorldSubsystem.h"

#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "JsonObjectConverter.h"
#include "Misc/App.h"
#include "SceneViewExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkySubsystem)

DECLARE_CYCLE_STAT(TEXT("DreamSky Update"), STAT_DreamSkyUpdate, STATGROUP_Game);

namespace DreamSky::Subsystem
{
	static const FName SnapshotKey(TEXT("DreamSky"));
}

float FDreamSkyActiveModifier::GetAlpha() const
{
	if (!Modifier)
	{
		return 0.0f;
	}
	return bManual ? FMath::Clamp(Progress, 0.0f, 1.0f) * FMath::Clamp(Modifier->MaxAlpha, 0.0f, 1.0f) : Modifier->Shape(Progress);
}

static TAutoConsoleVariable<float> CVarDreamSkyLogLighting(
	TEXT("DreamSky.LogLighting"), 0.0f,
	TEXT("Log the lighting the sky applies (exposure, lights, fog, haze, indoors, the weather it came from) every this many seconds. 0 = off."),
	ECVF_Default);

UDreamSkySubsystem* UDreamSkySubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamSkySubsystem>() : nullptr;
}

UDreamSkyStyle* UDreamSkySubsystem::GetDefaultStyle()
{
	// The class defaults are the default style: its constructor writes the default curves.
	return GetMutableDefault<UDreamSkyStyle>();
}

bool UDreamSkySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamSkySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UDreamWorldSubsystem* Clock = Collection.InitializeDependency<UDreamWorldSubsystem>();
	if (Clock)
	{
		ClockSubsystem = Clock;
		TickHandle = Clock->OnWorldTick.AddUObject(this, &UDreamSkySubsystem::Update);
		CaptureHandle = Clock->OnCaptureSnapshot.AddWeakLambda(this, [this](FDreamWorldSnapshot& Snapshot) { CaptureSnapshot(Snapshot); });
		ApplyHandle = Clock->OnApplySnapshot.AddWeakLambda(this, [this](const FDreamWorldSnapshot& Snapshot) { ApplySnapshot(Snapshot); });
		RecaptureTask = Clock->GetScheduler().Register(TEXT("DreamSky.Recapture"), 0, 0.5, 0.0, [this] { return RecaptureIfChanged(); });
	}
	if (FApp::CanEverRender())
	{
		CloudViewExtension = FSceneViewExtensions::NewExtension<FDreamSkyCloudViewExtension>(GetWorld(), this);
	}
}

void UDreamSkySubsystem::Deinitialize()
{
	if (UDreamWorldSubsystem* Clock = ClockSubsystem.Get())
	{
		Clock->OnWorldTick.Remove(TickHandle);
		Clock->OnCaptureSnapshot.Remove(CaptureHandle);
		Clock->OnApplySnapshot.Remove(ApplyHandle);
		Clock->GetScheduler().Unregister(RecaptureTask);
	}
	if (CloudViewExtension.IsValid())
	{
		CloudViewExtension->Detach();
		CloudViewExtension.Reset();
	}
	for (FDreamSkyActiveModifier& M : Modifiers)
	{
		if (M.PostProcess)
		{
			M.PostProcess->DestroyComponent();
		}
	}
	Modifiers.Reset();
	Skies.Reset();
	Super::Deinitialize();
}

void UDreamSkySubsystem::RegisterSky(ADreamSky* Sky)
{
	Skies.RemoveAll([](const TWeakObjectPtr<ADreamSky>& S) { return !S.IsValid(); });
	if (Sky && !Skies.Contains(Sky))
	{
		Skies.Add(Sky);
		if (Skies.Num() > 1)
		{
			UE_LOG(LogDreamSky, Warning, TEXT("%s: more than one Dream Sky in the level; %s is in charge."), *GetNameSafe(GetWorld()), *GetNameSafe(Skies[0].Get()));
		}
		bHasApplied = false;
	}
}

void UDreamSkySubsystem::UnregisterSky(ADreamSky* Sky)
{
	Skies.RemoveAll([Sky](const TWeakObjectPtr<ADreamSky>& S) { return !S.IsValid() || S.Get() == Sky; });
	bHasApplied = false;
}

ADreamSky* UDreamSkySubsystem::GetSky() const
{
	for (const TWeakObjectPtr<ADreamSky>& Sky : Skies)
	{
		if (ADreamSky* Alive = Sky.Get())
		{
			return Alive;
		}
	}
	return nullptr;
}

FDreamSkyLighting UDreamSkySubsystem::BlendLighting(const FDreamSkyLighting& From, const FDreamSkyLighting& To, float Alpha)
{
	FDreamSkyLighting Out = To;
	auto Mix = [Alpha](float A, float B) { return FMath::Lerp(A, B, Alpha); };
	auto MixC = [Alpha](const FLinearColor& A, const FLinearColor& B) { return FMath::Lerp(A, B, Alpha); };
	Out.SunColor = MixC(From.SunColor, To.SunColor);
	Out.SunIlluminance = Mix(From.SunIlluminance, To.SunIlluminance);
	Out.MoonColor = MixC(From.MoonColor, To.MoonColor);
	Out.MoonIlluminance = Mix(From.MoonIlluminance, To.MoonIlluminance);
	Out.MainLightColor = To.bMainLightIsMoon ? Out.MoonColor : Out.SunColor;
	Out.MainLightIlluminance = FMath::Max(Out.SunIlluminance, Out.MoonIlluminance);
	Out.AmbientSky = MixC(From.AmbientSky, To.AmbientSky);
	Out.AmbientEquator = MixC(From.AmbientEquator, To.AmbientEquator);
	Out.AmbientGround = MixC(From.AmbientGround, To.AmbientGround);
	Out.SkyLightIntensity = Mix(From.SkyLightIntensity, To.SkyLightIntensity);
	Out.FogColor = MixC(From.FogColor, To.FogColor);
	Out.FogDensity = Mix(From.FogDensity, To.FogDensity);
	Out.ExposureEV100 = Mix(From.ExposureEV100, To.ExposureEV100);
	return Out;
}

void UDreamSkySubsystem::Update(UDreamWorldSubsystem& Clock, float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamSkyUpdate);
	ADreamSky* Sky = GetSky();
	if (!Sky)
	{
		bHasApplied = false;
		return;
	}
	const UDreamSkyStyle* Style = Sky->GetEffectiveStyle();
	if (!Style)
	{
		return;
	}

	const FDreamCelestialState& Celestial = Clock.GetCelestial();
	FDreamSkyLighting Target = Style->EvaluateLighting(Celestial);
	Look = Style->EvaluateLook(Celestial);
	Style->ApplyWeather(Clock.GetEnvironment(), Target, Look);
	// Indoors goes in before the easing, so walking through a door fades the exposure rather than cutting it.
	Style->ApplyInterior(Clock.GetViewOcclusion(), Target);
	// The level's own scales over the style's lighting (everything below, lights and parameters alike, sees them).
	Sky->ApplyLightingOverrides(Target);

	// Ease while the clock runs; snap on jumps (scrubbing, set time, snapshots) and while it stands still.
	const bool bSnap = !bHasApplied || Clock.GetTimeJumpSerial() != LastJumpSerial || !Clock.IsClockRunning() || Style->LightResponseTime <= 0.0f;
	LastJumpSerial = Clock.GetTimeJumpSerial();
	if (bSnap)
	{
		Eased = Target;
	}
	else
	{
		const float Alpha = 1.0f - FMath::Exp(-FMath::Max(DeltaTime, 0.0f) / Style->LightResponseTime);
		Eased = BlendLighting(Eased, Target, Alpha);
	}
	bHasApplied = true;

	// The sky modifiers over the eased lighting (they fade in and out themselves), the weather's among them.
	++UpdateSerial;
	TakeWeatherModifiers(Clock.GetEnvironment());
	Applied = Eased;
	ApplyModifiers(Applied, Look, DeltaTime);
	UpdateModifierPostProcess(Sky);

	// A lightning flash goes on top, so it is as sudden as it should be.
	const FDreamSkyLighting Modified = Applied;
	Style->ApplyFlash(Clock.GetEnvironment(), Applied);
	Sky->ApplyLighting(Applied, *Style, bSnap);
	Sky->UpdateDome(Clock.GetViewLocation(), Clock.HasViewLocation());
	Sky->UpdateCloudShadow(Clock.GetViewLocation());
	WriteParameters(Clock, *Style, Sky->DrawsClouds());
	LogLighting(Clock);
	Applied = Modified;
}

FDreamSkyActiveModifier* UDreamSkySubsystem::FindModifier(const UDreamSkyModifier* Modifier, EDreamSkyModifierSource Source)
{
	return Modifiers.FindByPredicate([Modifier, Source](const FDreamSkyActiveModifier& M) { return M.Modifier == Modifier && M.Source == Source; });
}

FDreamSkyActiveModifier& UDreamSkySubsystem::FindOrAddModifier(UDreamSkyModifier* Modifier, EDreamSkyModifierSource Source)
{
	if (FDreamSkyActiveModifier* Found = FindModifier(Modifier, Source))
	{
		return *Found;
	}
	FDreamSkyActiveModifier& Added = Modifiers.AddDefaulted_GetRef();
	Added.Modifier = Modifier;
	Added.Source = Source;
	Added.Order = NextModifierOrder++;
	return Added;
}

void UDreamSkySubsystem::SetDrivenModifier(UDreamSkyModifier* Modifier, float Alpha, EDreamSkyModifierSource Source)
{
	if (!Modifier || Source == EDreamSkyModifierSource::Script)
	{
		return;
	}
	FDreamSkyActiveModifier& M = FindOrAddModifier(Modifier, Source);
	M.bManual = true;
	M.bRemoving = false;
	M.Progress = M.Target = FMath::Clamp(Alpha, 0.0f, 1.0f);
	M.Rate = 0.0f;
	M.DrivenSerial = UpdateSerial;
}

void UDreamSkySubsystem::TakeWeatherModifiers(const FDreamWorldEnvironment& Environment)
{
	for (const FDreamWorldWeightedObject& Entry : Environment.SkyModifiers)
	{
		if (UDreamSkyModifier* Modifier = Cast<UDreamSkyModifier>(Entry.Object.Get()))
		{
			SetDrivenModifier(Modifier, Entry.Weight, EDreamSkyModifierSource::Weather);
		}
	}
}

void UDreamSkySubsystem::AddModifier(UDreamSkyModifier* Modifier, float FadeInSeconds)
{
	if (!Modifier)
	{
		return;
	}
	FDreamSkyActiveModifier& M = FindOrAddModifier(Modifier);
	const float Seconds = FadeInSeconds >= 0.0f ? FadeInSeconds : Modifier->FadeInSeconds;
	if (M.bManual)
	{
		// (Its alpha as it stood, along the fade's linear progress: close enough to carry on from.)
		M.bManual = false;
	}
	M.Target = 1.0f;
	M.Rate = Seconds > 0.0f ? 1.0f / Seconds : 0.0f;
	M.bRemoving = false;
}

void UDreamSkySubsystem::RemoveModifier(UDreamSkyModifier* Modifier, float FadeOutSeconds)
{
	FDreamSkyActiveModifier* M = FindModifier(Modifier);
	if (!M)
	{
		return;
	}
	const float Seconds = FadeOutSeconds >= 0.0f ? FadeOutSeconds : Modifier->FadeOutSeconds;
	M->Target = 0.0f;
	M->Rate = Seconds > 0.0f ? 1.0f / Seconds : 0.0f;
	M->bRemoving = true;
}

void UDreamSkySubsystem::SetModifierAlpha(UDreamSkyModifier* Modifier, float Alpha, float Seconds)
{
	if (!Modifier)
	{
		return;
	}
	FDreamSkyActiveModifier& M = FindOrAddModifier(Modifier);
	M.bManual = true;
	M.bRemoving = false;
	M.Target = FMath::Clamp(Alpha, 0.0f, 1.0f);
	M.Rate = Seconds > 0.0f ? FMath::Abs(M.Target - M.Progress) / Seconds : 0.0f;
}

void UDreamSkySubsystem::ClearModifiers(float FadeOutSeconds)
{
	for (FDreamSkyActiveModifier& M : Modifiers)
	{
		if (M.Source != EDreamSkyModifierSource::Script)
		{
			continue;
		}
		const float Seconds = FadeOutSeconds >= 0.0f ? FadeOutSeconds : (M.Modifier ? M.Modifier->FadeOutSeconds : 0.0f);
		M.Target = 0.0f;
		M.Rate = Seconds > 0.0f ? 1.0f / Seconds : 0.0f;
		M.bRemoving = true;
	}
}

float UDreamSkySubsystem::GetModifierAlpha(const UDreamSkyModifier* Modifier) const
{
	// (All its entries: a modifier the weather holds and a script added as well goes on twice.)
	float Alpha = 0.0f;
	for (const FDreamSkyActiveModifier& M : Modifiers)
	{
		Alpha = M.Modifier == Modifier ? FMath::Max(Alpha, M.GetAlpha()) : Alpha;
	}
	return Alpha;
}

TArray<UDreamSkyModifier*> UDreamSkySubsystem::GetModifiers() const
{
	TArray<const FDreamSkyActiveModifier*> Sorted;
	for (const FDreamSkyActiveModifier& M : Modifiers)
	{
		if (M.Modifier)
		{
			Sorted.Add(&M);
		}
	}
	Sorted.Sort([](const FDreamSkyActiveModifier& A, const FDreamSkyActiveModifier& B)
	{
		return A.Modifier->Priority != B.Modifier->Priority ? A.Modifier->Priority < B.Modifier->Priority : A.Order < B.Order;
	});
	TArray<UDreamSkyModifier*> Out;
	for (const FDreamSkyActiveModifier* M : Sorted)
	{
		Out.Add(M->Modifier);
	}
	return Out;
}

void UDreamSkySubsystem::ApplyModifiers(FDreamSkyLighting& Lighting, FDreamSkyLook& InOutLook, float DeltaTime)
{
	const float Dt = FMath::Max(DeltaTime, 0.0f);
	for (int32 Index = Modifiers.Num() - 1; Index >= 0; --Index)
	{
		FDreamSkyActiveModifier& M = Modifiers[Index];
		if (!M.Modifier)
		{
			if (M.PostProcess)
			{
				M.PostProcess->DestroyComponent();
			}
			Modifiers.RemoveAt(Index);
			continue;
		}
		M.Progress = M.Rate > 0.0f ? FMath::FInterpConstantTo(M.Progress, M.Target, Dt, M.Rate) : M.Target;
		const bool bLetGo = M.Source != EDreamSkyModifierSource::Script && M.DrivenSerial != UpdateSerial;
		if ((M.bRemoving && M.Progress <= 0.0f) || bLetGo)
		{
			if (M.PostProcess)
			{
				M.PostProcess->DestroyComponent();
			}
			Modifiers.RemoveAt(Index);
		}
	}
	TArray<const FDreamSkyActiveModifier*> Sorted;
	for (const FDreamSkyActiveModifier& M : Modifiers)
	{
		Sorted.Add(&M);
	}
	Sorted.StableSort([](const FDreamSkyActiveModifier& A, const FDreamSkyActiveModifier& B)
	{
		return A.Modifier->Priority != B.Modifier->Priority ? A.Modifier->Priority < B.Modifier->Priority : A.Order < B.Order;
	});
	for (const FDreamSkyActiveModifier* M : Sorted)
	{
		M->Modifier->Settings.Apply(Lighting, InOutLook, M->GetAlpha());
	}
}

void UDreamSkySubsystem::UpdateModifierPostProcess(ADreamSky* Sky)
{
	// (Components of the sky actor: a sky that went away took them with it.)
	if (ModifierPostProcessSky.Get() != Sky)
	{
		for (FDreamSkyActiveModifier& M : Modifiers)
		{
			if (M.PostProcess && M.PostProcess->GetOwner() != Sky)
			{
				M.PostProcess->DestroyComponent();
				M.PostProcess = nullptr;
			}
		}
		ModifierPostProcessSky = Sky;
	}
	for (FDreamSkyActiveModifier& M : Modifiers)
	{
		const float Alpha = M.GetAlpha();
		const bool bWants = Sky && M.Modifier && M.Modifier->bUsePostProcess && Alpha > 0.0f;
		if (!bWants)
		{
			if (M.PostProcess)
			{
				M.PostProcess->DestroyComponent();
				M.PostProcess = nullptr;
			}
			continue;
		}
		if (!M.PostProcess)
		{
			M.PostProcess = NewObject<UPostProcessComponent>(Sky, NAME_None, RF_Transient);
			M.PostProcess->bUnbound = true;
			M.PostProcess->RegisterComponent();
		}
		M.PostProcess->Settings = M.Modifier->PostProcess;
		// (Over the sky's own exposure post process, which stands at -1.)
		M.PostProcess->Priority = float(M.Modifier->Priority);
		M.PostProcess->BlendWeight = Alpha;
	}
}

void UDreamSkySubsystem::CaptureSnapshot(FDreamWorldSnapshot& Snapshot) const
{
	FDreamSkySnapshotData Data;
	Data.NextOrder = int32(NextModifierOrder);
	for (const FDreamSkyActiveModifier& M : Modifiers)
	{
		// (The weather's and the conditions' come back with the next update.)
		if (!M.Modifier || M.Source != EDreamSkyModifierSource::Script)
		{
			continue;
		}
		FDreamSkyModifierState& State = Data.Modifiers.AddDefaulted_GetRef();
		State.Modifier = FSoftObjectPath(M.Modifier);
		State.Progress = M.Progress;
		State.Target = M.Target;
		State.Rate = M.Rate;
		State.bRemoving = M.bRemoving;
		State.bManual = M.bManual;
		State.Order = int32(M.Order);
	}
	FString Json;
	if (FJsonObjectConverter::UStructToJsonObjectString(Data, Json))
	{
		Snapshot.Extensions.Add(DreamSky::Subsystem::SnapshotKey, Json);
	}
}

void UDreamSkySubsystem::ApplySnapshot(const FDreamWorldSnapshot& Snapshot)
{
	for (FDreamSkyActiveModifier& M : Modifiers)
	{
		if (M.PostProcess)
		{
			M.PostProcess->DestroyComponent();
		}
	}
	Modifiers.Reset();
	NextModifierOrder = 0;
	const FString* Json = Snapshot.Extensions.Find(DreamSky::Subsystem::SnapshotKey);
	FDreamSkySnapshotData Data;
	if (!Json || !FJsonObjectConverter::JsonObjectStringToUStruct(*Json, &Data))
	{
		return;
	}
	NextModifierOrder = uint32(FMath::Max(Data.NextOrder, 0));
	for (const FDreamSkyModifierState& State : Data.Modifiers)
	{
		UObject* Found = State.Modifier.ResolveObject();
		UDreamSkyModifier* Modifier = Cast<UDreamSkyModifier>(Found ? Found : State.Modifier.TryLoad());
		if (!Modifier)
		{
			UE_LOG(LogDreamSky, Warning, TEXT("%s: a snapshot's sky modifier %s did not load; left out."), *GetNameSafe(GetWorld()), *State.Modifier.ToString());
			continue;
		}
		FDreamSkyActiveModifier& M = Modifiers.AddDefaulted_GetRef();
		M.Modifier = Modifier;
		M.Progress = State.Progress;
		M.Target = State.Target;
		M.Rate = State.Rate;
		M.bRemoving = State.bRemoving;
		M.bManual = State.bManual;
		M.Order = uint32(FMath::Max(State.Order, 0));
	}
}

void UDreamSkySubsystem::WriteParameters(UDreamWorldSubsystem& Clock, const UDreamSkyStyle& Style, bool bRendererDrawsClouds)
{
	using namespace DreamSky::Names;
	auto With = [](const FLinearColor& C, float A) { return FLinearColor(C.R, C.G, C.B, A); };

	Clock.SetVectorParameter(MPC_Zenith, With(Look.Zenith, 0.0f));
	Clock.SetVectorParameter(MPC_Horizon, With(Look.Horizon, Style.HorizonExponent));
	Clock.SetVectorParameter(MPC_Ground, With(Look.Ground, 0.0f));
	Clock.SetVectorParameter(MPC_SunHalo, With(Look.SunHalo, Style.SunHaloExponent));
	Clock.SetVectorParameter(MPC_SunDisk, With(Look.SunDisk, Style.SunDiskSize));
	Clock.SetVectorParameter(MPC_MoonDisk, With(Look.MoonDisk, Style.MoonDiskSize));
	Clock.SetVectorParameter(MPC_MoonHalo, With(Look.MoonHalo, Style.MoonHaloExponent));
	Clock.SetVectorParameter(MPC_Stars, FLinearColor(Look.StarIntensity, Style.StarDensity, Style.StarTwinkle, 0.0f));

	// Drift: shapes per game hour along CloudDirection, from the world time, so a given moment always shows
	// the same clouds. Wrapped to keep the shader's float precision.
	const double Distance = FMath::Fmod(Clock.GetWorldSeconds() / 3600.0 * double(Style.CloudSpeed), 4096.0);
	const double Yaw = FMath::DegreesToRadians(double(Style.CloudDirection));
	Clock.SetVectorParameter(MPC_Cloud0, FLinearColor(Look.CloudCoverage, 0.0f, Style.CloudScale, float(Style.CloudBands)));
	// z: how far the clouds have grown and dissolved (the formation noise's phase, from the world time as well).
	Clock.SetVectorParameter(MPC_Cloud1, FLinearColor(float(FMath::Cos(Yaw) * Distance), float(FMath::Sin(Yaw) * Distance),
		DreamSky::CloudFormation(Clock.GetWorldSeconds(), Style.CloudFormationSpeed), 0.0f));
	const float Grown = FMath::Lerp(1.0f, DreamSky::CumulusGrowth(Clock.GetTimeOfDay()), FMath::Clamp(Style.CloudDiurnal, 0.0f, 1.0f));
	Clock.SetVectorParameter(MPC_Cloud2, FLinearColor(Grown, Style.CloudErosion, Style.CloudDetail, 0.0f));
	Clock.SetVectorParameter(MPC_Cloud3, FLinearColor(Style.CloudVisibility, Style.CloudHazeHeight, Style.CloudHighCover, Style.CloudHighAltitude));
	Clock.SetVectorParameter(MPC_CloudLit, With(Look.CloudLit, Style.CloudRim));
	Clock.SetVectorParameter(MPC_CloudShadow, With(Look.CloudShadow, Style.CloudOpacity));
	// w: 1 while the sky cloud renderer draws the clouds (the dome's sky light capture then shows its panorama), 0 while
	// none are drawn.
	Clock.SetVectorParameter(MPC_CloudLayer, FLinearColor(Style.CloudLayerHeight, Look.CloudDarkness, Style.StormCloudDarkening, bRendererDrawsClouds ? 1.0f : 0.0f));
	Clock.SetVectorParameter(MPC_SkyExtra, FLinearColor(Style.CloudSkyTint, Style.CloudSunGlow, Style.SunDiskInSkyLight, Applied.FlashGain));
	Clock.SetVectorParameter(MPC_SkyHaze, Look.Haze);
	Clock.SetVectorParameter(MPC_SkyModClouds, FLinearColor(1.0f - Look.CloudCoverageKept, Look.CloudCoverageAdded, 1.0f - Look.CloudDarknessKept, Look.CloudDarknessAdded));

	const FVector& L = Applied.MainLightDirection;
	Clock.SetVectorParameter(MPC_MainLight, FLinearColor(float(L.X), float(L.Y), float(L.Z), Applied.MainLightIlluminance));
	Clock.SetVectorParameter(MPC_MainLightColor, With(Applied.MainLightColor, Applied.bMainLightIsMoon ? 1.0f : 0.0f));
	Clock.SetVectorParameter(MPC_AmbientSky, With(Applied.AmbientSky, 0.0f));
	Clock.SetVectorParameter(MPC_AmbientEquator, With(Applied.AmbientEquator, 0.0f));
	Clock.SetVectorParameter(MPC_AmbientGround, With(Applied.AmbientGround, 0.0f));
	Clock.SetVectorParameter(MPC_DayPhase, FLinearColor(Applied.DayPhase.Day, Applied.DayPhase.Twilight, Applied.DayPhase.Night, Applied.SunElevation / 90.0f));
}

void UDreamSkySubsystem::LogLighting(UDreamWorldSubsystem& Clock)
{
	const float Every = CVarDreamSkyLogLighting.GetValueOnGameThread();
	const double Now = FPlatformTime::Seconds();
	if (Every <= 0.0f || Now - LastLightingLog < double(Every))
	{
		return;
	}
	LastLightingLog = Now;
	const FDreamWorldEnvironment& Env = Clock.GetEnvironment();
	const FDreamViewOcclusion& Indoors = Clock.GetViewOcclusion();
	UE_LOG(LogDreamSky, Display,
		TEXT("Lighting: EV100 %.2f | sun %.0f lx, moon %.2f lx, sky light %.2f | fog %.6f (%.2f, %.2f, %.2f), haze %.2f | interior %.2f overhead %.2f | flash %.2f x%.1f")
		TEXT(" | weather %s: cover %.2f dark %.2f rain %.2f fog %.2f dust %.2f"),
		Applied.ExposureEV100, Applied.SunIlluminance, Applied.MoonIlluminance, Applied.SkyLightIntensity,
		Applied.FogDensity, Applied.FogColor.R, Applied.FogColor.G, Applied.FogColor.B, Look.Haze.A,
		Indoors.Interior, Indoors.Overhead, Env.LightningFlash, Applied.FlashGain,
		Env.bActive ? TEXT("on") : TEXT("off"), Env.CloudCoverage, Env.CloudDarkness, Env.Precipitation, Env.Fog, Env.Dust);
}

bool UDreamSkySubsystem::RecaptureIfChanged()
{
	ADreamSky* Sky = GetSky();
	if (!Sky || Sky->bRealTimeSkyCapture || !bHasApplied)
	{
		return false;
	}
	auto Moved = [](const FLinearColor& A, const FLinearColor& B)
	{
		const float Scale = FMath::Max3(A.R, A.G, FMath::Max(A.B, 1.0e-3f));
		return FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)) > 0.02f * Scale;
	};
	if (!Moved(Look.Zenith, CapturedZenith) && !Moved(Look.Horizon, CapturedHorizon) && FMath::Abs(Look.Haze.A - CapturedHaze) <= 0.02f)
	{
		return false;
	}
	CapturedZenith = Look.Zenith;
	CapturedHorizon = Look.Horizon;
	CapturedHaze = Look.Haze.A;
	Sky->RecaptureSkyLight();
	return true;
}
