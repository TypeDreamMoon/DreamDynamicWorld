// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamWindPassThroughComponent.generated.h"

/**
 * Marks an actor as transparent to the wind: the terrain / occlusion bake ignores its collision, so
 * it neither shades the wind behind it nor counts as ground. Put it on fences, foliage, thin props,
 * anything whose collision would otherwise cast a wind shadow. The actor tag "DreamWind.PassThrough"
 * does the same without a component. Re-bake the terrain map after adding or removing either.
 */
UCLASS(ClassGroup = (DreamWind), meta = (BlueprintSpawnableComponent, DisplayName = "DreamWind Pass Through"), HideCategories = (Activation, Collision, Cooking, Tags, AssetUserData, Replication, Navigation))
class DREAMWIND_API UDreamWindPassThroughComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamWindPassThroughComponent();

	/** Actor tag that means the same as this component. */
	static const FName PassThroughTag;

	/** True when the actor carries this component or the tag. */
	static bool IsPassThrough(const AActor* Actor);
};
