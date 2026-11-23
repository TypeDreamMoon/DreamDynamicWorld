// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWaterBody.h"
#include "DreamWaterSubsystem.generated.h"

class UDreamWorldSubsystem;
class UDreamWeatherSubsystem;
struct FDreamWeatherSample;
struct FDreamWorldSnapshot;

namespace DreamWater::Names
{
	/**
	 * The view's water, in the shared collection, each frame:
	 *   x  the surface's height (world cm) of the water body the view is over or in -- the nearest one whose area it is
	 *      in --, or -1e6 with none;
	 *   y  that body's turbidity, 0 clear .. 1 as muddy as it gets;
	 *   z  how deep the view is under its surface, cm (0 above it): what an underwater look (a later water module)
	 *      fades in by;
	 *   w  that body's level against its usual one, cm.
	 */
	inline const FName MPC_Water(TEXT("DDW_Water"));
}

/** The water bodies' states in a world snapshot, by body. */
USTRUCT()
struct DREAMWATER_API FDreamWaterSnapshotData
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, FDreamWaterState> Bodies;
};

/** What the view's water is (DDW_Water, for code). */
USTRUCT(BlueprintType)
struct DREAMWATER_API FDreamViewWater
{
	GENERATED_BODY()

	/** The body the view is over or in (null with none). */
	UPROPERTY(BlueprintReadOnly, Category = "Water")
	TObjectPtr<UDreamWaterBodyComponent> Body = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Water")
	float SurfaceHeight = -1.0e6f;

	UPROPERTY(BlueprintReadOnly, Category = "Water")
	float Turbidity = 0.0f;

	/** How deep the view is under the surface, cm (0 above it). */
	UPROPERTY(BlueprintReadOnly, Category = "Water")
	float Depth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Water")
	float LevelOffset = 0.0f;
};

/**
 * The water bodies of a world: each frame (with the weather) it moves their levels on by the world's time in the
 * weather over each, settles them across a jump of the clock (the hours jumped, in the weather as it is now, up to two
 * days), saves and restores them with the world's snapshot, and says which body the view is over or in (DDW_Water).
 */
UCLASS()
class DREAMWATER_API UDreamWaterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDreamWaterSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterBody(UDreamWaterBodyComponent* Body);
	void UnregisterBody(UDreamWaterBodyComponent* Body);

	/** The bodies (registered, alive). */
	TArray<UDreamWaterBodyComponent*> GetBodies() const;

	/** The water body a point is over or in: of those whose area it is in, the one whose surface is nearest it. */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	UDreamWaterBodyComponent* FindWaterAt(FVector Position) const;

	/** The view's water, as last written to DDW_Water. */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	FDreamViewWater GetViewWater() const { return ViewWater; }

	/** The weather over a body now. */
	FDreamWaterWeather WeatherAt(const UDreamWaterBodyComponent& Body) const;

	/** Moves every body on by Hours in the weather over it (in steps of at most a quarter of an hour). */
	void AdvanceAll(float Hours);

private:
	void HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime);
	void UpdateView(UDreamWorldSubsystem& Clock);
	void CaptureSnapshot(FDreamWorldSnapshot& Snapshot) const;
	void ApplySnapshot(const FDreamWorldSnapshot& Snapshot);
	static FString KeyOf(const UDreamWaterBodyComponent& Body);

	TArray<TWeakObjectPtr<UDreamWaterBodyComponent>> Bodies;
	TWeakObjectPtr<UDreamWorldSubsystem> ClockSubsystem;
	TWeakObjectPtr<UDreamWeatherSubsystem> WeatherSubsystem;
	FDelegateHandle FrameHandle;
	FDelegateHandle CaptureHandle;
	FDelegateHandle ApplyHandle;
	double LastWorldSeconds = -1.0;
	uint32 LastJump = MAX_uint32;
	FDreamViewWater ViewWater;
};
