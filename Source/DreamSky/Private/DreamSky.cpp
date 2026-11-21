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
#include "HAL/IConsoleManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/App.h"
#include "RHIFeatureLevel.h"
#include "Serialization/CustomVersion.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSky)

static TAutoConsoleVariable<bool> CVarDreamSkyCloudRenderEnable(
	TEXT("DreamSky.CloudRender.Enable"), true,
	TEXT("Whether the sky cloud renderer draws the sky's clouds. 0 leaves them out (the sky, the high cloud and the rain shafts")
	TEXT(" are still drawn): set it in a device profile to have no clouds on a platform. Where the renderer cannot run (below")
	TEXT(" Shader Model 5, the mobile renderer) they are left out whatever it says."),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarDreamSkyCloudRenderLook(
	TEXT("DreamSky.CloudRender.Look"), -1,
	TEXT("How the sky cloud renderer draws the clouds: -1 = each Dream Sky's CloudLook, 0 = toon (the surface, lit in the")
	TEXT(" style's tones), 1 = realistic (the density integrated along the view, lit by scattering), 2 = painted (the density")
	TEXT(" integrated, painted in the style's tones by the light through it)."),
	ECVF_Default);

namespace DreamSky::Actor
{
	/** Why a sky's clouds are not drawn (ADreamSky::WhyNoClouds). */
	enum class ECloudsOff : uint8
	{
		None,
		/** No renderer (-nullrhi, a server, a commandlet). */
		NoRenderer,
		/** Below Shader Model 5: the mobile renderer, which has none of the passes the sky cloud renderer draws in. */
		Unsupported,
		/** DreamSky.CloudRender.Enable 0. */
		TurnedOff,
	};

	/** How a sky was saved (its custom version). */
	struct FVersion
	{
		enum Type : int32
		{
			Initial = 0,
			/** The sky cloud renderer draws every sky's clouds: Clouds and VolumetricCloudMaterial are read into CloudLook once. */
			CloudsAlwaysRendered = 1,
			Latest = CloudsAlwaysRendered,
		};
		static const FGuid Key;
	};
	const FGuid FVersion::Key(0x6D2B8F41, 0x3C7A4E19, 0x9B05D2E8, 0x47A1C3F6);
	static FCustomVersionRegistration GRegisterSkyVersion(FVersion::Key, FVersion::Latest, TEXT("DreamSkyActor"));

	/** The engine's volumetric cloud materials the plugin had (a level saved with them takes the look they were). */
	static const TCHAR* RealisticCloudMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud.M_DreamVolumetricCloud");
	static const TCHAR* PaintedCloudMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted");

	/** The engine's sky sphere: a unit-ish sphere whose normals face inward. */
	static const TCHAR* DomeMeshPath = TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere");
	static const TCHAR* DefaultMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamSky.M_DreamSky");
	static const TCHAR* CloudShadowMaterialPath = TEXT("/DreamDynamicWorld/Sky/M_DreamCloudShadow.M_DreamCloudShadow");
	/** The sky cloud renderer's ground shadow: texels a side, and how far round the view it reaches (cm). */
	static constexpr int32 CloudShadowResolution = 256;
	static constexpr float CloudShadowHalfExtent = 2400000.0f;

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
	// The engine's volumetric clouds' shadows on the ground, for a level that adds a cloud layer of its own (nothing
	// without one; the sky's own clouds shadow the ground through a light function, UpdateCloudShadow).
	SunLight->bCastCloudShadows = true;

	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
	MoonLight->SetupAttachment(SceneRoot);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->SetUsingAbsoluteRotation(true);
	// The second atmosphere light: the moon lights a sky atmosphere (and any engine cloud layer a level adds) at night.
	// Without one this changes nothing else.
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

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;
	// Below any volume the project places, so a project volume that sets exposure wins.
	PostProcess->Priority = -1.0f;

	SkyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(DreamSky::Actor::DefaultMaterialPath));
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

bool ADreamSky::DrawsClouds() const
{
	return WhyNoClouds() == uint8(DreamSky::Actor::ECloudsOff::None);
}

uint8 ADreamSky::WhyNoClouds() const
{
	using namespace DreamSky::Actor;
	if (!FApp::CanEverRender())
	{
		return uint8(ECloudsOff::NoRenderer);
	}
	// (The world's: the editor's mobile preview draws it with the mobile renderer.)
	const UWorld* World = GetWorld();
	if ((World ? World->GetFeatureLevel() : GMaxRHIFeatureLevel) < ERHIFeatureLevel::SM5)
	{
		return uint8(ECloudsOff::Unsupported);
	}
	if (!CVarDreamSkyCloudRenderEnable.GetValueOnGameThread())
	{
		return uint8(ECloudsOff::TurnedOff);
	}
	return uint8(ECloudsOff::None);
}

void ADreamSky::TellCloudsOff(uint8 Why)
{
	using namespace DreamSky::Actor;
	if (Why == ToldCloudsOff)
	{
		return;
	}
	const uint8 Before = ToldCloudsOff;
	ToldCloudsOff = Why;
	switch (ECloudsOff(Why))
	{
	case ECloudsOff::Unsupported:
	{
		UE_LOG(LogDreamSky, Warning, TEXT("%s: the sky cloud renderer cannot run here (it needs Shader Model 5 or above; the mobile renderer has none of its passes): the sky is drawn without its clouds."), *GetPathName());
#if WITH_EDITOR
		// (Once an editor session: every play in editor world would say it again.)
		static bool bToldEditor = false;
		if (GIsEditor && !bToldEditor && FSlateApplication::IsInitialized())
		{
			bToldEditor = true;
			FNotificationInfo Info(NSLOCTEXT("DreamSky", "CloudsUnsupported", "Dream Sky: the clouds are not drawn here. The sky cloud renderer needs Shader Model 5 or above (the mobile renderer and its preview have no clouds)."));
			Info.ExpireDuration = 8.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
#endif
		break;
	}
	case ECloudsOff::TurnedOff:
		UE_LOG(LogDreamSky, Display, TEXT("%s: DreamSky.CloudRender.Enable is 0: the sky is drawn without its clouds."), *GetPathName());
		break;
	case ECloudsOff::None:
		if (Before != uint8(ECloudsOff::NoRenderer))
		{
			UE_LOG(LogDreamSky, Display, TEXT("%s: the sky cloud renderer draws the clouds again."), *GetPathName());
		}
		break;
	default:
		break;
	}
}

EDreamSkyCloudLook ADreamSky::GetEffectiveCloudLook() const
{
	const int32 Override = CVarDreamSkyCloudRenderLook.GetValueOnGameThread();
	return Override >= 0 ? EDreamSkyCloudLook(FMath::Min(Override, 2)) : CloudLook;
}

void ADreamSky::GetCloudLookParameters(FDreamSkyCloudLookParameters& Out) const
{
	Out = FDreamSkyCloudLookParameters();
	// The style's (with the style overrides written over it), then the level's cloud overrides below.
	if (const UDreamSkyStyle* Effective = GetEffectiveStyle())
	{
		const FDreamSkyRealisticClouds& R = Effective->RealisticClouds;
		Out.Extinction = R.Extinction;
		Out.Albedo = R.Albedo;
		Out.Glow = R.Glow;
		Out.BaseGlow = R.BaseGlow;
		Out.DeckMottle = R.DeckMottle;
		Out.ShadeTint = R.ShadeTint;
		Out.ViewThinning = R.ViewThinning;
		Out.ScatterForward = R.ScatterForward;
		Out.ScatterBackward = R.ScatterBackward;
		Out.ScatterBlend = R.ScatterBlend;
		Out.MultiScatter = R.MultiScatter;
		Out.MultiScatterOcclusion = R.MultiScatterOcclusion;
		Out.MultiScatterEccentricity = R.MultiScatterEccentricity;
		const FDreamSkyPaintedClouds& P = Effective->PaintedClouds;
		Out.PaintExtinction = P.Extinction;
		Out.PaintGain = P.PaintGain;
		Out.EdgeSoftness = P.EdgeSoftness;
		Out.HalfToneAt = P.HalfToneAt;
		Out.LitToneAt = P.LitToneAt;
		Out.ToneSoftness = P.ToneSoftness;
		Out.SilverLining = P.SilverLining;
		Out.BillowShade = P.BillowShade;
		Out.PaintLitTint = P.LitTint;
		Out.PaintShadeTint = P.ShadeTint;
		Out.ShadowReach = Effective->CloudShadowReach;
	}
	Out.SunScattering = CloudSunScattering;
	Out.ShadowTracingDistance = CloudShadowTracingDistance;
	// (The same light the main light parameters point at: the moon while it is the brighter.)
	const float SunLux = SunLight ? SunLight->Intensity : 0.0f;
	const float MoonLux = MoonLight ? MoonLight->Intensity : 0.0f;
	const UDirectionalLightComponent* Light = MoonLux > SunLux ? MoonLight.Get() : SunLight.Get();
	Out.LightIlluminance = Light ? Light->GetLightColor() * Light->Intensity : FLinearColor::Black;
	if (HeightFog && HeightFog->IsVisible())
	{
		Out.FogDensity = HeightFog->FogDensity;
		Out.FogHeightFalloff = HeightFog->FogHeightFalloff;
		Out.FogHeight = float(HeightFog->GetComponentLocation().Z);
		Out.FogStartDistance = HeightFog->StartDistance;
		Out.FogMaxOpacity = HeightFog->FogMaxOpacity;
		Out.FogColor = HeightFog->FogInscatteringLuminance;
	}
	else
	{
		Out.FogDensity = 0.0f;
	}
	// The level's cloud overrides: the extinction is the one of the look in use.
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
	TellCloudsOff(WhyNoClouds());
	const bool bWanted = SunLight && DrawsClouds();
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
	// As under the engine's clouds: all of the sun under the realistic ones; under the painted and toon ones, between
	// broken clouds, a little over half -- a painted background's cloud shadows lie light and keep some of the sun's
	// warmth -- rising to all of it as the cover closes (stepped, so the sun is not re-made every frame while the weather
	// turns). The GroundShadow override sets it for any look.
	const UDreamWorldSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
	const float Cover = Weather && Weather->GetEnvironment().bActive ? Weather->GetEnvironment().CloudCoverage : 0.0f;
	const float Painted = FMath::RoundToFloat(FMath::Lerp(0.55f, 1.0f, FMath::SmoothStep(0.6f, 0.9f, Cover)) * 20.0f) / 20.0f;
	const float Strength = FMath::Clamp(CloudOverrides.bOverride_GroundShadow ? CloudOverrides.GroundShadow
		: (GetEffectiveCloudLook() == EDreamSkyCloudLook::Realistic ? 1.0f : Painted), 0.0f, 1.0f);
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

void ADreamSky::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(DreamSky::Actor::FVersion::Key);
	Super::Serialize(Ar);
}

void ADreamSky::PostLoad()
{
	Super::PostLoad();
	using namespace DreamSky::Actor;
	// (A play in editor copy is made from the editor's sky, which has taken its saved choice in already.)
	const UPackage* Package = GetOutermost();
	if ((Package && Package->HasAnyPackageFlags(PKG_PlayInEditor)) || GetLinkerCustomVersion(FVersion::Key) >= FVersion::CloudsAlwaysRendered)
	{
		return;
	}
	MigrateSavedClouds();
}

void ADreamSky::MigrateSavedClouds()
{
	using namespace DreamSky::Actor;
	FString Lost;
	switch (Clouds_DEPRECATED)
	{
	case EDreamSkyClouds::Toon:
		// The dome's own toon clouds: the toon look draws the same.
		CloudLook = EDreamSkyCloudLook::Toon;
		break;
	case EDreamSkyClouds::Volumetric:
	{
		// The look of the material it drew them with (none saved: the plugin's realistic one, the default it had).
		const FSoftObjectPath Material = VolumetricCloudMaterial_DEPRECATED.ToSoftObjectPath();
		const FString Path = Material.ToString();
		if (Material.IsNull() || Path == RealisticCloudMaterialPath)
		{
			CloudLook = EDreamSkyCloudLook::Realistic;
		}
		else if (Path == PaintedCloudMaterialPath)
		{
			CloudLook = EDreamSkyCloudLook::Painted;
		}
		else
		{
			// A material of the level's own: its name says which of the two it was made from; its values are not taken.
			CloudLook = Material.GetAssetName().Contains(TEXT("Paint")) ? EDreamSkyCloudLook::Painted : EDreamSkyCloudLook::Realistic;
			Lost = Path;
		}
		break;
	}
	default:
		// The renderer: the look it had.
		break;
	}
	const FString Look = StaticEnum<EDreamSkyCloudLook>()->GetNameStringByValue(int64(CloudLook));
	if (Lost.IsEmpty())
	{
		UE_LOG(LogDreamSky, Display, TEXT("%s was saved by an older Dream Sky (clouds: %s): the sky cloud renderer draws its clouds now, in the %s look. Save the level to keep it so."),
			*GetPathName(), *StaticEnum<EDreamSkyClouds>()->GetNameStringByValue(int64(Clouds_DEPRECATED)), *Look);
	}
	else
	{
		UE_LOG(LogDreamSky, Warning, TEXT("%s was saved by an older Dream Sky with the volumetric cloud material %s, which is not drawn any more: the sky cloud renderer draws its clouds in the %s look, with the style's %sClouds settings. The material's values were not carried over: set them in the style asset (or the level's cloud overrides), then save the level."),
			*GetPathName(), *Lost, *Look, *Look);
	}
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
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, SkyMaterial) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, DomeRadius))
	{
		ApplyDome();
	}
	if (Property == GET_MEMBER_NAME_CHECKED(ADreamSky, Style) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, CloudOverrides)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, LightOverrides) || Property == GET_MEMBER_NAME_CHECKED(ADreamSky, AmbientOverrides)
		|| Property == GET_MEMBER_NAME_CHECKED(ADreamSky, ExposureOverrides))
	{
		RefreshOverrides();
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
		SkyDome->SetWorldScale3D(FVector(DomeRadius / MeshRadius));
	}
	if (HeightFog)
	{
		// The dome already shows the sky's colours at the horizon; fogging it again flattens the gradient. (The sky cloud
		// renderer lays the fog over its clouds itself, as the engine fogs its own.)
		HeightFog->SetFogCutoffDistance(DomeRadius * 0.8f);
	}
}

void ADreamSky::UpdateDome(const FVector& ViewLocation, bool bHasView)
{
	if (!SkyDome || !bHasView)
	{
		return;
	}
	if (FVector::DistSquared(SkyDome->GetComponentLocation(), ViewLocation) > FMath::Square(DomeRadius * 0.02f))
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
