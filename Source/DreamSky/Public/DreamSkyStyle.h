// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "DreamSkyTypes.h"
#include "DreamSkyStyle.generated.h"

/**
 * How a sky looks and lights the scene through the day. Every curve is keyed by the sun's elevation in
 * degrees (-90..90), not by the hour, so one style works for any day length, latitude or season.
 *
 * The defaults are a restrained stylized palette. A project makes its own by duplicating the default
 * asset (Setup creates /DreamDynamicWorld/Sky/DS_DreamSkyStylized) and editing the curves.
 */
UCLASS(BlueprintType)
class DREAMSKY_API UDreamSkyStyle : public UDataAsset
{
	GENERATED_BODY()

public:
	UDreamSkyStyle();

	/** The dome's colours, sun/moon/stars and clouds at a moment. */
	FDreamSkyLook EvaluateLook(const FDreamCelestialState& Celestial) const;

	/** Lights, ambient, fog and exposure at a moment (before any smoothing). */
	FDreamSkyLighting EvaluateLighting(const FDreamCelestialState& Celestial) const;

	/** What the weather does to both (when Environment.bActive): clouds, a dimmer and greyer sky, more fog. */
	void ApplyWeather(const FDreamWorldEnvironment& Environment, FDreamSkyLighting& Lighting, FDreamSkyLook& Look) const;

	/** What being indoors does to the lighting (FDreamViewOcclusion from the view probe). */
	void ApplyInterior(const FDreamViewOcclusion& Occlusion, FDreamSkyLighting& Lighting) const;

	/** A lightning flash on top of the (eased) lighting: instant, never smoothed. Always sets Lighting.FlashGain. */
	void ApplyFlash(const FDreamWorldEnvironment& Environment, FDreamSkyLighting& Lighting) const;

	/** Colour of a lightning flash in the ambient light, the sky and the flash light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning")
	FLinearColor LightningColor = FLinearColor(0.75f, 0.8f, 1.0f, 1.0f);

	/** How bright a full flash makes the ambient light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (ClampMin = "0", UIMax = "4"))
	float LightningAmbient = 0.8f;

	/** Illuminance of the directional light a full flash casts from the strike, lux (0 = no flash light). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (ClampMin = "0", UIMax = "50"))
	float LightningLightIlluminance = 8.0f;

	/** Whether the flash light casts shadows (each flash then redraws the shadow maps it reaches). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning")
	bool bLightningCastsShadows = false;

	/**
	 * How far a flash keeps up with the exposure: 0 = the same light day and night (lost in daylight), 1 = as
	 * bright on screen at noon as at night. A flash is brightened by 2^((EV100 - LightningReferenceEV) * this).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "1"))
	float LightningExposureCompensation = 0.5f;

	/** The exposure (EV100) at which a flash shows at its plain brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (UIMin = "-2", UIMax = "6"))
	float LightningReferenceEV = 1.0f;

	// ---- Sky dome ------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor ZenithColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor HorizonColor;

	/** Below the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor GroundColor;

	/** < 1 keeps the horizon colour low, > 1 lets it climb. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (ClampMin = "0.05", UIMax = "4"))
	float HorizonExponent = 0.45f;

	// ---- Sun ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Radiance"))
	FRuntimeCurveLinearColor SunDiskColor;

	/** The glow around the sun (and along the horizon beside it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor SunHaloColor;

	/** Angular radius of the disk, degrees. The real sun is 0.27; a readable stylized sun is several times that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0.05", UIMax = "10"))
	float SunDiskSize = 1.6f;

	/** Tightness of the halo: higher = smaller. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "1", UIMax = "64"))
	float SunHaloExponent = 6.0f;

	/**
	 * Share of the sun disk the sky light sees when it captures the dome. A full disk in the captured light
	 * makes a hot spot every rough surface reflects; the sun's light comes from the directional light anyway.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0", ClampMax = "1"))
	float SunDiskInSkyLight = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor SunLightColor;

	/** Illuminance of the sun's directional light, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Lux"))
	FRuntimeFloatCurve SunIlluminance;

	// ---- Moon and stars -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor MoonDiskColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor MoonHaloColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0.05", UIMax = "10"))
	float MoonDiskSize = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "1", UIMax = "128"))
	float MoonHaloExponent = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor MoonLightColor;

	/** Illuminance of the moon light at full moon, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Lux"))
	FRuntimeFloatCurve MoonIlluminance;

	/** How much a thin moon dims the moon light: 0 = never, 1 = by its lit fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonPhaseDimming = 0.3f;

	/**
	 * When the moon is below the horizon at night, light the night from where it would read best (opposite the
	 * sun) instead of leaving the scene without a main light. Most stylized games want this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon")
	bool bNightLightAlwaysUp = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (XAxisName = "Sun elevation", YAxisName = "Intensity"))
	FRuntimeFloatCurve StarIntensity;

	/** Relative number of stars. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0", UIMax = "4"))
	float StarDensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0", ClampMax = "1"))
	float StarTwinkle = 0.4f;

	// ---- Clouds -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor CloudLitColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor CloudShadowColor;

	/**
	 * How much the clouds take the colour of the sky around them: 0 = the cloud colours as they are everywhere,
	 * 1 = warm low in a sunset, deep blue overhead, like the sky behind them. The curves still set the level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudSkyTint = 0.6f;

	/** How much the lit side of clouds near the sun catches the sun's halo colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "2"))
	float CloudSunGlow = 0.5f;

	/** Fraction of the sky covered (weather will drive this later). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudCoverage = 0.45f;

	/** Edge softness of the cloud shapes. Small = hard toon edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.001", ClampMax = "0.5"))
	float CloudSoftness = 0.06f;

	/** Size of the cloud shapes; larger = bigger clouds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.05", UIMax = "8"))
	float CloudScale = 1.0f;

	/** Number of light bands across a cloud (toon steps). 0 = smooth shading. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "8"))
	int32 CloudBands = 3;

	/** How much the cloud layer curves down toward the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.01", UIMax = "2"))
	float CloudCurvature = 0.25f;

	/** Clouds fade out within this height of the horizon (dome direction z). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "0.5"))
	float CloudHorizonFade = 0.12f;

	/** Bright rim where a cloud edge is lit from behind (near the sun). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "2"))
	float CloudRim = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudOpacity = 1.0f;

	/** Drift speed, cloud shapes per real minute at the default day length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "4"))
	float CloudSpeed = 0.4f;

	/** Drift direction, degrees of yaw (until the wind drives it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	float CloudDirection = 30.0f;

	/** Height of the cloud layer above the camera, cm: where a point of the dome is looked up in the weather map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "1000", UIMax = "500000"))
	float CloudLayerHeight = 150000.0f;

	/** How dark storm clouds (weather darkness) draw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float StormCloudDarkening = 0.6f;

	// ---- Scene lighting -----------------------------------------------------------------------

	/** Ambient gradient = the dome's zenith / horizon / ground colours times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient", meta = (XAxisName = "Sun elevation", YAxisName = "Scale"))
	FRuntimeFloatCurve AmbientScale;

	/** The least ambient light there is at night, so the darkest scene stays readable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient")
	FLinearColor NightAmbientFloor = FLinearColor(0.02f, 0.026f, 0.05f, 1.0f);

	/** Intensity of the sky light that captures the dome. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient", meta = (XAxisName = "Sun elevation", YAxisName = "Intensity"))
	FRuntimeFloatCurve SkyLightIntensity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (XAxisName = "Sun elevation", YAxisName = "Density"))
	FRuntimeFloatCurve FogDensity;

	/** Fog colour = the horizon colour times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0", UIMax = "2"))
	float FogBrightness = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0.001", UIMax = "2"))
	float FogHeightFalloff = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float FogMaxOpacity = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure")
	EDreamSkyExposureMode ExposureMode = EDreamSkyExposureMode::Manual;

	/** Fixed exposure (EV100) in Manual mode. Lower = brighter picture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure", meta = (XAxisName = "Sun elevation", YAxisName = "EV100"))
	FRuntimeFloatCurve ExposureEV100;

	// ---- Weather ------------------------------------------------------------------------------

	/** How much a fully overcast sky dims the sun and the moon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float OvercastLightDimming = 0.8f;

	/** How grey an overcast sky turns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float OvercastDesaturation = 0.6f;

	/** How much storm clouds darken the sky, the clouds and the ambient light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float StormDarkening = 0.55f;

	/**
	 * Exposure lift (EV) under a heavy sky, so an overcast day is dim but not black. Storm-dark clouds take it
	 * back (none at full darkness): a storm is meant to look dark.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", UIMax = "3"))
	float OvercastExposureLift = 0.8f;

	/** Fog density multiplier at full weather fog (it grows with the square of the weather's fog). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "1", UIMax = "200"))
	float WeatherFogScale = 100.0f;

	/** How far weather fog turns the fog colour to a neutral grey-white (at full fog). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float FogNeutral = 0.7f;

	/** Exposure lift (EV) in thick fog, and in a dust storm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (UIMin = "-2", UIMax = "2"))
	float FogExposureLift = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (UIMin = "-2", UIMax = "2"))
	float DustExposureLift = 0.0f;

	/**
	 * How much of the dome weather fog hides, in the height fog's colour (the dome itself is past the fog's
	 * cutoff, so without this the sky stays clear above a fogged-in ground). Light fog hazes the horizon; at
	 * 1, full fog leaves only a trace of the sky overhead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float FogSkyHaze = 1.0f;

	/** The same for a dust storm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float DustSkyHaze = 0.7f;

	// ---- Indoors (the view probe) --------------------------------------------------------------

	/** Exposure lift (EV) indoors, so a room lit only by its windows is not black (the eye adapts too). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (UIMin = "0", UIMax = "4"))
	float InteriorExposureLift = 1.0f;

	/** Sky light intensity indoors, as a multiplier (lower it when the sky light has no occlusion of its own). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0", UIMax = "2"))
	float InteriorSkyLightScale = 1.0f;

	/** Sun and moon light indoors, as a multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0", UIMax = "2"))
	float InteriorDirectionalScale = 1.0f;

	// ---- Behaviour ----------------------------------------------------------------------------

	/** Seconds the light takes to follow a change while the clock runs (a toon ramp otherwise flickers across its threshold). Jumps snap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0", UIMax = "5"))
	float LightResponseTime = 0.4f;

	/** The directional lights turn only when the sun or moon moved this many degrees (moving a light invalidates cached shadows). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0", UIMax = "2"))
	float LightRotationThreshold = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	bool bMoonCastsShadows = true;

	/** Day-phase thresholds, degrees of sun elevation: day is full above DayBottom and gone below TwilightTop... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float DayBottom = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float TwilightTop = -2.0f;

	/** ...night starts below NightTop and is full below NightBottom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float NightTop = -4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float NightBottom = -12.0f;
};
