// Copyright DreamMoon. All Rights Reserved.
//
// Small placeable actors for demos and tests: a wind source with a visual, and a procedural field of
// wind-reactive grass. Neither is required by the runtime; both are handy for seeing the wind.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWindTypes.h"
#include "DreamWindDemoActors.generated.h"

class UDreamWindSourceComponent;
class UInstancedStaticMeshComponent;
class USphereComponent;
class UArrowComponent;
class UStaticMesh;
class UMaterialInterface;

/** A persistent wind source you can place: fan lane, updraft, vortex. The +X axis is the push / swirl axis. */
UCLASS(HideCategories = (Input, Rendering, Replication, Collision, HLOD, Physics, Networking, LevelInstance, DataLayers, Cooking))
class DREAMWIND_API ADreamWindSourceActor : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindSourceActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DreamWind")
	TObjectPtr<UDreamWindSourceComponent> Source;

	/** Draws the footprint radius. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DreamWind")
	TObjectPtr<USphereComponent> RadiusVisual;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UArrowComponent> Arrow;
#endif

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
};

/**
 * A field of grass blades (one instanced static mesh, deterministic scatter) using the DreamWind grass
 * material. Rebuilds whenever a property changes; keep Count moderate in the editor.
 * The instanced component ships with Affect Dynamic Indirect Lighting / Distance Field Lighting off:
 * dense instanced foliage in Lumen's surface cache costs hundreds of card captures per frame near the
 * ground. Do the same on your own wind foliage.
 */
UCLASS(HideCategories = (Input, Replication, Collision, HLOD, Physics, Networking, LevelInstance, DataLayers, Cooking))
class DREAMWIND_API ADreamWindGrassField : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindGrassField();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DreamWind")
	TObjectPtr<UInstancedStaticMeshComponent> Grass;

	/** Blade mesh; the default engine cone works with the material's bounds-based tip mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	TObjectPtr<UStaticMesh> Mesh;

	/** Material; null = M_DreamWindGrass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field", meta = (ClampMin = "0", ClampMax = "200000"))
	int32 Count = 6000;

	/** Half size of the square the blades are scattered over, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field", meta = (ClampMin = "10"))
	float Extent = 6000.0f;

	/** Uniform XY scale range of a blade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	FVector2D WidthScale = FVector2D(0.08, 0.14);

	/** Z scale range of a blade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	FVector2D HeightScale = FVector2D(0.9, 1.6);

	/** Blades are not placed inside these world-space boxes (leave room for a debug plane, a road...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	TArray<FBox> Exclusions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	int32 Seed = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind|Field")
	bool bCastShadow = false;

	/** Scatter the blades again. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "DreamWind|Field")
	void Rebuild();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
};
