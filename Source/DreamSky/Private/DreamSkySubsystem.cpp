// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkySubsystem.h"
#include "DreamSky.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkySubsystem)

DECLARE_CYCLE_STAT(TEXT("DreamSky Update"), STAT_DreamSkyUpdate, STATGROUP_Game);

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
		RecaptureTask = Clock->GetScheduler().Register(TEXT("DreamSky.Recapture"), 0, 0.5, 0.0, [this] { return RecaptureIfChanged(); });
	}
}

void UDreamSkySubsystem::Deinitialize()
{
	if (UDreamWorldSubsystem* Clock = ClockSubsystem.Get())
	{
		Clock->OnWorldTick.Remove(TickHandle);
		Clock->GetScheduler().Unregister(RecaptureTask);
	}
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

	// Ease while the clock runs; snap on jumps (scrubbing, set time, snapshots) and while it stands still.
	const bool bSnap = !bHasApplied || Clock.GetTimeJumpSerial() != LastJumpSerial || !Clock.IsClockRunning() || Style->LightResponseTime <= 0.0f;
	LastJumpSerial = Clock.GetTimeJumpSerial();
	if (bSnap)
	{
		Applied = Target;
	}
	else
	{
		const float Alpha = 1.0f - FMath::Exp(-FMath::Max(DeltaTime, 0.0f) / Style->LightResponseTime);
		Applied = BlendLighting(Applied, Target, Alpha);
	}
	bHasApplied = true;

	// A lightning flash goes on top of the eased lighting, so it is as sudden as it should be.
	const FDreamSkyLighting Eased = Applied;
	Style->ApplyFlash(Clock.GetEnvironment(), Applied);
	Sky->ApplyLighting(Applied, *Style, bSnap);
	Sky->UpdateDome(Clock.GetViewLocation(), Clock.HasViewLocation());
	WriteParameters(Clock, *Style);
	LogLighting(Clock);
	Applied = Eased;
}

void UDreamSkySubsystem::WriteParameters(UDreamWorldSubsystem& Clock, const UDreamSkyStyle& Style)
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
	Clock.SetVectorParameter(MPC_Cloud0, FLinearColor(Look.CloudCoverage, Style.CloudSoftness, Style.CloudScale, float(Style.CloudBands)));
	Clock.SetVectorParameter(MPC_Cloud1, FLinearColor(float(FMath::Cos(Yaw) * Distance), float(FMath::Sin(Yaw) * Distance), Style.CloudCurvature, Style.CloudHorizonFade));
	Clock.SetVectorParameter(MPC_CloudLit, With(Look.CloudLit, Style.CloudRim));
	Clock.SetVectorParameter(MPC_CloudShadow, With(Look.CloudShadow, Style.CloudOpacity));
	Clock.SetVectorParameter(MPC_CloudLayer, FLinearColor(Style.CloudLayerHeight, Look.CloudDarkness, Style.StormCloudDarkening, 0.0f));
	Clock.SetVectorParameter(MPC_SkyExtra, FLinearColor(Style.CloudSkyTint, Style.CloudSunGlow, Style.SunDiskInSkyLight, Applied.FlashGain));
	Clock.SetVectorParameter(MPC_SkyHaze, Look.Haze);

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
