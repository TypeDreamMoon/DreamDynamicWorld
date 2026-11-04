// Copyright DreamMoon. All Rights Reserved.

// Console commands for the world clock, and DreamWorld.ShotTour, which photographs the world at a list of
// hours (used to check a day cycle from a script: -ExecCmds="DreamWorld.ShotTour Hours=0/6/12/18 Quit").

#include "DreamDynamicWorldModule.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Tickable.h"
#include "UnrealClient.h"

namespace DreamWorld::Console
{
	static UDreamWorldSubsystem* Find(UWorld* World)
	{
		UDreamWorldSubsystem* Subsystem = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
		if (!Subsystem)
		{
			UE_LOG(LogDreamWorld, Warning, TEXT("No Dream World clock in this world."));
		}
		return Subsystem;
	}

	static FAutoConsoleCommandWithWorldAndArgs TimeCommand(
		TEXT("DreamWorld.Time"),
		TEXT("DreamWorld.Time <hours>: jump to an hour of the current day (e.g. 18.5). Without an argument, print the date and time."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWorldSubsystem* Clock = Find(World))
			{
				if (Args.Num() > 0)
				{
					Clock->SetTimeOfDay(FCString::Atof(*Args[0]));
				}
				const FDreamCelestialState& C = Clock->GetCelestial();
				UE_LOG(LogDreamWorld, Display, TEXT("%s  sun %.1f deg  moon %.1f deg (phase %.2f)  x%.0f %s"), *Clock->GetDateTime().ToString(),
					C.SunElevation, C.MoonElevation, C.MoonPhase, Clock->GetTimeScale(), Clock->IsClockRunning() ? TEXT("running") : TEXT("stopped"));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs DateCommand(
		TEXT("DreamWorld.Date"),
		TEXT("DreamWorld.Date <yyyy-mm-dd>: move to a date, keeping the time of day."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamWorldSubsystem* Clock = Find(World);
			FDateTime Date;
			if (Clock && Args.Num() > 0 && FDateTime::Parse(Args[0] + TEXT(" 00:00:00"), Date))
			{
				const FTimespan Time = Clock->GetDateTime().GetTimeOfDay();
				Clock->SetDateTime(Date + Time);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs PauseCommand(
		TEXT("DreamWorld.Pause"),
		TEXT("DreamWorld.Pause <0|1>: stop or restart the clock (toggles without an argument)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWorldSubsystem* Clock = Find(World))
			{
				Clock->SetPaused(Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : !Clock->IsPaused());
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs DayLengthCommand(
		TEXT("DreamWorld.DayLength"),
		TEXT("DreamWorld.DayLength <minutes>: real minutes per in-game day."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWorldSubsystem* Clock = Find(World); Clock && Args.Num() > 0)
			{
				Clock->SetDayLengthMinutes(FCString::Atof(*Args[0]));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs TransitionCommand(
		TEXT("DreamWorld.Transition"),
		TEXT("DreamWorld.Transition <hours> [seconds]: run the clock forward to an hour."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UDreamWorldSubsystem* Clock = Find(World); Clock && Args.Num() > 0)
			{
				Clock->TransitionToTimeOfDay(FCString::Atof(*Args[0]), Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f, true);
			}
		}));

	/** Steps a world through a list of hours, waits for the picture to settle, takes a screenshot at each. */
	class FShotTour : public FTickableGameObject
	{
	public:
		TWeakObjectPtr<UWorld> World;
		TArray<float> Hours;
		FString Prefix;
		int32 SettleFrames = 30;
		bool bQuit = false;
		TOptional<float> Pitch;
		TOptional<float> Yaw;

		virtual void Tick(float DeltaTime) override
		{
			UWorld* W = World.Get();
			UDreamWorldSubsystem* Clock = W ? W->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
			if (!Clock)
			{
				Finish(nullptr);
				return;
			}
			Aim(*W);
			if (FramesLeft > 0)
			{
				--FramesLeft;
				return;
			}
			// Shaders still compiling would show up as the default material.
			if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
			{
				return;
			}
			if (bShotPending)
			{
				bShotPending = false;
				++Index;
				FramesLeft = 3; // let the screenshot be written before the time moves
				return;
			}
			if (Index >= Hours.Num())
			{
				Finish(Clock);
				return;
			}
			if (!bTimeSet)
			{
				Clock->SetPaused(true);
				Clock->SetTimeOfDay(Hours[Index]);
				bTimeSet = true;
				FramesLeft = SettleFrames;
				return;
			}
			const int32 Minutes = FMath::RoundToInt(Hours[Index] * 60.0f);
			const FString File = FPaths::Combine(FPaths::ScreenShotDir(), TEXT("DreamWorld"), FString::Printf(TEXT("%s_%02d%02d.png"), *Prefix, Minutes / 60, Minutes % 60));
			FScreenshotRequest::RequestScreenshot(File, false, false);
			UE_LOG(LogDreamWorld, Display, TEXT("ShotTour: %s (sun %.1f deg, world tick %.3f ms avg)"), *File, Clock->GetCelestial().SunElevation, Clock->GetAverageTickMs());
			bShotPending = true;
			bTimeSet = false;
		}

		virtual bool IsTickable() const override { return !bDone; }
		virtual bool IsTickableInEditor() const override { return true; }
		virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FDreamWorldShotTour, STATGROUP_Tickables); }

	private:
		/** Turns the view (a camera actor, or the player's control rotation) to Pitch / Yaw, when given. */
		void Aim(UWorld& W) const
		{
			if (!Pitch.IsSet() && !Yaw.IsSet())
			{
				return;
			}
			APlayerController* PC = W.GetFirstPlayerController();
			AActor* Target = PC ? PC->GetViewTarget() : nullptr;
			if (!Target)
			{
				return;
			}
			const bool bCamera = Target->IsA<ACameraActor>();
			FRotator R = bCamera ? Target->GetActorRotation() : PC->GetControlRotation();
			R.Pitch = Pitch.Get(R.Pitch);
			R.Yaw = Yaw.Get(R.Yaw);
			R.Roll = 0.0;
			if (bCamera)
			{
				Target->SetActorRotation(R);
			}
			else
			{
				PC->SetControlRotation(R);
			}
		}

		void Finish(UDreamWorldSubsystem* Clock)
		{
			bDone = true;
			UE_LOG(LogDreamWorld, Display, TEXT("ShotTour: done, %d shots; world tick %.3f ms average"), Index, Clock ? Clock->GetAverageTickMs() : 0.0);
			if (bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("DreamWorld.ShotTour"));
			}
		}

		int32 Index = 0;
		int32 FramesLeft = 10;
		bool bTimeSet = false;
		bool bShotPending = false;
		bool bDone = false;
	};

	static TArray<TUniquePtr<FShotTour>> Tours;

	static FAutoConsoleCommandWithWorldAndArgs ShotTourCommand(
		TEXT("DreamWorld.ShotTour"),
		TEXT("DreamWorld.ShotTour [Hours=0/3/6/...] [Prefix=Day] [Settle=30] [Pitch=deg] [Yaw=deg] [Quit]: stop the clock and screenshot each hour into Saved/Screenshots/<platform>/DreamWorld (hours separated by / or ,; use / inside -ExecCmds, which splits at commas). Pitch / Yaw turn the view first."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TUniquePtr<FShotTour> Tour = MakeUnique<FShotTour>();
			Tour->World = World;
			Tour->Prefix = TEXT("Day");
			for (const FString& Arg : Args)
			{
				FString Key, Value;
				if (!Arg.Split(TEXT("="), &Key, &Value))
				{
					Key = Arg;
				}
				if (Key.Equals(TEXT("Hours"), ESearchCase::IgnoreCase))
				{
					// '/' as well as ',': -ExecCmds splits its commands at commas.
					TArray<FString> Parts;
					const TCHAR* Separators[] = { TEXT(","), TEXT("/") };
					Value.ParseIntoArray(Parts, Separators, UE_ARRAY_COUNT(Separators));
					for (const FString& Part : Parts)
					{
						Tour->Hours.Add(FCString::Atof(*Part));
					}
				}
				else if (Key.Equals(TEXT("Prefix"), ESearchCase::IgnoreCase))
				{
					Tour->Prefix = Value;
				}
				else if (Key.Equals(TEXT("Settle"), ESearchCase::IgnoreCase))
				{
					Tour->SettleFrames = FMath::Max(1, FCString::Atoi(*Value));
				}
				else if (Key.Equals(TEXT("Quit"), ESearchCase::IgnoreCase))
				{
					Tour->bQuit = true;
				}
				else if (Key.Equals(TEXT("Pitch"), ESearchCase::IgnoreCase))
				{
					Tour->Pitch = FCString::Atof(*Value);
				}
				else if (Key.Equals(TEXT("Yaw"), ESearchCase::IgnoreCase))
				{
					Tour->Yaw = FCString::Atof(*Value);
				}
			}
			if (Tour->Hours.Num() == 0)
			{
				for (int32 H = 0; H < 24; H += 3)
				{
					Tour->Hours.Add(float(H));
				}
			}
			UE_LOG(LogDreamWorld, Display, TEXT("ShotTour: %d shots, prefix %s"), Tour->Hours.Num(), *Tour->Prefix);
			Tours.Add(MoveTemp(Tour));
		}));
}
