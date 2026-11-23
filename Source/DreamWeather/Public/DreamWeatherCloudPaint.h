// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "DreamWeatherCloudPaint.generated.h"

class UBoxComponent;
class UTexture2D;

/**
 * Cloud cover painted by hand: a square grid of the cover to pull the weather's toward, and how far to pull it, over a
 * footprint a UDreamCloudPaintComponent puts in the world. The weather map draws it (DreamWeatherMap.usf) and the
 * weather's samples read it (SampleWeather), the same bilinear lookup both ways, over the zones: where it is painted it
 * has the last word on the cloud cover, and the precipitation follows the cover as everywhere (none under a clear sky).
 *
 * Painted in the editor (Dream World > Paint Cloud Cover), or from code with PaintAt / Fill.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dream Cloud Painting"))
class DREAMWEATHER_API UDreamCloudPainting : public UObject
{
	GENERATED_BODY()

public:
	/** Texels along each side. Change it with Resize (which keeps the painting). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cloud Paint")
	int32 Resolution = 256;

	/** The cover painted, 0 clear .. 255 overcast, Resolution x Resolution, rows from the footprint's -X edge (south). */
	UPROPERTY()
	TArray<uint8> Cover;

	/** How far each texel pulls the weather's cover to its own, 0 not at all .. 255 all the way. */
	UPROPERTY()
	TArray<uint8> Amount;

	/** A blank painting (nothing pulled) of a resolution. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Cloud Paint")
	void Initialize(int32 InResolution);

	/** Resamples the painting to another resolution. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Cloud Paint")
	void Resize(int32 InResolution);

	/** Sets every texel. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Cloud Paint")
	void Fill(float InCover, float InAmount);

	/**
	 * A brush stroke at a point of the footprint (UV 0..1, U along X, V along Y): within Radius (UV) the cover moves
	 * toward TargetCover and the amount up, by Strength at the middle falling smoothly to nothing at the rim. With
	 * bErase the amount goes down instead (the weather's own cover comes back). Returns whether anything changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Cloud Paint")
	bool PaintAt(FVector2D UV, float Radius, float TargetCover, float Strength, bool bErase = false);

	/** The cover and amount at a point of the footprint (UV 0..1), 0..1 each, bilinear between texel centres as the GPU
	 *  filters the texture; nothing (0, 0) outside it. */
	FVector2f Sample(FVector2D UV) const;

	/** True once Initialize has made the grids (and they match the resolution). */
	bool IsValidPainting() const;

	/** The painting as a texture (R cover, G amount), made and kept up to date on asking. */
	UTexture2D* GetTexture();

	/** Bumped by every change (a texture or a map drawn from an older one is stale). */
	uint32 GetRevision() const { return Revision; }

	/** Call after changing Cover / Amount by hand. */
	void MarkChanged();

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

private:
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Texture;
	uint32 Revision = 1;
	uint32 TextureRevision = 0;
};

/**
 * Puts a cloud painting in the world: its footprint is a square of Size centred on the component (axis-aligned; the
 * component's rotation is ignored). The weather uses one painting at a time, the latest one registered that is enabled
 * and has a valid painting.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Cloud Paint"))
class DREAMWEATHER_API UDreamCloudPaintComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamCloudPaintComponent();

	/** The painting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Paint")
	TObjectPtr<UDreamCloudPainting> Paint;

	/** The footprint's side, cm (default 40 km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Paint", meta = (ClampMin = "1000"))
	double Size = 4000000.0;

	/** Scales every texel's amount: 0 turns the painting off, 1 as painted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Paint", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud Paint")
	bool bEnabled = true;

	/** A world position's place on the footprint (0..1 inside it). */
	FVector2D WorldToUV(const FVector& World) const;

	/** A place on the footprint (UV) in the world, at the component's height. */
	FVector UVToWorld(const FVector2D& UV) const;

	/** The footprint: min x, min y, 1 / size, 1 / size (what the map shader takes). */
	FVector4f GetFootprintRecord() const;

	/** Whether the weather can use it (enabled, a strength, a valid painting). */
	bool IsUsable() const;

	/** Pulls InOut's cloud cover toward the painting at a position (what the weather map draws there). */
	void ApplyTo(const FVector& Position, float& InOutCover) const;

	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

/** A cloud painting placed in the level (see UDreamCloudPaintComponent); the box shows its footprint in the editor. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Cloud Paint"))
class DREAMWEATHER_API ADreamCloudPaint : public AActor
{
	GENERATED_BODY()

public:
	ADreamCloudPaint();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cloud Paint")
	TObjectPtr<UDreamCloudPaintComponent> CloudPaint;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> Footprint;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};
