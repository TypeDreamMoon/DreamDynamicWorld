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
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSky)

static TAutoConsoleVariable<int32> CVarDreamSkyClouds(
	TEXT("DreamSky.Clouds"), -1,
	TEXT("What draws the clouds: -1 = each Dream Sky's own choice, 0 = toon (the sky dome), 1 = volumetric (the engine's cloud layer)."),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarDreamSkyCloudMaterial(
	TEXT("DreamSky.CloudMaterial"), FString(),
	TEXT("The volumetric clouds' material, as an object path (for example /DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted); empty = each Dream Sky's own."),
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
	if (OverriddenStyle)
	{
		return OverriddenStyle;
	}
	if (Style)
	{
		return Style;
	}
	return UDreamSkySubsystem::GetDefaultStyle();
}

void ADreamSky::RebuildOverriddenStyle()
{
	UDreamSkyStyle* Base = Style ? Style.Get() : UDreamSkySubsystem::GetDefaultStyle();
	const bool bAny = CloudOverrides.TouchesStyle() || LightOverrides.TouchesStyle() || AmbientOverrides.TouchesStyle() || ExposureOverrides.TouchesStyle();
	if (!Base || !bAny)
	{
		OverriddenStyle = nullptr;
		return;
	}
	// A private copy of the preset, never saved: the preset stays as it is for every other level using it.
	FObjectDuplicationParameters Params(Base, this);
	Params.FlagMask = RF_AllFlags & ~(RF_Standalone | RF_Public | RF_MarkAsRootSet | RF_ArchetypeObject);
	Params.ApplyFlags = RF_Transient;
	OverriddenStyle = Cast<UDreamSkyStyle>(StaticDuplicateObjectEx(Params));
	if (OverriddenStyle)
	{
		CloudOverrides.ApplyTo(*OverriddenStyle);
		LightOverrides.ApplyTo(*OverriddenStyle);
		AmbientOverrides.ApplyTo(*OverriddenStyle);
		ExposureOverrides.ApplyTo(*OverriddenStyle);
	}
}

void ADreamSky::RefreshOverrides()
{
	RebuildOverriddenStyle();
	ApplyComponentOverrides(true);
	bCloudMaterialDirty = true;
	if (const UDreamSkyStyle* Effective = GetEffectiveStyle())
	{
		ApplyClouds(*Effective);
	}
}

void ADreamSky::ApplyLightingOverrides(FDreamSkyLighting& L) const
{
	const float Sun = LightOverrides.bOverride_SunIntensity ? FMath::Max(0.0f, LightOverrides.SunIntensity) : 1.0f;
	const float Moon = LightOverrides.bOverride_MoonIntensity ? FMath::Max(0.0f, LightOverrides.MoonIntensity) : 1.0f;
	L.SunIlluminance *= Sun;
	L.MoonIlluminance *= Moon;
	L.MainLightIlluminance *= L.bMainLightIsMoon ? Moon : Sun;
	if (AmbientOverrides.bOverride_SkyLightIntensity)
	{
		L.SkyLightIntensity *= FMath::Max(0.0f, AmbientOverrides.SkyLightIntensity);
	}
	if (AmbientOverrides.bOverride_FogDensity)
	{
		L.FogDensity *= FMath::Max(0.0f, AmbientOverrides.FogDensity);
	}
	// A brighter picture is a lower exposure value.
	if (ExposureOverrides.bOverride_Compensation)
	{
		L.ExposureEV100 -= ExposureOverrides.Compensation;
	}
}

void ADreamSky::ApplyComponentOverrides(bool bRestoreUnticked)
{
	// What an unticked setting goes back to: the sky's own default (only on an edit of the overrides, so a light set
	// by hand in the component tree keeps its settings otherwise).
	const ADreamSky* Defaults = GetDefault<ADreamSky>();
	const FDreamSkyLightOverrides& O = LightOverrides;
	auto Want = [bRestoreUnticked](bool bTicked, auto Value, auto Default, auto Current)
	{
		return bTicked ? Value : (bRestoreUnticked ? Default : Current);
	};
	auto ApplyLight = [&](UDirectionalLightComponent* Light, const UDirectionalLightComponent* Default, bool bSun)
	{
		if (!Light || !Default)
		{
			return;
		}
		const float Angle = bSun ? Want(O.bOverride_SunSourceAngle, O.SunSourceAngle, Default->LightSourceAngle, Light->LightSourceAngle)
			: Want(O.bOverride_MoonSourceAngle, O.MoonSourceAngle, Default->LightSourceAngle, Light->LightSourceAngle);
		if (Light->LightSourceAngle != Angle)
		{
			Light->SetLightSourceAngle(Angle);
		}
		const float Distance = Want(O.bOverride_ShadowDistance, O.ShadowDistance, Default->DynamicShadowDistanceMovableLight, Light->DynamicShadowDistanceMovableLight);
		if (Light->DynamicShadowDistanceMovableLight != Distance)
		{
			Light->SetDynamicShadowDistanceMovableLight(Distance);
		}
		const int32 Cascades = Want(O.bOverride_ShadowCascades, O.ShadowCascades, Default->DynamicShadowCascades, Light->DynamicShadowCascades);
		if (Light->DynamicShadowCascades != Cascades)
		{
			Light->SetDynamicShadowCascades(Cascades);
		}
		const float Scattering = Want(O.bOverride_VolumetricScattering, O.VolumetricScattering, Default->VolumetricScatteringIntensity, Light->VolumetricScatteringIntensity);
		if (Light->VolumetricScatteringIntensity != Scattering)
		{
			Light->SetVolumetricScatteringIntensity(Scattering);
		}
		bool bDirty = false;
		const float Contact = Want(O.bOverride_ContactShadowLength, O.ContactShadowLength, Default->ContactShadowLength, Light->ContactShadowLength);
		if (Light->ContactShadowLength != Contact)
		{
			Light->ContactShadowLength = Contact;
			bDirty = true;
		}
		if (bSun)
		{
			const float CloudShadow = Want(O.bOverride_CloudShadowStrength, O.CloudShadowStrength, Default->CloudShadowStrength, Light->CloudShadowStrength);
			if (Light->CloudShadowStrength != CloudShadow)
			{
				Light->CloudShadowStrength = CloudShadow;
				bDirty = true;
			}
		}
		if (bDirty)
		{
			Light->MarkRenderStateDirty();
		}
	};
	ApplyLight(SunLight, Defaults->SunLight, true);
	ApplyLight(MoonLight, Defaults->MoonLight, false);

	const FDreamSkyAmbientOverrides& A = AmbientOverrides;
	const UExponentialHeightFogComponent* DefaultFog = Defaults->HeightFog;
	if (HeightFog && DefaultFog)
	{
		const float Start = Want(A.bOverride_FogStartDistance, A.FogStartDistance, DefaultFog->StartDistance, HeightFog->StartDistance);
		if (HeightFog->StartDistance != Start)
		{
			HeightFog->SetStartDistance(Start);
		}
		const bool bVolumetric = Want(A.bOverride_VolumetricFog, A.bVolumetricFog, bool(DefaultFog->bEnableVolumetricFog), bool(HeightFog->bEnableVolumetricFog));
		if (bool(HeightFog->bEnableVolumetricFog) != bVolumetric)
		{
			HeightFog->SetVolumetricFog(bVolumetric);
		}
		const float VolumetricDistance = Want(A.bOverride_VolumetricFogDistance, A.VolumetricFogDistance, DefaultFog->VolumetricFogDistance, HeightFog->VolumetricFogDistance);
		if (HeightFog->VolumetricFogDistance != VolumetricDistance)
		{
			HeightFog->SetVolumetricFogDistance(VolumetricDistance);
		}
		const float Extinction = Want(A.bOverride_VolumetricFogExtinction, A.VolumetricFogExtinction, DefaultFog->VolumetricFogExtinctionScale, HeightFog->VolumetricFogExtinctionScale);
		if (HeightFog->VolumetricFogExtinctionScale != Extinction)
		{
			HeightFog->SetVolumetricFogExtinctionScale(Extinction);
		}
	}
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
		// The component starts with the engine's default cloud material: ours replaces it (or the console's).
		UMaterialInterface* Wanted = nullptr;
		const FString Override = CVarDreamSkyCloudMaterial.GetValueOnGameThread();
		static FString MissingOverride;
		if (!Override.IsEmpty() && Override != MissingOverride)
		{
			const TSoftObjectPtr<UMaterialInterface> Chosen{FSoftObjectPath(Override)};
			Wanted = Chosen.Get() ? Chosen.Get() : Chosen.LoadSynchronous();
			// (A path that loads nothing is not tried again every frame.)
			MissingOverride = Wanted ? FString() : Override;
		}
		if (!Wanted)
		{
			Wanted = VolumetricCloudMaterial.Get() ? VolumetricCloudMaterial.Get() : VolumetricCloudMaterial.LoadSynchronous();
		}
		// The material overrides go on an instance of it.
		if (Wanted && CloudOverrides.TouchesMaterial())
		{
			if (!CloudMaterialInstance || CloudMaterialInstance->Parent != Wanted)
			{
				CloudMaterialInstance = UMaterialInstanceDynamic::Create(Wanted, this);
				bCloudMaterialDirty = true;
			}
			if (bCloudMaterialDirty)
			{
				CloudMaterialInstance->ClearParameterValues();
				CloudOverrides.ApplyTo(*CloudMaterialInstance);
				bCloudMaterialDirty = false;
			}
			Wanted = CloudMaterialInstance;
		}
		else
		{
			CloudMaterialInstance = nullptr;
		}
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
		if (!FMath::IsNearlyEqual(VolumetricClouds->ShadowTracingDistance, VolumetricShadowTracingDistance, 1.0e-3f))
		{
			VolumetricClouds->SetShadowTracingDistance(VolumetricShadowTracingDistance);
		}
		if (!FMath::IsNearlyEqual(VolumetricClouds->SkyLightCloudBottomOcclusion, VolumetricBaseSkyOcclusion, 1.0e-3f))
		{
			VolumetricClouds->SetSkyLightCloudBottomOcclusion(VolumetricBaseSkyOcclusion);
		}
		// The shadow map and the sky light's occlusion map cover the clouds that are traced (the engine's default
		// spreads them over 150 km around the view, 586 m a texel): drawing them costs about their texel count, each
		// texel a march through the clouds' material.
		const float MapExtentKm = FMath::Max(VolumetricTracingDistance, 10.0f);
		if (SunLight && (!FMath::IsNearlyEqual(SunLight->CloudShadowExtent, MapExtentKm, 1.0e-2f)
			|| !FMath::IsNearlyEqual(SunLight->CloudShadowMapResolutionScale, VolumetricShadowResolution, 1.0e-3f)))
		{
			SunLight->CloudShadowExtent = MapExtentKm;
			SunLight->CloudShadowMapResolutionScale = VolumetricShadowResolution;
			SunLight->MarkRenderStateDirty();
		}
		// (The sky light's occlusion is soft: half the shadow map's resolution is plenty.)
		const float SkyMapScale = FMath::Max(0.25f, 0.5f * VolumetricShadowResolution);
		if (SkyLight && (!FMath::IsNearlyEqual(SkyLight->CloudAmbientOcclusionExtent, MapExtentKm, 1.0e-2f)
			|| !FMath::IsNearlyEqual(SkyLight->CloudAmbientOcclusionMapResolutionScale, SkyMapScale, 1.0e-3f)))
		{
			SkyLight->CloudAmbientOcclusionExtent = MapExtentKm;
			SkyLight->CloudAmbientOcclusionMapResolutionScale = SkyMapScale;
			SkyLight->MarkRenderStateDirty();
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
	// The overrides are taken in here: placed actors in a game are loaded, not constructed.
	RebuildOverriddenStyle();
	ApplyComponentOverrides(false);
	bCloudMaterialDirty = true;
#if WITH_EDITOR
	if (!StyleChangedHandle.IsValid())
	{
		StyleChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &ADreamSky::HandleObjectPropertyChanged);
	}
#endif
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
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(StyleChangedHandle);
	StyleChangedHandle.Reset();
#endif
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
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, Style) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, CloudOverrides)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, LightOverrides) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, AmbientOverrides)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, ExposureOverrides))
	{
		RefreshOverrides();
	}
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, Clouds) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricLayerThickness)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricTracingDistance) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricSampleCountScale)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricCloudMaterial)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricBaseSkyOcclusion) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricShadowTracingDistance)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricShadowResolution))
	{
		if (const UDreamSkyStyle* Effective = GetEffectiveStyle())
		{
			ApplyClouds(*Effective);
		}
	}
}

void ADreamSky::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// The preset was edited while the level is open: the private copy takes the change in.
	if (Object && OverriddenStyle && Object != OverriddenStyle && Object == (Style ? Style.Get() : UDreamSkySubsystem::GetDefaultStyle()))
	{
		RebuildOverriddenStyle();
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
		// The dome already shows the sky's colours at the horizon; fogging it again flattens the gradient. The
		// engine's volumetric clouds take the fog wherever they are, though: then the dome takes it too, or far
		// clouds fogged over an unfogged sky would stand as a bright band along the horizon.
		const bool bVolumetric = GetEffectiveClouds() == EDreamSkyClouds::Volumetric;
		HeightFog->SetFogCutoffDistance(bVolumetric ? 0.0f : GetEffectiveDomeRadius() * 0.8f);
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
		const bool bAuto = InStyle.ExposureMode == EDreamSkyExposureMode::Auto;
		// Manual exposure without the physical camera: the bias is the exposure, so EV100 = -bias. Auto: the eye
		// adapts, held within the style's range either side of the curve's exposure.
		S.bOverride_AutoExposureMethod = bManual || bAuto;
		S.AutoExposureMethod = bAuto ? AEM_Histogram : AEM_Manual;
		S.bOverride_AutoExposureApplyPhysicalCameraExposure = bManual || bAuto;
		S.AutoExposureApplyPhysicalCameraExposure = false;
		S.bOverride_AutoExposureBias = bManual || bAuto;
		S.AutoExposureBias = bManual ? -L.ExposureEV100 : 0.0f;
		S.bOverride_AutoExposureMinBrightness = bAuto;
		S.AutoExposureMinBrightness = L.ExposureEV100 - InStyle.ExposureAutoRange;
		S.bOverride_AutoExposureMaxBrightness = bAuto;
		S.AutoExposureMaxBrightness = L.ExposureEV100 + InStyle.ExposureAutoRange;
		S.bOverride_AutoExposureSpeedUp = bAuto;
		S.AutoExposureSpeedUp = InStyle.ExposureSpeedUp;
		S.bOverride_AutoExposureSpeedDown = bAuto;
		S.AutoExposureSpeedDown = InStyle.ExposureSpeedDown;
		PostProcess->bEnabled = bManual || bAuto;
	}
}

void ADreamSky::RecaptureSkyLight()
{
	if (SkyLight && !SkyLight->bRealTimeCapture)
	{
		SkyLight->RecaptureSky();
	}
}
