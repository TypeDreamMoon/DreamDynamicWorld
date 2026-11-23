// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Scene.h"
#include "Kismet/KismetMathLibrary.h"
#include "DreamSkyTypes.h"
#include "DreamSkyModifier.generated.h"

/**
 * What a sky modifier does to the sky as the style lights it each frame: tick a setting to use it. Intensities and
 * densities are scaled, tints multiply, the exposure is offset, and the few settings that are absolute (a colour to turn
 * toward, the cloud cover) are approached. At a modifier's alpha below 1 each goes that share of the way.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyModifierSettings
{
	GENERATED_BODY()

	// ---- Sun and moon -------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_SunIntensity = false;
	/** Multiplies the sun's illuminance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_SunIntensity", ClampMin = "0", UIMax = "4"))
	float SunIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_SunTint = false;
	/** Multiplies the sun's colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_SunTint", HideAlphaChannel))
	FLinearColor SunTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonIntensity = false;
	/** Multiplies the moon's illuminance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonIntensity", ClampMin = "0", UIMax = "8"))
	float MoonIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonTint = false;
	/** Multiplies the moon's colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonTint", HideAlphaChannel))
	FLinearColor MoonTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_SunDiskBrightness = false;
	/** Multiplies how bright the sun's disk and halo draw in the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_SunDiskBrightness", ClampMin = "0", UIMax = "4"))
	float SunDiskBrightness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (InlineEditConditionToggle))
	bool bOverride_MoonDiskBrightness = false;
	/** Multiplies how bright the moon's disk and halo draw in the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (EditCondition = "bOverride_MoonDiskBrightness", ClampMin = "0", UIMax = "4"))
	float MoonDiskBrightness = 1.0f;

	// ---- Ambient and exposure -----------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (InlineEditConditionToggle))
	bool bOverride_SkyLightIntensity = false;
	/** Multiplies the sky light's intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (EditCondition = "bOverride_SkyLightIntensity", ClampMin = "0", UIMax = "4"))
	float SkyLightIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (InlineEditConditionToggle))
	bool bOverride_AmbientTint = false;
	/** Multiplies the ambient gradient materials read (sky, horizon and ground alike). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (EditCondition = "bOverride_AmbientTint", HideAlphaChannel))
	FLinearColor AmbientTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (InlineEditConditionToggle))
	bool bOverride_ExposureCompensation = false;
	/** Added to the exposure, stops: positive = brighter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ambient", meta = (EditCondition = "bOverride_ExposureCompensation", UIMin = "-4", UIMax = "4"))
	float ExposureCompensation = 0.0f;

	// ---- Fog ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (InlineEditConditionToggle))
	bool bOverride_FogDensity = false;
	/** Multiplies the height fog's density. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (EditCondition = "bOverride_FogDensity", ClampMin = "0", UIMax = "20"))
	float FogDensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (InlineEditConditionToggle))
	bool bOverride_FogColor = false;
	/** The colour the height fog turns toward (its brightness as the sky's own fog colour has it: only the hue and saturation are taken). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (EditCondition = "bOverride_FogColor", HideAlphaChannel))
	FLinearColor FogColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (InlineEditConditionToggle))
	bool bOverride_Haze = false;
	/** How much of the sky the weather's haze hides, 0..1 (the dome's fog and dust). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog", meta = (EditCondition = "bOverride_Haze", ClampMin = "0", ClampMax = "1"))
	float Haze = 0.0f;

	// ---- Sky ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_SkyTint = false;
	/** Multiplies the dome's colours (zenith, horizon, ground and the sun's halo). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_SkyTint", HideAlphaChannel))
	FLinearColor SkyTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_SkySaturation = false;
	/** Saturation of the dome's and the clouds' colours: 0 = grey, 1 = as the style has them, above 1 = richer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_SkySaturation", ClampMin = "0", UIMax = "2"))
	float SkySaturation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_Aurora = false;
	/** How present the aurora turns, 0..1, wherever the world lies (it still shows only once the sky is dark). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_Aurora", ClampMin = "0", ClampMax = "1"))
	float Aurora = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_Noctilucent = false;
	/** How present noctilucent clouds turn, 0..1, wherever and whenever the world is (they still show only in the deep twilight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_Noctilucent", ClampMin = "0", ClampMax = "1"))
	float Noctilucent = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_Meteors = false;
	/** Scales the meteors' and satellites' rates (a meteor storm, an empty sky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_Meteors", ClampMin = "0", UIMax = "50"))
	float Meteors = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_LightPollution = false;
	/** How strong a city's light turns at the horizon, in the dome's measure (see the style's NightGlow). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_LightPollution", ClampMin = "0", UIMax = "0.3"))
	float LightPollution = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_Airglow = false;
	/** Multiplies the night sky's own glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_Airglow", ClampMin = "0", UIMax = "8"))
	float Airglow = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_LightShafts = false;
	/** Multiplies how bright the light shafts below the clouds are drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_LightShafts", ClampMin = "0", UIMax = "8"))
	float LightShafts = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_LensFlare = false;
	/** Multiplies how bright the sun's lens flare is drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_LensFlare", ClampMin = "0", UIMax = "8"))
	float LensFlare = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_ToonClouds = false;
	/** The toon clouds' tones to blend to (the bands or curve, the half tone, the underside, the key, the rim). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_ToonClouds"))
	FDreamSkyToonClouds ToonClouds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_PaintedClouds = false;
	/** The painted clouds' settings to blend to (their tones, tints, key, rim, decks, storms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_PaintedClouds"))
	FDreamSkyPaintedClouds PaintedClouds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_MilkyWay = false;
	/** Multiplies how bright the Milky Way is drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_MilkyWay", ClampMin = "0", UIMax = "8"))
	float MilkyWay = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_SolarEclipse = false;
	/** How much of the sun's disk the moon covers, 0..1 (1: total; the sky darkens, the corona shows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_SolarEclipse", ClampMin = "0", ClampMax = "1"))
	float SolarEclipse = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_LunarEclipse = false;
	/** How much of the moon's disk the earth's umbra covers, 0..1 (1: a blood moon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_LunarEclipse", ClampMin = "0", ClampMax = "1"))
	float LunarEclipse = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (InlineEditConditionToggle))
	bool bOverride_StarIntensity = false;
	/** Multiplies how bright the stars draw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky", meta = (EditCondition = "bOverride_StarIntensity", ClampMin = "0", UIMax = "8"))
	float StarIntensity = 1.0f;

	// ---- Clouds -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_CloudCoverage = false;
	/** The cloud cover the sky turns toward, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_CloudCoverage", ClampMin = "0", ClampMax = "1"))
	float CloudCoverage = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_CloudDarkness = false;
	/** How dark the clouds turn, 0..1 (storm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_CloudDarkness", ClampMin = "0", ClampMax = "1"))
	float CloudDarkness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_HighCloudCover = false;
	/** The thin high cloud's cover the sky turns toward, 0..1 (a sheet of it, over the style's ice halo threshold, shows the halos). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_HighCloudCover", ClampMin = "0", ClampMax = "1"))
	float HighCloudCover = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_CloudLitTint = false;
	/** Multiplies the clouds' lit colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_CloudLitTint", HideAlphaChannel))
	FLinearColor CloudLitTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (InlineEditConditionToggle))
	bool bOverride_CloudShadowTint = false;
	/** Multiplies the clouds' shade colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (EditCondition = "bOverride_CloudShadowTint", HideAlphaChannel))
	FLinearColor CloudShadowTint = FLinearColor::White;

	/**
	 * Goes Alpha (0..1) of the way from the sky as it stands to the sky with these settings. The sun's and the moon's
	 * colour and illuminance give the main light's again.
	 */
	void Apply(FDreamSkyLighting& Lighting, FDreamSkyLook& Look, float Alpha) const;
};

/**
 * A change to the sky that can be added and removed while the game runs, fading in and out: a cave's darkness, a
 * cursed sky, a dream sequence. Several stack; the higher priority goes on last. Add one through the sky subsystem
 * (AddModifier, RemoveModifier, SetModifierAlpha), or name one on a weather preset to have it follow that weather.
 */
UCLASS(BlueprintType)
class DREAMSKY_API UDreamSkyModifier : public UDataAsset
{
	GENERATED_BODY()

public:
	/** What it does to the sky. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (ShowOnlyInnerProperties))
	FDreamSkyModifierSettings Settings;

	/** Order among modifiers in effect together: higher goes on later, over the lower ones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier")
	int32 Priority = 0;

	/** The most it ever takes effect, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxAlpha = 1.0f;

	/** Real seconds it fades in over when added without a time of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade", meta = (ClampMin = "0", Units = "s"))
	float FadeInSeconds = 3.0f;

	/** Real seconds it fades out over when removed without a time of its own. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade", meta = (ClampMin = "0", Units = "s"))
	float FadeOutSeconds = 3.0f;

	/** The shape of the fade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade")
	TEnumAsByte<EEasingFunc::Type> Easing = EEasingFunc::EaseInOut;

	/** For the ease in/out shapes: how steep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fade", meta = (ClampMin = "1", UIMax = "6"))
	float EasingExponent = 2.0f;

	/** Blends PostProcess in with the modifier (an unbound post process on the sky, weighted by the modifier's alpha). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Post Process")
	bool bUsePostProcess = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Post Process", meta = (EditCondition = "bUsePostProcess"))
	FPostProcessSettings PostProcess;

	/** The fade's shape over a linear Progress (0..1), times MaxAlpha. */
	float Shape(float Progress) const;
};
