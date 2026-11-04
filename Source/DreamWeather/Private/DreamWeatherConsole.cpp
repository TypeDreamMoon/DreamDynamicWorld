// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherChannels.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSubsystem.h"

#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace DreamWeather::Console
{
	static UDreamWeatherSubsystem* Find(UWorld* World)
	{
		UDreamWeatherSubsystem* Weather = World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
		if (!Weather)
		{
			UE_LOG(LogDreamWeather, Warning, TEXT("No Dream Weather in this world."));
		}
		return Weather;
	}

	/** A preset by object path, or by name among the plugin's presets. */
	static UDreamWeatherPreset* FindPreset(const FString& Name)
	{
		const FString Path = Name.StartsWith(TEXT("/")) ? Name : FString::Printf(TEXT("/DreamDynamicWorld/Weather/Presets/%s.%s"), *Name, *Name);
		return LoadObject<UDreamWeatherPreset>(nullptr, *Path);
	}

	static FAutoConsoleCommandWithWorldAndArgs SetCommand(
		TEXT("DreamWeather.Set"),
		TEXT("DreamWeather.Set <preset> [seconds]: force a weather (WP_Rain, or an object path) until DreamWeather.Clear."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamWeatherSubsystem* Weather = Find(World);
			UDreamWeatherPreset* Preset = Args.Num() > 0 ? FindPreset(Args[0]) : nullptr;
			if (Weather && Preset)
			{
				Weather->ForceWeather(Preset, Args.Num() > 1 ? FCString::Atof(*Args[1]) : 5.0f);
			}
			else if (Args.Num() > 0 && !Preset)
			{
				UE_LOG(LogDreamWeather, Warning, TEXT("No weather preset '%s'."), *Args[0]);
			}
		}));

	/** One scripted strike this far in front of the view (cm), or above the origin without a view. */
	static void StrikeAhead(UWorld* World, double Distance)
	{
		UDreamWeatherSubsystem* Weather = World ? World->GetSubsystem<UDreamWeatherSubsystem>() : nullptr;
		const UDreamWorldSubsystem* Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		if (!Weather)
		{
			return;
		}
		FVector View = FVector::ZeroVector;
		FVector Ahead = FVector::ForwardVector;
		if (Clock && Clock->HasViewLocation())
		{
			View = Clock->GetViewLocation();
			Ahead = Clock->HasViewRotation() ? FRotator(0.0, Clock->GetViewRotation().Yaw, 0.0).Vector() : Ahead;
		}
		Weather->StrikeLightning(FVector(View.X, View.Y, 0.0) + Ahead * Distance, 1.0f);
	}

	static FAutoConsoleCommandWithWorldAndArgs StrikeCommand(
		TEXT("DreamWeather.Strike"),
		TEXT("DreamWeather.Strike [metres ahead=2000] [count=1] [interval seconds=0.8]: scripted lightning in front of the view, whatever the weather."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!Find(World))
			{
				return;
			}
			const double Distance = (Args.Num() > 0 ? FCString::Atod(*Args[0]) : 2000.0) * 100.0;
			const int32 Count = FMath::Max(1, Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1);
			const float Interval = FMath::Max(0.05f, Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.8f);
			StrikeAhead(World, Distance);
			if (Count > 1)
			{
				// The rest on a timer (game worlds), one every Interval real seconds.
				TSharedRef<int32> Left = MakeShared<int32>(Count - 1);
				TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
				TWeakObjectPtr<UWorld> WeakWorld(World);
				World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, Distance, Left, Handle]
				{
					UWorld* W = WeakWorld.Get();
					if (!W)
					{
						return;
					}
					StrikeAhead(W, Distance);
					if (--(*Left) <= 0)
					{
						W->GetTimerManager().ClearTimer(*Handle);
					}
				}), Interval, true);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs ClearCommand(
		TEXT("DreamWeather.Clear"),
		TEXT("DreamWeather.Clear [seconds]: drop the forced weather."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWeatherSubsystem* Weather = Find(World))
			{
				Weather->ClearForcedWeather(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.0f);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs ForecastCommand(
		TEXT("DreamWeather.Forecast"),
		TEXT("DreamWeather.Forecast [hours] [step]: print the scheduled weather ahead."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWeatherSubsystem* Weather = Find(World))
			{
				const float Hours = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 24.0f;
				const float Step = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f;
				for (const FDreamWeatherForecastEntry& E : Weather->GetForecast(Hours, Step))
				{
					UE_LOG(LogDreamWeather, Display, TEXT("  %s  %s"), *E.Time.ToString(TEXT("%Y-%m-%d %H:%M")), E.Preset ? *E.Preset->GetDisplayNameOrName().ToString() : TEXT("-"));
				}
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs DumpCommand(
		TEXT("DreamWeather.Dump"),
		TEXT("DreamWeather.Dump: print the weather at the view, every channel."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWeatherSubsystem* Weather = Find(World))
			{
				const FDreamWeatherSample& S = Weather->GetViewSample();
				const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
				UE_LOG(LogDreamWeather, Display, TEXT("Weather %s (%s), %.1f C, %d modifiers, tags %s"),
					Weather->IsActive() ? TEXT("on") : TEXT("off"),
					Weather->GetCurrentWeather() ? *Weather->GetCurrentWeather()->GetName() : TEXT("-"), S.Temperature, Weather->GetNumModifiers(), *S.Tags.ToStringSimple());
				for (int32 i = 0; i < Defs.Num(); ++i)
				{
					UE_LOG(LogDreamWeather, Display, TEXT("  %-40s %.3f"), *Defs[i].Channel.ToString(), S.Values.Get(i));
				}
			}
		}));
}
