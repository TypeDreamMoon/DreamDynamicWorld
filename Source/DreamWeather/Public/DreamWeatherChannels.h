// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWeatherTypes.h"

/**
 * The registry of weather channels: the built-in ones first (fixed indices, below), then the project's own from
 * Project Settings > Dream Weather. Every FDreamWeatherValues is laid out in this order.
 */
class DREAMWEATHER_API FDreamWeatherChannels
{
public:
	enum EBuiltIn : int32
	{
		CloudCoverage = 0,
		CloudDarkness,
		Precipitation,
		SnowRatio,
		Fog,
		Dust,
		Wind,
		Lightning,
		Temperature,
		Wetness,
		SnowCover,
		FreezingRain,
		Hail,
		Sleet,
		NumBuiltIn
	};

	/** Every channel, built-in first. */
	static const TArray<FDreamWeatherChannelDef>& Get();

	/** Index of a channel, or INDEX_NONE. */
	static int32 IndexOf(const FGameplayTag& Channel);

	static int32 Num() { return Get().Num(); }

	/** Every channel at its default. */
	static FDreamWeatherValues Defaults();

	/** The built-in channels with their default timings (what the settings start from). */
	static TArray<FDreamWeatherChannelDef> MakeBuiltIn();

	/** Rebuilds the registry from the settings (after they changed). */
	static void Invalidate();

	/** Combines a value into a channel with weight W, the way modifiers and zones do. */
	static float Combine(EDreamWeatherBlend Blend, float Current, float Value, float W);

	/**
	 * Holds precipitation and lightning to what the clouds allow (Dream Weather settings > Clouds): no rain from a
	 * cover that is not closed, no lightning without a dark one. Applied to every value at a point, after the zones.
	 */
	static void ApplyCloudGates(FDreamWeatherValues& InOut);
};
