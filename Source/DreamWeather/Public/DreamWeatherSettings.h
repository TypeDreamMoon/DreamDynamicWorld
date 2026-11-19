// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamWeatherTypes.h"
#include "DreamWeatherSettings.generated.h"

/** Project Settings > DreamPlugin > Dream Weather. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Weather"))
class DREAMWEATHER_API UDreamWeatherSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamWeatherSettings();

	static const UDreamWeatherSettings* Get() { return GetDefault<UDreamWeatherSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- Channels -----------------------------------------------------------------------------

	/** The built-in channels' defaults and timings (their tags and order are fixed). */
	UPROPERTY(Config, EditAnywhere, Category = "Channels", EditFixedSize)
	TArray<FDreamWeatherChannelDef> BuiltInChannels;

	/** The project's own channels (register their tags under Dream.Weather.Channel or anywhere else). */
	UPROPERTY(Config, EditAnywhere, Category = "Channels")
	TArray<FDreamWeatherChannelDef> CustomChannels;

	// ---- Without a weather actor ----------------------------------------------------------------

	/** Run the weather in levels that have no ADreamWeather (with DefaultPreset). Off = the sky keeps to its style. */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bRunWithoutWeatherActor = false;

	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (AllowedClasses = "/Script/DreamWeather.DreamWeatherPreset"))
	FSoftObjectPath DefaultPreset = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/Presets/WP_Clear.WP_Clear"));

	// ---- Temperature ----------------------------------------------------------------------------

	/** Yearly mean at the base height, degrees Celsius. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature")
	float MeanTemperature = 14.0f;

	/** Half the difference between the warmest and coldest month. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0"))
	float SeasonalAmplitude = 9.0f;

	/** Half the difference between the afternoon and the night. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0"))
	float DiurnalAmplitude = 5.0f;

	/** Hour of the day's warmest point. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0", ClampMax = "24"))
	float WarmestHour = 15.0f;

	/** Degrees lost per 100 m of height above BaseHeight. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature", meta = (ClampMin = "0", UIMax = "2"))
	float LapseRatePer100m = 0.65f;

	/** World Z (cm) the mean temperature is for. */
	UPROPERTY(Config, EditAnywhere, Category = "Temperature")
	float BaseHeight = 0.0f;

	// ---- Wind -----------------------------------------------------------------------------------

	/** The weather sets the global wind's speed and gusts from its Wind channel (direction is left alone). */
	UPROPERTY(Config, EditAnywhere, Category = "Wind")
	bool bDriveWind = true;

	/** Wind speed (cm/s) at Wind 0 and 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Wind", meta = (ClampMin = "0"))
	FVector2D WindSpeedRange = FVector2D(150.0, 2200.0);

	/** Gust strength at Wind 0 and 1. */
	UPROPERTY(Config, EditAnywhere, Category = "Wind", meta = (ClampMin = "0"))
	FVector2D WindGustRange = FVector2D(0.25, 0.9);

	// ---- Shelter --------------------------------------------------------------------------------

	/** The wind's baked level heights (ADreamWindGlobal's terrain map) also count as roofs. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter")
	bool bUseWindTerrainHeights = true;

	/** A surface this far (cm) above a point covers it. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter", meta = (ClampMin = "0"))
	float RoofClearance = 30.0f;

	/** How fast rain falls (cm/s): with the wind's speed it gives the slant under which rain reaches under eaves. */
	UPROPERTY(Config, EditAnywhere, Category = "Shelter", meta = (ClampMin = "100"))
	float RainFallSpeed = 900.0f;

	// ---- What the clouds allow ------------------------------------------------------------------

	/**
	 * Rain and snow need a closed cloud cover: precipitation is held to nothing below the first cloud coverage and let
	 * through in full from the second, after every preset, modifier and zone (at a point, and in the weather map). A
	 * change of weather then stops raining before its clouds break up, and starts only once they have closed in.
	 * Second <= first: no limit.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D PrecipitationCloudCover = FVector2D(0.75, 0.9);

	/** Lightning needs a storm cloud: held to nothing below the first cloud darkness, in full from the second (second <= first: no limit). */
	UPROPERTY(Config, EditAnywhere, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D LightningCloudDarkness = FVector2D(0.45, 0.75);

	/** ...under a closed cover: held to nothing below the first cloud coverage, in full from the second (second <= first: no limit). */
	UPROPERTY(Config, EditAnywhere, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D LightningCloudCover = FVector2D(0.85, 0.97);

	// ---- Lightning ------------------------------------------------------------------------------

	/** Strikes per real minute at Lightning 1 (real, so a storm looks the same at any day length). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0"))
	float StrikesPerMinute = 10.0f;

	/** Strikes land this far from the view, cm (nearest, farthest). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning")
	FVector2D StrikeRange = FVector2D(30000.0, 600000.0);

	/**
	 * Share of strikes placed in the half of the sky the view faces (0 = all around, 1 = always in front), so a
	 * storm's lightning is seen rather than only heard. Still only where the weather allows lightning.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "1"))
	float StrikeViewBias = 0.6f;

	/** Real seconds a flash lasts, first stroke to the end of its afterglow (each flash draws its own from the range). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning")
	FVector2D FlashDuration = FVector2D(0.8, 1.6);

	/** Strokes in one flash: the bright stutter of a real strike (each flash draws its own from the range). */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning")
	FIntPoint FlashStrokes = FIntPoint(2, 4);

	/** Thunder delay per km of distance, seconds. Sound takes 2.9 s/km; games often shorten it. */
	UPROPERTY(Config, EditAnywhere, Category = "Lightning", meta = (ClampMin = "0", UIMax = "3"))
	float ThunderSecondsPerKm = 2.9155f;

	// ---- Events ---------------------------------------------------------------------------------

	/** Precipitation at the view above this starts "it is raining/snowing"... */
	UPROPERTY(Config, EditAnywhere, Category = "Events", meta = (ClampMin = "0", ClampMax = "1"))
	float PrecipitationStart = 0.15f;

	/** ...and below this stops it. */
	UPROPERTY(Config, EditAnywhere, Category = "Events", meta = (ClampMin = "0", ClampMax = "1"))
	float PrecipitationStop = 0.05f;

	// ---- Weather map ----------------------------------------------------------------------------

	/** Draw the zones into a map around the camera that the sky reads (storm clouds over a far valley). */
	UPROPERTY(Config, EditAnywhere, Category = "Weather Map")
	bool bWeatherMap = true;

	UPROPERTY(Config, EditAnywhere, Category = "Weather Map", meta = (AllowedClasses = "/Script/Engine.TextureRenderTarget2D"))
	FSoftObjectPath WeatherMapTexture = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/RT_DreamWeatherMap.RT_DreamWeatherMap"));

	/** Texels per side. */
	UPROPERTY(Config, EditAnywhere, Category = "Weather Map", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 WeatherMapResolution = 512;

	/**
	 * World size the map covers, cm (centred on the camera). The sky reads it as far as half of it: a storm zone
	 * shows as towers on the horizon (30-60 km out) only if the map reaches it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather Map", meta = (ClampMin = "10000"))
	float WeatherMapExtent = 12000000.0f;

	/** Real seconds between redraws while something changes. */
	UPROPERTY(Config, EditAnywhere, Category = "Weather Map", meta = (ClampMin = "0"))
	float WeatherMapInterval = 0.1f;

	/** Most zones drawn into the map (the highest priorities win). */
	UPROPERTY(Config, EditAnywhere, Category = "Weather Map", meta = (ClampMin = "1", ClampMax = "256"))
	int32 MaxMapZones = 64;
};
