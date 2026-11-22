// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
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
 * One step of a tone ramp: where (the light, 0..1) the tone changes, how soft the change is, and the tone it reaches
 * there (0 the shade, 0.5 the half tone, 1 the lit tone; for an underside: 0 its deepest shade, 0.5 the shade, 1 the
 * half tone).
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyToneBand
{
	GENERATED_BODY()

	FDreamSkyToneBand() = default;
	FDreamSkyToneBand(float InAt, float InSoftness, float InTone) : At(InAt), Softness(InSoftness), Tone(InTone) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float At = 0.5f;

	/** How wide the change from the tone before is (0 = a hard edge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", UIMax = "0.5"))
	float Softness = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float Tone = 1.0f;
};

/** The toon look's tones baked for the renderer: the ramps sampled 256 times over 0..1, and its constants. */
struct DREAMSKY_API FDreamSkyToonBaked
{
	static constexpr int32 RampSize = 256;
	/** x the tone by the light, y an underside's tone by its glow. */
	TArray<FVector2f> Ramp;
	FVector4f Half = FVector4f(0.96f, 0.97f, 1.03f, 0.55f);    // rgb the half tone's tint, a how far it goes toward the lit
	FVector4f Deep = FVector4f(0.62f, 0.64f, 0.7f, 0.35f);     // rgb an underside's deepest shade (of the shade), a the key's height pivot
	FVector4f Key = FVector4f(0.7f, 0.45f, 0.5f, 0.55f);       // the key's weights: facing the light, facing up, height; a storm's darkening of the lit side
	FVector4f Key2 = FVector4f(-0.3f, 0.6f, 3.0f, 2.0f);       // where the light starts, how far it ramps; the rim's power and strength
	float RimFocus = 4.0f;                                       // how tightly the rim glows toward the light behind
	bool IsValid() const { return Ramp.Num() == RampSize; }
	/** Blends toward another bake (a sky modifier's), by Alpha. */
	void BlendToward(const FDreamSkyToonBaked& To, float Alpha);
};

/**
 * The sky cloud renderer's toon look (ADreamSky::CloudLook = Toon): the clouds' surface painted in the style's tones, each
 * billow by its own normal. How the light on a billow is reckoned (the key), the ramp from it to the tones -- the bands,
 * or a curve -- the half tone, an underside's own ramp and deepest shade, the silver lining. The defaults are the look's
 * own; a sky modifier may bring another set and blend to it.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyToonClouds
{
	GENERATED_BODY()

	FDreamSkyToonClouds();

	/** The steps from the shade to the lit tone by the light on a billow (sorted by At; as many as wanted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones")
	TArray<FDreamSkyToneBand> Bands;

	/** Use the curve below instead of the bands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones")
	bool bUseToneCurve = false;

	/** The light on a billow (0..1) to its tone (0 shade, 0.5 half, 1 lit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (EditCondition = "bUseToneCurve", XAxisName = "Light", YAxisName = "Tone"))
	FRuntimeFloatCurve ToneCurve;

	/** How far the half tone lies from the shade toward the lit tone, and its tint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float HalfToneMix = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (HideAlphaChannel))
	FLinearColor HalfToneTint = FLinearColor(0.96f, 0.97f, 1.03f);

	/** An underside's steps from its deepest shade (0) through the shade (0.5) to the half tone (1), by how much it glows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones")
	TArray<FDreamSkyToneBand> UndersideBands;

	/** An underside's deepest shade, of the shade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (HideAlphaChannel))
	FLinearColor UndersideDeepTint = FLinearColor(0.62f, 0.64f, 0.7f);

	/** The light on a billow: how much facing the light, facing up and standing high in the cloud count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Key", meta = (UIMin = "0", UIMax = "2"))
	float KeyFacing = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Key", meta = (UIMin = "0", UIMax = "2"))
	float KeyUp = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Key", meta = (UIMin = "0", UIMax = "2"))
	float KeyHeight = 0.5f;
	/** The height in the cloud (0 base .. 1 top) where standing high starts to count for more light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Key", meta = (ClampMin = "0", ClampMax = "1"))
	float KeyHeightPivot = 0.35f;
	/** The key where the light starts (0), and how far on it is full (1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Key", meta = (UIMin = "-1", UIMax = "1"))
	float KeyLow = -0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Key", meta = (ClampMin = "0.01", UIMax = "2"))
	float KeyRange = 0.6f;

	/** The silver lining: how close to the outline it stays (a power), how bright, how tightly toward the light behind. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (ClampMin = "0.5", UIMax = "8"))
	float RimPower = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (ClampMin = "0", UIMax = "8"))
	float RimStrength = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (ClampMin = "0.5", UIMax = "16"))
	float RimFocus = 4.0f;

	/** How dark a storm leaves the lit side (the shade goes all the way). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Toon Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float StormLitDarkening = 0.55f;

	/** The ramps sampled, and the constants, for the renderer. */
	void Bake(FDreamSkyToonBaked& Out) const;
	/** A ramp of bands at X (0..1). */
	static float EvaluateBands(const TArray<FDreamSkyToneBand>& InBands, float X);
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

	/** How far the half tone lies from the shade toward the lit tone, and its tint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float HalfToneMix = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (HideAlphaChannel))
	FLinearColor HalfToneTint = FLinearColor(0.97f, 0.97f, 1.02f);

	/** How fast the light falls off into a cloud toward the sun (smaller = the shade comes on sooner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0.005", UIMax = "0.2"))
	float KeyFalloff = 0.04f;

	/** The silver lining's falloff off the outline and its power. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0.001", UIMax = "0.2"))
	float RimFalloff = 0.025f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0.5", UIMax = "8"))
	float RimPower = 4.0f;

	/** How dark the shade is at a cloud's base, of its shade at the top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float ShadeAtBase = 0.84f;

	/** The least a cloud's base keeps of its shade, however deep in the cloud's own shadow (from below a cloud is never black). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tones", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseFloor = 0.85f;
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
	float PaintHalfToneMix = 0.55f;
	FLinearColor PaintHalfToneTint = FLinearColor(0.97f, 0.97f, 1.02f, 1.0f);
	float PaintKeyFalloff = 0.04f;
	float PaintRimFalloff = 0.025f;
	float PaintRimPower = 4.0f;
	float PaintShadeAtBase = 0.84f;
	float PaintBaseFloor = 0.85f;
	// Toon (FDreamSkyToonClouds, baked; the sky modifiers' blend of it).
	FDreamSkyToonBaked Toon;
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
	/** The toon clouds' tones baked (the style's ToonClouds, the sky modifiers' blended toward theirs). */
	FDreamSkyToonBaked Toon;
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
