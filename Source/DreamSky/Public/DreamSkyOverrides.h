// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamSkyTypes.h"
#include "DreamSkyOverrides.generated.h"

class UDreamSkyStyle;
class UMaterialInstanceDynamic;

/*
 * A Dream Sky's own settings over its style, for this level only (the style stays the preset): tick a setting to use
 * it, untick it to go back to the style's (or, for the lights' and the fog's own settings, the sky's defaults).
 */

/** The clouds: their shape, their drift and the volumetric materials' looks. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyCloudOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Coverage = false;
	/** Fraction of the sky covered when no weather drives it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Coverage", ClampMin = "0", ClampMax = "1"))
	float Coverage = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Scale = false;
	/** Size of the cloud field: larger = bigger clouds, further apart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Scale", ClampMin = "0.05", UIMax = "8"))
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_BaseAltitude = false;
	/** Altitude of the clouds' base, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_BaseAltitude", ClampMin = "1000", UIMax = "500000", Units = "cm"))
	float BaseAltitude = 60000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Speed = false;
	/** Drift speed, cloud shapes per real minute at the default day length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Speed", ClampMin = "0", UIMax = "4"))
	float Speed = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_FormationSpeed = false;
	/** How fast the clouds grow and fall apart: 1 = a cloud builds and dissolves over some 20 game minutes, 0 = the shapes hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_FormationSpeed", ClampMin = "0", UIMax = "4"))
	float FormationSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Direction = false;
	/** Drift direction, degrees of yaw (until the wind drives it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Direction", UIMin = "0", UIMax = "360"))
	float Direction = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Diurnal = false;
	/** How much the hour shapes fair-weather cumulus: 0 = the same all day .. 1 = fewest in the morning, most in the afternoon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Diurnal", ClampMin = "0", ClampMax = "1"))
	float Diurnal = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Erosion = false;
	/** How deep the billows carve into the clouds: 0 = smooth blobs .. 1 = deeply billowed, broken up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Erosion", ClampMin = "0", UIMax = "1.5"))
	float Erosion = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Detail = false;
	/** Fine fraying of the clouds' edges near the view: 0 none .. 0.5 ragged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Detail", ClampMin = "0", UIMax = "0.5"))
	float Detail = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_HighCover = false;
	/** Thin high cirrus over the cumulus: 0 none .. 1 most of the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_HighCover", ClampMin = "0", ClampMax = "1"))
	float HighCover = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_StormDarkening = false;
	/** How dark storm clouds draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_StormDarkening", ClampMin = "0", ClampMax = "1"))
	float StormDarkening = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_Visibility = false;
	/** How far the air lets clouds be seen in clear weather (far ones sink into the sky's colour), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_Visibility", ClampMin = "100000", UIMax = "20000000", Units = "cm"))
	float Visibility = 5000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_Extinction = false;
	/** Extinction of a solid cloud, per metre (either material; realistic 0.08, painted 0.3): higher = harder edges, the inside unseen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_Extinction", ClampMin = "0.005", UIMax = "1"))
	float Extinction = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_Glow = false;
	/** Realistic material: how much the clouds' tops glow with the style's shade colour (the sky light falling on them). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_Glow", ClampMin = "0", UIMax = "4"))
	float Glow = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_BaseGlow = false;
	/** Realistic material: how much of that glow reaches a cloud's base: lower = darker, flatter bases. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_BaseGlow", ClampMin = "0", ClampMax = "1"))
	float BaseGlow = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_PaintGain = false;
	/** Painted material: brightness of the painted tones. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_PaintGain", ClampMin = "0", UIMax = "2"))
	float PaintGain = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_EdgeSoftness = false;
	/** Painted material: how wide the soft outer skin of a cloud is: wider = softer, cleaner outlines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_EdgeSoftness", ClampMin = "0.02", UIMax = "0.3"))
	float EdgeSoftness = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_HalfToneAt = false;
	/** Painted material: where the half tone starts (light reaching the cloud, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_HalfToneAt", ClampMin = "0", ClampMax = "1"))
	float HalfToneAt = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_LitToneAt = false;
	/** Painted material: where the lit tone starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_LitToneAt", ClampMin = "0", ClampMax = "1"))
	float LitToneAt = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_ToneSoftness = false;
	/** Painted material: how soft the edges between the tones are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_ToneSoftness", ClampMin = "0.005", UIMax = "0.2"))
	float ToneSoftness = 0.06f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (InlineEditConditionToggle))
	bool bOverride_SilverLining = false;
	/** Painted material: the silver lining on edges near the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds|Volumetric Material", meta = (EditCondition = "bOverride_SilverLining", ClampMin = "0", UIMax = "4"))
	float SilverLining = 1.5f;

	/** Writes the ticked style settings into Style. */
	void ApplyTo(UDreamSkyStyle& Style) const;
	/** Whether any style setting is ticked. */
	bool TouchesStyle() const;
	/** Whether any material setting is ticked. */
	bool TouchesMaterial() const;
	/** Sets the ticked material settings on Material (by parameter name; a material without one ignores it). */
	void ApplyTo(UMaterialInstanceDynamic& Material) const;
};

/** The sun and the moon: their strength, their shadows. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyLightOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_SunIntensity = false;
	/** Multiplies the sun's illuminance (as the style sets it through the day). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_SunIntensity", ClampMin = "0", UIMax = "4"))
	float SunIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonIntensity = false;
	/** Multiplies the moon's illuminance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonIntensity", ClampMin = "0", UIMax = "8"))
	float MoonIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_SunSourceAngle = false;
	/** The sun's angular diameter as a light, degrees: larger = softer shadows, broader highlights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_SunSourceAngle", ClampMin = "0", UIMax = "5", Units = "Degrees"))
	float SunSourceAngle = 0.5357f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonSourceAngle = false;
	/** The moon's angular diameter as a light, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonSourceAngle", ClampMin = "0", UIMax = "5", Units = "Degrees"))
	float MoonSourceAngle = 0.5357f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_ShadowDistance = false;
	/** How far from the view the sun and moon cast dynamic shadows, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_ShadowDistance", ClampMin = "0", UIMax = "100000", Units = "cm"))
	float ShadowDistance = 20000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_ShadowCascades = false;
	/** Cascades the dynamic shadows are split into (more = sharper far shadows, costlier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_ShadowCascades", ClampMin = "0", ClampMax = "10"))
	int32 ShadowCascades = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_ContactShadowLength = false;
	/** Screen-space contact shadows (fine shadows where things touch), as a share of the screen: 0 off .. 0.1 long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_ContactShadowLength", ClampMin = "0", ClampMax = "1", UIMax = "0.1"))
	float ContactShadowLength = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_VolumetricScattering = false;
	/** How strongly the sun and moon light volumetric fog (light shafts through it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_VolumetricScattering", ClampMin = "0", UIMax = "4"))
	float VolumetricScattering = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_CloudShadowStrength = false;
	/** How dark the volumetric clouds' shadows on the ground are. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_CloudShadowStrength", ClampMin = "0", ClampMax = "1"))
	float CloudShadowStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonCastsShadows = false;
	/** Whether the moon casts shadows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonCastsShadows"))
	bool bMoonCastsShadows = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_SunDiskSize = false;
	/** Angular radius of the sun's disk in the sky, degrees (art, not physics). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_SunDiskSize", ClampMin = "0.05", UIMax = "10", Units = "Degrees"))
	float SunDiskSize = 1.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonDiskSize = false;
	/** Angular radius of the moon's disk in the sky, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonDiskSize", ClampMin = "0.05", UIMax = "10", Units = "Degrees"))
	float MoonDiskSize = 2.2f;

	/** Writes the ticked style settings into Style. */
	void ApplyTo(UDreamSkyStyle& Style) const;
	bool TouchesStyle() const;
};

/** The sky light and the height fog. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyAmbientOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_SkyLightIntensity = false;
	/** Multiplies the sky light's intensity (the ambient light from the sky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_SkyLightIntensity", ClampMin = "0", UIMax = "4"))
	float SkyLightIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_FogDensity = false;
	/** Multiplies the height fog's density (as the style and the weather set it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_FogDensity", ClampMin = "0", UIMax = "10"))
	float FogDensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_FogHeightFalloff = false;
	/** How fast the fog thins with height: larger = fog hugs the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_FogHeightFalloff", ClampMin = "0.001", UIMax = "2"))
	float FogHeightFalloff = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_FogMaxOpacity = false;
	/** The most the fog may hide, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_FogMaxOpacity", ClampMin = "0", ClampMax = "1"))
	float FogMaxOpacity = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_FogStartDistance = false;
	/** Distance from the view where the fog starts, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_FogStartDistance", ClampMin = "0", UIMax = "100000", Units = "cm"))
	float FogStartDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_VolumetricFog = false;
	/** Volumetric fog: light shafts and lit haze near the view (costly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_VolumetricFog"))
	bool bVolumetricFog = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_VolumetricFogDistance = false;
	/** How far from the view the volumetric fog reaches, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_VolumetricFogDistance", ClampMin = "100", UIMax = "100000", Units = "cm"))
	float VolumetricFogDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (InlineEditConditionToggle))
	bool bOverride_VolumetricFogExtinction = false;
	/** Multiplies how thick the volumetric fog is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (EditCondition = "bOverride_VolumetricFogExtinction", ClampMin = "0", UIMax = "10"))
	float VolumetricFogExtinction = 1.0f;

	/** Writes the ticked style settings into Style. */
	void ApplyTo(UDreamSkyStyle& Style) const;
	bool TouchesStyle() const;
};

/** What the sky does to the exposure. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyExposureOverrides
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (InlineEditConditionToggle))
	bool bOverride_Mode = false;
	/** None = leave exposure to the project; Manual = fixed from the style's curve; Auto = eye adaptation around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bOverride_Mode"))
	EDreamSkyExposureMode Mode = EDreamSkyExposureMode::Manual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (InlineEditConditionToggle))
	bool bOverride_Compensation = false;
	/** Brightens (+) or darkens (-) the picture, EV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bOverride_Compensation", UIMin = "-4", UIMax = "4"))
	float Compensation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (InlineEditConditionToggle))
	bool bOverride_AutoRange = false;
	/** Auto mode: how far, EV, the eye may adapt either side of the style's exposure. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bOverride_AutoRange", ClampMin = "0", UIMax = "8"))
	float AutoRange = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (InlineEditConditionToggle))
	bool bOverride_AutoSpeedUp = false;
	/** Auto mode: how fast the eye adapts to brighter light, EV per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bOverride_AutoSpeedUp", ClampMin = "0.02", UIMax = "20"))
	float AutoSpeedUp = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (InlineEditConditionToggle))
	bool bOverride_AutoSpeedDown = false;
	/** Auto mode: how fast the eye adapts to darker light, EV per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (EditCondition = "bOverride_AutoSpeedDown", ClampMin = "0.02", UIMax = "20"))
	float AutoSpeedDown = 1.0f;

	/** Writes the ticked style settings into Style. */
	void ApplyTo(UDreamSkyStyle& Style) const;
	bool TouchesStyle() const;
};
