// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWindSliceActor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/** What a wind slice shows. */
UENUM(BlueprintType)
enum class EDreamWindSliceMode : uint8
{
	/** The whole wind: hue = direction, brightness = speed. */
	Wind = 0,
	/** The terrain map alone: how much wind its shadow leaves, where it speeds the wind up or slows it. */
	Terrain = 1,
	/** The dynamic volume alone: its residual, and the near window's edge. */
	Volume = 2,
	/** The engine's global distance field (what the fluid wind's obstacles come from): red on surfaces, blue far off. */
	DistanceField = 3,
};

/**
 * A debug plane that paints one layer of the wind field where it stands (M_DreamWindSlice), or a slice of the global
 * distance field (M_DreamWindProbeSlice): horizontal, or vertical along its yaw. It can follow the camera at a fixed
 * height. Seen in the 3D view and in screenshots (the debug arrows are not). Console: DreamWind.Slice.
 */
UCLASS(HideCategories = (Input, Replication, Collision, HLOD, Physics, Networking, LevelInstance, DataLayers, Cooking))
class DREAMWIND_API ADreamWindSliceActor : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindSliceActor();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice")
	EDreamWindSliceMode Mode = EDreamWindSliceMode::Wind;

	/** Width of the plane, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice", meta = (ClampMin = "100"))
	float Size = 6000.0f;

	/** Stand upright (a vertical slice across its yaw) instead of lying flat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice")
	bool bVertical = false;

	/** Keep centred under the camera (x, y), at the actor's own height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice")
	bool bFollowCamera = false;

	/** Wind speed (cm/s) that maps to full brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice", meta = (ClampMin = "1"))
	float SpeedForFullBrightness = 1200.0f;

	/** Distance field slice: texels per side, and how often it is written again (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice|Distance Field", meta = (ClampMin = "16", ClampMax = "512"))
	int32 ProbeResolution = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice|Distance Field", meta = (ClampMin = "0.05"))
	float ProbeInterval = 0.5f;

	/** Distance (m) that maps to full blue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Slice|Distance Field", meta = (ClampMin = "0.1"))
	float ProbeRange = 5.0f;

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void Refresh();

	UPROPERTY(VisibleAnywhere, Category = "Wind Slice")
	TObjectPtr<UStaticMeshComponent> Plane;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Probe;

	EDreamWindSliceMode MaterialMode = EDreamWindSliceMode::Wind;
	bool bHasMaterial = false;
	float SinceProbe = 1.0e9f;
};
