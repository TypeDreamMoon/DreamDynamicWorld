// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldSequencerSubsystem.h"
#include "DreamWorldTracks.h"
#include "DreamWeather.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldCelestial.h"
#include "DreamWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorldSequencer::Tests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UDreamWeatherPreset* MakePreset(const TCHAR* Name, std::initializer_list<TPair<FGameplayTag, float>> Channels)
	{
		UDreamWeatherPreset* Preset = NewObject<UDreamWeatherPreset>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UDreamWeatherPreset::StaticClass(), Name));
		for (const TPair<FGameplayTag, float>& Channel : Channels)
		{
			Preset->Channels.Add(Channel.Key, Channel.Value);
		}
		return Preset;
	}

	/** A game world with clear weather, the clock on 21 June. */
	struct FWorld
	{
		UWorld* World = nullptr;
		UDreamWorldSubsystem* Clock = nullptr;
		UDreamWeatherSubsystem* Weather = nullptr;

		FWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			Clock = World ? World->GetSubsystem<UDreamWorldSubsystem>() : nullptr;
			Weather = UDreamWeatherSubsystem::Get(World);
			if (!Clock || !Weather)
			{
				return;
			}
			UDreamWeatherSelector_Fixed* Fixed = NewObject<UDreamWeatherSelector_Fixed>();
			Fixed->Preset = MakePreset(TEXT("Clear"), { { DreamWeather::Tags::Channel_CloudCoverage, 0.1f } });
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ADreamWeather* Actor = World->SpawnActor<ADreamWeather>(Spawn);
			Fixed->Rename(nullptr, Actor);
			Actor->Selector = Fixed;
			FDreamWorldClockConfig Config;
			Config.StartTime = FDateTime(2026, 6, 21, 9, 0, 0);
			Clock->ApplyClockConfig(Config, true);
			Clock->ReportView(FVector(0, 0, 170), FRotator(0, 0, 0), true);
			Clock->Tick(0.016f);
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		bool IsValid() const { return Clock && Weather; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldClockDriveTest, "DreamWorld.Clock.ExternalDrive", DreamWorldSequencer::Tests::Flags)
bool FDreamWorldClockDriveTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSequencer::Tests;
	FWorld W;
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	UObject* Driver = W.Weather;
	const double Start = W.Clock->GetWorldSeconds();
	W.Clock->BeginExternalDrive(Driver);
	TestTrue(TEXT("held"), W.Clock->IsDrivenExternally());
	TestFalse(TEXT("a held clock does not run"), W.Clock->IsClockRunning());
	W.Clock->Tick(1.0f);
	TestEqual(TEXT("and stays where it is"), W.Clock->GetWorldSeconds(), Start);

	const uint32 Serial = W.Clock->GetTimeJumpSerial();
	W.Clock->DriveWorldSeconds(Start + 60.0);
	TestEqual(TEXT("a small step forward runs"), W.Clock->GetTimeJumpSerial(), Serial);
	TestEqual(TEXT("to where it was driven"), W.Clock->GetWorldSeconds(), Start + 60.0);
	W.Clock->DriveWorldSeconds(Start - 600.0);
	TestTrue(TEXT("a step back jumps"), W.Clock->GetTimeJumpSerial() != Serial);
	const uint32 Serial2 = W.Clock->GetTimeJumpSerial();
	W.Clock->DriveWorldSeconds(Start + 7200.0);
	TestTrue(TEXT("a long step jumps"), W.Clock->GetTimeJumpSerial() != Serial2);

	W.Clock->EndExternalDrive(Driver);
	TestFalse(TEXT("let go"), W.Clock->IsDrivenExternally());

	// Cinematic updates: every frame counts as a jump for what eases.
	IConsoleVariable* Mode = IConsoleManager::Get().FindConsoleVariable(TEXT("DreamWorld.CinematicMode"));
	if (TestNotNull(TEXT("DreamWorld.CinematicMode"), Mode))
	{
		TestFalse(TEXT("not cinematic outside a render"), W.Clock->IsCinematicMode());
		const int32 Old = Mode->GetInt();
		ON_SCOPE_EXIT { Mode->Set(Old, ECVF_SetByCode); };
		Mode->Set(1, ECVF_SetByCode);
		const uint32 Before = W.Clock->GetTimeJumpSerial();
		W.Clock->Tick(0.016f);
		W.Clock->Tick(0.016f);
		TestEqual(TEXT("cinematic: everything snaps every frame"), W.Clock->GetTimeJumpSerial(), Before + 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSequencerSectionsTest, "DreamWorld.Sequencer.Sections", DreamWorldSequencer::Tests::Flags)
bool FDreamWorldSequencerSectionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSequencer::Tests;
	FWorld W;
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	UDreamWorldTimeSection* Time = NewObject<UDreamWorldTimeSection>();
	Time->Hours.AddLinearKey(FFrameNumber(0), 18.0f);
	Time->Hours.AddLinearKey(FFrameNumber(1000), 26.0f);
	double Seconds = 0.0;
	TestTrue(TEXT("the time curve evaluates"), Time->EvaluateWorldSeconds(FFrameTime(500), W.World, Seconds));
	const double Midnight = DreamWorld::Celestial::FromDateTime(FDateTime(2026, 6, 21));
	TestNearlyEqual(TEXT("halfway is 22:00 of the clock's start date"), Seconds, Midnight + 22.0 * 3600.0, 1.0);
	TestTrue(TEXT("past 24 runs into the next day"), Time->EvaluateWorldSeconds(FFrameTime(1000), W.World, Seconds) && Seconds > Midnight + 86400.0);
	Time->bOverrideDate = true;
	Time->Date = FDateTime(2026, 12, 21);
	Time->EvaluateWorldSeconds(FFrameTime(0), W.World, Seconds);
	TestNearlyEqual(TEXT("or of its own date"), Seconds, DreamWorld::Celestial::FromDateTime(FDateTime(2026, 12, 21, 18)), 1.0);

	UDreamWeatherChannelSection* Channels = NewObject<UDreamWeatherChannelSection>();
	Channels->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(1000)));
	Channels->Precipitation.AddLinearKey(FFrameNumber(0), 0.0f);
	Channels->Precipitation.AddLinearKey(FFrameNumber(1000), 1.0f);
	const FDreamWeatherModifierDesc Desc = Channels->MakeModifier(FFrameTime(250));
	TestEqual(TEXT("only the keyed channel applies"), Desc.Channels.Num(), 1);
	TestNearlyEqual(TEXT("at its curve"), Desc.Channels.FindRef(DreamWeather::Tags::Channel_Precipitation), 0.25f, 1.0e-4f);
	TestEqual(TEXT("in full without easing"), Desc.Weight, 1.0f);
	// Any channel the weather has, by its tag: past the first eleven built-ins (freezing rain, hail, sleet) and the
	// project's own alike.
	if (FMovieSceneFloatChannel* Hail = Channels->GetOrAddCurve(DreamWeather::Tags::Channel_Hail))
	{
		Hail->AddLinearKey(FFrameNumber(0), 0.5f);
		const FDreamWeatherModifierDesc WithHail = Channels->MakeModifier(FFrameTime(250));
		TestNearlyEqual(TEXT("the hail channel keyed by its tag"), WithHail.Channels.FindRef(DreamWeather::Tags::Channel_Hail), 0.5f, 1.0e-4f);
		TestEqual(TEXT("with the precipitation still"), WithHail.Channels.Num(), 2);
	}
	else
	{
		AddError(TEXT("no curve for the hail channel"));
	}
	TestNull(TEXT("no curve for a tag the weather does not have"), Channels->GetOrAddCurve(FGameplayTag()));
	for (const FDreamWeatherChannelDef& Def : FDreamWeatherChannels::Get())
	{
		Channels->GetOrAddCurve(Def.Channel);
	}
	TestEqual(TEXT("a curve for every channel the weather has"), Channels->GetCurves().Num(), FDreamWeatherChannels::Num());

	UDreamLightningSection* Lightning = NewObject<UDreamLightningSection>();
	Lightning->Strikes.AddConstantKey(FFrameNumber(100), 1.0f);
	Lightning->Strikes.AddConstantKey(FFrameNumber(200), 0.5f);
	const FVector A = Lightning->StrikeLocation(0, FVector::ZeroVector, FRotator::ZeroRotator);
	TestEqual(TEXT("a key strikes the same place every play"), Lightning->StrikeLocation(0, FVector::ZeroVector, FRotator::ZeroRotator), A);
	TestTrue(TEXT("ahead of the view"), A.X > 100000.0 && FMath::Abs(A.Y) <= Lightning->Scatter);
	const FVector Turned = Lightning->StrikeLocation(0, FVector::ZeroVector, FRotator(0.0, 90.0, 0.0));
	TestTrue(TEXT("and turns with it"), Turned.Y > 100000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSequencerPlaybackTest, "DreamWorld.Sequencer.Playback", DreamWorldSequencer::Tests::Flags)
bool FDreamWorldSequencerPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSequencer::Tests;
	FWorld W;
	if (!TestTrue(TEXT("world"), W.IsValid()))
	{
		return false;
	}
	ULevelSequence* Sequence = NewObject<ULevelSequence>(GetTransientPackage(), NAME_None, RF_Transient);
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	const FFrameRate Tick = Scene->GetTickResolution();
	const FFrameNumber TenSeconds = (10.0 * Tick).FloorToFrame();
	Scene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), TenSeconds));

	// Dusk to night over the ten seconds, and a storm that comes in over the second half.
	UDreamWorldTimeTrack* TimeTrack = Scene->AddTrack<UDreamWorldTimeTrack>();
	UDreamWorldTimeSection* Time = Cast<UDreamWorldTimeSection>(TimeTrack->CreateNewSection());
	Time->SetRange(TRange<FFrameNumber>(FFrameNumber(0), TenSeconds));
	Time->Hours.AddLinearKey(FFrameNumber(0), 18.0f);
	Time->Hours.AddLinearKey(TenSeconds, 22.0f);
	TimeTrack->AddSection(*Time);

	UDreamWeatherTrack* WeatherTrack = Scene->AddTrack<UDreamWeatherTrack>();
	UDreamWeatherPresetSection* Storm = Cast<UDreamWeatherPresetSection>(WeatherTrack->CreateNewSection());
	Storm->Preset = MakePreset(TEXT("Storm"), { { DreamWeather::Tags::Channel_CloudCoverage, 1.0f }, { DreamWeather::Tags::Channel_Precipitation, 1.0f } });
	Storm->SetRange(TRange<FFrameNumber>(TenSeconds / 2, TenSeconds));
	Storm->Easing.bManualEaseIn = true;
	Storm->Easing.ManualEaseInDuration = (2.0 * Tick).FloorToFrame().Value;
	WeatherTrack->AddSection(*Storm);

	FMovieSceneSequencePlaybackSettings Settings;
	ALevelSequenceActor* Actor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(W.World, Sequence, Settings, Actor);
	if (!TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	// A section that has just become active runs its hooks from the evaluation after (a frame late in a game):
	// evaluate every position twice.
	auto JumpTo = [&](float Seconds)
	{
		for (int32 i = 0; i < 2; ++i)
		{
			Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(Seconds, EUpdatePositionMethod::Jump));
			W.Clock->Tick(0.016f);
		}
	};

	JumpTo(5.25f);
	TestNearlyEqual(TEXT("past halfway: 20:06"), W.Clock->GetTimeOfDay(), 20.1f, 0.02f);
	TestTrue(TEXT("the track holds the clock"), W.Clock->IsDrivenExternally());
	TestEqual(TEXT("the storm section has begun: its modifier is in the weather's stack"), UDreamWorldSequencerSubsystem::Get(W.World)->GetNumHeld(), 1);
	JumpTo(9.5f);
	TestTrue(FString::Printf(TEXT("eased in: it pours (%.2f)"), W.Weather->GetViewSample().Precipitation), W.Weather->GetViewSample().Precipitation > 0.9f);
	JumpTo(6.0f);
	const float Halfway = W.Weather->GetGlobalValues().Get(0);
	TestTrue(FString::Printf(TEXT("halfway through its easing: the clouds are coming in (%.2f)"), Halfway), Halfway > 0.2f && Halfway < 0.95f);
	JumpTo(2.0f);
	TestEqual(TEXT("before the storm: no modifier"), UDreamWorldSequencerSubsystem::Get(W.World)->GetNumHeld(), 0);
	TestTrue(TEXT("before the storm: dry"), W.Weather->GetViewSample().Precipitation < 0.05f);
	TestNearlyEqual(TEXT("and 18:48"), W.Clock->GetTimeOfDay(), 18.8f, 0.02f);

	Player->Stop();
	W.Clock->Tick(0.016f);
	TestFalse(TEXT("stopped: the clock is its own again"), W.Clock->IsDrivenExternally());
	return true;
}

#endif
