// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DreamWeatherTypes.generated.h"

class UDreamWeatherPreset;

/** A weather channel: a named number the weather is made of (cloud cover, rain, a project's own "ash"...). */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherChannelDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (Categories = "Dream.Weather.Channel"))
	FGameplayTag Channel;

	/** The value when no weather says otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	float Default = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	float Min = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel")
	float Max = 1.0f;

	/** Game minutes a full change of weather takes for this channel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (ClampMin = "0", UIMax = "240"))
	float TransitionMinutes = 20.0f;

	/**
	 * Game minutes this channel starts changing BEFORE the weather turns (negative = after). Clouds and wind
	 * leading and rain trailing is what makes a change of weather read as coming.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Channel", meta = (UIMin = "-120", UIMax = "120"))
	float LeadMinutes = 0.0f;
};

/** How a modifier or zone combines its value with the weather underneath. */
UENUM(BlueprintType)
enum class EDreamWeatherBlend : uint8
{
	/** Pull toward the value by the weight. */
	Override,
	/** Add value * weight. */
	Add,
	/** Multiply by lerp(1, value, weight). */
	Multiply,
	/** Raise to at least the value (by the weight). */
	Max,
	/** Lower to at most the value (by the weight). */
	Min,
};

/** Values for every registered channel, in registry order (see FDreamWeatherChannels). */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherValues
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	TArray<float> Values;

	float Get(int32 Index) const { return Values.IsValidIndex(Index) ? Values[Index] : 0.0f; }
	void Set(int32 Index, float Value) { if (Values.IsValidIndex(Index)) { Values[Index] = Value; } }
};

/** Something that bends the weather for a while: a scripted storm, a spell that brings rain, "keep it clear here". */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherModifierDesc
{
	GENERATED_BODY()

	/** Who asked (for debugging and save games). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	FName Source;

	/** Channel values to apply. Applied on top of Preset's values when both are given. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier", meta = (Categories = "Dream.Weather.Channel"))
	TMap<FGameplayTag, float> Channels;

	/** A preset whose channels the modifier applies (optional). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	TObjectPtr<UDreamWeatherPreset> Preset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	EDreamWeatherBlend Blend = EDreamWeatherBlend::Override;

	/** Full strength, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier", meta = (ClampMin = "0", ClampMax = "1"))
	float Weight = 1.0f;

	/** Higher applies later (wins). Equal priorities apply in the order they were pushed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier")
	int32 Priority = 0;

	/** Real seconds to fade in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifier", meta = (ClampMin = "0"))
	float BlendInSeconds = 5.0f;
};

/** A handle to a pushed modifier. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherModifierHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Modifier")
	int32 Id = 0;

	bool IsValid() const { return Id != 0; }
};

/** A modifier as it stands (what a save game stores). */
USTRUCT()
struct DREAMWEATHER_API FDreamWeatherModifierState
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	UPROPERTY()
	FDreamWeatherModifierDesc Desc;

	/** Current strength, 0..Desc.Weight. */
	UPROPERTY()
	float Current = 0.0f;

	/** Where it is heading (0 while fading out). */
	UPROPERTY()
	float Target = 0.0f;

	/** Real seconds the current fade takes over its whole range. */
	UPROPERTY()
	float FadeSeconds = 0.0f;

	/** Fading out to be dropped. */
	UPROPERTY()
	bool bRemoving = false;

	/** A forced weather being replaced: held until this modifier (its replacement) is fully in, then dropped. */
	UPROPERTY()
	int32 SupersededBy = 0;
};

/** The weather's part of a world snapshot. */
USTRUCT()
struct DREAMWEATHER_API FDreamWeatherSnapshotData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDreamWeatherModifierState> Modifiers;

	UPROPERTY()
	int32 NextModifierId = 1;
};

/** The weather at one point. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weather") float CloudCoverage = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float CloudDarkness = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Precipitation = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SnowRatio = 0.0f;
	/** The share of the rain that is supercooled, 0..1 (the FreezingRain channel): it stays rain below freezing. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float FreezingRain = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Fog = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Dust = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Wind = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Lightning = 0.0f;
	/** Air temperature, degrees Celsius (climate, season, hour, height and the weather's offset). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Temperature = 15.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Wetness = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SnowCover = 0.0f;

	/** Rain falling (precipitation that is mostly rain), in the open. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Rain = 0.0f;
	/** Snow falling, in the open. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Snow = 0.0f;
	/** Hail falling, in the open (the Hail channel's share of the precipitation). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Hail = 0.0f;
	/** Sleet falling, in the open (the Sleet channel's share of what is left). Rain and Snow are what is left of the
	 *  precipitation after the hail and the sleet. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Sleet = 0.0f;

	/** How open the point is to the sky, 0 under a roof .. 1 in the open (shelters, baked level heights). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Exposure = 1.0f;
	/** Exposure below one half. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") bool bSheltered = false;
	/** Rain actually landing on the point (Rain * Exposure). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float RainHere = 0.0f;
	/** Snow actually landing on the point. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SnowHere = 0.0f;
	/** Hail and sleet actually landing on the point. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float HailHere = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SleetHere = 0.0f;

	/** Dream.Weather.State.* tags for this point. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") FGameplayTagContainer Tags;

	/** Every channel, registry order. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") FDreamWeatherValues Values;
};

/** A lightning strike. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamLightningStrike
{
	GENERATED_BODY()

	/** Where it hits the ground (or the rod that drew it). */
	UPROPERTY(BlueprintReadOnly, Category = "Lightning")
	FVector Location = FVector::ZeroVector;

	/** 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightning")
	float Intensity = 1.0f;

	/** Horizontal distance from the view, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightning")
	float Distance = 0.0f;

	/** Real seconds until the thunder reaches the view (sound at 343 m/s). */
	UPROPERTY(BlueprintReadOnly, Category = "Lightning")
	float ThunderDelay = 0.0f;

	/** The rod it struck, if one drew it. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightning")
	TWeakObjectPtr<UObject> Target;

	/** World seconds it happened at. */
	double WorldSeconds = 0.0;
};

/** One step of a forecast. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherForecastEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	FDateTime Time;

	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;
};
