// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWeatherTypes.h"
#include "DreamWorldSequencerSubsystem.generated.h"

class UDreamWeatherSubsystem;

/**
 * Which weather modifier each active weather section of each playing sequence stands for. The modifiers
 * themselves live in the weather's stack (saved, replicated one day, inspected like any other); this only keeps
 * their handles.
 */
UCLASS()
class DREAMWORLDSEQUENCER_API UDreamWorldSequencerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDreamWorldSequencerSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Apply what Section stands for in the sequence instance Instance at weight Desc.Weight: push its modifier the
	 * first time, update it after.
	 */
	void ApplyModifier(const UObject* Section, const void* Instance, const FDreamWeatherModifierDesc& Desc);

	/** The section ended: its modifier goes. */
	void ReleaseModifier(const UObject* Section, const void* Instance);

	/** Modifiers held for sections now. */
	int32 GetNumHeld() const { return Held.Num(); }

private:
	TMap<TPair<const void*, const void*>, FDreamWeatherModifierHandle> Held;
};
