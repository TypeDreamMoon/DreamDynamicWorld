// Copyright DreamMoon. All Rights Reserved.

#include "DreamSky.h"
#include "DreamSkyModule.h"
#include "DreamSkyStyle.h"
#include "DreamSkySubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSky)

static TAutoConsoleVariable<int32> CVarDreamSkyClouds(
	TEXT("DreamSky.Clouds"), -1,
	TEXT("What draws the clouds: -1 = each Dream Sky's own choice, 0 = toon (the sky dome), 1 = volumetric (the engine's cloud layer)."),
	ECVF_Default);

namespace DreamSky::Actor
{
	/** The engine's sky sphere: a unit-ish sphere whose normals face inward. */
	static const TCHAR* DomeMeshPath = TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere");
	static const TCHAR* DefaultMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamSky.M_DreamSky");
	static const TCHAR* DefaultCloudMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud.M_DreamVolumetricCloud");

	static bool Differs(float A, float B, float Relative, float Absolute)
	{
		return FMath::Abs(A - B) > FMath::Max(Absolute, Relative * FMath::Max(FMath::Abs(A), FMath::Abs(B)));
	}

	static bool Differs(const FLinearColor& A, const FLinearColor& B, float Tolerance)
	{
		return FMath::Abs(A.R - B.R) > Tolerance || FMath::Abs(A.G - B.G) > Tolerance || FMath::Abs(A.B - B.B) > Tolerance;
	}
}

ADreamSky::ADreamSky()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
	SkyDome->SetupAttachment(SceneRoot);
	SkyDome->SetMobility(EComponentMobility::Movable);
	SkyDome->SetUsingAbsoluteLocation(true);
	SkyDome->SetUsingAbsoluteRotation(true);
	SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyDome->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SkyDome->SetCastShadow(false);
	SkyDome->bCastDynamicShadow = false;
	SkyDome->bAffectDynamicIndirectLighting = false;
	SkyDome->bAffectDistanceFieldLighting = false;
	SkyDome->bReceivesDecals = false;
	SkyDome->bUseAsOccluder = false;
	SkyDome->bNeverDistanceCull = true;
	SkyDome->bAllowCullDistanceVolume = false;
	SkyDome->bVisibleInRayTracing = false;
	SkyDome->bVisibleInReflectionCaptures = true;
	SkyDome->bVisibleInRealTimeSkyCaptures = true;
	SkyDome->bSelectable = false;
	SkyDome->SetCanEverAffectNavigation(false);

	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(SceneRoot);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetUsingAbsoluteRotation(true);
	// The atmosphere sun: what other systems look for when they want "the sun".
	SunLight->bAtmosphereSunLight = true;
	SunLight->AtmosphereSunLightIndex = 0;
	SunLight->Intensity = 10.0f;
	SunLight->CastShadows = true;
	// The volumetric clouds' shadows on the ground (nothing without them).
	SunLight->bCastCloudShadows = true;

	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLight->SetupAttachment(SceneRoot);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->SetUsingAbsoluteRotation(true);
	// The second atmosphere light: the engine's volumetric clouds are lit by the atmosphere lights only, so the
	// moon lights them at night. Without a sky atmosphere this changes nothing else.
	MoonLight->bAtmosphereSunLight = true;
	MoonLight->AtmosphereSunLightIndex = 1;
	MoonLight->Intensity = 0.0f;
	MoonLight->CastShadows = true;

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = SLS_CapturedScene;
	SkyLight->bRealTimeCapture = true;
	SkyLight->bLowerHemisphereIsBlack = false;

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(SceneRoot);
	HeightFog->SetMobility(EComponentMobility::Movable);

	LightningLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("LightningLight"));
	LightningLight->SetupAttachment(SceneRoot);
	LightningLight->SetMobility(EComponentMobility::Movable);
	LightningLight->SetUsingAbsoluteRotation(true);
	LightningLight->bAtmosphereSunLight = false;
	LightningLight->Intensity = 0.0f;
	LightningLight->CastShadows = false;
	LightningLight->bAffectsWorld = true;

	VolumetricClouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("VolumetricClouds"));
	VolumetricClouds->SetupAttachment(SceneRoot);
	VolumetricClouds->SetMobility(EComponentMobility::Movable);
	VolumetricClouds->SetVisibility(false);
	// The sky light reaching a cloud's underside from below is the dome's ground colour: mostly held back, so
	// undersides keep the sky's blue (and the material's own shade glow) rather than turning brown.
	VolumetricClouds->SkyLightCloudBottomOcclusion = 0.85f;

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;
	// Below any volume the project places, so a project volume that sets exposure wins.
	PostProcess->Priority = -1.0f;

	SkyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(DreamSky::Actor::DefaultMaterialPath));
	VolumetricCloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(DreamSky::Actor::DefaultCloudMaterialPath));
}

UDreamSkyStyle* ADreamSky::GetEffectiveStyle() const
{
	if (Style)
	{
		return Style;
	}
	return UDreamSkySubsystem::GetDefaultStyle();
}

EDreamSkyClouds ADreamSky::GetEffectiveClouds() const
{
	const int32 Override = CVarDreamSkyClouds.GetValueOnGameThread();
	if (Override == 0)
	{
		return EDreamSkyClouds::Toon;
	}
	if (Override == 1)
	{
		return EDreamSkyClouds::Volumetric;
	}
	return Clouds;
}

float ADreamSky::GetEffectiveDomeRadius() const
{
	// The engine's clouds stop at whatever the depth buffer holds, and the dome is in it.
	return GetEffectiveClouds() == EDreamSkyClouds::Volumetric ? FMath::Max(DomeRadius, VolumetricTracingDistance * 100000.0f * 1.1f) : DomeRadius;
}

void ADreamSky::ApplyClouds(const UDreamSkyStyle& InStyle)
{
	if (!VolumetricClouds)
	{
		return;
	}
	const bool bVolumetric = GetEffectiveClouds() == EDreamSkyClouds::Volumetric;
	if (bVolumetric)
	{
		// The component starts with the engine's default cloud material: ours replaces it.
		UMaterialInterface* Wanted = VolumetricCloudMaterial.Get() ? VolumetricCloudMaterial.Get() : VolumetricCloudMaterial.LoadSynchronous();
		if (Wanted && VolumetricClouds->GetMaterial() != Wanted)
		{
			VolumetricClouds->SetMaterial(Wanted);
		}
		const float BottomKm = FMath::Max(0.0f, InStyle.CloudLayerHeight / 100000.0f);
		if (!FMath::IsNearlyEqual(VolumetricClouds->LayerBottomAltitude, BottomKm, 1.0e-3f))
		{
			VolumetricClouds->SetLayerBottomAltitude(BottomKm);
		}
		if (!FMath::IsNearlyEqual(VolumetricClouds->LayerHeight, VolumetricLayerThickness, 1.0e-3f))
		{
			VolumetricClouds->SetLayerHeight(VolumetricLayerThickness);
		}
		if (!FMath::IsNearlyEqual(VolumetricClouds->TracingMaxDistance, VolumetricTracingDistance, 1.0e-3f))
		{
			VolumetricClouds->SetTracingMaxDistance(VolumetricTracingDistance);
		}
		if (!FMath::IsNearlyEqual(VolumetricClouds->ViewSampleCountScale, VolumetricSampleCountScale, 1.0e-3f))
		{
			VolumetricClouds->SetViewSampleCountScale(VolumetricSampleCountScale);
		}
	}
	if (VolumetricClouds->GetVisibleFlag() != bVolumetric)
	{
		VolumetricClouds->SetVisibility(bVolumetric);
		ApplyDome();
	}
	// The volumetric clouds shade the sky light under them (part of how they read as solid); the toon clouds are
	// painted and cast none.
	if (SkyLight && bool(SkyLight->bCloudAmbientOcclusion) != bVolumetric)
	{
		SkyLight->bCloudAmbientOcclusion = bVolumetric;
		SkyLight->MarkRenderStateDirty();
	}
}

FDreamWorldClockConfig ADreamSky::MakeClockConfig() const
{
	FDreamWorldClockConfig Config = Clock;
	const FDateTime Date = Clock.StartTime.GetDate();
	Config.StartTime = Date + FTimespan::FromHours(double(FMath::Clamp(TimeOfDay, 0.0f, 23.9999f)));
	return Config;
}

void ADreamSky::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDome();
	SkyLight->SetRealTimeCapture(bRealTimeSkyCapture);
}

void ADreamSky::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UWorld* World = GetWorld();
	if (!World || IsTemplate())
	{
		return;
	}
	if (UDreamSkySubsystem* Sky = World->GetSubsystem<UDreamSkySubsystem>())
	{
		Sky->RegisterSky(this);
	}
	// In editor worlds the level's clock is shown as soon as the level is open; game worlds take it in BeginPlay.
	if (!World->IsGameWorld())
	{
		PushClockToWorld(true);
	}
}

void ADreamSky::PostUnregisterAllComponents()
{
	if (UWorld* World = GetWorld())
	{
		if (UDreamSkySubsystem* Sky = World->GetSubsystem<UDreamSkySubsystem>())
		{
			Sky->UnregisterSky(this);
		}
	}
	Super::PostUnregisterAllComponents();
}

void ADreamSky::BeginPlay()
{
	Super::BeginPlay();
	PushClockToWorld(true);
}

void ADreamSky::PushClockToWorld(bool bResetTime)
{
	if (UDreamWorldSubsystem* WorldClock = UDreamWorldSubsystem::Get(this))
	{
		WorldClock->ApplyClockConfig(MakeClockConfig(), bResetTime);
	}
}

void ADreamSky::SetTimeOfDayAndClock(float Hours, bool bMarkModified)
{
	if (bMarkModified)
	{
		// Records the old value for an enclosing undo transaction, and marks the level dirty.
		Modify();
	}
	TimeOfDay = FMath::Clamp(Hours, 0.0f, 23.99f);
	if (UDreamWorldSubsystem* WorldClock = UDreamWorldSubsystem::Get(this))
	{
		WorldClock->SetTimeOfDay(TimeOfDay);
	}
}

#if WITH_EDITOR
void ADreamSky::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Property = PropertyChangedEvent.GetMemberPropertyName();
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, TimeOfDay) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, Clock))
	{
		PushClockToWorld(true);
	}
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, SkyMaterial) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, DomeRadius)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricTracingDistance))
	{
		ApplyDome();
	}
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricCloudMaterial) && VolumetricClouds)
	{
		VolumetricClouds->SetMaterial(VolumetricCloudMaterial.LoadSynchronous());
	}
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, Clouds) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricLayerThickness)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricTracingDistance) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricSampleCountScale))
	{
		if (const UDreamSkyStyle* Effective = GetEffectiveStyle())
		{
			ApplyClouds(*Effective);
		}
	}
}
#endif

void ADreamSky::ApplyDome()
{
	if (!SkyDome)
	{
		return;
	}
	if (!SkyDome->GetStaticMesh())
	{
		SkyDome->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, DreamSky::Actor::DomeMeshPath));
	}
	if (UMaterialInterface* Material = SkyMaterial.LoadSynchronous())
	{
		SkyDome->SetMaterial(0, Material);
	}
	if (const UStaticMesh* Mesh = SkyDome->GetStaticMesh())
	{
		const float MeshRadius = FMath::Max(1.0f, float(Mesh->GetBounds().SphereRadius));
		SkyDome->SetWorldScale3D(FVector(GetEffectiveDomeRadius() / MeshRadius));
	}
	if (HeightFog)
	{
		// The dome already shows the sky's colours at the horizon; fogging it again flattens the gradient.
		HeightFog->SetFogCutoffDistance(GetEffectiveDomeRadius() * 0.8f);
	}
}

void ADreamSky::UpdateDome(const FVector& ViewLocation, bool bHasView)
{
	if (!SkyDome || !bHasView)
	{
		return;
	}
	if (FVector::DistSquared(SkyDome->GetComponentLocation(), ViewLocation) > FMath::Square(GetEffectiveDomeRadius() * 0.02f))
	{
		SkyDome->SetWorldLocation(ViewLocation);
	}
}

void ADreamSky::ApplyDirectional(UDirectionalLightComponent* Light, const FVector& Direction, const FLinearColor& Color, float Lux, float ThresholdDeg, bool bShadows, bool bSnap)
{
	using namespace DreamSky::Actor;
	if (!Light)
	{
		return;
	}
	// The light travels away from the body. Kept pointing even at zero intensity: a black light is not
	// rendered at all (no shadow cost), and anything reading "where the sun is" still finds it.
	// bSnap (a jump, or the clock standing still) drops the thresholds to what is still a change; nothing is
	// written when nothing changed, since every write marks the light's render state dirty.
	const FVector Forward = -Direction.GetSafeNormal();
	if (!Forward.IsNearlyZero())
	{
		const double CosAngle = FVector::DotProduct(Light->GetForwardVector(), Forward);
		const double Threshold = FMath::Cos(FMath::DegreesToRadians(bSnap ? 0.001 : double(ThresholdDeg)));
		if (CosAngle < Threshold)
		{
			Light->SetWorldRotation(Forward.Rotation());
		}
	}
	const float Target = Lux > 1.0e-3f ? Lux : 0.0f;
	if (Differs(Light->Intensity, Target, bSnap ? 1.0e-5f : 0.003f, 1.0e-5f))
	{
		Light->SetIntensity(Target);
	}
	if (Differs(Light->GetLightColor(), Color, bSnap ? 1.0e-5f : 0.002f))
	{
		Light->SetLightColor(Color, false);
	}
	if (Light->CastShadows != bShadows)
	{
		Light->SetCastShadows(bShadows);
	}
}

void ADreamSky::ApplyLighting(const FDreamSkyLighting& L, const UDreamSkyStyle& InStyle, bool bSnap)
{
	using namespace DreamSky::Actor;
	ApplyDirectional(SunLight, L.SunLightDirection, L.SunColor, L.SunIlluminance, InStyle.LightRotationThreshold, true, bSnap);
	ApplyDirectional(MoonLight, L.MoonLightDirection, L.MoonColor, L.MoonIlluminance, InStyle.LightRotationThreshold, InStyle.bMoonCastsShadows, bSnap);
	// A flash is instant: no thresholds. Between flashes the light stays black (not rendered).
	ApplyDirectional(LightningLight, L.LightningDirection, L.LightningColor, L.LightningIlluminance, 0.0f, InStyle.bLightningCastsShadows, true);

	const float Relative = bSnap ? 1.0e-5f : 0.005f;
	const float ColorTolerance = bSnap ? 1.0e-5f : 0.002f;
	if (SkyLight)
	{
		if (Differs(SkyLight->Intensity, L.SkyLightIntensity, Relative, 1.0e-5f))
		{
			SkyLight->SetIntensity(L.SkyLightIntensity);
		}
		if (Differs(SkyLight->LowerHemisphereColor, L.AmbientGround, ColorTolerance))
		{
			SkyLight->SetLowerHemisphereColor(L.AmbientGround);
		}
		if (SkyLight->bRealTimeCapture != bRealTimeSkyCapture)
		{
			SkyLight->SetRealTimeCapture(bRealTimeSkyCapture);
		}
	}

	if (HeightFog)
	{
		if (Differs(HeightFog->FogInscatteringLuminance, L.FogColor, ColorTolerance))
		{
			HeightFog->SetFogInscatteringColor(L.FogColor);
		}
		if (Differs(HeightFog->FogDensity, L.FogDensity, Relative, 1.0e-7f))
		{
			HeightFog->SetFogDensity(L.FogDensity);
		}
		if (HeightFog->FogHeightFalloff != InStyle.FogHeightFalloff)
		{
			HeightFog->SetFogHeightFalloff(InStyle.FogHeightFalloff);
		}
		if (HeightFog->FogMaxOpacity != InStyle.FogMaxOpacity)
		{
			HeightFog->SetFogMaxOpacity(InStyle.FogMaxOpacity);
		}
	}

	if (PostProcess)
	{
		FPostProcessSettings& S = PostProcess->Settings;
		const bool bManual = InStyle.ExposureMode == EDreamSkyExposureMode::Manual;
		// Manual exposure without the physical camera: the bias is the exposure, so EV100 = -bias.
		S.bOverride_AutoExposureMethod = bManual;
		S.AutoExposureMethod = AEM_Manual;
		S.bOverride_AutoExposureApplyPhysicalCameraExposure = bManual;
		S.AutoExposureApplyPhysicalCameraExposure = false;
		S.bOverride_AutoExposureBias = bManual;
		S.AutoExposureBias = -L.ExposureEV100;
		PostProcess->bEnabled = bManual;
	}
}

void ADreamSky::RecaptureSkyLight()
{
	if (SkyLight && !SkyLight->bRealTimeCapture)
	{
		SkyLight->RecaptureSky();
	}
}
