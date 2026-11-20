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

/** What draws the clouds. */
UENUM(BlueprintType)
enum class EDreamSkyClouds : uint8
{
	/** Painted volumes the sky dome ray marches itself: the style's toon tones, crisp edges. */
	Toon UMETA(DisplayName = "Toon (sky dome)"),
	/** The engine's volumetric cloud layer with the plugin's cloud material: soft, lit by scattering, casting shadows. */
	Volumetric UMETA(DisplayName = "Volumetric (engine)"),
	/**
	 * The plugin's own cloud renderer: a compute pass per view marches the cloud layer at reduced resolution, filters it
	 * over frames and composites it into the sky behind the scene (DreamSky.CloudRender.* settings).
	 */
	Rendered UMETA(DisplayName = "Rendered (sky cloud renderer)"),
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
	/** Cloud cover where the view is (the style's own, or the weather's). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudCoverage = 0.0f;
	/** How dark the clouds are (storm), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") float CloudDarkness = 0.0f;
	/** Weather fog or dust over the dome: rgb = the height fog's colour, a = how much of the sky it hides (0..1). */
	UPROPERTY(BlueprintReadOnly, Category = "Sky") FLinearColor Haze = FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);
};
