// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldTypes.h"
#include "DreamSkyTypes.generated.h"

/** What the sky does to the scene's exposure. */
UENUM(BlueprintType)
enum class EDreamSkyExposureMode : uint8
{
	/** Leave exposure to the project's post process volumes. */
	None,
	/** Fixed exposure from the style's EV100 curve (no eye adaptation lag when time jumps). */
	Manual,
	/** Eye adaptation, held within the style's ExposureAutoRange of its EV100 curve. */
	Auto,
};

/**
 * What drew the clouds in a level saved before the sky cloud renderer drew them all: kept only so such a level loads
 * (ADreamSky picks its CloudLook from it, once).
 */
UENUM()
enum class EDreamSkyClouds : uint8
{
	/** The sky dome's own toon clouds. */
	Toon,
	/** The engine's volumetric cloud layer with the plugin's cloud material. */
	Volumetric,
	/** The sky cloud renderer. */
	Rendered,
};

/**
 * How the sky cloud renderer draws the clouds: a compute pass per view marches the cloud layer at reduced resolution,
 * filters it over frames and composites it into the sky behind the scene (DreamSky.CloudRender.* settings).
 */
UENUM(BlueprintType)
enum class EDreamSkyCloudLook : uint8
{
	/** The clouds' surface, lit and painted in the style's few tones (as the dome's toon clouds). */
	Toon,
	/** The density integrated along the view, lit by scattering (the style's RealisticClouds). */
	Realistic,
	/** The density integrated, painted in the style's tones by the light through it (the style's PaintedClouds). */
	Painted,
};

/** The sky cloud renderer's realistic look (ADreamSky::CloudLook = Realistic): the clouds' density lit by scattering. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyRealisticClouds
{
	GENERATED_BODY()

	/** Extinction of a solid cloud, per metre (a real cumulus is about 0.04 to 0.2): higher = harder, the inside unseen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.001", UIMin = "0.01", UIMax = "0.3"))
	float Extinction = 0.08f;

	/** How much of the light a cloud scatters rather than absorbs (a real cloud: nearly all of it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1", UIMin = "0.5"))
	float Albedo = 0.98f;

	/** How much of the style's shade colour the clouds glow with at their tops (the sky light falling on them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "4"))
	float Glow = 0.75f;

	/** How much of that glow reaches a cloud's base (the sky above it is hidden by the cloud): lower = darker, flatter bases. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseGlow = 0.15f;

	/** How mottled an overcast deck is from below (darker where it is thicker). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float DeckMottle = 0.45f;

	/** Multiplies the style's shade colour in the glow: the shaded sides keep the sky's blue-grey even where the weather greys the style's colours. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	FLinearColor ShadeTint = FLinearColor(0.86f, 0.95f, 1.14f, 1.0f);

	/** By day, how thick the body of a near cloud is to the eye (not to the light): lower = less grain, the inside seen a little deeper. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.05", ClampMax = "1"))
	float ViewThinning = 0.35f;

	/** Forward scattering lobe (silver lining): stronger makes small lumps near the sun glow as white balls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scattering", meta = (ClampMin = "-0.95", ClampMax = "0.95"))
	float ScatterForward = 0.8f;

	/** Backward scattering lobe (the sunlit faces seen with the sun behind the view). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scattering", meta = (ClampMin = "-0.95", ClampMax = "0.95"))
	float ScatterBackward = -0.3f;

	/** Blend between the forward and the backward lobe. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scattering", meta = (ClampMin = "0", ClampMax = "1"))
	float ScatterBlend = 0.5f;

	/** Multiple scattering: how much light keeps scattering inside. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scattering", meta = (ClampMin = "0", ClampMax = "1"))
	float MultiScatter = 0.85f;

	/** How much the cloud occludes its own multiple scattering: higher = the light spreads less deep, the bases darker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scattering", meta = (ClampMin = "0", ClampMax = "1"))
	float MultiScatterOcclusion = 0.5f;

	/** How directional multiple scattering stays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scattering", meta = (ClampMin = "0", ClampMax = "1"))
	float MultiScatterEccentricity = 0.5f;
};

/**
 * The sky cloud renderer's painted look (ADreamSky::CloudLook = Painted): the clouds' density painted in the style's
 * three tones by the light through it, hard-edged, as an anime background's.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyPaintedClouds
{
	GENERATED_BODY()

	/** Extinction of a solid cloud, per metre: high, so the inside is unseen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.001", UIMin = "0.05", UIMax = "1"))
	float Extinction = 0.3f;

	/** Brightness of the painted tones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMin = "0.5", UIMax = "2"))
	float PaintGain = 1.0f;

	/** How wide the soft outer skin of a cloud is (in the shape's own measure): wider = softer, cleaner outlines. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.005", UIMin = "0.02", UIMax = "0.3"))
	float EdgeSoftness = 0.06f;

	/** Where the half tone starts (light reaching the cloud, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float HalfToneAt = 0.32f;

	/** Where the lit tone starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float LitToneAt = 0.62f;

	/** How soft the edges between the tones are. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0.001", UIMin = "0.005", UIMax = "0.2"))
	float ToneSoftness = 0.06f;

	/** Silver lining on the edges near the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", UIMax = "4"))
	float SilverLining = 1.5f;

	/** How much the billows shade themselves (0 = only the big forms are shaded). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float BillowShade = 0.3f;

	/** Multiplies the style's lit cloud colour (warm near-white). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones")
	FLinearColor LitTint = FLinearColor(1.0f, 0.975f, 0.93f, 1.0f);

	/** Multiplies the style's shade colour: blue-lavender, which it keeps where the weather greys the style's colours. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones")
	FLinearColor ShadeTint = FLinearColor(0.86f, 0.84f, 1.02f, 1.0f);
};

/**
 * What the sky cloud renderer's realistic and painted looks are drawn with this frame: the style's realistic and painted
 * clouds with the level's cloud overrides over them, and the light and the fog as the sky's components have them.
 */
struct FDreamSkyCloudLookParameters
{
	// Realistic (FDreamSkyRealisticClouds).
	float Extinction = 0.08f;          // per metre
	float Albedo = 0.98f;
	float Glow = 0.75f;
	float BaseGlow = 0.15f;
	float DeckMottle = 0.45f;
	FLinearColor ShadeTint = FLinearColor(0.86f, 0.95f, 1.14f, 1.0f);
	float ViewThinning = 0.35f;
	float ScatterForward = 0.8f;
	float ScatterBackward = -0.3f;
	float ScatterBlend = 0.5f;
	float MultiScatter = 0.85f;
	float MultiScatterOcclusion = 0.5f;
	float MultiScatterEccentricity = 0.5f;
	float SunScattering = 2.5f;        // ADreamSky::CloudSunScattering
	float ShadowTracingDistance = 6.0f; // ADreamSky::CloudShadowTracingDistance, km: how far toward the sun a sample's shadow looks
	float ShadowReach = 6000000.0f;    // UDreamSkyStyle::CloudShadowReach, cm: the clouds' shading toward the light fades out by where their shadow lands, from a quarter of this on
	// Painted (FDreamSkyPaintedClouds).
	float PaintExtinction = 0.3f;      // per metre
	float PaintGain = 1.0f;
	float EdgeSoftness = 0.06f;
	float HalfToneAt = 0.32f;
	float LitToneAt = 0.62f;
	float ToneSoftness = 0.06f;
	float SilverLining = 1.5f;
	float BillowShade = 0.3f;
	FLinearColor PaintLitTint = FLinearColor(1.0f, 0.975f, 0.93f, 1.0f);
	FLinearColor PaintShadeTint = FLinearColor(0.86f, 0.84f, 1.02f, 1.0f);
	// The light the clouds scatter, as the engine's clouds take it: the sky's sun light, or its moon light while that is
	// the brighter, in the component's own colour (the low sun's deep red) times its illuminance (lux).
	FLinearColor LightIlluminance = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
	// The sky's height fog, which the engine lays over its clouds at their depth (its cut-off is at the dome while the
	// renderer draws them, so the renderer lays it on itself): the component's own settings.
	float FogDensity = 0.0f;           // as the component has it (per 1000 cm)
	float FogHeightFalloff = 0.2f;     // per 1000 cm
	float FogHeight = 0.0f;            // cm, the component's height
	float FogStartDistance = 0.0f;     // cm
	float FogMaxOpacity = 1.0f;
	FLinearColor FogColor = FLinearColor::Black;
};

/**
 * The sky's lighting: what the lights, the sky light and the fog are set to, and what any material or
 * character shader can read (main light, ambient gradient, day phase). Both sky looks (stylized now, a
 * physical one later) produce this.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyLighting
{
	GENERATED_BODY()

	/** Toward the light that dominates: the sun by day, the moon (or the night light) after dark. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FVector MainLightDirection = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor MainLightColor = FLinearColor::White;

	/** Lux. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float MainLightIlluminance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	bool bMainLightIsMoon = false;

	/**
	 * What an overcast leaves of the sun for whatever the clouds' own shadow does not reach -- the shared parameters'
	 * main light (materials, toon characters), which reads no light function. 1 when the sun's light itself was dimmed
	 * for the overcast; below 1 while the sky cloud renderer's shadow takes the sun off the ground instead (the sun light
	 * is then left whole, so the same clouds do not darken it twice).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float SunOvercastScale = 1.0f;

	/** Toward the sun / the moon light as the directional lights point (the moon light may stand in when the moon is down). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FVector SunLightDirection = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FVector MoonLightDirection = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor SunColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float SunIlluminance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor MoonColor = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float MoonIlluminance = 0.0f;

	/** Ambient light from above, the horizon and below (intensity included). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor AmbientSky = FLinearColor::Black;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor AmbientEquator = FLinearColor::Black;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor AmbientGround = FLinearColor::Black;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float SkyLightIntensity = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor FogColor = FLinearColor::Gray;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float FogDensity = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float ExposureEV100 = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FDreamDayPhase DayPhase;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float SunElevation = 90.0f;

	/** The light of a lightning flash, lux (0 between flashes), toward the strike from the view. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float LightningIlluminance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FVector LightningDirection = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	FLinearColor LightningColor = FLinearColor::White;

	/** How much a flash is brightened to keep up with the exposure (1 at the reference exposure). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky")
	float FlashGain = 1.0f;
};

/** The look of the sky dome for one moment (written to the shared collection; the sky material draws it). */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyLook
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor Zenith = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor Horizon = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor Ground = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor SunHalo = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor SunDisk = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor MoonDisk = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor MoonHalo = FLinearColor::Black;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor CloudLit = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor CloudShadow = FLinearColor::Gray;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float StarIntensity = 0.0f;
	/** The share of the stars, the dimmest first, left out (a total eclipse shows only the brightest). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float StarsHidden = 0.0f;
	/** The sky gradient's horizon exponent (the style's HorizonExponent; smaller keeps the horizon colour lower). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float HorizonExponent = 0.45f;
	/** Cloud cover where the view is (the style's own, or the weather's). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudCoverage = 0.0f;
	/** How dark the clouds are (storm), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudDarkness = 0.0f;
	/** Weather fog or dust over the dome: rgb = the height fog's colour, a = how much of the sky it hides (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor Haze = FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);
	/** How present the aurora is, 0..1 (where the world lies, or a modifier's); the night's darkness goes on when it is drawn. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float Aurora = 0.0f;
	/** How strong the city's light is at the horizon (the style's LightPollution, or a modifier's), in the dome's measure. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float LightPollution = 0.0f;
	/** Scales the night sky's own glow (the style's Airglow). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float AirglowScale = 1.0f;
	/** How bright the light shafts below the clouds are drawn (the style's LightShafts.Intensity, a modifier's scale on it). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float LightShafts = 0.0f;
	/** How bright the sun's lens flare is drawn (the style's LensFlare.Intensity, a modifier's scale on it). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float LensFlare = 0.0f;
	/** How bright the Milky Way is drawn on the darkest night (the style's MilkyWay.Intensity, a modifier's scale on it). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float MilkyWay = 0.0f;
	/** How much of the sun's disk the moon covers, 0..1, and where the moon's centre stands on it (sun radii, DreamSky::Eclipse). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float SolarEclipse = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FVector2D SolarEclipseOffset = FVector2D(1000.0, 0.0);
	/** How much of the moon's disk the earth's umbra covers, 0..1, and where its centre stands on it (moon radii). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float LunarEclipse = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FVector2D LunarEclipseOffset = FVector2D(1000.0, 0.0);
	/**
	 * What the sky modifiers do to the cloud cover and darkness wherever the weather map gives them (CloudCoverage and
	 * CloudDarkness above have it already): each is kept this much and this much added.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudCoverageKept = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudCoverageAdded = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudDarknessKept = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudDarknessAdded = 0.0f;
};
