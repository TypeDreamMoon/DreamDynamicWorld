// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamWindTypes.h"
#include "DreamWindSourceComponent.generated.h"

/**
 * A persistent wind source that follows the component's transform: fans, vents, a character's wake,
 * a spinning attack, a sheltered room. Directional sources push along the component's +X axis,
 * vortices swirl around it, shelters are a box in the component's frame (BoxExtent x the component
 * scale). The source is blended into the CPU sample and injected into the dynamic volume every frame
 * it is active.
 */
UCLASS(ClassGroup = (DreamWind), meta = (BlueprintSpawnableComponent), HideCategories = (Mobility, LOD, Collision, Physics, Rendering))
class DREAMWIND_API UDreamWindSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamWindSourceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind")
	EDreamWindSourceType Type = EDreamWindSourceType::Directional;

	/** Footprint radius, cm (sphere types). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "1", EditCondition = "Type != EDreamWindSourceType::Shelter"))
	float Radius = 400.0f;

	/**
	 * Length of the footprint along +X as a multiple of Radius: 1 = sphere, 4 = a jet four radii long.
	 * Directional and Vortex only. Radius x Stretch is how far a fan reaches; Strength is how hard.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "1", UIMax = "10", EditCondition = "Type == EDreamWindSourceType::Directional || Type == EDreamWindSourceType::Vortex || Type == EDreamWindSourceType::Twister"))
	float Stretch = 1.0f;

	/** A Twister (its axis the component's Z, standing on the component): the rise in its core and the inflow along the ground, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", EditCondition = "Type == EDreamWindSourceType::Twister"))
	float Updraft = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", EditCondition = "Type == EDreamWindSourceType::Twister"))
	float Inflow = 0.0f;

	/** Half extents of a Shelter box in the component's local frame, cm; scaled by the component scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (EditCondition = "Type == EDreamWindSourceType::Shelter"))
	FVector BoxExtent = FVector(500.0, 500.0, 300.0);

	/**
	 * Velocity the source wants at full weight, cm/s. Negative pulls (Directional) or reverses the swirl.
	 * For a Shelter, 0 = calm interior, > 0 = a wind zone blowing along +X.
	 */
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
