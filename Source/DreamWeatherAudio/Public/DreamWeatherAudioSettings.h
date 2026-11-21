// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "DreamWeatherAudioTypes.h"
#include "DreamWeatherAudioSettings.generated.h"

class UDreamAmbiencePreset;
class USoundClass;

/**
 * A set of the weather's sounds, as an asset: hand one to the settings (SoundSet) or to the subsystem at run time
 * (a desert's wind, a city's rain). Its filled slots win over the settings' own.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dream Weather Sound Set"))
class DREAMWEATHERAUDIO_API UDreamWeatherSoundSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds", meta = (ShowOnlyInnerProperties))
	FDreamWeatherSounds Sounds;
};

/**
 * Project Settings > DreamPlugin > Dream Weather Audio: every sound of the weather and the ambiences (one slot each,
 * empty until the project fills it), the graphs that play them and how the weather turns into loudness.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Dream Weather Audio"))
class DREAMWEATHERAUDIO_API UDreamWeatherAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamWeatherAudioSettings();

	static const UDreamWeatherAudioSettings* Get() { return GetDefault<UDreamWeatherAudioSettings>(); }

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("DreamPlugin"); }

	/** Play the weather's sound (in game worlds). */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	/** Play it in editor viewports too (with the viewport's realtime on). */
	UPROPERTY(Config, EditAnywhere, Category = "General")
	bool bInEditor = false;

	/** Loudness of everything the weather plays. */
	UPROPERTY(Config, EditAnywhere, Category = "General", meta = (ClampMin = "0", ClampMax = "2"))
	float MasterVolume = 1.0f;

	// ---- Sounds -------------------------------------------------------------------------------

	/** The sounds, one slot per voice. */
	UPROPERTY(Config, EditAnywhere, Category = "Sounds")
	FDreamWeatherSounds Sounds;

	/** A sound set whose filled slots replace these (optional). */
	UPROPERTY(Config, EditAnywhere, Category = "Sounds")
	TSoftObjectPtr<UDreamWeatherSoundSet> SoundSet;

	/** The graph a sound wave in a slot is played through (it loops, low-passes and places it). */
	UPROPERTY(Config, EditAnywhere, Category = "Sounds", meta = (AllowedClasses = "/Script/MetasoundEngine.MetaSoundSource"))
	FSoftObjectPath VoiceGraph = FSoftObjectPath(TEXT("/DreamDynamicWorld/Audio/MS_DreamWeatherVoice.MS_DreamWeatherVoice"));

	/** The sound class of everything the weather plays (parent it under your own SFX class to mix it). */
	UPROPERTY(Config, EditAnywhere, Category = "Sounds", meta = (AllowedClasses = "/Script/Engine.SoundClass"))
	FSoftObjectPath WeatherSoundClass = FSoftObjectPath(TEXT("/DreamDynamicWorld/Audio/SC_DreamWeather.SC_DreamWeather"));

	/**
	 * "Outdoor" sound: give this class (or a class under it) to your own sounds of the outside - traffic, a river,
	 * birds - and walls and roofs muffle them like the rain. The weather pushes a sound mix that sets the class's
	 * volume and low-pass from the view probe.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Sounds", meta = (AllowedClasses = "/Script/Engine.SoundClass"))
	FSoftObjectPath OutdoorSoundClass = FSoftObjectPath(TEXT("/DreamDynamicWorld/Audio/SC_DreamOutdoor.SC_DreamOutdoor"));

	// ---- Ambience -----------------------------------------------------------------------------

	/** The ambience that plays where nothing else asked for one (none = silence). */
	UPROPERTY(Config, EditAnywhere, Category = "Ambience")
	TSoftObjectPtr<UDreamAmbiencePreset> DefaultAmbience;

	UPROPERTY(Config, EditAnywhere, Category = "Ambience", meta = (ClampMin = "0", ClampMax = "2"))
	float AmbienceVolume = 1.0f;

	// ---- Loudness -----------------------------------------------------------------------------

	/** Wind speed (cm/s) at which the wind is at full loudness. */
	UPROPERTY(Config, EditAnywhere, Category = "Loudness", meta = (ClampMin = "100"))
	float WindLoudSpeed = 1800.0f;

	/** Seconds a voice takes to fade out before it is stopped. */
	UPROPERTY(Config, EditAnywhere, Category = "Loudness", meta = (ClampMin = "0"))
	float FadeOutSeconds = 2.0f;

	// ---- Occlusion ----------------------------------------------------------------------------

	/** Loudness of outdoor sound in a closed room. */
	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "0", ClampMax = "1"))
	float InteriorMinVolume = 0.2f;

	/** Low-pass cutoff in a closed room, Hz (20 kHz in the open). */
	UPROPERTY(Config, EditAnywhere, Category = "Occlusion", meta = (ClampMin = "50", ClampMax = "20000"))
	float MinCutoffHz = 600.0f;

	// ---- Thunder ------------------------------------------------------------------------------

	/** Strikes closer than this (cm) clap (ThunderNear); further ones rumble (ThunderFar). */
	UPROPERTY(Config, EditAnywhere, Category = "Thunder", meta = (ClampMin = "0"))
	float ThunderNearDistance = 300000.0f;

	/** Strikes further than this (cm) are not heard. */
	UPROPERTY(Config, EditAnywhere, Category = "Thunder", meta = (ClampMin = "1000"))
	float ThunderMaxDistance = 1500000.0f;

	/** How much of the walls' muffling thunder takes (its low end carries), 0..1. */
	UPROPERTY(Config, EditAnywhere, Category = "Thunder", meta = (ClampMin = "0", ClampMax = "1"))
	float ThunderInteriorMuffle = 0.6f;

	/** Thunder claps under way at most; a further strike replaces the quietest. */
	UPROPERTY(Config, EditAnywhere, Category = "Thunder", meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaxThunders = 6;

	// ---- Rain surfaces ------------------------------------------------------------------------

	/** The rain's fall paths ending this close (cm) say what the rain near the listener lands on. */
	UPROPERTY(Config, EditAnywhere, Category = "Rain Surfaces", meta = (ClampMin = "100"))
	float SurfaceRadius = 1500.0f;

	/** The sounds in use: the settings' slots, with the sound set's filled ones over them. */
	FDreamWeatherSounds ResolveSounds() const;

	/** The tuning the model runs with. */
	FDreamWeatherAudioTuning MakeTuning(const FDreamWeatherSounds& InSounds) const;
};
