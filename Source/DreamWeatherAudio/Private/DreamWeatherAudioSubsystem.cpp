// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherAudioSubsystem.h"
#include "DreamAmbiencePreset.h"
#include "DreamWeatherAudioModel.h"
#include "DreamWeatherAudioSettings.h"
#include "DreamWeatherFXSubsystem.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWeatherTypes.h"
#include "DreamWindSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "AudioDevice.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundSource.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherAudioSubsystem)

DECLARE_CYCLE_STAT(TEXT("DreamWeather Audio"), STAT_DreamWeatherAudio, STATGROUP_Game);

const FName UDreamWeatherAudioSubsystem::WaveParam(TEXT("Wave"));
const FName UDreamWeatherAudioSubsystem::LoopParam(TEXT("Loop"));
const FName UDreamWeatherAudioSubsystem::LeftParam(TEXT("Left"));
const FName UDreamWeatherAudioSubsystem::RightParam(TEXT("Right"));
const FName UDreamWeatherAudioSubsystem::MuffleParam(TEXT("Muffle"));
const FName UDreamWeatherAudioSubsystem::MinCutoffParam(TEXT("MinCutoff"));
const FName UDreamWeatherAudioSubsystem::IntensityParam(TEXT("Intensity"));
const FName UDreamWeatherAudioSubsystem::GustParam(TEXT("Gust"));
const FName UDreamWeatherAudioSubsystem::DistanceParam(TEXT("Distance"));

namespace DreamWeatherAudio
{
	/** Below this a voice counts as silent. */
	static constexpr float Silence = 1.0e-3f;
}

UDreamWeatherAudioSubsystem* UDreamWeatherAudioSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UDreamWeatherAudioSubsystem>() : nullptr;
}

bool UDreamWeatherAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Editor);
}

void UDreamWeatherAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UDreamWeatherSubsystem* Weather = Collection.InitializeDependency<UDreamWeatherSubsystem>())
	{
		WeatherSubsystem = Weather;
		FrameHandle = Weather->OnWeatherFrame.AddUObject(this, &UDreamWeatherAudioSubsystem::HandleWeatherFrame);
		StoppedHandle = Weather->OnWeatherStopped.AddUObject(this, &UDreamWeatherAudioSubsystem::HandleWeatherStopped);
		Weather->OnLightningStrike.AddUniqueDynamic(this, &UDreamWeatherAudioSubsystem::HandleLightningStrike);
	}
	Collection.InitializeDependency<UDreamWeatherFXSubsystem>();
}

void UDreamWeatherAudioSubsystem::Deinitialize()
{
	if (UDreamWeatherSubsystem* Weather = WeatherSubsystem.Get())
	{
		Weather->OnWeatherFrame.Remove(FrameHandle);
		Weather->OnWeatherStopped.Remove(StoppedHandle);
		Weather->OnLightningStrike.RemoveDynamic(this, &UDreamWeatherAudioSubsystem::HandleLightningStrike);
	}
	StopAll();
	if (bOutdoorMixPushed && OutdoorMix)
	{
		UGameplayStatics::PopSoundMixModifier(this, OutdoorMix);
		bOutdoorMixPushed = false;
	}
	Super::Deinitialize();
}

void UDreamWeatherAudioSubsystem::ResolveSounds()
{
	const UDreamWeatherAudioSettings* S = UDreamWeatherAudioSettings::Get();
	Sounds = S->ResolveSounds();
	if (SoundSetOverride)
	{
		FDreamWeatherSounds Over = SoundSetOverride->Sounds;
		Over.FillEmptyFrom(Sounds);
		Sounds = MoveTemp(Over);
	}
	Tuning = S->MakeTuning(Sounds);
	VoiceGraph = Cast<USoundBase>(S->VoiceGraph.TryLoad());
	WeatherClass = Cast<USoundClass>(S->WeatherSoundClass.TryLoad());
	OutdoorClass = Cast<USoundClass>(S->OutdoorSoundClass.TryLoad());
	bResolved = true;
}

void UDreamWeatherAudioSubsystem::SetSoundSet(UDreamWeatherSoundSet* SoundSet)
{
	SoundSetOverride = SoundSet;
	// The voices pick their sounds again.
	StopAll();
	ResolveSounds();
}

void UDreamWeatherAudioSubsystem::SetAmbience(UDreamAmbiencePreset* Preset, float FadeSeconds)
{
	bAmbienceChosen = true;
	if (Preset == AmbiencePreset)
	{
		return;
	}
	AmbiencePreset = Preset;
	AmbienceFadeSeconds = FMath::Max(FadeSeconds, 0.0f);
	if (UAudioComponent* Old = Ambience.FadingOut.Get())
	{
		Old->Stop();
	}
	Ambience.FadingOut = Ambience.Component;
	AmbienceFadeOutLevel = AmbienceFade;
	Ambience.Component = nullptr;
	AmbienceFade = 0.0f;
}

void UDreamWeatherAudioSubsystem::SetMuted(bool bInMuted, float FadeSeconds)
{
	bMuted = bInMuted;
	MuteFadeSeconds = FMath::Max(FadeSeconds, 0.0f);
}

FDreamWeatherVoiceMix UDreamWeatherAudioSubsystem::GetVoiceMix(EDreamWeatherVoice Voice) const
{
	return Voice < EDreamWeatherVoice::Num ? Mixes[(int32)Voice] : FDreamWeatherVoiceMix();
}

UAudioComponent* UDreamWeatherAudioSubsystem::GetVoiceComponent(EDreamWeatherVoice Voice) const
{
	return Voice < EDreamWeatherVoice::Num ? Voices[(int32)Voice].Component.Get() : nullptr;
}

bool UDreamWeatherAudioSubsystem::CanPlay() const
{
	const UDreamWeatherAudioSettings* S = UDreamWeatherAudioSettings::Get();
	const UWorld* World = GetWorld();
	if (!S->bEnabled || !World || (!World->IsGameWorld() && !S->bInEditor) || World->IsNetMode(NM_DedicatedServer))
	{
		return false;
	}
	return World->GetAudioDevice().IsValid();
}

float UDreamWeatherAudioSubsystem::Gain() const
{
	return UDreamWeatherAudioSettings::Get()->MasterVolume * MuteLevel;
}

void UDreamWeatherAudioSubsystem::HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWeatherAudio);
	if (!bResolved)
	{
		ResolveSounds();
	}
	const float Target = bMuted ? 0.0f : 1.0f;
	MuteLevel = MuteFadeSeconds > 0.0f ? FMath::FInterpConstantTo(MuteLevel, Target, DeltaTime, 1.0f / MuteFadeSeconds) : Target;
	SinceNearClap += FMath::Max(DeltaTime, 0.0f);

	GatherInputs(Clock, View);
	Outdoor = DreamWeatherAudio::Occlude(LastInputs.Occlusion, LastInputs.ListenerRight, Tuning.InteriorMinVolume, Tuning.MinCutoffHz);
	DreamWeatherAudio::Mix(LastInputs, Tuning, Mixes);

	Components.RemoveAll([](const TObjectPtr<UAudioComponent>& C) { return !IsValid(C); });
	UpdateVoices(DeltaTime);
	UpdateThunder(DeltaTime);
	UpdateOutdoorClass();
	UpdateAmbience(Clock, View);
	if (AmbienceFade < 1.0f)
	{
		AmbienceFade = AmbienceFadeSeconds > 0.0f ? FMath::Min(1.0f, AmbienceFade + DeltaTime / AmbienceFadeSeconds) : 1.0f;
	}
	if (UAudioComponent* Old = Ambience.FadingOut.Get())
	{
		AmbienceFadeOutLevel = AmbienceFadeSeconds > 0.0f ? AmbienceFadeOutLevel - DeltaTime / AmbienceFadeSeconds : 0.0f;
		if (AmbienceFadeOutLevel <= 0.0f)
		{
			Old->Stop();
			Ambience.FadingOut = nullptr;
		}
		else
		{
			Old->SetVolumeMultiplier(AmbienceFadeOutLevel * Gain());
		}
	}
}

void UDreamWeatherAudioSubsystem::HandleWeatherStopped()
{
	StopAll();
}

void UDreamWeatherAudioSubsystem::GatherInputs(UDreamWorldSubsystem& Clock, const FDreamWeatherSample& View)
{
	FDreamWeatherAudioInputs In;
	ListenerLocation = Clock.GetViewLocation();
	if (Clock.HasViewRotation())
	{
		In.ListenerRight = FRotationMatrix(FRotator(0.0, Clock.GetViewRotation().Yaw, 0.0)).GetUnitAxis(EAxis::Y);
	}
	In.Rain = View.Rain;
	In.Snow = View.Snow;
	In.Dust = View.Dust;
	In.Occlusion = Clock.GetViewOcclusion();

	UWorld* World = GetWorld();
	if (const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(World))
	{
		const FDreamWindSampleResult Sample = Wind->SampleWind(ListenerLocation);
		In.WindSpeed = Sample.Speed;
		In.WindGust = Sample.Gust;
	}
	else
	{
		In.WindSpeed = View.Wind * Tuning.WindLoudSpeed;
	}

	if (In.Rain > 0.0f)
	{
		if (const UDreamWeatherFXSubsystem* FX = UDreamWeatherFXSubsystem::Get(World))
		{
			const FDreamWeatherFallPaths& Paths = FX->GetPaths(UDreamWeatherFXSubsystem::RainLayer);
			if (Paths.GetEnds().Num() > 0)
			{
				Paths.GetSurfaceShares(ListenerLocation, UDreamWeatherAudioSettings::Get()->SurfaceRadius, In.Surfaces);
			}
		}
	}
	LastInputs = In;
}

USoundBase* UDreamWeatherAudioSubsystem::PickSound(const FDreamWeatherSoundSlot& Slot, uint32 Variant) const
{
	if (Slot.Sounds.Num() == 0)
	{
		return nullptr;
	}
	const int32 Count = Slot.Sounds.Num();
	for (int32 i = 0; i < Count; ++i)
	{
		if (USoundBase* Sound = Slot.Sounds[(Variant + uint32(i)) % uint32(Count)].LoadSynchronous())
		{
			return Sound;
		}
	}
	return nullptr;
}

UAudioComponent* UDreamWeatherAudioSubsystem::MakeComponent(USoundBase* Sound, const FDreamWeatherSoundSlot* Slot, uint32 Variant, bool bLoop)
{
	USoundBase* Picked = Sound ? Sound : (Slot ? PickSound(*Slot, Variant) : nullptr);
	if (!Picked || !CanPlay())
	{
		return nullptr;
	}
	USoundWave* Wave = Cast<USoundWave>(Picked);
	USoundBase* ToPlay = (Wave && VoiceGraph) ? VoiceGraph.Get() : Picked;
	UAudioComponent* Component = UGameplayStatics::CreateSound2D(this, ToPlay, 1.0f, 1.0f, 0.0f, nullptr, false, !bLoop);
	if (!Component)
	{
		return nullptr;
	}
	if (WeatherClass)
	{
		Component->SoundClassOverride = WeatherClass;
	}
	if (ToPlay != Picked)
	{
		Component->SetObjectParameter(WaveParam, Wave);
		Component->SetBoolParameter(LoopParam, bLoop);
	}
	Component->SetFloatParameter(MinCutoffParam, Tuning.MinCutoffHz);
	Components.Add(Component);
	return Component;
}

float UDreamWeatherAudioSubsystem::GetThunderDuck() const
{
	const UDreamWeatherAudioSettings* S = UDreamWeatherAudioSettings::Get();
	return S->bThunderDucking ? DreamWeatherAudio::ThunderDuck(SinceNearClap, S->ThunderDuckDb, S->ThunderDuckSeconds) : 1.0f;
}

void UDreamWeatherAudioSubsystem::Apply(UAudioComponent* Component, const FDreamWeatherVoiceMix& Mix, float Gust, float Distance, float ExtraGain) const
{
	if (!Component)
	{
		return;
	}
	const float G = Gain() * ExtraGain;
	if (Cast<UMetaSoundSource>(Component->GetSound()))
	{
		Component->SetFloatParameter(LeftParam, Mix.Left * G);
		Component->SetFloatParameter(RightParam, Mix.Right * G);
		Component->SetFloatParameter(MuffleParam, Mix.Muffle);
		Component->SetFloatParameter(IntensityParam, Mix.Intensity);
		Component->SetFloatParameter(GustParam, Gust);
		Component->SetFloatParameter(DistanceParam, Distance);
	}
	else
	{
		// A plain sound: no placement, only the loudness and the low-pass.
		Component->SetVolumeMultiplier(Mix.Loudest() * G);
		Component->SetLowPassFilterEnabled(Mix.Muffle > 0.01f);
		Component->SetLowPassFilterFrequency(DreamWeatherAudio::MuffleToCutoff(Mix.Muffle, Tuning.MinCutoffHz));
	}
}

void UDreamWeatherAudioSubsystem::UpdateVoices(float DeltaTime)
{
	const float FadeOut = UDreamWeatherAudioSettings::Get()->FadeOutSeconds;
	// A near clap ducks the wind and the rain for a moment.
	const float Duck = GetThunderDuck();
	const float Gust = FMath::Clamp((LastInputs.WindGust - 1.0f) / 0.8f, 0.0f, 1.0f);
	const uint32 Seed = uint32(UDreamWorldSubsystem::Get(this) ? UDreamWorldSubsystem::Get(this)->GetSeed() : 0);
	for (const EDreamWeatherVoice VoiceId : TEnumRange<EDreamWeatherVoice>())
	{
		const int32 v = (int32)VoiceId;
		FVoice& Voice = Voices[v];
		const FDreamWeatherVoiceMix& Mix = Mixes[v];
		UAudioComponent* Component = Voice.Component.Get();
		const bool bAudible = Mix.Loudest() * Gain() > DreamWeatherAudio::Silence;
		if (bAudible)
		{
			Voice.Silent = 0.0f;
			if (!Component)
			{
				Component = MakeComponent(nullptr, &Sounds.Get(VoiceId), HashCombine(Seed, uint32(v)), true);
				Voice.Component = Component;
			}
			if (Component)
			{
				Apply(Component, Mix, Gust, 0.0f, Duck);
				if (!Component->IsPlaying())
				{
					// A loop starts at a point of its own, so two voices of one recording do not phase.
					Component->Play(0.0f);
				}
			}
		}
		else if (Component)
		{
			Apply(Component, Mix, Gust, 0.0f, Duck);
			Voice.Silent += DeltaTime;
			if (Voice.Silent > FadeOut && Component->IsPlaying())
			{
				Component->Stop();
			}
		}
	}
}

void UDreamWeatherAudioSubsystem::HandleLightningStrike(const FDreamLightningStrike& Strike)
{
	if (!bResolved)
	{
		ResolveSounds();
	}
	FDreamThunder Thunder;
	if (!DreamWeatherAudio::Thunder(Strike, ListenerLocation, LastInputs.ListenerRight, Outdoor, Tuning, Thunder))
	{
		return;
	}
	++StrikesHeard;
	const int32 Max = FMath::Max(UDreamWeatherAudioSettings::Get()->MaxThunders, 1);
	if (Pending.Num() >= Max)
	{
		int32 Quietest = 0;
		for (int32 i = 1; i < Pending.Num(); ++i)
		{
			if (Pending[i].Mix.Intensity < Pending[Quietest].Mix.Intensity)
			{
				Quietest = i;
			}
		}
		if (Pending[Quietest].Mix.Intensity >= Thunder.Mix.Intensity)
		{
			return;
		}
		Pending.RemoveAt(Quietest);
	}
	Pending.Add(Thunder);
}

void UDreamWeatherAudioSubsystem::UpdateThunder(float DeltaTime)
{
	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		FDreamThunder& Thunder = Pending[i];
		Thunder.Delay -= DeltaTime;
		if (Thunder.Delay > 0.0f)
		{
			continue;
		}
		const FDreamWeatherSoundSlot& Slot = (Thunder.bNear || Sounds.ThunderFar.IsEmpty()) ? Sounds.ThunderNear : Sounds.ThunderFar;
		if (UAudioComponent* Component = MakeComponent(nullptr, &Slot, Thunder.Variant, false))
		{
			Apply(Component, Thunder.Mix, 0.0f, Thunder.Distance);
			Component->Play(0.0f);
		}
		++ThunderHeard;
		if (Thunder.bNear)
		{
			SinceNearClap = 0.0f;
		}
		Pending.RemoveAt(i);
	}
}

void UDreamWeatherAudioSubsystem::UpdateOutdoorClass()
{
	if (!OutdoorClass || !CanPlay())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!OutdoorMix)
	{
		OutdoorMix = NewObject<USoundMix>(this, TEXT("DreamOutdoorMix"), RF_Transient);
		FSoundClassAdjuster Adjuster;
		Adjuster.SoundClassObject = OutdoorClass;
		Adjuster.bApplyToChildren = true;
		OutdoorMix->SoundClassEffects.Add(Adjuster);
		OutdoorMix->FadeInTime = 0.0f;
		OutdoorMix->FadeOutTime = 0.0f;
		OutdoorMix->Duration = -1.0f;
	}
	if (!bOutdoorMixPushed)
	{
		UGameplayStatics::PushSoundMixModifier(World, OutdoorMix);
		bOutdoorMixPushed = true;
		PushedOutdoor.Volume = -1.0f;
	}
	const float Volume = Outdoor.Volume * MuteLevel;
	if (FMath::Abs(Volume - PushedOutdoor.Volume) < 0.005f && FMath::Abs(Outdoor.CutoffHz - PushedOutdoor.CutoffHz) < 10.0f)
	{
		return;
	}
	PushedOutdoor = Outdoor;
	PushedOutdoor.Volume = Volume;
	// The device reads a mix's adjusters on the audio thread every update: change them there.
	USoundMix* Mix = OutdoorMix;
	const float Cutoff = Outdoor.CutoffHz;
	FAudioThread::RunCommandOnAudioThread([Mix, Volume, Cutoff]()
	{
		if (Mix->SoundClassEffects.Num() > 0)
		{
			Mix->SoundClassEffects[0].VolumeAdjuster = Volume;
			Mix->SoundClassEffects[0].LowPassFilterFrequency = Cutoff;
		}
	});
}

void UDreamWeatherAudioSubsystem::UpdateAmbience(UDreamWorldSubsystem& Clock, const FDreamWeatherSample& View)
{
	// The numbers, whether or not anything plays.
	FDreamAmbienceParams& P = AmbienceParams;
	P.TimeOfDay = Clock.GetTimeOfDay() / 24.0f;
	P.Phase = Clock.GetDayPhase();
	FDreamAmbienceParams::SeasonWeights(Clock.GetDateTime(), Clock.GetCelestialSettings().Latitude < 0.0, P.Seasons, P.Year);
	P.Cloud = View.CloudCoverage;
	P.Rain = View.Rain;
	P.Snow = View.Snow;
	P.Fog = View.Fog;
	P.Dust = View.Dust;
	P.Lightning = View.Lightning;
	P.Wind = DreamWeatherAudio::WindAmount(LastInputs.WindSpeed, Tuning.WindLoudSpeed);
	P.Gust = FMath::Clamp((LastInputs.WindGust - 1.0f) / 0.8f, 0.0f, 1.0f);
	P.Interior = LastInputs.Occlusion.bValid ? LastInputs.Occlusion.Interior : 0.0f;

	if (!bAmbienceChosen && !AmbiencePreset)
	{
		AmbiencePreset = UDreamWeatherAudioSettings::Get()->DefaultAmbience.LoadSynchronous();
		bAmbienceChosen = true;
	}
	const UDreamAmbiencePreset* Preset = AmbiencePreset;
	P.Occlusion = FDreamWeatherVoiceMix();
	P.Occlusion.Intensity = 1.0f;
	if (Preset && Preset->bOutdoor)
	{
		DreamWeatherAudio::Place(Outdoor.Volume, Outdoor.Balance, P.Occlusion.Left, P.Occlusion.Right);
		P.Occlusion.Muffle = Outdoor.Muffle;
	}
	else
	{
		P.Occlusion.Left = P.Occlusion.Right = 1.0f;
	}

	UAudioComponent* Component = Ambience.Component.Get();
	if (!Preset)
	{
		return;
	}
	if (!Component)
	{
		USoundBase* Sound = Preset->Sound.LoadSynchronous();
		if (!Sound || !CanPlay())
		{
			return;
		}
		Component = UGameplayStatics::CreateSound2D(this, Sound, 0.0f, 1.0f, 0.0f, nullptr, false, false);
		if (!Component)
		{
			return;
		}
		if (WeatherClass)
		{
			Component->SoundClassOverride = WeatherClass;
		}
		for (const FDreamAmbienceInput& Input : Preset->Inputs)
		{
			const FDreamWeatherSoundSlot* Slot = Input.Sounds.IsEmpty() ? Sounds.Ambience.Find(Input.Input) : &Input.Sounds;
			if (!Slot || Slot->IsEmpty())
			{
				continue;
			}
			if (Input.bArray)
			{
				TArray<UObject*> Waves;
				for (const TSoftObjectPtr<USoundBase>& Soft : Slot->Sounds)
				{
					if (USoundWave* Wave = Cast<USoundWave>(Soft.LoadSynchronous()))
					{
						Waves.Add(Wave);
					}
				}
				Component->SetObjectArrayParameter(Input.Input, Waves);
			}
			else if (USoundWave* Wave = Cast<USoundWave>(PickSound(*Slot, uint32(Clock.GetSeed()))))
			{
				Component->SetObjectParameter(Input.Input, Wave);
			}
		}
		for (const TPair<FName, float>& Constant : Preset->Constants)
		{
			Component->SetFloatParameter(Constant.Key, Constant.Value);
		}
		Components.Add(Component);
		Ambience.Component = Component;
	}
	P.ForEach([Component](FName Name, float Value) { Component->SetFloatParameter(Name, Value); });
	Component->SetVolumeMultiplier(Preset->Volume * UDreamWeatherAudioSettings::Get()->AmbienceVolume * AmbienceFade * Gain());
	if (!Component->IsPlaying())
	{
		Component->Play(0.0f);
	}
}

void UDreamWeatherAudioSubsystem::StopAll()
{
	for (FVoice& Voice : Voices)
	{
		if (UAudioComponent* Component = Voice.Component.Get())
		{
			Component->Stop();
			Component->DestroyComponent();
		}
		Voice = FVoice();
	}
	for (TWeakObjectPtr<UAudioComponent>* Weak : { &Ambience.Component, &Ambience.FadingOut })
	{
		if (UAudioComponent* Component = Weak->Get())
		{
			Component->Stop();
			Component->DestroyComponent();
		}
		*Weak = nullptr;
	}
	for (const TObjectPtr<UAudioComponent>& Component : Components)
	{
		if (IsValid(Component))
		{
			Component->Stop();
		}
	}
	Components.Reset();
	Pending.Reset();
	AmbienceFade = 1.0f;
}
