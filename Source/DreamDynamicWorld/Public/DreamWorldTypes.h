// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldTypes.generated.h"

/** How the sun and the moon move. */
UENUM(BlueprintType)
enum class EDreamCelestialMode : uint8
{
	/** Sunrise and sunset at fixed hours, a set height at midday; the moon where it reads best. Same every day. */
	Artistic,
	/** Sun and moon computed for a place on Earth and the date (accurate to about a degree), including the moon's real phase. */
	Realistic,
};

/** A daylight saving rule: when the clocks go an hour forward. */
UENUM(BlueprintType)
enum class EDreamDaylightSaving : uint8
{
	None,
	/** The European Union and the United Kingdom: the last Sunday of March to the last Sunday of October. */
	Europe,
	/** The United States and Canada: the second Sunday of March to the first Sunday of November. */
	NorthAmerica,
	/** South-eastern Australia: the first Sunday of October to the first Sunday of April. */
	SouthernAustralia,
	/** New Zealand: the last Sunday of September to the first Sunday of April. */
	NewZealand,
};

/** Where the sun and the moon are, for every day of the world. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamCelestialSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial")
	EDreamCelestialMode Mode = EDreamCelestialMode::Artistic;

	/** World yaw (degrees) of north. 0 = north is +X, east is +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial", meta = (UIMin = "-180", UIMax = "180"))
	float NorthYaw = 0.0f;

	/** Artistic: hour the sun crosses the horizon in the morning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Artistic", meta = (ClampMin = "0", ClampMax = "23.9", EditCondition = "Mode == EDreamCelestialMode::Artistic"))
	float SunriseHour = 6.0f;

	/** Artistic: hour the sun crosses the horizon in the evening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Artistic", meta = (ClampMin = "0.1", ClampMax = "24", EditCondition = "Mode == EDreamCelestialMode::Artistic"))
	float SunsetHour = 18.0f;

	/** Artistic: height of the sun above the horizon (degrees) halfway between sunrise and sunset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Artistic", meta = (ClampMin = "5", ClampMax = "90", EditCondition = "Mode == EDreamCelestialMode::Artistic"))
	float NoonElevation = 60.0f;

	/**
	 * Artistic: keep the moon opposite the sun (up all night, as at full moon) whatever its phase. The disk
	 * still shows the phase of the date. Off = the moon sits where its phase puts it and may be down at night.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Artistic", meta = (EditCondition = "Mode == EDreamCelestialMode::Artistic"))
	bool bMoonOppositeSun = true;

	/** Realistic: latitude in degrees, north positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Realistic", meta = (ClampMin = "-89.9", ClampMax = "89.9", EditCondition = "Mode == EDreamCelestialMode::Realistic"))
	double Latitude = 35.68;

	/** Realistic: longitude in degrees, east positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Realistic", meta = (ClampMin = "-180", ClampMax = "180", EditCondition = "Mode == EDreamCelestialMode::Realistic"))
	double Longitude = 139.69;

	/** Realistic: hours the world's clock is ahead of UTC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Realistic", meta = (ClampMin = "-14", ClampMax = "14", EditCondition = "Mode == EDreamCelestialMode::Realistic"))
	float UtcOffsetHours = 9.0f;

	/** Daylight saving: an hour more on the clock while the rule says (the clock tells local time, as a wall clock does). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Realistic", meta = (EditCondition = "Mode == EDreamCelestialMode::Realistic"))
	EDreamDaylightSaving DaylightSaving = EDreamDaylightSaving::None;

	/** A built-in city (UDreamWorldCalendar::GetCityPresets): its latitude, longitude, time and daylight saving in place
	 *  of the fields above. None: the fields above. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Celestial|Realistic", meta = (GetOptions = "DreamDynamicWorld.DreamWorldCalendar.GetCityNames", EditCondition = "Mode == EDreamCelestialMode::Realistic"))
	FName City;
};

/** Where the sun and the moon are at one moment. Directions point from the ground TO the body. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamCelestialState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	FVector SunDirection = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	FVector MoonDirection = -FVector::UpVector;

	/** Degrees above the horizon (negative below). */
	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float SunElevation = 90.0f;

	/** Degrees clockwise from north. */
	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float SunAzimuth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float MoonElevation = -90.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float MoonAzimuth = 0.0f;

	/** Position in the synodic month: 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter. */
	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float MoonPhase = 0.5f;

	/** Lit fraction of the moon's disk, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float MoonIllumination = 1.0f;

	/** Rotation of the star field about the celestial pole, degrees (local sidereal angle). */
	UPROPERTY(BlueprintReadOnly, Category = "Celestial")
	float StarRotation = 0.0f;
};

/** How time runs in a world. A level sets it through its ADreamSky (or any actor) and gameplay can change it. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamWorldClockConfig
{
	GENERATED_BODY()

	/** Date and time the world starts at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	FDateTime StartTime = FDateTime(2026, 6, 21, 9, 0, 0);

	/** Real minutes one in-game day lasts. 1440 = real time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock", meta = (ClampMin = "0.1", UIMin = "1", UIMax = "1440"))
	float DayLengthMinutes = 24.0f;

	/** Time runs in game worlds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	bool bAnimate = true;

	/** Time runs in the editor viewport too (otherwise it only moves when set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	bool bAnimateInEditor = false;

	/** Decorrelates everything random in the world (cloud shapes, later the weather). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	int32 Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	FDreamCelestialSettings Celestial;

	/** A custom calendar to tell the dates by (GetCalendarDate, the date events); none: the Gregorian. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clock")
	TObjectPtr<class UDreamCalendarAsset> Calendar;
};

/**
 * Everything needed to put a world's clock back where it was: store it in a save game and apply it on load.
 * Other subsystems add their state under Extensions (keyed by subsystem) through the world subsystem's
 * OnCaptureSnapshot / OnApplySnapshot.
 */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamWorldSnapshot
{
	GENERATED_BODY()

	/** Seconds since 0001-01-01 00:00 of the world's own calendar. */
	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Snapshot")
	double WorldSeconds = 0.0;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Snapshot")
	float TimeScale = 60.0f;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Snapshot")
	bool bPaused = false;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Snapshot")
	int32 Seed = 1337;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Snapshot")
	FDreamCelestialSettings Celestial;

	UPROPERTY(BlueprintReadWrite, SaveGame, Category = "Snapshot")
	TMap<FName, FString> Extensions;
};

/** An object in effect at some weight (FDreamWorldEnvironment::SkyModifiers). */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamWorldWeightedObject
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UObject> Object;

	/** 0..1. */
	UPROPERTY()
	float Weight = 0.0f;
};

/**
 * What the weather is doing where the world is being looked at from, as the sky and other looks need it.
 * Written by the weather every frame (before the sky updates); bActive is false when no weather runs, and the
 * sky then keeps to its own style.
 */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamWorldEnvironment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	bool bActive = false;

	/** Fraction of the sky covered by cloud, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float CloudCoverage = 0.0f;

	/** How dark and heavy the clouds are (storm), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float CloudDarkness = 0.0f;

	/** Rain or snow, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Precipitation = 0.0f;

	/** 0 rain .. 1 snow. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float SnowRatio = 0.0f;

	/** Fog, 0..1 (multiplies the sky's fog density). */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Fog = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Dust = 0.0f;

	/** Wind strength, 0 calm .. 1 storm. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Wind = 0.0f;

	/** Lightning frequency, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Lightning = 0.0f;

	/** Air temperature at the view, degrees Celsius. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float Temperature = 15.0f;

	/** A lightning flash dying away, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	float LightningFlash = 0.0f;

	/** Toward the flash, from the view. */
	UPROPERTY(BlueprintReadOnly, Category = "Environment")
	FVector LightningDirection = FVector::UpVector;

	/**
	 * The sky modifiers of the weathers in effect at the view (UDreamWeatherPreset::SkyModifier), each at how far its
	 * weather is in effect there. The weather holds them; the sky takes them on.
	 */
	UPROPERTY()
	TArray<FDreamWorldWeightedObject> SkyModifiers;
};

/**
 * How enclosed the view is, from rays cast around the camera (UDreamWorldSubsystem's view probe): what makes
 * the picture adapt when walking indoors, sounds muffle, rain stop drumming on the screen. Smoothed over a
 * fraction of a second; snaps when the view teleports.
 */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamViewOcclusion
{
	GENERATED_BODY()

	/** 0 out in the open .. 1 inside a closed room. */
	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	float Interior = 0.0f;

	/** 0 open sky above .. 1 a roof overhead (a porch or a bridge is covered without being indoors). */
	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	float Overhead = 0.0f;

	/** How closed each side is, 0..1, for sound from one direction (wind, rain on a wall). */
	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	float PlusX = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	float MinusX = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	float PlusY = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	float MinusY = 0.0f;

	/** False until a full round of rays has been cast (or when probing is off). */
	UPROPERTY(BlueprintReadOnly, Category = "View Occlusion")
	bool bValid = false;
};

/** The part of the day, as weights that always sum to 1. */
USTRUCT(BlueprintType)
struct DREAMDYNAMICWORLD_API FDreamDayPhase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Day Phase")
	float Day = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Day Phase")
	float Twilight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Day Phase")
	float Night = 0.0f;
};
