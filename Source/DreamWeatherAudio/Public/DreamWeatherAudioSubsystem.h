// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWeatherAudioTypes.h"
#include "DreamAmbiencePreset.h"
#include "DreamWeatherAudioSubsystem.generated.h"

class UAudioComponent;
class UDreamAmbiencePreset;
class UDreamWeatherSoundSet;
class UDreamWeatherSubsystem;
class UDreamWorldSubsystem;
class USoundBase;
class USoundClass;
class USoundMix;
struct FDreamLightningStrike;
struct FDreamWeatherSample;

/**
 * The weather's sound at the listener (the view the world is looked at from). Every frame the weather runs
 * (UDreamWeatherSubsystem::OnWeatherFrame) it gathers the rain, snow and dust there, the wind's speed and gusts
 * (DreamWind), what the rain lands on nearby (the rain's fall paths: ground, roofs, leaves) and the walls and roof
 * around the view (the view probe), turns them into the voices' loudness, muffle and placement
 * (DreamWeatherAudio::Mix) and hands those to the voices, one looping sound each. A lightning strike is heard
 * after its thunder delay, near or far, through the same walls. Sounds of the project given the outdoor sound
 * class are muffled the same way, and an ambience preset plays the place's own sounds.
 *
 * Without an audio device (a server, -nosound) the numbers are still worked out, so gameplay and tests can read
 * them; nothing plays.
 */
UCLASS()
class DREAMWEATHERAUDIO_API UDreamWeatherAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Audio", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Weather Audio"))
	static UDreamWeatherAudioSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Use these sounds from now on (null = back to the settings'). Its filled slots win. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Audio")
	void SetSoundSet(UDreamWeatherSoundSet* SoundSet);

	/** Play this ambience, crossfading from the current one over FadeSeconds (null = silence). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Audio")
	void SetAmbience(UDreamAmbiencePreset* Preset, float FadeSeconds = 3.0f);

	UFUNCTION(BlueprintPure, Category = "Dream Weather|Audio")
	UDreamAmbiencePreset* GetAmbience() const { return AmbiencePreset; }

	/** Mute (or bring back) the weather's sound, fading over FadeSeconds; the numbers keep being worked out. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Audio")
	void SetMuted(bool bInMuted, float FadeSeconds = 1.0f);

	/** A voice's loudness, muffle and placement now. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Audio")
	FDreamWeatherVoiceMix GetVoiceMix(EDreamWeatherVoice Voice) const;

	/** What outdoor sound goes through at the listener now (also what the outdoor sound class gets). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Audio")
	const FDreamOutdoorOcclusion& GetOutdoorOcclusion() const { return Outdoor; }

	/** Thunder on its way to the listener. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Audio")
	const TArray<FDreamThunder>& GetPendingThunder() const { return Pending; }

	/** Thunder heard so far. */
	int32 GetThunderHeardCount() const { return ThunderHeard; }

	/** How far a near clap ducks the wind and the rain now, 0..1 (1 = not at all). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather|Audio")
	float GetThunderDuck() const;

	/** The ambience's parameters now. */
	const FDreamAmbienceParams& GetAmbienceParams() const { return AmbienceParams; }

	/** The voice's component, when it plays. */
	UAudioComponent* GetVoiceComponent(EDreamWeatherVoice Voice) const;

	/** The sounds in use. */
	const FDreamWeatherSounds& GetSounds() const { return Sounds; }

	/** Gathered inputs of the last frame (tests). */
	const FDreamWeatherAudioInputs& GetLastInputs() const { return LastInputs; }

	/** Names of the voice graph's inputs. */
	static const FName WaveParam;
	static const FName LoopParam;
	static const FName LeftParam;
	static const FName RightParam;
	static const FName MuffleParam;
	static const FName MinCutoffParam;
	static const FName IntensityParam;
	static const FName GustParam;
	static const FName DistanceParam;

private:
	struct FVoice
	{
		TWeakObjectPtr<UAudioComponent> Component;
		TWeakObjectPtr<USoundBase> Playing;
		float Silent = 0.0f;
	};

	struct FAmbienceState
	{
		TWeakObjectPtr<UAudioComponent> Component;
		TWeakObjectPtr<UAudioComponent> FadingOut;
	};

	void HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime);
	void HandleWeatherStopped();

	UFUNCTION()
	void HandleLightningStrike(const FDreamLightningStrike& Strike);

	void GatherInputs(UDreamWorldSubsystem& Clock, const FDreamWeatherSample& View);
	void UpdateVoices(float DeltaTime);
	void UpdateThunder(float DeltaTime);
	void UpdateOutdoorClass();
	void UpdateAmbience(UDreamWorldSubsystem& Clock, const FDreamWeatherSample& View);
	void StopAll();

	/** A component playing Sound in the weather's sound class (null without an audio device). */
	UAudioComponent* MakeComponent(USoundBase* Sound, const FDreamWeatherSoundSlot* Slot, uint32 Variant, bool bLoop);
	/** Send a voice's numbers to its component (graph parameters, or volume and low-pass for a plain sound). */
	void Apply(UAudioComponent* Component, const FDreamWeatherVoiceMix& Mix, float Gust, float Distance, float ExtraGain = 1.0f) const;
	USoundBase* PickSound(const FDreamWeatherSoundSlot& Slot, uint32 Variant) const;
	bool CanPlay() const;
	float Gain() const;
	void ResolveSounds();

	FVoice Voices[(int32)EDreamWeatherVoice::Num];
	FDreamWeatherVoiceMix Mixes[(int32)EDreamWeatherVoice::Num];
	FDreamOutdoorOcclusion Outdoor;
	FDreamWeatherAudioInputs LastInputs;
	FDreamWeatherAudioTuning Tuning;
	FDreamWeatherSounds Sounds;
	FVector ListenerLocation = FVector::ZeroVector;

	TArray<FDreamThunder> Pending;
	int32 ThunderHeard = 0;
	/** Seconds since the last near clap was heard (large: none). */
	float SinceNearClap = 1.0e6f;
	int32 StrikesHeard = 0;

	FAmbienceState Ambience;
	FDreamAmbienceParams AmbienceParams;
	float AmbienceFade = 1.0f;
	float AmbienceFadeSeconds = 3.0f;
	float AmbienceFadeOutLevel = 0.0f;

	bool bMuted = false;
	float MuteLevel = 1.0f;
	float MuteFadeSeconds = 1.0f;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWeatherSoundSet> SoundSetOverride;

	UPROPERTY(Transient)
	TObjectPtr<UDreamAmbiencePreset> AmbiencePreset;

	/** Set by SetAmbience (the settings' default otherwise). */
	bool bAmbienceChosen = false;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> VoiceGraph;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> WeatherClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> OutdoorClass;

	/** Sets the outdoor class's volume and low-pass. */
	UPROPERTY(Transient)
	TObjectPtr<USoundMix> OutdoorMix;
	bool bOutdoorMixPushed = false;
	FDreamOutdoorOcclusion PushedOutdoor;

	/** Keeps the components alive. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> Components;

	TWeakObjectPtr<UDreamWeatherSubsystem> WeatherSubsystem;
	FDelegateHandle FrameHandle;
	FDelegateHandle StoppedHandle;
	bool bResolved = false;
};
