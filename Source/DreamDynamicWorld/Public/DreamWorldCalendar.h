// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamWorldTypes.h"
#include "DreamWorldCalendar.generated.h"

/** A place on Earth the realistic sky can stand at: where it is, its standard time, its daylight saving. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamCityPreset
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar")
	double Latitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar")
	double Longitude = 0.0;

	/** Its standard time's offset from UTC, hours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar")
	float UtcOffsetHours = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar")
	EDreamDaylightSaving DaylightSaving = EDreamDaylightSaving::None;
};

/** A month of a custom calendar. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamCalendarMonth
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar")
	FText Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calendar", meta = (ClampMin = "1", UIMax = "100"))
	int32 Days = 30;
};

/** A date as a calendar tells it (the Gregorian one, or a custom one). */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamCalendarDate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 Year = 1;
	/** 1 .. the number of months. */
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 Month = 1;
	/** 1 .. the month's days. */
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 Day = 1;
	/** 1 .. the year's days. */
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 DayOfYear = 1;
	/** 0 .. the week's days - 1 (Gregorian: 0 Monday). */
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 WeekDay = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 Hour = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") int32 Minute = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") FText MonthName;
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") FText WeekDayName;
	UPROPERTY(BlueprintReadOnly, Category = "Calendar") FText EraName;
};

/**
 * A custom calendar: its months (names, days), its week, its era and where its years start counting. The world clock
 * still runs on the Gregorian calendar underneath (the sun and the seasons go by it); this one tells the dates (the
 * world clock's GetCalendarDate, UI) and their events, the year's first day lining up with the Gregorian year's day
 * FirstDayOfYear.
 */
UCLASS(BlueprintType)
class DREAMDYNAMICWORLD_API UDreamCalendarAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	TArray<FDreamCalendarMonth> Months;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	TArray<FText> WeekDays;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	FText EraName;

	/** The year this calendar calls the world's first (day 0001-01-01 of the Gregorian underneath). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calendar")
	int32 FirstYear = 1;

	/** How many days its years run, the months' days together. */
	UFUNCTION(BlueprintPure, Category = "Calendar")
	int32 GetDaysInYear() const;

	/** The date at a world time (seconds from 0001-01-01, as the world clock counts). */
	FDreamCalendarDate ToDate(double WorldSeconds) const;
};

/**
 * The world's calendar: the built-in city presets the realistic sky can stand at (FDreamCelestialSettings::City),
 * daylight saving by its rules, and dates as the Gregorian or a custom calendar tells them.
 */
UCLASS()
class DREAMDYNAMICWORLD_API UDreamWorldCalendar : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The built-in cities. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Calendar")
	static const TArray<FDreamCityPreset>& GetCityPresets();

	/** The built-in cities' names (the City pick list). */
	UFUNCTION(BlueprintPure, Category = "Dream World|Calendar")
	static TArray<FName> GetCityNames();

	UFUNCTION(BlueprintPure, Category = "Dream World|Calendar")
	static bool FindCity(FName Name, FDreamCityPreset& OutCity);

	/** Whether daylight saving is on at a local date and time under a rule. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Calendar")
	static bool IsDaylightSaving(EDreamDaylightSaving Rule, FDateTime Local);

	/** Writes a picked city's latitude, longitude, standard time and daylight saving rule into the settings (nothing if
	 *  none is picked or it is not found). The world clock does it as a config is applied. */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Calendar")
	static void ApplyCity(UPARAM(ref) FDreamCelestialSettings& Settings);

	/** The settings the sky goes by at a local time: the city's place, time and rule (if one is picked), the offset from
	 *  UTC with an hour more under daylight saving. */
	static FDreamCelestialSettings Resolve(const FDreamCelestialSettings& Settings, const FDateTime& Local);

	/** A date as the Gregorian calendar tells it (Monday the week's first day). */
	static FDreamCalendarDate GregorianDate(double WorldSeconds);
};
