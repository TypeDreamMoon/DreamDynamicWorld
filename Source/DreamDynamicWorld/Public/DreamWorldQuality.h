// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamWorldQuality.generated.h"

/** The groups of Dream Dynamic World's features a quality level is chosen for, each following an engine scalability group. */
UENUM(BlueprintType)
enum class EDreamWorldQualityGroup : uint8
{
	/** The sky cloud renderer (resolution, samples, the sun maps, the flat clouds in its place), the light shafts, the
	 *  aurora, halos, meteors and noctilucent clouds. Follows sg.EffectsQuality. */
	Sky,
	/** The surface maps (wetness, snow, ice). Follows sg.TextureQuality (r.DreamSurface.Quality). */
	Surface,
	/** The weather's particles (rain, snow, dust, debris): how many. Follows sg.EffectsQuality. */
	Particles,
	/** The weather's volumetric fog. Follows sg.ShadowQuality (as the engine's volumetric fog does). */
	Fog,
	/** The wind field. Follows sg.EffectsQuality (r.DreamWind.Quality); the wind system takes its level. */
	Wind,
};

/** One console variable a quality level sets, and to what. */
struct FDreamWorldQualitySetting
{
	const TCHAR* Name;
	const TCHAR* Values[5];   // low, medium, high, epic, cinematic
};

/**
 * Dream Dynamic World's quality levels: for each group of features the engine's scalability level its group follows
 * (low 0 .. cinematic 4), or Dream Dynamic World's own (r.DreamWorld.Quality, -1 = the engine's). Each frame the world
 * clock looks at the levels and, when one changed, sets the console variables its group maps to (QualityTable) at the
 * scalability's priority -- so a value set by hand, in the console or an ini, still wins. Switching at run time swaps
 * settings in place: the clouds keep drawing (their history starts afresh), nothing goes blank.
 */
UCLASS()
class DREAMDYNAMICWORLD_API UDreamWorldQuality : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The level in use for a group, 0 low .. 4 cinematic. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Quality")
	static int32 GetLevel(EDreamWorldQualityGroup Group);

	/** Dream Dynamic World's own level for every group (0 low .. 4 cinematic), or -1: the engine's scalability. */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Quality")
	static void SetQualityOverride(int32 Level);

	UFUNCTION(BlueprintPure, Category = "Dream World|Quality")
	static int32 GetQualityOverride();

	/** Sets the console variables of every group whose level changed since the last call (all of them with bForce). */
	static void Apply(bool bForce = false);

	/** The console variables each group's levels set. */
	static TConstArrayView<FDreamWorldQualitySetting> QualityTable(EDreamWorldQualityGroup Group);
};
