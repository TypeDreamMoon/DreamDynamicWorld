// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamWeatherSelector.generated.h"

class UDreamWeatherPreset;

/** The stretch of time a weather holds: from Start for Length seconds Current is the weather, Previous the one before. */
struct FDreamWeatherSlot
{
	const UDreamWeatherPreset* Previous = nullptr;
	const UDreamWeatherPreset* Current = nullptr;
	/** World seconds. */
	double Start = 0.0;
	/** Seconds; a very large number for a weather that never changes. */
	double Length = 1.0e12;
	/** Index of the slot (forecasts and tests). */
	int64 Index = 0;
};

/**
 * Chooses the weather over time. Every selector is a pure function of the world time and the world seed: the
 * same moment has the same weather on every machine, after every load, in every replay. Time is cut into
 * slots; the weather changes at slot boundaries and each channel eases across them (see FDreamWeatherChannelDef).
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class DREAMWEATHER_API UDreamWeatherSelector : public UObject
{
	GENERATED_BODY()

public:
	/** The slot containing WorldSeconds. */
	virtual FDreamWeatherSlot GetSlot(double WorldSeconds, int32 Seed) const PURE_VIRTUAL(UDreamWeatherSelector::GetSlot, return FDreamWeatherSlot(););

	/** A number in [0, 1) from a seed, a slot and a salt: the selectors' only source of chance. */
	static float Hash01(int32 Seed, int64 Slot, uint32 Salt);

	/** Northern-hemisphere season of a moment, 0 spring .. 3 winter (flip with bSouthernHemisphere). */
	static int32 SeasonAt(double WorldSeconds, bool bSouthernHemisphere);

	/**
	 * A selector that stands for a climate gives the temperature model its numbers (yearly mean, seasonal and daily
	 * half-swings, degrees Celsius) in place of the project settings'. False: use the settings.
	 */
	virtual bool GetClimateTemperature(float& OutMean, float& OutSeasonalAmplitude, float& OutDiurnalAmplitude) const { return false; }
};

/** One weather, always. */
UCLASS(meta = (DisplayName = "Fixed"))
class DREAMWEATHER_API UDreamWeatherSelector_Fixed : public UDreamWeatherSelector
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;

	virtual FDreamWeatherSlot GetSlot(double WorldSeconds, int32 Seed) const override;
};

USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherSequenceEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;

	/** Game hours it lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.1"))
	float Hours = 6.0f;
};

/** A fixed cycle of weathers (a scripted week, a level that always storms at dusk...). Repeats from the calendar's epoch. */
UCLASS(meta = (DisplayName = "Sequence"))
class DREAMWEATHER_API UDreamWeatherSelector_Sequence : public UDreamWeatherSelector
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TArray<FDreamWeatherSequenceEntry> Entries;

	virtual FDreamWeatherSlot GetSlot(double WorldSeconds, int32 Seed) const override;
};

USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherWeightedEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float Weight = 1.0f;
};

/** Weather drawn every few hours by chance; a weather tends to stay for a while (Persistence). */
UCLASS(Abstract)
class DREAMWEATHER_API UDreamWeatherSelector_Drawn : public UDreamWeatherSelector
{
	GENERATED_BODY()

public:
	/** Game hours per draw. Should be longer than the slowest channel's change (40 game minutes by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0.5"))
	float SlotHours = 3.0f;

	/** Chance a draw keeps the weather it follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "0.95"))
	float Persistence = 0.4f;

	virtual FDreamWeatherSlot GetSlot(double WorldSeconds, int32 Seed) const override;

protected:
	/** The preset slot Index draws, given each entry's weight. */
	const UDreamWeatherPreset* Choose(int64 Index, int32 Seed, int32 Depth) const;
	virtual int32 NumEntries() const { return 0; }
	virtual float WeightOf(int32 EntryIndex, int64 SlotIndex) const { return 0.0f; }
	virtual const UDreamWeatherPreset* PresetOf(int32 EntryIndex) const { return nullptr; }
};

/** Weather drawn at random from fixed weights. */
UCLASS(meta = (DisplayName = "Random"))
class DREAMWEATHER_API UDreamWeatherSelector_Random : public UDreamWeatherSelector_Drawn
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TArray<FDreamWeatherWeightedEntry> Entries;

protected:
	virtual int32 NumEntries() const override { return Entries.Num(); }
	virtual float WeightOf(int32 EntryIndex, int64 SlotIndex) const override;
	virtual const UDreamWeatherPreset* PresetOf(int32 EntryIndex) const override { return Entries[EntryIndex].Preset; }
};

USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherSeasonalEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float Spring = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float Summer = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float Autumn = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0"))
	float Winter = 1.0f;
};

/** Random weather whose odds follow the season of the date (a climate). */
UCLASS(meta = (DisplayName = "Seasonal"))
class DREAMWEATHER_API UDreamWeatherSelector_Seasonal : public UDreamWeatherSelector_Drawn
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TArray<FDreamWeatherSeasonalEntry> SeasonalEntries;

	/** Seasons are those of the southern hemisphere (summer in December). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bSouthernHemisphere = false;

protected:
	virtual float WeightOf(int32 EntryIndex, int64 SlotIndex) const override;
	virtual int32 NumEntries() const override { return SeasonalEntries.Num(); }
	virtual const UDreamWeatherPreset* PresetOf(int32 EntryIndex) const override { return SeasonalEntries[EntryIndex].Preset; }
};
