// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "DreamWorldTypes.h"
#include "DreamSkyConditions.generated.h"

class UDreamSkyModifier;

/** A weather value a condition can look at (FDreamWorldEnvironment, at the view). */
UENUM(BlueprintType)
enum class EDreamSkyWeatherValue : uint8
{
	CloudCoverage,
	CloudDarkness,
	/** Rain or snow, 0..1. */
	Precipitation,
	/** The rain part of it, 0..1. */
	Rain,
	/** The snow part of it, 0..1. */
	Snow,
	Fog,
	Dust,
	Wind,
	Lightning,
	/** Air temperature at the view, degrees Celsius. */
	Temperature,
};

/** Where the view must be for a condition to hold (the view probe's interior: 0 outside .. 1 in a closed room). */
UENUM(BlueprintType)
enum class EDreamSkyPlace : uint8
{
	Anywhere,
	/** As much as the view is outdoors. */
	Outdoors,
	/** As much as the view is indoors. */
	Indoors,
};

/** A weather value between two bounds, with soft edges. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyWeatherRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EDreamSkyWeatherValue Value = EDreamSkyWeatherValue::Precipitation;

	/** Holds from here up (fully once Soft above it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	float Min = 0.3f;

	/** Holds up to here (fading out over Soft above it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	float Max = 1000.0f;

	/** How wide the soft edges are (0: hard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0"))
	float Soft = 0.1f;

	/** How far the value holds, 0..1. */
	float Evaluate(const FDreamWorldEnvironment& Environment) const;
};

/**
 * When something takes effect (a post process, a sky modifier): every part that is on must hold, and each part goes
 * smoothly from 0 to 1 at its edges, the whole being their product.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyCondition
{
	GENERATED_BODY()

	/** Only between StartHour and EndHour (the window may run past midnight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (InlineEditConditionToggle))
	bool bHours = false;

	/** The hour it starts fading in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (EditCondition = "bHours", ClampMin = "0", ClampMax = "24"))
	float StartHour = 19.0f;

	/** The hour it has faded out by. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (EditCondition = "bHours", ClampMin = "0", ClampMax = "24"))
	float EndHour = 5.0f;

	/** Hours it fades over at each end, inside the window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (EditCondition = "bHours", ClampMin = "0", ClampMax = "12"))
	float FadeHours = 1.0f;

	/** Weather values that must hold (all of them). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	TArray<FDreamSkyWeatherRange> Weather;

	/** Where the view must be. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	EDreamSkyPlace Place = EDreamSkyPlace::Anywhere;

	/** How far it holds, 0..1, at an hour of the day (0..24) with this weather and view. */
	float Evaluate(float Hours, const FDreamWorldEnvironment& Environment, const FDreamViewOcclusion& View) const;
};

/** A post process the sky blends in while its condition holds (ADreamSky::PostProcesses). */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyConditionalPostProcess
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process")
	bool bEnabled = true;

	/** A name to know it by (the editor's and the log's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process")
	FDreamSkyCondition Condition;

	/** Its weight while the condition holds in full, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxWeight = 1.0f;

	/** Seconds its weight takes to follow the condition (on top of the condition's own soft edges); 0 = at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process", meta = (ClampMin = "0", Units = "s"))
	float BlendSeconds = 2.0f;

	/** Among post processes, higher goes on later (the sky's own exposure stands at -1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process")
	float Priority = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process")
	FPostProcessSettings Settings;
};

/** A sky modifier the sky holds at the strength its condition holds to (ADreamSky::ConditionalModifiers). */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyConditionalModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	TObjectPtr<UDreamSkyModifier> Modifier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	FDreamSkyCondition Condition;

	/** Seconds its strength takes to follow the condition; 0 = at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier", meta = (ClampMin = "0", Units = "s"))
	float BlendSeconds = 2.0f;
};

namespace DreamSky::Conditions
{
	/** Eases a weight toward Target: at once when Seconds is 0 or on a snap, else exponentially over Seconds. */
	DREAMSKY_API float Follow(float Weight, float Target, float Seconds, float DeltaTime, bool bSnap);
}
