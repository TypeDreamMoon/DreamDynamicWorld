// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamWeatherAudioTypes.h"
#include "DreamAmbiencePreset.generated.h"

class USoundBase;

/** A sound input of an ambience's graph. */
USTRUCT(BlueprintType)
struct DREAMWEATHERAUDIO_API FDreamAmbienceInput
{
	GENERATED_BODY()

	/** The graph input it is given to (a WaveAsset input, or a WaveAsset array with bArray). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	FName Input;

	/** The graph takes every sound of the slot as an array (birds drawn at random) rather than one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	bool bArray = false;

	/** The sounds; empty = the settings' Ambience slot of the same name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	FDreamWeatherSoundSlot Sounds;
};

/**
 * The ambience of a place: a MetaSound that makes the forest, the city or the shore out of the hour, the weather,
 * the wind and the season. Every frame the graph is given, all 0..1:
 *   TimeOfDay (hour / 24), Day, Twilight, Night (they sum to 1), Spring, Summer, Autumn, Winter (they sum to 1,
 *   blending across the turn of the season), Year (0 on 1 March .. 1), Cloud, Rain, Snow, Fog, Dust, Lightning,
 *   Wind, Gust, Interior, and the occlusion Left / Right / Muffle (as the weather's voices get it, when bOutdoor).
 * The plugin ships MS_DreamAmbienceForest (birds by day, insects at night, wind in the trees) and its preset.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dream Ambience Preset"))
class DREAMWEATHERAUDIO_API UDreamAmbiencePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The graph (a MetaSound source; any sound plays, but only a MetaSound reads the parameters). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	TSoftObjectPtr<USoundBase> Sound;

	/** Its sound inputs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	TArray<FDreamAmbienceInput> Inputs;

	/** More float inputs, constant (a density, a mood). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	TMap<FName, float> Constants;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience", meta = (ClampMin = "0", ClampMax = "2"))
	float Volume = 1.0f;

	/** The ambience is outdoor sound: walls and roofs muffle it (Left / Right / Muffle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambience")
	bool bOutdoor = true;
};

/** The numbers an ambience graph is given (see UDreamAmbiencePreset). */
struct DREAMWEATHERAUDIO_API FDreamAmbienceParams
{
	float TimeOfDay = 0.5f;
	FDreamDayPhase Phase;
	/** Spring, summer, autumn, winter. */
	float Seasons[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float Year = 0.0f;
	float Cloud = 0.0f;
	float Rain = 0.0f;
	float Snow = 0.0f;
	float Fog = 0.0f;
	float Dust = 0.0f;
	float Lightning = 0.0f;
	float Wind = 0.0f;
	float Gust = 0.0f;
	float Interior = 0.0f;
	FDreamWeatherVoiceMix Occlusion;

	/** Every parameter by its graph input name, in a fixed order. */
	void ForEach(TFunctionRef<void(FName, float)> Visit) const;

	/**
	 * The four seasons' weights for a date: each meteorological season is whole in its middle month and blends into
	 * the next over the month around the turn (spring March-May in the north; the south is six months on).
	 */
	static void SeasonWeights(const FDateTime& Date, bool bSouthernHemisphere, float OutWeights[4], float& OutYear);
};
