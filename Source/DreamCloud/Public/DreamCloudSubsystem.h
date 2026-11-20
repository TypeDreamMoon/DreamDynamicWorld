// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamCloudSubsystem.generated.h"

class UDreamCloudCarverComponent;
class UDreamCloudVolumeComponent;
class UMaterialParameterCollection;

namespace DreamCloud
{
	/** What a quality level (the DreamCloud.Quality console variable, 0 low .. 3 epic) asks of a cloud volume. */
	struct FQuality
	{
		/** Multiplies the march's fine step. */
		float StepScale = 1.0f;
		/** Light map texels along the box's longest axis, and its steps toward the light. */
		int32 LightMapResolution = 32;
		int32 LightMapSteps = 48;
	};

	DREAMCLOUD_API int32 GetQualityLevel();
	DREAMCLOUD_API FQuality GetQuality();

	/** DreamCloud.UseLightMap: whether volumes read their light map (1) or march toward the light (0). */
	DREAMCLOUD_API bool UseLightMap();

	/** DreamCloud.UseLightMap 2, 3 or 4: a debug view (see the variable's help), else 0. */
	DREAMCLOUD_API int32 DebugView();
}

/**
 * Keeps a world's cloud volumes up to date: each frame it lets carvers cut holes into the volumes they pass through,
 * and has every volume let its holes grow back, follow the light (its light map is drawn again when the light turns
 * far enough within it) and thin its steps while it fills much of the view. Ticks in the editor too, so a volume
 * reacts to the time of day while it is edited. The light is the plugin's main light (the sun or the moon) from its
 * shared parameter collection, the one the volumes' material reads.
 */
UCLASS()
class DREAMCLOUD_API UDreamCloudSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterVolume(UDreamCloudVolumeComponent* Volume);
	void UnregisterVolume(UDreamCloudVolumeComponent* Volume);
	void RegisterCarver(UDreamCloudCarverComponent* Carver);
	void UnregisterCarver(UDreamCloudCarverComponent* Carver);

	const TArray<TWeakObjectPtr<UDreamCloudVolumeComponent>>& GetVolumes() const { return Volumes; }

	/** Toward the main light, world space, unit. */
	FVector GetLightDirection() const;

	/** Overrides the light direction (tests, worlds without the plugin's sky); zero clears it. */
	void SetLightDirectionOverride(const FVector& Direction) { LightOverride = Direction.GetSafeNormal(); }

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

private:
	TArray<TWeakObjectPtr<UDreamCloudVolumeComponent>> Volumes;
	TArray<TWeakObjectPtr<UDreamCloudCarverComponent>> Carvers;
	FVector LightOverride = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Collection;
};
