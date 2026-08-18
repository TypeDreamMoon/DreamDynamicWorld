// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DreamWindTypes.h"
#include "DreamWindPreset.generated.h"

/**
 * A named global-wind state (calm, breeze, storm, "the wind of act 3"...). Weather and gameplay code
 * blend between presets with UDreamWindSubsystem::ApplyPreset; ADreamWindGlobal can be pinned to one.
 * The plugin ships DWP_Calm / DWP_Breeze / DWP_Storm under /DreamWind/Presets (Tools > DreamWind:
 * Setup Assets recreates them).
 */
UCLASS(BlueprintType)
class DREAMWIND_API UDreamWindPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ShowOnlyInnerProperties))
	FDreamWindGlobalWind Wind;

	/** Free text for the designer: what this wind is for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (MultiLine = "true"))
	FText Description;
};
