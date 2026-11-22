// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "DreamSkyGrade.generated.h"

struct FDreamWorldEnvironment;

/** The grade at one moment, ready for Shaders/Shared/DreamSkyGradeShared.h (DSG_Grade). */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyGradeValues
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	float ExposureScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	float Saturation = 1.0f;

	/** Radians. */
	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	float HueShift = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	float Contrast = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	FLinearColor Tint = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	FLinearColor ZenithTint = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	FLinearColor HorizonTint = FLinearColor::White;

	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	float TintSpread = 0.5f;

	/** How much of the old analytic gradient the dome shows instead (0 the physical sky, 1 the gradient). */
	UPROPERTY(BlueprintReadOnly, Category = "Grade")
	float GradientBlend = 0.0f;

	/** The four float4s DSG_Grade reads: (exposure, saturation, hue, contrast), (tint, spread), zenith tint, horizon tint. */
	void Pack(FVector4f Out[4]) const;

	/** This grade applied to a colour seen at a view elevation (its direction's z), on the CPU (the shared function). */
	FLinearColor Apply(const FLinearColor& Color, float ViewZ) const;
};

/**
 * The sky style's grade over the physical sky (DreamAtmosphere): the air gives how bright the sky is and how its hue turns
 * with the sun; the grade gives the style's look on top, for the dome, the fog, the clouds' sky and the ambient light alike.
 * Everything that varies over the day is a curve over the sun's elevation (degrees), editable in the style.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyGrade
{
	GENERATED_BODY()

	FDreamSkyGrade();

	/** 0: the physical sky, graded. 1: the old analytic gradient of the Dome group (to compare, or for a style drawn by hand). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (ClampMin = "0", ClampMax = "1"))
	float GradientBlend = 0.0f;

	/** Exposure offset over the day, EV (+1 twice as bright). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "EV"))
	FRuntimeFloatCurve Exposure;

	/** Saturation over the day (1 as the air has it, 0 grey, above 1 richer: an anime sky). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "Saturation"))
	FRuntimeFloatCurve Saturation;

	/** Hue turned over the day, degrees (toward cyan or violet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "Degrees"))
	FRuntimeFloatCurve HueShift;

	/** Contrast about a middle grey over the day (1 as the air has it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "Contrast"))
	FRuntimeFloatCurve Contrast;

	/** A colour over the whole sky, over the day. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "Tint"))
	FRuntimeCurveLinearColor Tint;

	/** The tint overhead, over the day (a cyan zenith). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "Tint"))
	FRuntimeCurveLinearColor ZenithTint;

	/** The tint at the horizon, over the day (a warm band). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (XAxisName = "Sun elevation", YAxisName = "Tint"))
	FRuntimeCurveLinearColor HorizonTint;

	/** How the zenith tint gives way to the horizon's down the sky: below 1 the zenith's reaches low, above 1 the horizon's climbs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (ClampMin = "0.05", UIMax = "4"))
	float TintSpread = 0.5f;

	/** The twilight's glow (sun from 0 to 12 degrees under) scaled: brighter or softer afterglow than the air's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (ClampMin = "0", UIMax = "4"))
	float Afterglow = 1.0f;

	/** The saturation an overcast leaves (multiplied in at full cover). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float OvercastSaturation = 0.35f;

	/** The exposure an overcast takes off at full cover, EV. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade|Weather", meta = (UIMin = "-3", UIMax = "0"))
	float OvercastExposure = -0.5f;

	/** The colour dust in the air gives the sky, and how far at full dust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade|Weather", meta = (HideAlphaChannel))
	FLinearColor DustTint = FLinearColor(1.0f, 0.72f, 0.42f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float DustStrength = 0.8f;

	/** How grey fog makes the sky at full fog (the saturation it takes away). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade|Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float FogGrey = 0.6f;

	/** The grade for a sun elevation (degrees) and the weather (ignored unless Environment.bActive). */
	FDreamSkyGradeValues Evaluate(float SunElevation, const FDreamWorldEnvironment& Environment) const;
};
