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
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSky)

static TAutoConsoleVariable<int32> CVarDreamSkyClouds(
	TEXT("DreamSky.Clouds"), -1,
	TEXT("What draws the clouds: -1 = each Dream Sky's own choice, 0 = toon (the sky dome), 1 = volumetric (the engine's cloud layer),")
	TEXT(" 2 = the plugin's sky cloud renderer."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderLook(
	TEXT("DreamSky.CloudRender.Look"), -1,
	TEXT("How the sky cloud renderer draws the clouds: -1 = each Dream Sky's CloudLook, 0 = toon (the surface, lit in the")
	TEXT(" style's tones), 1 = realistic (the density integrated along the view, lit by scattering), 2 = painted (the density")
	TEXT(" integrated, painted in the style's tones by the light through it)."),
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
	static const TCHAR* CloudsBelowMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamSkyBelow.M_DreamSkyBelow");
	static const TCHAR* CloudShadowMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamCloudShadow.M_DreamCloudShadow");
	static const TCHAR* PaintedCloudMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted");
	/** The sky cloud renderer's ground shadow: texels a side, and how far round the view it reaches (cm). */
	static constexpr int32 CloudShadowResolution = 256;
	static constexpr float CloudShadowHalfExtent = 2400000.0f;
	/** cm: the sphere the clouds under the horizon are drawn on (anything nearer than this hides them, as it should). */
	static constexpr float CloudsBelowRadius = 50000.0f;
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

	CloudsBelow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CloudsBelow"));
	CloudsBelow->SetupAttachment(SceneRoot);
	CloudsBelow->SetMobility(EComponentMobility::Movable);
	CloudsBelow->SetUsingAbsoluteLocation(true);
	CloudsBelow->SetUsingAbsoluteRotation(true);
	CloudsBelow->SetUsingAbsoluteScale(true);
	CloudsBelow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CloudsBelow->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	CloudsBelow->SetCastShadow(false);
	CloudsBelow->bCastDynamicShadow = false;
	CloudsBelow->bAffectDynamicIndirectLighting = false;
	CloudsBelow->bAffectDistanceFieldLighting = false;
	CloudsBelow->bReceivesDecals = false;
	CloudsBelow->bUseAsOccluder = false;
	CloudsBelow->bNeverDistanceCull = true;
	CloudsBelow->bAllowCullDistanceVolume = false;
	CloudsBelow->bVisibleInRayTracing = false;
	CloudsBelow->bVisibleInReflectionCaptures = false;
	CloudsBelow->bVisibleInRealTimeSkyCaptures = false;
	CloudsBelow->bSelectable = false;
	CloudsBelow->SetCanEverAffectNavigation(false);
	// Drawn before any other translucency (rain, particles), which stands in front of it.
	CloudsBelow->SetTranslucentSortPriority(-1000);

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
	if (Override == 2)
	{
		return EDreamSkyClouds::Rendered;
	}
	return Clouds;
}

bool ADreamSky::CloudsShadowTheSun() const
{
	return GetEffectiveClouds() == EDreamSkyClouds::Volumetric && SunLight && SunLight->bCastCloudShadows && SunLight->CloudShadowStrength > 0.0f;
}

EDreamSkyCloudLook ADreamSky::GetEffectiveCloudLook() const
{
	const int32 Override = CVarDreamSkyCloudRenderLook.GetValueOnGameThread();
	return Override >= 0 ? EDreamSkyCloudLook(FMath::Min(Override, 2)) : CloudLook;
}

const UMaterialInterface* ADreamSky::GetCloudLookMaterial(bool bPainted) const
{
	using namespace DreamSky::Actor;
	// The level's volumetric cloud material (its overrides' instance while the engine's clouds draw), when it is of
	// this look (the painted material has a PaintGain), else the plugin's material of it.
	const UMaterialInterface* Mine = CloudMaterialInstance ? CloudMaterialInstance.Get()
		: (VolumetricCloudMaterial.Get() ? VolumetricCloudMaterial.Get() : VolumetricCloudMaterial.LoadSynchronous());
	float Unused = 0.0f;
	if (Mine && Mine->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PaintGain")), Unused) == bPainted)
	{
		return Mine;
	}
	TObjectPtr<UMaterialInterface>& Default = bPainted ? PaintedCloudMaterial : RealisticCloudMaterial;
	if (!Default)
	{
		Default = LoadObject<UMaterialInterface>(nullptr, bPainted ? PaintedCloudMaterialPath : DefaultCloudMaterialPath);
	}
	return Default;
}

void ADreamSky::GetCloudLookParameters(FDreamSkyCloudLookParameters& Out) const
{
	Out = FDreamSkyCloudLookParameters();
	auto Scalar = [](const UMaterialInterface* Material, const TCHAR* Name, float& Value)
	{
		if (Material)
		{
			Material->GetScalarParameterValue(FMaterialParameterInfo(Name), Value);
		}
	};
	auto Vector = [](const UMaterialInterface* Material, const TCHAR* Name, FLinearColor& Value)
	{
		if (Material)
		{
			Material->GetVectorParameterValue(FMaterialParameterInfo(Name), Value);
		}
	};
	const UMaterialInterface* Realistic = GetCloudLookMaterial(false);
	Scalar(Realistic, TEXT("CloudExtinction"), Out.Extinction);
	Scalar(Realistic, TEXT("CloudAlbedo"), Out.Albedo);
	Scalar(Realistic, TEXT("CloudGlow"), Out.Glow);
	Scalar(Realistic, TEXT("BaseGlow"), Out.BaseGlow);
	Scalar(Realistic, TEXT("DeckMottle"), Out.DeckMottle);
	Vector(Realistic, TEXT("ShadeTint"), Out.ShadeTint);
	Scalar(Realistic, TEXT("ViewThinning"), Out.ViewThinning);
	Scalar(Realistic, TEXT("ScatterForward"), Out.ScatterForward);
	Scalar(Realistic, TEXT("ScatterBackward"), Out.ScatterBackward);
	Scalar(Realistic, TEXT("ScatterBlend"), Out.ScatterBlend);
	Scalar(Realistic, TEXT("MultiScatter"), Out.MultiScatter);
	Scalar(Realistic, TEXT("MultiScatterOcclusion"), Out.MultiScatterOcclusion);
	Scalar(Realistic, TEXT("MultiScatterEccentricity"), Out.MultiScatterEccentricity);
	Out.SunScattering = VolumetricSunScattering;
	Out.ShadowTracingDistance = VolumetricShadowTracingDistance;
	const UMaterialInterface* Painted = GetCloudLookMaterial(true);
	Scalar(Painted, TEXT("CloudExtinction"), Out.PaintExtinction);
	Scalar(Painted, TEXT("PaintGain"), Out.PaintGain);
	Scalar(Painted, TEXT("EdgeSoftness"), Out.EdgeSoftness);
	Scalar(Painted, TEXT("HalfToneAt"), Out.HalfToneAt);
	Scalar(Painted, TEXT("LitToneAt"), Out.LitToneAt);
	Scalar(Painted, TEXT("ToneSoftness"), Out.ToneSoftness);
	Scalar(Painted, TEXT("SilverLining"), Out.SilverLining);
	Scalar(Painted, TEXT("BillowShade"), Out.BillowShade);
	Vector(Painted, TEXT("LitTint"), Out.PaintLitTint);
	Vector(Painted, TEXT("ShadeTint"), Out.PaintShadeTint);
	// The overrides, as they go on either material (FDreamSkyCloudOverrides::ApplyTo): the extinction is the one of the
	// look in use.
	const FDreamSkyCloudOverrides& O = CloudOverrides;
	if (O.bOverride_Extinction)
	{
		(GetEffectiveCloudLook() == EDreamSkyCloudLook::Painted ? Out.PaintExtinction : Out.Extinction) = O.Extinction;
	}
	if (O.bOverride_Glow) { Out.Glow = O.Glow; }
	if (O.bOverride_BaseGlow) { Out.BaseGlow = O.BaseGlow; }
	if (O.bOverride_PaintGain) { Out.PaintGain = O.PaintGain; }
	if (O.bOverride_EdgeSoftness) { Out.EdgeSoftness = O.EdgeSoftness; }
	if (O.bOverride_HalfToneAt) { Out.HalfToneAt = O.HalfToneAt; }
	if (O.bOverride_LitToneAt) { Out.LitToneAt = O.LitToneAt; }
	if (O.bOverride_ToneSoftness) { Out.ToneSoftness = O.ToneSoftness; }
	if (O.bOverride_SilverLining) { Out.SilverLining = O.SilverLining; }
}

void ADreamSky::UpdateCloudShadow(const FVector& ViewLocation)
{
	using namespace DreamSky::Actor;
	const bool bWanted = SunLight && GetEffectiveClouds() == EDreamSkyClouds::Rendered && FApp::CanEverRender();
	if (!bWanted)
	{
		if (SunLight && CloudShadowFunction && SunLight->LightFunctionMaterial == CloudShadowFunction)
		{
			// (The engine's defaults back.)
			SunLight->SetLightFunctionMaterial(nullptr);
			SunLight->SetLightFunctionFadeDistance(100000.0f);
			SunLight->SetLightFunctionDisabledBrightness(0.5f);
		}
		CloudShadowTarget = nullptr;
		CloudShadowPlacement.W = 0.0f;
		return;
	}
	if (!CloudShadowTarget)
	{
		CloudShadowTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("CloudShadow"), RF_Transient);
		CloudShadowTarget->RenderTargetFormat = RTF_R16f;
		CloudShadowTarget->bCanCreateUAV = true;
		CloudShadowTarget->ClearColor = FLinearColor::White;
		CloudShadowTarget->AddressX = TA_Clamp;
		CloudShadowTarget->AddressY = TA_Clamp;
		CloudShadowTarget->InitAutoFormat(CloudShadowResolution, CloudShadowResolution);
		CloudShadowTarget->UpdateResourceImmediate(true);
	}
	if (!CloudShadowFunction)
	{
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, CloudShadowMaterialPath))
		{
			CloudShadowFunction = UMaterialInstanceDynamic::Create(Material, this);
		}
	}
	if (!CloudShadowFunction)
	{
		return;
	}
	if (CloudShadowFunction->K2_GetTextureParameterValue(TEXT("CloudShadowMap")) != CloudShadowTarget)
	{
		CloudShadowFunction->SetTextureParameterValue(TEXT("CloudShadowMap"), CloudShadowTarget);
	}
	// Centred on the view, snapped to its texels (the shadows do not swim as the view moves); all the sun the clouds
	// stand in front of is taken unless the overrides say otherwise.
	const double Texel = 2.0 * CloudShadowHalfExtent / CloudShadowResolution;
	const float X = float(FMath::RoundToDouble(ViewLocation.X / Texel) * Texel);
	const float Y = float(FMath::RoundToDouble(ViewLocation.Y / Texel) * Texel);
	const float Strength = FMath::Clamp(CloudOverrides.bOverride_GroundShadow ? CloudOverrides.GroundShadow : 1.0f, 0.0f, 1.0f);
	const FVector4f Placement(X, Y, CloudShadowHalfExtent, Strength);
	if (Placement != CloudShadowPlacement)
	{
		CloudShadowPlacement = Placement;
		CloudShadowFunction->SetVectorParameterValue(TEXT("ShadowMap"), FLinearColor(X, Y, 0.5f / CloudShadowHalfExtent, Strength));
	}
	if (SunLight->LightFunctionMaterial != CloudShadowFunction)
	{
		SunLight->SetLightFunctionMaterial(CloudShadowFunction);
		// (Its own placement fades it out: the engine's fade, 1 km by default, would take the shadows off the far ground
		// and leave it at half the sun.)
		SunLight->SetLightFunctionFadeDistance(1.0e9f);
		SunLight->SetLightFunctionDisabledBrightness(1.0f);
	}
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
				// (The clouds' shadows fade out short of the shadow map's edge: the map reaches the traced distance.)
				CloudMaterialInstance->SetScalarParameterValue(TEXT("ShadowReach"), FMath::Max(VolumetricTracingDistance, 10.0f) * 100000.0f);
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
		// How much sun the clouds' shadows take off the ground: all of it under realistic clouds; under the painted ones
		// (the material with a PaintGain), between broken clouds, a little over half, so the shadows lie light and keep
		// some of the sun's warmth, as in a painted background, instead of the sky light's dark blue alone. Under a
		// closing cover it goes back to all of it: an overcast or a storm lets no sun through to cast shadows. (Stepped,
		// so the sun is not re-made every frame while the weather turns.)
		float PaintGain = 0.0f;
		const bool bPainted = Wanted && Wanted->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PaintGain")), PaintGain);
		const UDreamWorldSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		const float Cover = Weather && Weather->GetEnvironment().bActive ? Weather->GetEnvironment().CloudCoverage : 0.0f;
		const float PaintedShadow = FMath::RoundToFloat(FMath::Lerp(0.55f, 1.0f, FMath::SmoothStep(0.6f, 0.9f, Cover)) * 20.0f) / 20.0f;
		const float GroundShadow = FMath::Clamp(CloudOverrides.bOverride_GroundShadow ? CloudOverrides.GroundShadow : (bPainted ? PaintedShadow : 1.0f), 0.0f, 1.0f);
		if (SunLight && !FMath::IsNearlyEqual(SunLight->CloudShadowOnSurfaceStrength, GroundShadow, 1.0e-3f))
		{
			SunLight->CloudShadowOnSurfaceStrength = GroundShadow;
			SunLight->MarkRenderStateDirty();
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
			|| !FMath::IsNearlyEqual(SunLight->CloudShadowMapResolutionScale, VolumetricShadowResolution, 1.0e-3f)
			|| !FMath::IsNearlyEqual(SunLight->CloudShadowRaySampleCountScale, VolumetricShadowSamples, 1.0e-3f)))
		{
			SunLight->CloudShadowExtent = MapExtentKm;
			SunLight->CloudShadowMapResolutionScale = VolumetricShadowResolution;
			SunLight->CloudShadowRaySampleCountScale = VolumetricShadowSamples;
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
		// The sun's light scattered in the clouds, and the moon's while it lights anything: a second atmosphere light
		// the clouds scatter costs a second march toward it at every lit sample, so a dark moon is left out of them.
		// (The earth's shadow: with the sun below the horizon the clouds, a few kilometres up, see it until it is some
		// two degrees down. Stepped, so the light is not re-made every frame through the dusk.)
		const UDreamWorldSubsystem* WorldClock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		const float SunUp = WorldClock ? FMath::SmoothStep(-2.5f, 0.5f, WorldClock->GetCelestial().SunElevation) : 1.0f;
		const float SunScale = VolumetricSunScattering * FMath::RoundToFloat(SunUp * 20.0f) / 20.0f;
		const FLinearColor SunScatter(SunScale, SunScale, SunScale);
		const FLinearColor MoonScale(VolumetricSunScattering, VolumetricSunScattering, VolumetricSunScattering);
		if (SunLight && SunLight->CloudScatteredLuminanceScale != SunScatter)
		{
			SunLight->CloudScatteredLuminanceScale = SunScatter;
			SunLight->MarkRenderStateDirty();
		}
		const FLinearColor MoonScatter = MoonLight && MoonLight->Intensity > 1.0e-4f ? MoonScale : FLinearColor::Black;
		if (MoonLight && MoonLight->CloudScatteredLuminanceScale != MoonScatter)
		{
			MoonLight->CloudScatteredLuminanceScale = MoonScatter;
			MoonLight->MarkRenderStateDirty();
		}
		UpdateCloudTracing(bPainted);
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

void ADreamSky::UpdateCloudTracing(bool bPainted)
{
	const UWorld* World = GetWorld();
	const UDreamWorldSubsystem* WorldClock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	const float Flash = WorldClock ? WorldClock->GetEnvironment().LightningFlash : 0.0f;
	if (Flash > 0.02f)
	{
		CloudFlashLitFrame = GFrameCounter;
	}

	// (Each setting is left alone where a project or the console has set it.)
	auto SetByUs = [](IConsoleVariable* Variable)
	{
		const EConsoleVariableFlags SetBy = EConsoleVariableFlags(Variable->GetFlags() & ECVF_SetByMask);
		return SetBy <= ECVF_SetByScalability || SetBy == ECVF_SetByCode;
	};
	// Half resolution traced whole every frame, if asked, for the realistic clouds. (Never switched while running: the
	// renderer rebuilds its targets on a switch, a hitch of tens of milliseconds.)
	static IConsoleVariable* RenderTargetMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget.Mode"));
	if (RenderTargetMode && SetByUs(RenderTargetMode))
	{
		const int32 WantedMode = bVolumetricHalfResTracing && !bPainted ? 1 : 0;
		if (RenderTargetMode->GetInt() != WantedMode)
		{
			RenderTargetMode->Set(WantedMode, ECVF_SetByCode);
		}
	}
	// A lightning flash relights the whole field from one frame to the next, while the default tracing rebuilds three
	// quarters of the pixels from the frames before: a flash came out as a checkerboard, and the frames after it, as
	// the history caught up with the fading light, as stripes. While a flash lasts, and a few frames after it is out,
	// the clouds are drawn from each frame's own trace alone (a little softer and noisier for that half second, at no
	// cost); once the light holds still the history takes over again.
	static IConsoleVariable* Reprojection = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VolumetricRenderTarget.MinimumDistanceKmToEnableReprojection"));
	if (Reprojection && SetByUs(Reprojection))
	{
		const bool bHold = CloudFlashLitFrame != 0 && GFrameCounter - CloudFlashLitFrame <= 4;
		if (bHold != bHoldingCloudHistory)
		{
			bHoldingCloudHistory = bHold;
			Reprojection->Set(bHold ? 1.0e6f : 0.0f, ECVF_SetByCode);
		}
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
		bCloudMaterialDirty = true;
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
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricShadowResolution) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricSunScattering)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, VolumetricShadowSamples) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, bVolumetricHalfResTracing))
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
	if (CloudsBelow)
	{
		if (!CloudsBelow->GetStaticMesh())
		{
			CloudsBelow->SetStaticMesh(SkyDome->GetStaticMesh());
		}
		if (!CloudsBelow->GetMaterial(0) || CloudsBelow->GetMaterial(0)->GetPathName() != DreamSky::Actor::CloudsBelowMaterialPath)
		{
			CloudsBelow->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, DreamSky::Actor::CloudsBelowMaterialPath));
		}
		if (const UStaticMesh* Mesh = CloudsBelow->GetStaticMesh())
		{
			CloudsBelow->SetWorldScale3D(FVector(DreamSky::Actor::CloudsBelowRadius / FMath::Max(1.0f, float(Mesh->GetBounds().SphereRadius))));
		}
		// The engine's volumetric clouds draw their own, in front of the ground as they should. (Without its material it
		// would show the sphere mesh's own material round the view.)
		const UMaterialInterface* BelowMaterial = CloudsBelow->GetMaterial(0);
		const bool bToon = GetEffectiveClouds() == EDreamSkyClouds::Toon && BelowMaterial
			&& BelowMaterial->GetPathName() == DreamSky::Actor::CloudsBelowMaterialPath;
		if (CloudsBelow->GetVisibleFlag() != bToon)
		{
			CloudsBelow->SetVisibility(bToon);
		}
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
	// (Its drawing goes by the view direction alone; it only has to stay round the view, and is small.)
	if (CloudsBelow && FVector::DistSquared(CloudsBelow->GetComponentLocation(), ViewLocation) > FMath::Square(DreamSky::Actor::CloudsBelowRadius * 0.01f))
	{
		CloudsBelow->SetWorldLocation(ViewLocation);
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

	// Clouds that shadow the sun: a Manual curve cannot know whether the view stands in their shadow. The eye may
	// brighten meanwhile, by what it actually sees (Auto), at most CloudShadowExposureLift under a closed grey cover,
	// little under a storm (it is meant to look dark), and never darker than the curve: a clear day, and white tops
	// seen from above, keep the curve's exposure.
	if (PostProcess && InStyle.ExposureMode == EDreamSkyExposureMode::Manual && InStyle.CloudShadowExposureLift > 0.0f && CloudsShadowTheSun())
	{
		const UDreamWorldSubsystem* WorldClock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		const bool bWeather = WorldClock && WorldClock->GetEnvironment().bActive;
		const float Cover = bWeather ? WorldClock->GetEnvironment().CloudCoverage : 0.0f;
		const float Dark = bWeather ? WorldClock->GetEnvironment().CloudDarkness : 0.0f;
		FPostProcessSettings& S = PostProcess->Settings;
		S.AutoExposureMethod = AEM_Histogram;
		S.AutoExposureBias = 0.0f;
		S.bOverride_AutoExposureMinBrightness = true;
		S.AutoExposureMinBrightness = L.ExposureEV100 - InStyle.CloudShadowExposureLiftFor(Cover, Dark);
		S.bOverride_AutoExposureMaxBrightness = true;
		S.AutoExposureMaxBrightness = L.ExposureEV100;
		S.bOverride_AutoExposureSpeedUp = true;
		S.AutoExposureSpeedUp = InStyle.ExposureSpeedUp;
		S.bOverride_AutoExposureSpeedDown = true;
		S.AutoExposureSpeedDown = InStyle.ExposureSpeedDown;
	}
}

void ADreamSky::RecaptureSkyLight()
{
	if (SkyLight && !SkyLight->bRealTimeCapture)
	{
		SkyLight->RecaptureSky();
	}
}
