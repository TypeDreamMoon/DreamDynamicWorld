// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UWorld;

/**
 * Who may write the plugin's shared render targets (RT_DreamWeatherMap, the surface maps, the wind's volumes and terrain
 * map, the clouds' panorama): assets, one each for the whole engine, while every world (the editor's, a play-in-editor
 * one) keeps its own clock, weather and view and its own material parameter collection. Two worlds writing one target
 * would each draw over the other's frame, and each world's materials would read whichever drew last. So one world owns
 * them at a time:
 *   - a game world (a standalone game) first;
 *   - else the first play-in-editor world (the lowest instance; Simulate is one too);
 *   - else the editor world.
 * Every writer asks before it draws (FWriter) and, when the targets come back to it (play-in-editor ended), draws them
 * all afresh: they still hold what the other world drew. A world the engine does not list (a test's, a tool's) takes
 * part once it asks, by its kind: a test's game world has the targets over the editor world, as a game world would.
 * Runtime only: the engine's world contexts, no editor module.
 */
namespace DreamWorld::SharedTargets
{
	/** A world the engine runs, as the rule sees it. */
	struct FCandidate
	{
		EWorldType::Type Type = EWorldType::None;
		/** Play-in-editor instance (INDEX_NONE for the others). */
		int32 PIEInstance = INDEX_NONE;
	};

	/** The rule: the candidate that owns the targets (its index), or INDEX_NONE when none may (previews only, say). */
	DREAMDYNAMICWORLD_API int32 PickOwner(TConstArrayView<FCandidate> Candidates);

	/** Whether World owns the targets now. */
	DREAMDYNAMICWORLD_API bool Owns(const UWorld* World);

	/** Changes every time the targets pass to another world. */
	DREAMDYNAMICWORLD_API uint32 GetOwnerSerial();

	/** A writer of shared targets (one per world's writer). */
	struct DREAMDYNAMICWORLD_API FWriter
	{
		/**
		 * Whether the writer's world may write the targets this frame. bOutRedrawAll: the first time it may since another
		 * world had them (or ever): draw everything afresh, nothing in the targets is its own.
		 */
		bool Acquire(const UWorld* World, bool& bOutRedrawAll);

		/** Acquire, the redraw flag left to the caller to ignore (a writer that draws everything every time anyway). */
		bool Acquire(const UWorld* World)
		{
			bool bRedrawAll = false;
			return Acquire(World, bRedrawAll);
		}

	private:
		uint32 SeenSerial = 0;
	};
}
