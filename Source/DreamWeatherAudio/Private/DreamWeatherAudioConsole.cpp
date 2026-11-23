// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherAudioSubsystem.h"

#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamWeatherAudio, Log, All);

namespace DreamWeatherAudio::Console
{
	static const TCHAR* VoiceName(EDreamWeatherVoice Voice)
	{
		switch (Voice)
		{
		case EDreamWeatherVoice::Wind: return TEXT("Wind");
		case EDreamWeatherVoice::WindGust: return TEXT("WindGust");
		case EDreamWeatherVoice::RainLight: return TEXT("RainLight");
		case EDreamWeatherVoice::RainHeavy: return TEXT("RainHeavy");
		case EDreamWeatherVoice::RainOnRoof: return TEXT("RainOnRoof");
		case EDreamWeatherVoice::RainOnLeaves: return TEXT("RainOnLeaves");
		case EDreamWeatherVoice::Snow: return TEXT("Snow");
		case EDreamWeatherVoice::Hail: return TEXT("Hail");
		default: return TEXT("Dust");
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs Dump(
		TEXT("DreamWeather.Audio.Dump"),
		TEXT("DreamWeather.Audio.Dump: log every weather voice (its loudness left / right, muffle, whether it plays and what), the outdoor occlusion and thunder on its way."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const UDreamWeatherAudioSubsystem* Audio = UDreamWeatherAudioSubsystem::Get(World);
			if (!Audio)
			{
				return;
			}
			const FDreamOutdoorOcclusion& O = Audio->GetOutdoorOcclusion();
			UE_LOG(LogDreamWeatherAudio, Display, TEXT("Outdoor: volume %.2f, muffle %.2f (%.0f Hz), balance %+.2f; thunder on its way %d, heard %d"),
				O.Volume, O.Muffle, O.CutoffHz, O.Balance, Audio->GetPendingThunder().Num(), Audio->GetThunderHeardCount());
			for (const EDreamWeatherVoice Voice : TEnumRange<EDreamWeatherVoice>())
			{
				const FDreamWeatherVoiceMix Mix = Audio->GetVoiceMix(Voice);
				UAudioComponent* Component = Audio->GetVoiceComponent(Voice);
				UE_LOG(LogDreamWeatherAudio, Display, TEXT("  %-12s intensity %.2f  L %.2f R %.2f  muffle %.2f  %s %s"), VoiceName(Voice), Mix.Intensity, Mix.Left,
					Mix.Right, Mix.Muffle, Component && Component->IsPlaying() ? TEXT("playing") : TEXT("silent"),
					Component && Component->GetSound() ? *Component->GetSound()->GetName() : TEXT(""));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs Record(
		TEXT("DreamWeather.Audio.Record"),
		TEXT("DreamWeather.Audio.Record <seconds> <name>: record the game's audio output for that long into <project>/Saved/DreamWeatherAudio/<name>.wav."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 2)
			{
				return;
			}
			const float Seconds = FMath::Max(FCString::Atof(*Args[0]), 0.1f);
			const FString Name = Args[1];
			UAudioMixerBlueprintLibrary::StartRecordingOutput(World, Seconds);
			FTimerHandle Handle;
			TWeakObjectPtr<UWorld> WeakWorld = World;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Name]()
			{
				if (UWorld* W = WeakWorld.Get())
				{
					const FString Path = FPaths::ProjectSavedDir() / TEXT("DreamWeatherAudio");
					UAudioMixerBlueprintLibrary::StopRecordingOutput(W, EAudioRecordingExportType::WavFile, Name, Path);
					UE_LOG(LogDreamWeatherAudio, Display, TEXT("Recorded %s/%s.wav"), *Path, *Name);
				}
			}), Seconds, false);
		}));
}
