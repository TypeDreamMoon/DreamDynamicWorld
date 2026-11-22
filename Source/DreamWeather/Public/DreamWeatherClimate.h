// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherClimate.generated.h"

class UDreamWeatherPreset;

/** The Köppen climate types, each with a table of representative numbers (FDreamClimate::Koppen). */
UENUM(BlueprintType)
enum class EDreamKoppenClimate : uint8
{
	Af UMETA(DisplayName = "Af - Tropical rainforest"),
	Am UMETA(DisplayName = "Am - Tropical monsoon"),
	Aw UMETA(DisplayName = "Aw - Tropical savanna"),
	BWh UMETA(DisplayName = "BWh - Hot desert"),
	BWk UMETA(DisplayName = "BWk - Cold desert"),
	BSh UMETA(DisplayName = "BSh - Hot semi-arid"),
	BSk UMETA(DisplayName = "BSk - Cold semi-arid"),
	Csa UMETA(DisplayName = "Csa - Hot-summer Mediterranean"),
	Csb UMETA(DisplayName = "Csb - Warm-summer Mediterranean"),
	Cwa UMETA(DisplayName = "Cwa - Monsoon humid subtropical"),
	Cwb UMETA(DisplayName = "Cwb - Subtropical highland"),
	Cfa UMETA(DisplayName = "Cfa - Humid subtropical"),
	Cfb UMETA(DisplayName = "Cfb - Oceanic"),
	Cfc UMETA(DisplayName = "Cfc - Subpolar oceanic"),
	Dsa UMETA(DisplayName = "Dsa - Hot dry-summer continental"),
	Dsb UMETA(DisplayName = "Dsb - Warm dry-summer continental"),
	Dwa UMETA(DisplayName = "Dwa - Monsoon hot-summer continental"),
	Dwb UMETA(DisplayName = "Dwb - Monsoon warm-summer continental"),
	Dwc UMETA(DisplayName = "Dwc - Monsoon subarctic"),
	Dfa UMETA(DisplayName = "Dfa - Hot-summer humid continental"),
	Dfb UMETA(DisplayName = "Dfb - Warm-summer humid continental"),
	Dfc UMETA(DisplayName = "Dfc - Subarctic"),
	Dfd UMETA(DisplayName = "Dfd - Extremely cold subarctic"),
	ET UMETA(DisplayName = "ET - Tundra"),
	EF UMETA(DisplayName = "EF - Ice cap"),
	Num UMETA(Hidden)
};

/** One season of a climate (meteorological: spring is March to May in the north). */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamClimateSeason
{
	GENERATED_BODY()

	/** Mean daily high and low, degrees Celsius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	float High = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	float Low = 10.0f;

	/** Rain and snow (water) over the three months, mm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate", meta = (ClampMin = "0"))
	float PrecipitationMm = 150.0f;

	/** Share of the time the sky is mostly cloudy, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate", meta = (ClampMin = "0", ClampMax = "1"))
	float Cloudiness = 0.5f;
};

/** A climate: its four seasons (northern-hemisphere order; the south is six months on), and whether dust blows. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamClimate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	FDreamClimateSeason Spring;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	FDreamClimateSeason Summer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	FDreamClimateSeason Autumn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	FDreamClimateSeason Winter;

	/** Dry and windy enough for dust and sand storms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	bool bDust = false;

	const FDreamClimateSeason& Season(int32 Index) const;

	/** The representative numbers of a Köppen type. */
	static const FDreamClimate& Koppen(EDreamKoppenClimate Type);

	/**
	 * The temperature model's three numbers (yearly mean, half the swing between the warmest and the coldest season,
	 * half the daily swing), so the weather's temperature follows the climate.
	 */
	void GetTemperature(float& OutMean, float& OutSeasonalAmplitude, float& OutDiurnalAmplitude) const;

	/**
	 * Odds of each kind of weather in a season, relative: clear, cloudy, overcast, rain, storm, snow, fog, dust. From
	 * how wet (precipitation), how cloudy and how cold (snow instead of rain) the season is; storms in warm wet
	 * seasons, dust only where it blows.
	 */
	static constexpr int32 NumKinds = 8;
	void GetWeatherOdds(int32 Season, float OutOdds[NumKinds]) const;
};

/** A climate of your own (a fantasy world), for the Climate selector. */
UCLASS(BlueprintType, meta = (DisplayName = "Dream Climate"))
class DREAMWEATHER_API UDreamClimatePreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climate")
	FDreamClimate Climate;
};

/**
 * Random weather whose odds and temperature come from a climate: pick a Köppen type (or your own climate asset), the
 * weather draws clear, cloudy, overcast, rain, storm, snow, fog or dust by the season's odds, and the temperature
 * takes the climate's means and swings instead of the project settings'.
 */
UCLASS(meta = (DisplayName = "Climate"))
class DREAMWEATHER_API UDreamWeatherSelector_Climate : public UDreamWeatherSelector_Drawn
{
	GENERATED_BODY()

public:
	UDreamWeatherSelector_Climate();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	EDreamKoppenClimate Climate = EDreamKoppenClimate::Cfb;

	/** Use this climate instead of the Köppen type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	TObjectPtr<UDreamClimatePreset> CustomClimate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate")
	bool bSouthernHemisphere = false;

	/** The presets for each kind of weather: clear, cloudy, overcast, rain, storm, snow, fog, dust (the plugin's by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Climate", EditFixedSize)
	TArray<TSoftObjectPtr<UDreamWeatherPreset>> Presets;

	const FDreamClimate& GetClimate() const;

	virtual bool GetClimateTemperature(float& OutMean, float& OutSeasonalAmplitude, float& OutDiurnalAmplitude) const override;

protected:
	virtual float WeightOf(int32 EntryIndex, int64 SlotIndex) const override;
	virtual int32 NumEntries() const override { return Presets.Num(); }
	virtual const UDreamWeatherPreset* PresetOf(int32 EntryIndex) const override { return Presets[EntryIndex].LoadSynchronous(); }
};
