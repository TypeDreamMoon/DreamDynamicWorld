// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldSharedTargets.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DreamWorldSharedTargetsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	DreamWorld::SharedTargets::FCandidate Make(EWorldType::Type Type, int32 PIEInstance = INDEX_NONE)
	{
		DreamWorld::SharedTargets::FCandidate C;
		C.Type = Type;
		C.PIEInstance = PIEInstance;
		return C;
	}

	int32 Pick(std::initializer_list<DreamWorld::SharedTargets::FCandidate> List)
	{
		const TArray<DreamWorld::SharedTargets::FCandidate> Candidates(List);
		return DreamWorld::SharedTargets::PickOwner(Candidates);
	}

	/** The editor world the engine lists, if any (an editor or an automation run). */
	UWorld* EditorWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Editor && Context.World())
				{
					return Context.World();
				}
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSharedTargetsRuleTest, "DreamWorld.SharedTargets.Rule", DreamWorldSharedTargetsTests::Flags)
bool FDreamWorldSharedTargetsRuleTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSharedTargetsTests;

	// The rule itself.
	TestEqual(TEXT("the editor alone has them"), Pick({ Make(EWorldType::Editor) }), 0);
	TestEqual(TEXT("play in editor takes them from the editor"), Pick({ Make(EWorldType::Editor), Make(EWorldType::PIE, 0) }), 1);
	TestEqual(TEXT("of several play sessions, the first instance"), Pick({ Make(EWorldType::Editor), Make(EWorldType::PIE, 1), Make(EWorldType::PIE, 0) }), 2);
	TestEqual(TEXT("a game world over all"), Pick({ Make(EWorldType::Editor), Make(EWorldType::PIE, 0), Make(EWorldType::Game) }), 2);
	TestEqual(TEXT("previews and inactive worlds never"), Pick({ Make(EWorldType::EditorPreview), Make(EWorldType::Inactive), Make(EWorldType::GamePreview) }), INDEX_NONE);
	TestEqual(TEXT("a preview does not stand in the editor's way"), Pick({ Make(EWorldType::EditorPreview), Make(EWorldType::Editor) }), 1);
	TestEqual(TEXT("nobody"), Pick({}), INDEX_NONE);

	// Worlds the engine does not list (a test's) take part by their kind, in the order they asked.
	UWorld* A = UWorld::CreateWorld(EWorldType::Game, false);
	UWorld* B = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("world A"), A) || !TestNotNull(TEXT("world B"), B))
	{
		return false;
	}
	bool bADestroyed = false;
	ON_SCOPE_EXIT
	{
		if (!bADestroyed)
		{
			A->DestroyWorld(false);
		}
		B->DestroyWorld(false);
	};
	DreamWorld::SharedTargets::FWriter WriterA, WriterB;
	bool bRedraw = false;
	TestTrue(TEXT("A asks first and has them"), WriterA.Acquire(A, bRedraw));
	TestTrue(TEXT("its first time: draw everything"), bRedraw);
	TestTrue(TEXT("A again"), WriterA.Acquire(A, bRedraw));
	TestFalse(TEXT("nothing to redraw while it keeps them"), bRedraw);
	TestFalse(TEXT("B, asking after it, does not"), WriterB.Acquire(B, bRedraw));
	TestFalse(TEXT("(and is told nothing to draw)"), bRedraw);
	if (UWorld* Editor = EditorWorld())
	{
		TestFalse(TEXT("nor does the editor world while a game world runs"), DreamWorld::SharedTargets::Owns(Editor));
	}

	// A goes: B has them, and draws afresh (they hold A's frame).
	A->DestroyWorld(false);
	bADestroyed = true;
	const uint32 Serial = DreamWorld::SharedTargets::GetOwnerSerial();
	TestTrue(TEXT("A gone, B has them"), WriterB.Acquire(B, bRedraw));
	TestTrue(TEXT("and draws them afresh"), bRedraw);
	TestNotEqual(TEXT("the owner changed"), DreamWorld::SharedTargets::GetOwnerSerial(), Serial);
	TestTrue(TEXT("B again"), WriterB.Acquire(B, bRedraw));
	TestFalse(TEXT("nothing more to redraw"), bRedraw);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
