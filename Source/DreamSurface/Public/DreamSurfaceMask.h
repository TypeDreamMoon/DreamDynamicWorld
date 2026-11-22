// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "DreamSurfaceMask.generated.h"

class UBoxComponent;

UENUM(BlueprintType)
enum class EDreamSurfaceMaskShape : uint8
{
	/** A box seen from above, turned with the component's yaw. */
	Box,
	/** A circle seen from above (Extent.X is its radius). */
	Circle,
};

/**
 * A brush that masks or strengthens what the weather leaves on the surfaces beneath it: no snow on a heated square, a
 * path kept clear, extra water in a gutter. It acts on the footprint from above, whatever the height (as the weather's
 * zones do), at full effect inside and fading over FadeDistance outside. Each amount is multiplied: 0 masks it out, 1
 * leaves it, above 1 strengthens it. Overlapping brushes multiply. SampleSurface and the surface maps both see it.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Surface Mask"))
class DREAMSURFACE_API UDreamSurfaceMaskComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamSurfaceMaskComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EDreamSurfaceMaskShape Shape = EDreamSurfaceMaskShape::Box;

	/** Half size of the footprint, cm (a circle's radius in X); the extent is unscaled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "1"))
	FVector2D Extent = FVector2D(300.0, 300.0);

	/** Distance outside the footprint over which the brush fades out, cm (a soft edge). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0"))
	float FadeDistance = 150.0f;

	/** What the wet film becomes beneath it: 0 masks it out, 1 leaves it, above 1 strengthens it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0", UIMax = "2"))
	float Wetness = 0.0f;

	/** What the standing water becomes beneath it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0", UIMax = "2"))
	float Water = 0.0f;

	/** What the lying snow becomes beneath it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0", UIMax = "2"))
	float Snow = 0.0f;

	/** What the settled dust becomes beneath it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0", UIMax = "2"))
	float Dust = 0.0f;

	/** How much of all that it does, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	/** The multipliers at a point (wet, water, snow, dust): 1 where the brush does not reach. */
	FVector4f GetMultipliersAt(const FVector& Position) const;

	/** The brush as the surface maps read it (three float4s, DreamSurfaceMap.usf). */
	void MakeMapRecord(TArray<FVector4f>& Out) const;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

/** A surface mask brush you can drop in a level. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Surface Mask"))
class DREAMSURFACE_API ADreamSurfaceMask : public AActor
{
	GENERATED_BODY()

public:
	ADreamSurfaceMask();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mask")
	TObjectPtr<UDreamSurfaceMaskComponent> Mask;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> Outline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};
