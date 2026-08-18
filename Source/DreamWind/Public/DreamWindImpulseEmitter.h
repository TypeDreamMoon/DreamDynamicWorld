// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWindTypes.h"
#include "DreamWindImpulseEmitter.generated.h"

class USphereComponent;

/**
 * Demo / test helper: fires a wind impulse at its own location every Interval seconds, in the editor
 * viewport as well as in game. Drop one into a level to watch grass, leaves and the debug arrows
 * answer a burst without writing any Blueprint.
 */
UCLASS(HideCategories = (Input, Rendering, Replication, Collision, HLOD, Physics, Networking, LevelInstance, DataLayers, Cooking))
class DREAMWIND_API ADreamWindImpulseEmitter : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindImpulseEmitter();

	/** Seconds between impulses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0.1"))
	float Interval = 4.0f;

	/** The impulse fired each time (its Position is replaced by the actor's location). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind")
	FDreamWindSourceDesc Impulse;

	/** How long each impulse lives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0"))
	float Duration = 0.4f;

	/** Fade-out at the end of each impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0"))
	float FadeOutTime = 0.25f;

	/** Also fire while the level is only being edited (viewport preview). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind")
	bool bFireInEditor = true;

	/** Fire one impulse now. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DreamWind")
	void Fire();

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bFireInEditor; }

private:
	float TimeToNext = 0.0f;

	UPROPERTY()
	TObjectPtr<USphereComponent> Sphere;
};
