// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "DreamGroundFog.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;

namespace DreamWeatherFX::GroundFog
{
	/** The ground fog's material, marched over the fog component's box. */
	inline const TCHAR* MaterialPath = TEXT("/DreamDynamicWorld/Weather/M_DreamGroundFog.M_DreamGroundFog");

	/** A fog actor's Density (and MistDensity) times this is the marched fog's extinction per metre: 1 sees some 75 m. */
	constexpr float ExtinctionPerDensity = 0.04f;

	/**
	 * The fog actors draw their fog marched as a volume (DreamWeather.Fog.Volumetric 1, the default from the medium
	 * quality level up) or with the engine's local fog volumes (0: the low level's, the cheaper look).
	 */
	DREAMWEATHERFX_API bool UseVolumetric();

	/** How many fine steps a ray through the fog takes (DreamWeather.Fog.Samples, by the quality level). */
	DREAMWEATHERFX_API int32 Samples();
}

/** What a fog actor tells its fog volume each frame. */
struct FDreamGroundFogParams
{
	/** 0..1: the pooled fog (filling each hollow toward its depth in the layer) and the mist over it all. */
	float Radiation = 0.0f;
	/** 0..1: the fog over all the low ground, up to Lowest + Advection * AdvectionDepth. */
	float Advection = 0.0f;
	/** The lowest ground, cm above the layer's origin. */
	float Lowest = 0.0f;
	float AdvectionDepth = 0.0f;
	/** The mist's thickness at full radiation fog, cm. */
	float MistThickness = 0.0f;
	/** Extinction per metre of the pooled fog and of the mist. */
	float PoolExtinction = 0.06f;
	float MistExtinction = 0.015f;
	/** The noise that breaks the fog up: its size (cm), how patchy (0..1), its churn upward (cm/s). */
	float NoiseSize = 1500.0f;
	float NoiseAmount = 0.8f;
	float Churn = 8.0f;
	/** The share of the layer at its rim the fog fades out over. */
	float EdgeFade = 0.08f;
	/** Multiply the main light and the sky light. */
	float SunStrength = 1.0f;
	float AmbientStrength = 1.0f;
	/** The box drawn: its lowest and highest z, world cm (the actor keeps it round the fog there is). */
	float BoxLow = 0.0f;
	float BoxHigh = 100.0f;
};

/**
 * Ground fog marched as a participating medium over a box (M_DreamGroundFog, on the clouds' march and light): a layer
 * over the ground the owning actor hands in (SetLayer: the ground's height, how deep a hollow is, where there is ground,
 * on a grid), filled as deep as the fog's state says, broken up by the clouds' noise drifting with the wind, lit by the
 * main light through the fog above and by the sky. The engine's unit cube is drawn over the box, back faces only with
 * the depth test off, so the fog shows from outside the box and from in it and the scene cuts it where it stands. No
 * limit on how many draw (each is a translucent draw over the part of the screen its box covers).
 */
UCLASS(ClassGroup = Rendering, meta = (BlueprintSpawnableComponent))
class DREAMWEATHERFX_API UDreamGroundFogComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UDreamGroundFogComponent(const FObjectInitializer& ObjectInitializer);

	/**
	 * The layer: Cells x Cells values (row by row from the -X -Y corner) over Origin.xy +- Area. Heights world Z (NoGround
	 * where there is none), Hollows how deep the pooled fog may get there (cm).
	 */
	void SetLayer(const FVector& Origin, const FVector2D& Area, int32 Cells, TConstArrayView<float> Heights, TConstArrayView<float> Hollows, float NoGround);

	/** A flat layer at Origin.z, no hollows (a fog bank: its mist alone). */
	void SetFlatLayer(const FVector& Origin, const FVector2D& Area);

	/** The fog now, and the box round it; hides the component when there is none. */
	void SetFog(const FDreamGroundFogParams& Params);

	/** The material drawn (an instance of M_DreamGroundFog), once loaded. */
	UMaterialInstanceDynamic* GetFogMaterial() const { return FogMaterial; }

	/** The layer's map (r ground from the origin, g hollow, b ground there). */
	UTexture2D* GetLayerMap() const { return LayerMap; }

	/** The last parameters set. */
	const FDreamGroundFogParams& GetFogParams() const { return Params; }

private:
	void EnsureMaterial();
	void UploadLayer(int32 Cells, const TArray<FFloat16Color>& Texels);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FogMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LayerMap;

	FDreamGroundFogParams Params;
	FVector LayerOrigin = FVector::ZeroVector;
	FVector2D LayerArea = FVector2D(100.0, 100.0);
};
