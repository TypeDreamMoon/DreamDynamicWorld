// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamWeatherShelter.h"
#include "GameFramework/Actor.h"
#include "DreamSurfaceRoofCapture.generated.h"

class UBoxComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * A box that captures, from above, the height of whatever stands inside it - roofs, a canopy, a bridge deck - so that
 * what lies beneath stays dry and free of snow, with no shelter box to place by hand. It captures when it registers in a
 * game world, when it moves, and on CaptureNow; the heights go to the surface maps' cover (materials) and are read back
 * once per capture, so the box is a weather shelter too: queries of the weather (exposure, rain landing), the surfaces
 * and the rain's fall paths all see it.
 *
 * Like the wind's baked heights it takes the top surface: a point below what was captured above it (less the roof
 * clearance) is covered, and the roof itself, at the captured height, is in the open.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Surface Roof Capture"))
class DREAMSURFACE_API UDreamSurfaceRoofCaptureComponent : public USceneComponent, public IDreamWeatherShelterProvider
{
	GENERATED_BODY()

public:
	UDreamSurfaceRoofCaptureComponent();

	/** Half size of the box, cm (unscaled; turned with the component's yaw). What stands inside it is captured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof")
	FVector Extent = FVector(500.0, 500.0, 400.0);

	/** Texels across the box (the capture is square, at the box's longer side). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "16", ClampMax = "1024"))
	int32 Resolution = 128;

	/** 0..1: how much the captured roofs keep out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	/** Capture the heights again now (after the roofs changed). */
	UFUNCTION(BlueprintCallable, Category = "Dream Surface")
	void CaptureNow();

	/** The captured height (world cm) over a point, or a very low number where nothing was captured or outside the box. */
	float GetHeightAt(double WorldX, double WorldY) const;

	/** Captures so far (tests). */
	int32 GetCaptureCount() const { return CaptureCount; }

	/** The capture's render target (R = depth below the camera, cm), null before the first capture. */
	UTextureRenderTarget2D* GetCaptureTarget() const { return Target; }

	/** World Z the capture looks down from, and the size of the square it covers. */
	double GetCaptureZ() const;
	double GetCaptureSize() const { return FMath::Max(Extent.X, Extent.Y) * 2.0; }

	/** Sets the heights directly, row-major, Resolution square (tests; a game captures them). */
	void SetHeightsForTest(const TArray<float>& InHeights);

	//~ IDreamWeatherShelterProvider
	virtual float GetShelterAt(const FVector& Position) const override;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;

private:
	void ReadBack();

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Target;

	/** The heights read back, row-major (world cm; very low where nothing stands). */
	TArray<float> Heights;
	int32 HeightsSize = 0;
	FTransform HeightsFrame;
	int32 CaptureCount = 0;
	bool bCapturePending = false;
};

/** A roof capture box you can drop in a level. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Surface Roof Capture"))
class DREAMSURFACE_API ADreamSurfaceRoofCapture : public AActor
{
	GENERATED_BODY()

public:
	ADreamSurfaceRoofCapture();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	TObjectPtr<UDreamSurfaceRoofCaptureComponent> Roof;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> Outline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};
