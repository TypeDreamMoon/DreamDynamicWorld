// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldSharedTargets.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/WeakObjectPtr.h"

static TAutoConsoleVariable<int32> CVarDreamWorldSharedTargets(
	TEXT("DreamWorld.SharedTargets"),
	1,
	TEXT("1: one world at a time writes the shared render targets (a game or play-in-editor world over the editor's). 0: every world writes them (the old way, to compare: they flicker in play in editor)."),
	ECVF_Default);

namespace DreamWorldSharedTargetsImpl
{
	/** The owner the last question found, and the serial that changes with it (from 1: a writer's 0 is "never"). */
	TWeakObjectPtr<const UWorld> LastOwner;
	bool bHadOwner = false;
	uint32 Serial = 1;

	int32 Rank(const DreamWorld::SharedTargets::FCandidate& C)
	{
		switch (C.Type)
		{
		case EWorldType::Game: return 0;
		case EWorldType::PIE:  return 1;
		case EWorldType::Editor: return 2;
		default: return INDEX_NONE;
		}
	}

	/** Worlds the engine does not list that asked (a test's, a tool's), in the order they first asked. */
	TArray<TWeakObjectPtr<const UWorld>> PrivateWorlds;

	bool IsLive(const UWorld* World)
	{
		// (A destroyed world lingers until the garbage collector takes it; cleaned up, it is no longer initialised.)
		return IsValid(World) && World->bIsWorldInitialized && !World->bIsTearingDown;
	}

	/** The owner now among the engine's worlds and the private ones (null: none). World asks: a private one joins in. */
	const UWorld* FindOwner(const UWorld* World)
	{
		TArray<DreamWorld::SharedTargets::FCandidate, TInlineAllocator<8>> Candidates;
		TArray<const UWorld*, TInlineAllocator<8>> Worlds;
		bool bListed = false;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				const UWorld* Candidate = Context.World();
				if (!Candidate)
				{
					continue;
				}
				bListed |= Candidate == World;
				DreamWorld::SharedTargets::FCandidate& C = Candidates.AddDefaulted_GetRef();
				C.Type = Context.WorldType;
				C.PIEInstance = Context.WorldType == EWorldType::PIE ? Context.PIEInstance : INDEX_NONE;
				Worlds.Add(Candidate);
			}
		}
		if (!bListed && IsLive(World))
		{
			PrivateWorlds.AddUnique(World);
		}
		PrivateWorlds.RemoveAll([](const TWeakObjectPtr<const UWorld>& W) { return !IsLive(W.Get()); });
		for (const TWeakObjectPtr<const UWorld>& Private : PrivateWorlds)
		{
			DreamWorld::SharedTargets::FCandidate& C = Candidates.AddDefaulted_GetRef();
			C.Type = Private->WorldType;
			Worlds.Add(Private.Get());
		}
		const int32 Index = DreamWorld::SharedTargets::PickOwner(Candidates);
		return Worlds.IsValidIndex(Index) ? Worlds[Index] : nullptr;
	}
}

int32 DreamWorld::SharedTargets::PickOwner(TConstArrayView<FCandidate> Candidates)
{
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		const int32 R = DreamWorldSharedTargetsImpl::Rank(Candidates[i]);
		if (R == INDEX_NONE)
		{
			continue;
		}
		if (Best == INDEX_NONE)
		{
			Best = i;
			continue;
		}
		const int32 BestRank = DreamWorldSharedTargetsImpl::Rank(Candidates[Best]);
		// The better kind; among play-in-editor worlds the first instance; else the first listed.
		if (R < BestRank || (R == BestRank && Candidates[i].Type == EWorldType::PIE && Candidates[i].PIEInstance < Candidates[Best].PIEInstance))
		{
			Best = i;
		}
	}
	return Best;
}

bool DreamWorld::SharedTargets::Owns(const UWorld* World)
{
	if (!World)
	{
		return false;
	}
	if (CVarDreamWorldSharedTargets.GetValueOnGameThread() == 0)
	{
		return true;
	}
	const UWorld* Owner = DreamWorldSharedTargetsImpl::FindOwner(World);
	// (A world that passes on: the serial moves, so whoever has the targets next draws them afresh.)
	if (Owner != DreamWorldSharedTargetsImpl::LastOwner.Get() || (Owner != nullptr) != DreamWorldSharedTargetsImpl::bHadOwner)
	{
		DreamWorldSharedTargetsImpl::LastOwner = Owner;
		DreamWorldSharedTargetsImpl::bHadOwner = Owner != nullptr;
		++DreamWorldSharedTargetsImpl::Serial;
	}
	return Owner == World;
}

uint32 DreamWorld::SharedTargets::GetOwnerSerial()
{
	return DreamWorldSharedTargetsImpl::Serial;
}

bool DreamWorld::SharedTargets::FWriter::Acquire(const UWorld* World, bool& bOutRedrawAll)
{
	bOutRedrawAll = false;
	if (!Owns(World))
	{
		return false;
	}
	const uint32 Now = GetOwnerSerial();
	bOutRedrawAll = SeenSerial != Now;
	SeenSerial = Now;
	return true;
}
