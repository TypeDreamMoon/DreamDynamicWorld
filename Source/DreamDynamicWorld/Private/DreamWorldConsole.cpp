// Copyright DreamMoon. All Rights Reserved.

// Console commands for the world clock, and DreamWorld.ShotTour, which photographs the world at a list of
// hours (used to check a day cycle from a script: -ExecCmds="DreamWorld.ShotTour Hours=0/6/12/18 Quit"), and
// DreamWorld.Script, which runs a file of steps (weathers, views, a flying camera, frame sequences, GPU profiles).

#include "DreamDynamicWorldModule.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"
#include "DreamWorldInterior.h"

#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
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

	static FAutoConsoleCommandWithWorldAndArgs PortalsCommand(
		TEXT("DreamWorld.Portals"),
		TEXT("DreamWorld.Portals <openness 0..1> [name]: open or close the doors and windows (UDreamPortalComponent) whose actor's name or label holds the name (all of them without one), and list them."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UDreamWorldSubsystem* Clock = Find(World);
			if (!Clock || Args.Num() < 1)
			{
				return;
			}
			const float Openness = FCString::Atof(*Args[0]);
			for (const UDreamPortalComponent* Const : Clock->GetPortals())
			{
				UDreamPortalComponent* Portal = const_cast<UDreamPortalComponent*>(Const);
				const AActor* Owner = Portal->GetOwner();
				const FString Name = Owner ? Owner->GetActorNameOrLabel() : Portal->GetName();
				if (Args.Num() < 2 || Name.Contains(Args[1]))
				{
					Portal->SetOpenness(Openness);
				}
				UE_LOG(LogDreamWorld, Display, TEXT("Portal %s: openness %.2f, ventilating %s, %.0f x %.0f cm"), *Name, Portal->Openness,
					Portal->bVentilate ? TEXT("yes") : TEXT("no"), Portal->Width, Portal->Height);
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
		/** Raise the view this far before the first shot, cm (to look at the clouds from among or above them). */
		TOptional<float> Rise;

		virtual void Tick(float DeltaTime) override
		{
			UWorld* W = World.Get();
			UDreamWorldSubsystem* Clock = W ? W->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
			if (!Clock)
			{
				Finish(nullptr);
				return;
			}
			Raise(*W);
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
		/** Raises the view (a camera actor, or the player's pawn) by Rise, once. */
		void Raise(UWorld& W)
		{
			if (!Rise.IsSet() || bRaised)
			{
				return;
			}
			APlayerController* PC = W.GetFirstPlayerController();
			AActor* Target = PC ? PC->GetViewTarget() : nullptr;
			if (Target && !Target->IsA<ACameraActor>() && PC->GetPawn())
			{
				Target = PC->GetPawn();
			}
			if (Target)
			{
				Target->AddActorWorldOffset(FVector(0.0, 0.0, Rise.GetValue()));
				bRaised = true;
			}
		}
		bool bRaised = false;

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

	/** The actor that carries the view: a camera actor the player looks through, else the player's pawn. */
	static AActor* FindViewCarrier(UWorld& W)
	{
		APlayerController* PC = W.GetFirstPlayerController();
		AActor* Target = PC ? PC->GetViewTarget() : nullptr;
		if (Target && !Target->IsA<ACameraActor>() && PC->GetPawn())
		{
			Target = PC->GetPawn();
		}
		return Target;
	}

	/**
	 * Runs a script of console commands, one step per frame or more, for measurements a single -ExecCmds line cannot
	 * express (a sequence of weathers and views, a camera flying while frames are saved). See DreamWorld.Script's help.
	 */
	class FScriptRun : public FTickableGameObject
	{
	public:
		TWeakObjectPtr<UWorld> World;
		TArray<FString> Lines;
		FString Name;
		int32 SettleFrames = 30;

		virtual void Tick(float DeltaTime) override
		{
			UWorld* W = World.Get();
			if (!W)
			{
				Finish(TEXT("the world went away"), false);
				return;
			}
			++Frame;
			if (CaptureLeft > 0)
			{
				// One saved frame per step of the view: hold the view and the script while the previous request is pending.
				if (!FScreenshotRequest::IsScreenshotRequested())
				{
					Fly(*W, DeltaTime);
					const FString File = FPaths::Combine(FPaths::ScreenShotDir(), TEXT("DreamWorld"), FString::Printf(TEXT("%s_f%04d.png"), *CaptureName, CaptureIndex));
					FScreenshotRequest::RequestScreenshot(File, false, false);
					const AActor* Carrier = FindViewCarrier(*W);
					const FVector At = Carrier ? Carrier->GetActorLocation() : FVector::ZeroVector;
					UE_LOG(LogDreamWorld, Display, TEXT("Script: frame %s %d dt %.4f at %.0f %.0f %.0f"), *CaptureName, CaptureIndex, DeltaTime, At.X, At.Y, At.Z);
					++CaptureIndex;
					--CaptureLeft;
				}
				return;
			}
			Fly(*W, DeltaTime);
			if (WaitFrames > 0)
			{
				--WaitFrames;
				return;
			}
			if (bSettling)
			{
				// Shaders still compiling would show up as the default material.
				if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling())
				{
					return;
				}
				bSettling = false;
				WaitFrames = SettleFrames;
				return;
			}
			if (FScreenshotRequest::IsScreenshotRequested())
			{
				return;
			}
			// Run lines up to the next one that takes frames.
			while (Line < Lines.Num() && !bDone)
			{
				if (Step(*W, Lines[Line++].TrimStartAndEnd()))
				{
					return;
				}
			}
			if (!bDone)
			{
				Finish(TEXT("end of script"), false);
			}
		}

		virtual bool IsTickable() const override { return !bDone; }
		virtual bool IsTickableInEditor() const override { return true; }
		virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FDreamWorldScriptRun, STATGROUP_Tickables); }

	private:
		/** Runs one line; true when it takes frames (the script resumes on a later tick). */
		bool Step(UWorld& W, const FString& Text)
		{
			if (Text.IsEmpty() || Text.StartsWith(TEXT("#")) || Text.StartsWith(TEXT("//")))
			{
				return false;
			}
			TArray<FString> Words;
			Text.ParseIntoArrayWS(Words);
			const FString Verb = Words[0].ToLower();
			auto Num = [&Words](int32 I, double Default) { return Words.IsValidIndex(I) ? FCString::Atod(*Words[I]) : Default; };
			UE_LOG(LogDreamWorld, Display, TEXT("Script: [%d] %s"), Frame, *Text);
			if (Verb == TEXT("wait"))
			{
				WaitFrames = FMath::Max(1, int32(Num(1, 1)));
				return true;
			}
			if (Verb == TEXT("settle"))
			{
				bSettling = true;
				if (Words.IsValidIndex(1))
				{
					SettleFrames = FMath::Max(1, int32(Num(1, SettleFrames)));
				}
				return true;
			}
			if (Verb == TEXT("time"))
			{
				if (UDreamWorldSubsystem* Clock = W.GetSubsystem<UDreamWorldSubsystem>())
				{
					Clock->SetPaused(true);
					Clock->SetTimeOfDay(float(Num(1, 12)));
				}
				return false;
			}
			if (Verb == TEXT("aim") || Verb == TEXT("move") || Verb == TEXT("rise") || Verb == TEXT("home"))
			{
				APlayerController* PC = W.GetFirstPlayerController();
				AActor* Carrier = FindViewCarrier(W);
				if (!Carrier)
				{
					UE_LOG(LogDreamWorld, Warning, TEXT("Script: no view to %s"), *Verb);
					return false;
				}
				if (!Home.IsSet())
				{
					Home = Carrier->GetActorLocation();
				}
				if (Verb == TEXT("home"))
				{
					Carrier->SetActorLocation(Home.GetValue());
				}
				else if (Verb == TEXT("move"))
				{
					Carrier->SetActorLocation(FVector(Num(1, 0), Num(2, 0), Num(3, Carrier->GetActorLocation().Z)));
				}
				else if (Verb == TEXT("rise"))
				{
					Carrier->AddActorWorldOffset(FVector(0.0, 0.0, Num(1, 0)));
				}
				else
				{
					AActor* ViewTarget = PC ? PC->GetViewTarget() : nullptr;
					const bool bCamera = !PC || (ViewTarget && ViewTarget->IsA<ACameraActor>());
					const double Yaw = bCamera ? (ViewTarget ? ViewTarget : Carrier)->GetActorRotation().Yaw : PC->GetControlRotation().Yaw;
					FRotator R(Num(1, 0), Num(2, Yaw), 0.0);
					// (aim sun|moon|antisun [pitch offset] [yaw offset]: toward the body, or away from the sun.)
					const FString Body = Words.IsValidIndex(1) ? Words[1].ToLower() : FString();
					const UDreamWorldSubsystem* Clock = W.GetSubsystem<UDreamWorldSubsystem>();
					if (Clock && (Body == TEXT("sun") || Body == TEXT("moon") || Body == TEXT("antisun")))
					{
						const FDreamCelestialState& Sky = Clock->GetCelestial();
						const FVector D = Body == TEXT("moon") ? Sky.MoonDirection : (Body == TEXT("sun") ? Sky.SunDirection : -Sky.SunDirection);
						R = FRotator(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Z, -1.0, 1.0))) + Num(2, 0),
							FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)) + Num(3, 0), 0.0);
					}
					if (bCamera)
					{
						(ViewTarget ? ViewTarget : Carrier)->SetActorRotation(R);
					}
					else
					{
						PC->SetControlRotation(R);
					}
				}
				return false;
			}
			if (Verb == TEXT("fly"))
			{
				// fly <m/s> [heading deg, the view's yaw when left out] [climb m/s]
				const AActor* Carrier = FindViewCarrier(W);
				APlayerController* PC = W.GetFirstPlayerController();
				double Yaw = Num(2, 0);
				if (!Words.IsValidIndex(2) && PC)
				{
					Yaw = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Yaw : PC->GetControlRotation().Yaw;
				}
				FlyVelocity = FRotator(0.0, Yaw, 0.0).Vector() * Num(1, 0) * 100.0 + FVector(0.0, 0.0, Num(3, 0) * 100.0);
				UE_LOG(LogDreamWorld, Display, TEXT("Script: flying %.0f cm/s heading %.1f from %s"), FlyVelocity.Size(), Yaw, Carrier ? *Carrier->GetActorLocation().ToString() : TEXT("nowhere"));
				return false;
			}
			if (Verb == TEXT("shot") || Verb == TEXT("frames"))
			{
				CaptureName = Words.IsValidIndex(1) ? Words[1] : Name;
				CaptureLeft = Verb == TEXT("shot") ? 1 : FMath::Max(1, int32(Num(2, 1)));
				CaptureIndex = 0;
				if (Verb == TEXT("shot"))
				{
					const FString File = FPaths::Combine(FPaths::ScreenShotDir(), TEXT("DreamWorld"), CaptureName + TEXT(".png"));
					FScreenshotRequest::RequestScreenshot(File, false, false);
					CaptureLeft = 0;
				}
				return true;
			}
			if (Verb == TEXT("mark"))
			{
				UE_LOG(LogDreamWorld, Display, TEXT("Script: mark %s"), *Text.Mid(4).TrimStartAndEnd());
				return false;
			}
			if (Verb == TEXT("quit"))
			{
				Finish(TEXT("quit"), true);
				return true;
			}
			// Anything else is a console command, run against this world.
			GEngine->Exec(&W, *Text);
			return false;
		}

		void Fly(UWorld& W, float DeltaTime)
		{
			if (FlyVelocity.IsNearlyZero())
			{
				return;
			}
			if (AActor* Carrier = FindViewCarrier(W))
			{
				Carrier->AddActorWorldOffset(FlyVelocity * DeltaTime);
			}
		}

		void Finish(const TCHAR* Why, bool bQuit)
		{
			bDone = true;
			UE_LOG(LogDreamWorld, Display, TEXT("Script: done (%s) after %d frames"), Why, Frame);
			if (bQuit)
			{
				FPlatformMisc::RequestExit(false, TEXT("DreamWorld.Script"));
			}
		}

		int32 Line = 0;
		int32 Frame = 0;
		int32 WaitFrames = 10;
		bool bSettling = false;
		bool bDone = false;
		FVector FlyVelocity = FVector::ZeroVector;
		/** Where the view was when the script first moved or turned it ('home' goes back there). */
		TOptional<FVector> Home;
		FString CaptureName;
		int32 CaptureLeft = 0;
		int32 CaptureIndex = 0;
	};

	static TArray<TUniquePtr<FScriptRun>> Scripts;

	static FAutoConsoleCommandWithWorldAndArgs ScriptCommand(
		TEXT("DreamWorld.Script"),
		TEXT("DreamWorld.Script <file> [Settle=30]: run a script, one line at a time (relative paths from the project). Lines: a console command;")
		TEXT(" 'wait <frames>'; 'settle [frames]' (until no shader compiles, then that many frames); 'time <hours>' (stops the clock there);")
		TEXT(" 'aim <pitch> [yaw]'; 'move <x> <y> [z]' and 'rise <dz>' (cm, the view); 'home' (back where the view was before the script moved it); 'fly <m/s> [heading deg] [climb m/s]' (keeps moving the view, 'fly 0' stops);")
		TEXT(" 'shot <name>' (Saved/Screenshots/<platform>/DreamWorld/<name>.png); 'frames <name> <count>' (that many consecutive frames, <name>_fNNNN.png);")
		TEXT(" 'mark <label>' (a log line, to find what follows); 'quit'. '#' starts a comment."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogDreamWorld, Warning, TEXT("DreamWorld.Script: which file?"));
				return;
			}
			TUniquePtr<FScriptRun> Run = MakeUnique<FScriptRun>();
			Run->World = World;
			FString Path = Args[0].TrimQuotes();
			if (FPaths::IsRelative(Path))
			{
				Path = FPaths::Combine(FPaths::ProjectDir(), Path);
			}
			if (!FFileHelper::LoadFileToStringArray(Run->Lines, *Path))
			{
				UE_LOG(LogDreamWorld, Warning, TEXT("DreamWorld.Script: cannot read %s"), *Path);
				return;
			}
			Run->Name = FPaths::GetBaseFilename(Path);
			for (int32 I = 1; I < Args.Num(); ++I)
			{
				FString Key, Value;
				if (Args[I].Split(TEXT("="), &Key, &Value) && Key.Equals(TEXT("Settle"), ESearchCase::IgnoreCase))
				{
					Run->SettleFrames = FMath::Max(1, FCString::Atoi(*Value));
				}
			}
			UE_LOG(LogDreamWorld, Display, TEXT("Script: %s, %d lines"), *Path, Run->Lines.Num());
			Scripts.Add(MoveTemp(Run));
		}));

	static TArray<TUniquePtr<FShotTour>> Tours;

	static FAutoConsoleCommandWithWorldAndArgs ShotTourCommand(
		TEXT("DreamWorld.ShotTour"),
		TEXT("DreamWorld.ShotTour [Hours=0/3/6/...] [Prefix=Day] [Settle=30] [Pitch=deg] [Yaw=deg] [Rise=cm] [Quit]: stop the clock and screenshot each hour into Saved/Screenshots/<platform>/DreamWorld (hours separated by / or ,; use / inside -ExecCmds, which splits at commas). Pitch / Yaw turn the view first, Rise raises it."),
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
				else if (Key.Equals(TEXT("Rise"), ESearchCase::IgnoreCase))
				{
					Tour->Rise = FCString::Atof(*Value);
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
