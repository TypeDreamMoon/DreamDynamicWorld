// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamCloudCarverComponent.generated.h"

/**
 * Cuts a passage through the cloud volumes it moves through: put it on a character, a plane, a ship. Each frame the
 * capsule it swept since the last one is carved out of every carvable volume it touches (with a noisy edge); the
 * volume grows back over its RefillTime once the carver has gone.
 */
UCLASS(ClassGroup = (DreamDynamicWorld), meta = (BlueprintSpawnableComponent))
class DREAMCLOUD_API UDreamCloudCarverComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** How wide a passage it leaves (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud", meta = (ClampMin = "1", UIMax = "2000"))
	float Radius = 200.0f;

	/** How fully it clears the cloud (1 = a clean passage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud")
	bool bCarving = true;

	/** Where it was when it last carved (the subsystem's), to carve the whole path between frames. */
	TOptional<FVector> LastCarved;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
