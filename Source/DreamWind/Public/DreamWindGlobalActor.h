// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "DreamWindTypes.h"
#include "DreamWindGlobalActor.generated.h"

class UArrowComponent;
class UBillboardComponent;

/**
 * Level authoring for the global wind: place one per level. Its arrow shows the direction, and its
 * settings are pushed to the wind subsystem when the level loads and whenever they are edited, so
 * the viewport shows the result live. Overrides Project Settings > Moon Wind > Default Wind.
 * The actor's rotation is ignored: edit Wind.Direction.
 */
UCLASS(HideCategories = (Actor, Input, Rendering, Replication, Collision, HLOD, Physics, Networking, LevelInstance, DataLayers, Cooking))
class DREAMWIND_API ADreamWindGlobal : public AInfo
{
	GENERATED_BODY()

public:
	ADreamWindGlobal();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ShowOnlyInnerProperties))
	FDreamWindGlobalWind Wind;

	/** Seconds to blend from whatever wind is in effect when this actor applies (0 = snap). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0"))
	float BlendTime = 0.0f;

	/** Push the wind to the subsystem now. */
	UFUNCTION(BlueprintCallable, Category = "DreamWind")
	void ApplyToWorld();

	//~ AActor
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
#endif

private:
	void UpdateArrow();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UArrowComponent> Arrow;
#endif
};
