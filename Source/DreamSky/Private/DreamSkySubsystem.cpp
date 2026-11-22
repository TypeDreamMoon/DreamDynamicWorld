// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkySubsystem.h"
#include "DreamSky.h"
#include "DreamSkyCloudRenderer.h"
#include "DreamSkyEclipse.h"
#include "DreamSkyModifier.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamWorldCelestial.h"
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
	Look.Aurora = Style->EvaluateAuroraPresence(Clock.GetCelestialSettings());
	if (Style->Eclipse.bFromSunAndMoon && Clock.GetCelestialSettings().Mode == EDreamCelestialMode::Realistic)
	{
		FVector2D Offset;
		Look.SolarEclipse = DreamSky::Eclipse::Solar(Celestial.SunDirection, Celestial.MoonDirection, Offset);
		Look.SolarEclipseOffset = Offset;
		Look.LunarEclipse = DreamSky::Eclipse::Lunar(Celestial.SunDirection, Celestial.MoonDirection, Offset);
		Look.LunarEclipseOffset = Offset;
	}
	Style->ApplyWeather(Clock.GetEnvironment(), Target, Look);
	// Indoors goes in before the easing, so walking through a door fades the exposure rather than cutting it.
	Style->ApplyInterior(Clock.GetViewOcclusion(), Target);
	// The level's own scales over the style's lighting (everything below, lights and parameters alike, sees them).
	Sky->ApplyLightingOverrides(Target);

	// Ease while the clock runs; snap on jumps (scrubbing, set time, snapshots; every frame in cinematic updates) and while
	// it stands still.
	const bool bJumped = !bHasApplied || Clock.GetTimeJumpSerial() != LastJumpSerial;
	const bool bSnap = bJumped || !Clock.IsClockRunning() || Style->LightResponseTime <= 0.0f;
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

	// The sky modifiers over the eased lighting (they fade in and out themselves), the weather's and those the sky holds by
	// a condition among them; and the sky's conditional post processes. (Those follow their conditions with the clock
	// stopped as well: a door walked through still fades; a jump of the clock cuts them.)
	++UpdateSerial;
	TakeWeatherModifiers(Clock.GetEnvironment());
	Sky->UpdateConditions(*this, Clock.GetTimeOfDay(), Clock.GetEnvironment(), Clock.GetViewOcclusion(), DeltaTime, bJumped);
	Applied = Eased;
	// (In cinematic updates every frame stands on its own: the fades are where they are heading.)
	ApplyModifiers(Applied, Look, DeltaTime, Clock.IsCinematicMode());
	ApplyEclipses(Applied, Look, *Style);
	UpdateModifierPostProcess(Sky);

	// A lightning flash goes on top, so it is as sudden as it should be.
	const FDreamSkyLighting Modified = Applied;
	Style->ApplyFlash(Clock.GetEnvironment(), Applied);
	Sky->ApplyLighting(Applied, *Style, bSnap);
	Sky->UpdateDome(Clock.GetViewLocation(), Clock.HasViewLocation());
	Sky->UpdateCloudShadow(Clock.GetViewLocation());
	bStylizedClouds = Sky->GetEffectiveCloudLook() != EDreamSkyCloudLook::Realistic;
	WriteParameters(Clock, *Style, Sky->DrawsClouds());
	LogLighting(Clock);
	Applied = Modified;
}

float UDreamSkySubsystem::GetLightShaftStrength() const
{
	const ADreamSky* Sky = GetSky();
	const UDreamSkyStyle* Style = Sky ? Sky->GetEffectiveStyle() : nullptr;
	if (!Style || !bHasApplied)
	{
		return 0.0f;
	}
	const FDreamSkyLightShafts& S = Style->LightShafts;
	const float Broken = FMath::SmoothStep(FMath::Min(S.CoverStart, S.CoverFull - 0.01f), S.CoverFull, Look.CloudCoverage);
	const float Up = FMath::SmoothStep(0.0f, 0.05f, float(Applied.MainLightDirection.Z));
	return FMath::Max(Look.LightShafts, 0.0f) * Broken * Up;
}

void UDreamSkySubsystem::ApplyEclipses(FDreamSkyLighting& L, FDreamSkyLook& K, const UDreamSkyStyle& Style) const
{
	using namespace DreamSky::Eclipse;
	// A coverage a modifier set (or turned) gets the moon, or the umbra, a place where it covers that much: along the
	// way it stood, or from the lower left.
	auto Place = [](float Coverage, FVector2D& Offset, float R)
	{
		if (Coverage <= 0.0f || FMath::Abs(DiskOverlap(float(Offset.Size()), R) - Coverage) <= 1.0e-3f)
		{
			return;
		}
		const FVector2D Way = Offset.Size() > 1.0e-3 && Offset.Size() < 100.0 ? Offset.GetSafeNormal() : FVector2D(-0.6, -0.8);
		Offset = Way * DistanceForOverlap(Coverage, R);
	};
	K.SolarEclipse = FMath::Clamp(K.SolarEclipse, 0.0f, 1.0f);
	K.LunarEclipse = FMath::Clamp(K.LunarEclipse, 0.0f, 1.0f);
	Place(K.SolarEclipse, K.SolarEclipseOffset, float(MoonToSun));
	Place(K.LunarEclipse, K.LunarEclipseOffset, float(UmbraRadius / MoonRadius));

	// The sun's light goes with its disk; the sky's, and the light it gives, near totality (never quite all of it: the
	// horizon all round stays lit, a twilight glow).
	if (K.SolarEclipse > 0.0f)
	{
		const float Left = 1.0f - K.SolarEclipse;
		const float Sky = FMath::Max(Left, 0.004f);
		L.SunIlluminance *= Left;
		L.SkyLightIntensity *= Sky;
		L.AmbientSky *= Sky;
		L.AmbientEquator *= Sky;
		L.AmbientGround *= Sky;
		K.Zenith *= Sky;
		K.Horizon *= FMath::Max(Left, 0.02f);
		K.Ground *= Sky;
		K.SunHalo *= Sky;
		K.CloudLit *= Sky;
		K.CloudShadow *= Sky;
		K.StarIntensity = FMath::Max(K.StarIntensity, FMath::SmoothStep(0.95f, 1.0f, K.SolarEclipse));
	}
	// The moon in the umbra: its light failing to a dark copper.
	if (K.LunarEclipse > 0.0f)
	{
		L.MoonIlluminance *= FMath::Lerp(1.0f, FMath::Clamp(Style.Eclipse.BloodMoonLight, 0.0f, 1.0f), K.LunarEclipse);
		L.MoonColor = FMath::Lerp(L.MoonColor, L.MoonColor * Style.Eclipse.BloodMoonColor, K.LunarEclipse);
		K.MoonHalo *= FMath::Lerp(1.0f, 0.05f, K.LunarEclipse);
	}
	if (K.SolarEclipse > 0.0f || K.LunarEclipse > 0.0f)
	{
		L.MainLightColor = L.bMainLightIsMoon ? L.MoonColor : L.SunColor;
		L.MainLightIlluminance = L.bMainLightIsMoon ? L.MoonIlluminance : L.SunIlluminance;
	}
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

void UDreamSkySubsystem::ApplyModifiers(FDreamSkyLighting& Lighting, FDreamSkyLook& InOutLook, float DeltaTime, bool bAtOnce)
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
		M.Progress = M.Rate > 0.0f && !bAtOnce ? FMath::FInterpConstantTo(M.Progress, M.Target, Dt, M.Rate) : M.Target;
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
	// The aurora toward the magnetic pole (north, or south below the equator in the realistic mode), as bright as the
	// night shows it.
	{
		const FDreamCelestialSettings& Settings = Clock.GetCelestialSettings();
		const bool bSouth = Settings.Mode == EDreamCelestialMode::Realistic && Settings.Latitude < 0.0;
		const FVector Pole = DreamWorld::Celestial::DirectionFromAngles(0.0, bSouth ? 180.0 : 0.0, Settings.NorthYaw);
		const float Strength = FMath::Max(Look.Aurora, 0.0f) * UDreamSkyStyle::AuroraNight(Applied.SunElevation) * FMath::Max(Style.Aurora.Brightness, 0.0f)
			* (Style.Aurora.bStylizedTones && bStylizedClouds ? -1.0f : 1.0f);
		Clock.SetVectorParameter(MPC_Aurora, FLinearColor(Strength, Style.Aurora.Speed, float(Pole.X), float(Pole.Y)));
		Clock.SetVectorParameter(MPC_AuroraLow, With(Style.Aurora.LowColor, Style.Aurora.ArcDistance));
		Clock.SetVectorParameter(MPC_AuroraHigh, With(Style.Aurora.HighColor, FMath::Max(Style.Aurora.ArcWidth, 1.0f)));
	}
	// The eclipses: the moon on the sun (and the corona near totality), the umbra on the moon.
	{
		const float Corona = FMath::Max(Style.Eclipse.Corona, 0.0f) * FMath::SmoothStep(0.96f, 1.0f, Look.SolarEclipse);
		Clock.SetVectorParameter(MPC_Eclipse, FLinearColor(Look.SolarEclipse, float(Look.SolarEclipseOffset.X), float(Look.SolarEclipseOffset.Y), Corona));
		Clock.SetVectorParameter(MPC_LunarEclipse, FLinearColor(Look.LunarEclipse, float(Look.LunarEclipseOffset.X), float(Look.LunarEclipseOffset.Y),
			float(DreamSky::Eclipse::UmbraRadius / DreamSky::Eclipse::MoonRadius)));
	}
	// The night's own glow, and a city's light from the horizon (weather haze spreading it further up and brighter).
	{
		const float Night = UDreamSkyStyle::GlowNight(Applied.SunElevation);
		const FLinearColor Airglow = Style.NightGlow.Airglow * (FMath::Max(Look.AirglowScale, 0.0f) * Night);
		Clock.SetVectorParameter(MPC_NightGlow, With(Airglow, FMath::Max(Style.NightGlow.CloudUnderLight, 0.0f)));
		const float City = FMath::Max(Look.LightPollution, 0.0f) * Night * (1.0f + Look.Haze.A);
		const float Climb = FMath::Clamp(Style.NightGlow.Climb, 0.01f, 1.0f) * (1.0f + 1.5f * Look.Haze.A);
		Clock.SetVectorParameter(MPC_LightPollution, With(Style.NightGlow.LightPollutionColor * City, Climb));
	}

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
