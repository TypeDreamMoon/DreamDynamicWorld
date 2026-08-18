// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamWindTypes.h"
#include "DreamWindSourceComponent.generated.h"

/**
 * A persistent wind source that follows the component's transform: fans, vents, a character's wake,
 * a spinning attack. Directional sources push along the component's +X axis, vortices swirl around it.
 * The source is blended into the CPU sample and injected into the dynamic volume every frame it is
 * active.
 */
UCLASS(ClassGroup = (DreamWind), meta = (BlueprintSpawnableComponent), HideCategories = (Mobility, LOD, Collision, Physics, Rendering))
class DREAMWIND_API UDreamWindSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamWindSourceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind")
	EDreamWindSourceType Type = EDreamWindSourceType::Directional;

	/** Footprint radius, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "1"))
	float Radius = 400.0f;

	/** Velocity the source wants at full weight, cm/s. Negative pulls (Directional) or reverses the swirl. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind")
	float Strength = 1200.0f;

	/** How fast the dynamic volume adopts the source's velocity, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", UIMax = "60"))
	float InjectRate = 15.0f;

	/** Fraction of the radius that receives full weight before the falloff starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", ClampMax = "1"))
	float InnerRadiusFraction = 0.2f;

	/**
	 * Scale the strength by the component's own world velocity along its push direction: a wake that
	 * only exists while the owner moves. 0 = ignore movement.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0"))
	float VelocityScale = 0.0f;

	/** Only sources whose owner is within this distance of the volume centre are injected. 0 = always. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0"))
	float CullDistance = 0.0f;

	/** Build the source record for this frame. Returns false when the source contributes nothing. */
	bool BuildSourceDesc(FDreamWindSourceDesc& OutDesc, float DeltaTime);

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
};
