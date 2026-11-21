// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldTypes.h"
#include "DreamWeatherAudioTypes.generated.h"

class USoundBase;

/** The looping sounds of the weather (thunder is a one-shot of its own). */
UENUM(BlueprintType)
enum class EDreamWeatherVoice : uint8
{
	/** The wind itself, by the wind's speed at the listener. */
	Wind,
	/** Whistling gusts, by how gusty a strong wind is. */
	WindGust,
	/** A light rain, strongest in a drizzle and giving way to the heavy rain. */
	RainLight,
	/** A downpour. */
	RainHeavy,
	/** Rain drumming on a roof: on the roof over the listener, or the roofs around. */
	RainOnRoof,
	/** Rain on leaves: under trees, or with foliage around. */
	RainOnLeaves,
	/** The hush of falling snow. */
	Snow,
	/** Blowing dust and sand. */
	Dust,
	Num UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EDreamWeatherVoice, EDreamWeatherVoice::Num);

/**
 * One swappable sound. A sound wave is played through the plugin's voice graph (MS_DreamWeatherVoice), which
 * loops it, low-passes and places it; a MetaSound source of your own is played as it is and receives the same
 * parameters (see the README); any other sound (a cue) is played with the component's volume and low-pass. With
 * more than one sound, a loop keeps the one the world seed picks and a one-shot draws a new one each time.
 */
USTRUCT(BlueprintType)
struct DREAMWEATHERAUDIO_API FDreamWeatherSoundSlot
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	/** Loudness of this slot at full strength, 0..2. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0", ClampMax = "2"))
	float Volume = 1.0f;

	bool IsEmpty() const { return Sounds.Num() == 0; }
};

/** Every sound of the weather and of the ambiences, in one place: what a project swaps to give the world its voice. */
USTRUCT(BlueprintType)
struct DREAMWEATHERAUDIO_API FDreamWeatherSounds
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FDreamWeatherSoundSlot Wind;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Wind")
	FDreamWeatherSoundSlot WindGust;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rain")
	FDreamWeatherSoundSlot RainLight;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rain")
	FDreamWeatherSoundSlot RainHeavy;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rain")
	FDreamWeatherSoundSlot RainOnRoof;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rain")
	FDreamWeatherSoundSlot RainOnLeaves;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Snow and dust")
	FDreamWeatherSoundSlot Snow;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Snow and dust")
	FDreamWeatherSoundSlot Dust;

	/** Thunder from a strike closer than the settings' near distance (one-shots, one drawn per strike). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Thunder")
	FDreamWeatherSoundSlot ThunderNear;

	/** Rolling thunder from further away (one-shots); empty = the near thunder, low-passed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Thunder")
	FDreamWeatherSoundSlot ThunderFar;

	/**
	 * Sounds for the ambiences' named inputs (Birds, Insects, TreeWindLight...), used where an ambience preset
	 * leaves its own input empty.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Ambience")
	TMap<FName, FDreamWeatherSoundSlot> Ambience;

	const FDreamWeatherSoundSlot& Get(EDreamWeatherVoice Voice) const;
	FDreamWeatherSoundSlot& Get(EDreamWeatherVoice Voice);

	/** Fill every empty slot of this from Other. */
	void FillEmptyFrom(const FDreamWeatherSounds& Other);
};

/** How the walls and roof around the listener change what is heard from outside (from the view probe). */
USTRUCT(BlueprintType)
struct DREAMWEATHERAUDIO_API FDreamOutdoorOcclusion
{
	GENERATED_BODY()

	/** Loudness of outdoor sound, InteriorMinVolume (a closed room) .. 1 (the open). */
	UPROPERTY(BlueprintReadOnly, Category = "Occlusion")
	float Volume = 1.0f;

	/** How muffled, 0 (the open) .. 1 (a closed room): the low-pass from 20 kHz down to the settings' cutoff. */
	UPROPERTY(BlueprintReadOnly, Category = "Occlusion")
	float Muffle = 0.0f;

	/** Where the opening is, -1 (to the listener's left) .. 1 (right); 0 in the open or a closed room. */
	UPROPERTY(BlueprintReadOnly, Category = "Occlusion")
	float Balance = 0.0f;

	/** The low-pass cutoff that Muffle means, Hz. */
	UPROPERTY(BlueprintReadOnly, Category = "Occlusion")
	float CutoffHz = 20000.0f;
};

/** What one voice plays at now: the parameters its graph receives, all 0..1. */
USTRUCT(BlueprintType)
struct DREAMWEATHERAUDIO_API FDreamWeatherVoiceMix
{
	GENERATED_BODY()

	/** How strongly the weather calls for this sound, before walls and roofs (the slot's volume included). */
	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	float Intensity = 0.0f;

	/** Gain of the left and right channel (Intensity after occlusion and placement). */
	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	float Left = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	float Right = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Voice")
	float Muffle = 0.0f;

	float Loudest() const { return FMath::Max(Left, Right); }
};

/** One thunder clap on its way to the listener. */
USTRUCT(BlueprintType)
struct DREAMWEATHERAUDIO_API FDreamThunder
{
	GENERATED_BODY()

	/** Real seconds until it is heard. */
	UPROPERTY(BlueprintReadOnly, Category = "Thunder")
	float Delay = 0.0f;

	/** A near clap (the ThunderNear slot) rather than a far rumble. */
	UPROPERTY(BlueprintReadOnly, Category = "Thunder")
	bool bNear = true;

	/** 0 at the strike .. 1 at the edge of hearing. */
	UPROPERTY(BlueprintReadOnly, Category = "Thunder")
	float Distance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Thunder")
	FDreamWeatherVoiceMix Mix;

	/** Which sound of the slot it plays (drawn from the strike). */
	uint32 Variant = 0;
};

/** What the weather's sound is made from at the listener, gathered every frame. */
struct DREAMWEATHERAUDIO_API FDreamWeatherAudioInputs
{
	/** Rain, snow and dust falling in the open at the listener, 0..1. */
	float Rain = 0.0f;
	float Snow = 0.0f;
	float Dust = 0.0f;
	/** Wind speed at the listener (cm/s) and the gusts' speed multiplier there (1 = no gust). */
	float WindSpeed = 0.0f;
	float WindGust = 1.0f;
	/** The view probe. */
	FDreamViewOcclusion Occlusion;
	/** How the rain lands near the listener: air, ground, roof, leaves (FDreamWeatherFallPaths::ESurface), sums to 1. */
	float Surfaces[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	/** The listener's right, horizontal. */
	FVector ListenerRight = FVector(0.0, 1.0, 0.0);
};

/** The numbers that turn the weather into loudness (from the settings; kept apart so the model can be tested). */
struct DREAMWEATHERAUDIO_API FDreamWeatherAudioTuning
{
	float WindLoudSpeed = 1800.0f;
	float InteriorMinVolume = 0.2f;
	float MinCutoffHz = 600.0f;
	float ThunderNearDistance = 300000.0f;
	float ThunderMaxDistance = 1500000.0f;
	float ThunderInteriorMuffle = 0.6f;
	/** The slots' volumes, by voice. */
	float SlotVolume[(int32)EDreamWeatherVoice::Num] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
	float ThunderNearVolume = 1.0f;
	float ThunderFarVolume = 1.0f;
};
