// Copyright DreamMoon. All Rights Reserved.

// The shared render targets with a play session: while it runs only it draws them, the editor world leaves them (and
// its own materials) alone; when it ends the editor world draws them all afresh. Needs the editor with a renderer.

#include "CoreMinimal.h"
#include "DreamSurfaceSubsystem.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSharedTargets.h"

#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Engine/World.h"
#include "LevelEditorViewport.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorldSharedTargetsPIETests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static const TCHAR* Map = TEXT("/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcase");

	struct FCounts
	{
		uint32 Weather = 0;
		uint32 Surface = 0;
	};

	FCounts CountsOf(const UWorld* World)
	{
		FCounts C;
		if (World)
		{
			if (const UDreamWeatherSubsystem* Weather = World->GetSubsystem<UDreamWeatherSubsystem>())
			{
				C.Weather = Weather->GetWeatherMapDrawCount();
			}
			if (const UDreamSurfaceSubsystem* Surface = World->GetSubsystem<UDreamSurfaceSubsystem>())
			{
				C.Surface = Surface->GetMapDrawCount();
			}
		}
		return C;
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UWorld* PlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	void WaitFrames(int32 Frames)
	{
		TSharedRef<uint64> Start = MakeShared<uint64>(0);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Start, Frames]()
		{
			if (*Start == 0)
			{
				*Start = GFrameCounter;
			}
			return GFrameCounter - *Start >= uint64(Frames);
		}));
	}

	void Then(TFunction<void()> Step)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Step]() { Step(); return true; }));
	}

	/** What a world's maps are up to (when a wait runs out). */
	FString Describe(const UWorld* World)
	{
		if (!World)
		{
			return TEXT("no world");
		}
		const UDreamWeatherSubsystem* Weather = World->GetSubsystem<UDreamWeatherSubsystem>();
		const UDreamSurfaceSubsystem* Surface = World->GetSubsystem<UDreamSurfaceSubsystem>();
		const FCounts C = CountsOf(World);
		return FString::Printf(TEXT("%s: owns the targets %d, weather active %d, surface active %d, weather maps %u, surface maps %u"),
			*World->GetName(), DreamWorld::SharedTargets::Owns(World) ? 1 : 0, Weather && Weather->IsActive() ? 1 : 0,
			Surface && Surface->IsActive() ? 1 : 0, C.Weather, C.Surface);
	}

	/**
	 * Frames until Condition holds (a map is drawn when its interval of real time has gone by and the world has ticked:
	 * a fixed count of frames can be too few on a fast machine), at most MaxSeconds of real time; Out says how it went.
	 */
	struct FWait
	{
		int32 Frames = 0;
		bool bMet = false;
	};
	void WaitUntil(TSharedRef<FWait> Out, TFunction<bool()> Condition, double MaxSeconds = 10.0)
	{
		TSharedRef<double> Start = MakeShared<double>(0.0);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Out, Condition, Start, MaxSeconds]()
		{
			if (*Start == 0.0)
			{
				*Start = FPlatformTime::Seconds();
			}
			++Out->Frames;
			Out->bMet = Condition();
			return Out->bMet || FPlatformTime::Seconds() - *Start > MaxSeconds;
		}));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSharedTargetsPIETest, "DreamWorld.SharedTargets.PlayInEditor", DreamWorldSharedTargetsPIETests::Flags)
bool FDreamWorldSharedTargetsPIETest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSharedTargetsPIETests;
	if (!FApp::CanEverRender() || !GEditor)
	{
		AddWarning(TEXT("Needs the editor with a renderer (not -nullrhi)."));
		return true;
	}
	GetMutableDefault<UEditorPerformanceSettings>()->bThrottleCPUWhenNotForeground = false;
	FAutomationEditorCommonUtils::LoadMap(Map);
	for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
	{
		if (C && C->IsPerspective())
		{
			C->SetRealtime(true);
		}
	}
	TSharedRef<FCounts> Before = MakeShared<FCounts>();
	TSharedRef<FCounts> AtStart = MakeShared<FCounts>();
	TSharedRef<FCounts> PlayAtStart = MakeShared<FCounts>();
	TSharedRef<FCounts> AtEnd = MakeShared<FCounts>();

	// The editor world draws the maps while nothing plays.
	WaitFrames(30);
	Then([Before]() { *Before = CountsOf(EditorWorld()); });
	TSharedRef<FWait> Drawing = MakeShared<FWait>();
	WaitUntil(Drawing, [Before]() { return CountsOf(EditorWorld()).Surface > Before->Surface; });
	Then([this, Before, Drawing]()
	{
		const FCounts Now = CountsOf(EditorWorld());
		AddInfo(FString::Printf(TEXT("nothing playing, after %d frames: %s"), Drawing->Frames, *Describe(EditorWorld())));
		TestTrue(TEXT("the editor world owns the shared targets with nothing playing"), DreamWorld::SharedTargets::Owns(EditorWorld()));
		TestTrue(FString::Printf(TEXT("and draws the surface maps (%u -> %u)"), Before->Surface, Now.Surface), Drawing->bMet);
	});

	// Play, in a window of its own (the editor viewport keeps drawing the editor world beside it, as when the targets
	// flickered): from its first frames the play session has them; the editor world draws nothing more.
	Then([]()
	{
		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(Params);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { return PlayWorld() != nullptr; }));
	WaitFrames(2);
	Then([this, AtStart, PlayAtStart]()
	{
		*AtStart = CountsOf(EditorWorld());
		*PlayAtStart = CountsOf(PlayWorld());
		TestTrue(TEXT("the play session owns the shared targets"), DreamWorld::SharedTargets::Owns(PlayWorld()));
		TestFalse(TEXT("the editor world does not"), DreamWorld::SharedTargets::Owns(EditorWorld()));
	});
	TSharedRef<FWait> Playing = MakeShared<FWait>();
	WaitUntil(Playing, [PlayAtStart]()
	{
		const FCounts Play = CountsOf(PlayWorld());
		return Play.Surface > PlayAtStart->Surface && Play.Weather > 0;
	});
	WaitFrames(60);   // (and a while more: the editor world must stay quiet all along)
	Then([this, AtStart, PlayAtStart, AtEnd, Playing]()
	{
		const FCounts Editor = CountsOf(EditorWorld());
		const FCounts Play = CountsOf(PlayWorld());
		AddInfo(FString::Printf(TEXT("playing, after %d frames: %s; %s"), Playing->Frames + 60, *Describe(PlayWorld()), *Describe(EditorWorld())));
		TestEqual(TEXT("while it plays the editor world draws no weather map"), Editor.Weather, AtStart->Weather);
		TestEqual(TEXT("nor any surface map"), Editor.Surface, AtStart->Surface);
		TestTrue(FString::Printf(TEXT("the play session draws its surface maps (%u -> %u)"), PlayAtStart->Surface, Play.Surface), Play.Surface > PlayAtStart->Surface);
		TestTrue(FString::Printf(TEXT("and has drawn its weather map (%u)"), Play.Weather), Play.Weather > 0);
		*AtEnd = Editor;
	});

	// Stop: the editor world has them back and draws them all afresh (the play session's frame is in them).
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { return PlayWorld() == nullptr; }));
	TSharedRef<FWait> Back = MakeShared<FWait>();
	WaitUntil(Back, [AtEnd]()
	{
		const FCounts Editor = CountsOf(EditorWorld());
		return Editor.Weather > AtEnd->Weather && Editor.Surface > AtEnd->Surface;
	});
	Then([this, AtEnd, Back]()
	{
		const FCounts Editor = CountsOf(EditorWorld());
		AddInfo(FString::Printf(TEXT("back in the editor, after %d frames: %s"), Back->Frames, *Describe(EditorWorld())));
		TestTrue(TEXT("back in the editor, it owns them again"), DreamWorld::SharedTargets::Owns(EditorWorld()));
		TestTrue(FString::Printf(TEXT("and redraws the weather map at once (%u -> %u)"), AtEnd->Weather, Editor.Weather), Editor.Weather > AtEnd->Weather);
		TestTrue(FString::Printf(TEXT("and the surface maps (%u -> %u)"), AtEnd->Surface, Editor.Surface), Editor.Surface > AtEnd->Surface);
		FAutomationEditorCommonUtils::CreateNewMap();
	});
	// (The showcase out of memory: a test that loads it next gets it fresh from disk, not the editor's played copy.)
	WaitFrames(2);
	Then([]() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS); });
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
