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
	WaitFrames(60);
	Then([this, Before]()
	{
		const FCounts Now = CountsOf(EditorWorld());
		TestTrue(TEXT("the editor world owns the shared targets with nothing playing"), DreamWorld::SharedTargets::Owns(EditorWorld()));
		TestTrue(FString::Printf(TEXT("and draws the surface maps (%u -> %u)"), Before->Surface, Now.Surface), Now.Surface > Before->Surface);
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
	WaitFrames(180);
	Then([this, AtStart, PlayAtStart, AtEnd]()
	{
		const FCounts Editor = CountsOf(EditorWorld());
		const FCounts Play = CountsOf(PlayWorld());
		TestEqual(TEXT("while it plays the editor world draws no weather map"), Editor.Weather, AtStart->Weather);
		TestEqual(TEXT("nor any surface map"), Editor.Surface, AtStart->Surface);
		TestTrue(FString::Printf(TEXT("the play session draws its surface maps (%u -> %u)"), PlayAtStart->Surface, Play.Surface), Play.Surface > PlayAtStart->Surface);
		TestTrue(FString::Printf(TEXT("and has drawn its weather map (%u)"), Play.Weather), Play.Weather > 0);
		*AtEnd = Editor;
	});

	// Stop: the editor world has them back and draws them all afresh (the play session's frame is in them).
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([]() { return PlayWorld() == nullptr; }));
	WaitFrames(30);
	Then([this, AtEnd]()
	{
		const FCounts Editor = CountsOf(EditorWorld());
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
