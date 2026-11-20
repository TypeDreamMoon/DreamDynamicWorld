// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "DreamCloudField.h"
#include "DreamCloudVolumeComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextureRenderTargetVolume;
class UVolumeTexture;

/** Where a cloud volume's shape comes from. */
UENUM(BlueprintType)
enum class EDreamCloudShapeSource : uint8
{
	/** A few spheres, boxes and capsules blended together. */
	Primitives,
	/** A static mesh (closed meshes work best). */
	Mesh,
};

/** How a cloud volume is drawn. */
UENUM(BlueprintType)
enum class EDreamCloudLook : uint8
{
	/** A participating medium: light scattered through it, its own shadow, soft edges. */
	Realistic,
	/** A surface painted in a few tones following its lobes, the edges clean. */
	Toon,
	/** Between the two: painted tones over a soft volume (the sky's painted look). */
	Painted,
};

/**
 * A cloud standing in the level: its shape (a mesh, or primitives) baked to a signed distance field, drawn by marching
 * that field inside the field's box with a translucent material (M_DreamCloudVolume). The view can be inside it; scene
 * geometry inside the box cuts the march (soft contact), and the cloud is fogged per pixel.
 *
 * Only a new shape bakes the field again (another mesh, other primitives, another resolution); moving, turning and
 * scaling the component only moves the box it is drawn in, so the cloud follows at once. The box is drawn with the
 * engine's unit cube placed over the field (GetRenderMatrix), and the material marches in the shape's space.
 */
UCLASS(ClassGroup = (DreamDynamicWorld), meta = (BlueprintSpawnableComponent),
	HideCategories = (StaticMesh, Materials, Physics, Collision, Lighting, HLOD, Navigation, VirtualTexture, MaterialParameters, Mobile, RayTracing, TextureStreaming))
class DREAMCLOUD_API UDreamCloudVolumeComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UDreamCloudVolumeComponent(const FObjectInitializer& ObjectInitializer);

	// ---- Shape ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Shape")
	EDreamCloudShapeSource ShapeSource = EDreamCloudShapeSource::Primitives;

	/** The mesh the cloud takes the shape of, in the component's space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Shape", meta = (EditCondition = "ShapeSource == EDreamCloudShapeSource::Mesh", EditConditionHides))
	TObjectPtr<UStaticMesh> ShapeMesh;

	/** The primitives the cloud is made of, in the component's space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Shape", meta = (EditCondition = "ShapeSource == EDreamCloudShapeSource::Primitives", EditConditionHides))
	TArray<FDreamCloudPrimitiveShape> Primitives;

	/** How far the seams between primitives are rounded over (cm): 0 = a hard union. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Shape", meta = (ClampMin = "0", UIMax = "2000", EditCondition = "ShapeSource == EDreamCloudShapeSource::Primitives", EditConditionHides))
	float PrimitiveBlend = 200.0f;

	/** Texels along the field's longest axis (more = finer shape, slower to bake, more memory: 128^3 half floats are 4 MB). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Shape", meta = (ClampMin = "16", ClampMax = "256", UIMin = "32", UIMax = "128"))
	int32 Resolution = 96;

	/** Room around the shape inside the field's box, as a share of its largest size: the cloud may grow past the shape
	 *  by Inflate and its noise only within it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Shape", meta = (ClampMin = "0", UIMax = "0.5"))
	float Padding = 0.15f;

	// ---- Appearance ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance")
	EDreamCloudLook Look = EDreamCloudLook::Realistic;

	/** How far the cloud reaches past the shape's surface (cm; < 0 shrinks it inside). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (UIMin = "-500", UIMax = "1000"))
	float Inflate = 0.0f;

	/** Over how many cm from its surface inward the cloud's density rises to full (soft, smoky edges when large). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (ClampMin = "1", UIMax = "2000"))
	float EdgeSoftness = 50.0f;

	/** Extinction of the cloud at full density, per metre (a real cumulus: some 0.04 to 0.2, but a cloud a few tens of metres across needs more to read as one). Realistic and painted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (ClampMin = "0.001", UIMax = "5"))
	float Extinction = 1.5f;

	/** Multiplies the sunlight the cloud scatters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (ClampMin = "0", UIMax = "4"))
	float SunStrength = 1.5f;

	/** Multiplies the sky light the cloud scatters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (ClampMin = "0", UIMax = "4"))
	float AmbientStrength = 1.0f;

	/** Tints the lit side (multiplies the sky style's lit cloud colour in the toon look, the sunlight otherwise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance")
	FLinearColor LitTint = FLinearColor::White;

	/** Tints the shaded side (multiplies the sky style's cloud shade colour in the toon look, the sky light otherwise). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance")
	FLinearColor ShadeTint = FLinearColor::White;

	/** Toon: the size of the lobes the tones follow (cm): the normal is taken this far around a point, so the noise's
	 *  small bumps do not break the tones up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (ClampMin = "1", UIMax = "2000"))
	float LobeScale = 200.0f;

	/** Toon: how soft the edges between the tones are (in the light's key, 0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Appearance", meta = (ClampMin = "0", UIMax = "0.3"))
	float ToneSoftness = 0.03f;

	// ---- Noise ----

	/** The size of the noise that erodes the cloud's edges (cm a tile; the finest billows are an eighth of it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Noise", meta = (ClampMin = "10", UIMax = "20000"))
	float NoiseScale = 1500.0f;

	/** How deep the noise eats into the cloud (cm): 0 = the shape's own smooth surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Noise", meta = (ClampMin = "0", UIMax = "2000"))
	float NoiseErosion = 250.0f;

	/** How the noise drifts through the cloud (cm/s, in the component's space): the cloud churns in place. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Noise")
	FVector NoiseDrift = FVector(20.0, 0.0, 5.0);

	// ---- Quality ----

	/** Multiplies the march's step inside the cloud (half a texel at 1): larger = faster and coarser. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Quality", meta = (ClampMin = "0.25", UIMax = "4"))
	float StepScale = 1.0f;

	/** How far the light may turn within the volume (degrees) before its light map is drawn again. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Quality", meta = (ClampMin = "0.1", UIMax = "10"))
	float LightUpdateAngle = 1.0f;

	// ---- Carving ----

	/** Whether carvers (UDreamCloudCarverComponent) cut passages through this cloud. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Carving")
	bool bCarvable = true;

	/** How long a passage stays open after the carver left (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Carving", meta = (ClampMin = "0", UIMax = "30"))
	float HoldTime = 2.0f;

	/** How long a passage then takes to grow back (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Carving", meta = (ClampMin = "0.1", UIMax = "60"))
	float RefillTime = 8.0f;

	/** How ragged a passage's walls are (0 = smooth tubes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Carving", meta = (ClampMin = "0", UIMax = "1"))
	float HoleEdgeNoise = 0.35f;

	/** Texels of the carving grid along the box's longest axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud|Carving", meta = (ClampMin = "8", ClampMax = "128"))
	int32 HoleResolution = 48;

	// ---- The baked field ----

	/** The shape baked (see FDreamCloudField). Baked again whenever the shape changes. */
	UPROPERTY(VisibleAnywhere, Category = "Cloud|Shape", AdvancedDisplay)
	FDreamCloudField Field;

	/** Bakes the field from the shape now (a cloud whose shape a game changes at run time calls this). */
	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	bool Rebake();

	/** Whether the shape changed since the field was baked. */
	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	bool NeedsBake() const;

	/** Pushes the appearance, noise and quality settings to the material (after changing them at run time). */
	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	void UpdateCloudMaterial();

	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	UVolumeTexture* GetFieldTexture() const { return FieldTexture; }

	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	UMaterialInstanceDynamic* GetCloudMaterial() const { return CloudMaterial; }

	/** The box the field fills, in the component's space. */
	FBox GetFieldBox() const;

	/** Cuts the capsule from Start to End (world) out of the cloud, Strength 0..1 of a clean passage. */
	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	void Carve(FVector WorldStart, FVector WorldEnd, float Radius, float Strength = 1.0f);

	/** How much is carved out at a world point (0 = untouched, 1 = a clean passage). */
	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	float GetCarving(FVector WorldPoint) const;

	/** Lets every passage close at once. */
	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	void ClearCarving();

	/** The carving grid (values above 1 are passages still held open), and its size. */
	TConstArrayView<float> GetHoles() const { return Holes; }
	FIntVector GetHoleGridSize() const { return HoleGridSize; }

	/** The light map's settings for a light direction (world) now: what the subsystem draws it with. */
	struct FLightState
	{
		FVector3f Direction = FVector3f::ZeroVector;
		float SigmaPerUnit = 0.0f;
		float Inflate = 0.0f;
		float Edge = 0.0f;
	};
	FLightState GetLightState(const FVector& LightDirection) const;

	/** How many times the light map was asked to be drawn (it is drawn on the GPU when the world renders). */
	int32 GetLightMapUpdates() const { return LightMapUpdates; }

	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	UTextureRenderTargetVolume* GetLightMap() const { return LightMap; }

	/** The subsystem's per-frame update: holes grow back, the light map follows the light, the steps thin out while
	 *  the cloud fills much of the view (ViewLocation, when there is a view). */
	void TickCloud(float DeltaTime, const FVector& LightDirection, const FVector* ViewLocation);

	/** What the shape is now, as FDreamCloudField::SourceHash. */
	uint32 ComputeSourceHash() const;

	// UPrimitiveComponent
	virtual FMatrix GetRenderMatrix() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	void UploadField();
	void EnsureMaterial();
	void ResetCarving();
	void UploadHoles();
	float MeanScale() const;

	TArray<float> Holes;
	FIntVector HoleGridSize = FIntVector::ZeroValue;
	bool bHolesChanged = false;
	bool bHolesLive = false;
	float HoleUploadWait = 0.0f;
	float LightHoleWait = 0.0f;
	bool bLightStale = true;
	FLightState LastLight;
	int32 LastLightResolution = 0;
	int32 LightMapUpdates = 0;
	bool bLightMapDrawn = false;
	float CoverageStepScale = 1.0f;
	float LastQualityStep = -1.0f;
	bool bUsedLightMap = true;
	int32 UsedDebugView = 0;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> HoleTexture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTargetVolume> LightMap;

	UPROPERTY(Transient)
	TObjectPtr<UVolumeTexture> FieldTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;
};
