// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamWaterBody.generated.h"

class UDreamWaterBodyComponent;

/** Where a water body's level stands against its usual one. */
UENUM(BlueprintType)
enum class EDreamWaterBand : uint8
{
	/** Below LowBelow: a dry spell has drawn it down. */
	Low,
	/** Round its usual level. */
	Normal,
	/** Above HighAbove: the rain has raised it. */
	High,
	/** At its brim (MaxRise): it spills over. */
	Overflow,
};

/** How a water body fills and drains: a bucket the rain falls into, drained back to its usual level. */
USTRUCT(BlueprintType)
struct DREAMWATER_API FDreamWaterRules
{
	GENERATED_BODY()

	/** The ground the rain runs off into the water, as a share of the water's own surface (a pond in a hollow: tens). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0"))
	float CatchmentRatio = 20.0f;

	/** The rain at the weather's full rain, mm an hour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0"))
	float RainAtFull = 8.0f;

	/** How long a rise takes to drain halfway back to the usual level (out over its outlet), game hours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.1", Units = "Hours"))
	float DrainHours = 12.0f;

	/** How long a fall takes to fill halfway back from the ground water, game hours (slow: a dry spell draws it down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.1", Units = "Hours"))
	float RefillHours = 240.0f;

	/** How much a warm, clear day evaporates, cm a day (at 20 C and more; none below -5 C; a third under full cloud). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0"))
	float EvaporationPerDay = 0.6f;

	/** The brim: the most the level rises above its usual one, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0", Units = "cm"))
	float MaxRise = 150.0f;

	/** The most the level falls below its usual one, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0", Units = "cm"))
	float MaxDrop = 80.0f;

	/** The high band from this far above the usual level, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0", Units = "cm"))
	float HighAbove = 40.0f;

	/** The low band from this far below the usual level, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0", Units = "cm"))
	float LowBelow = 25.0f;

	/** How muddy each cm the rain brings in makes it (0..1 a cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0"))
	float TurbidityPerCm = 0.02f;

	/** How long the mud takes to settle halfway, game hours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0.1", Units = "Hours"))
	float ClearHours = 36.0f;
};

/** A water body's state: its level against the usual one and how muddy it is. */
USTRUCT(BlueprintType)
struct DREAMWATER_API FDreamWaterState
{
	GENERATED_BODY()

	/** cm above (or below) the usual level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Water")
	float LevelOffset = 0.0f;

	/** 0 clear .. 1 as muddy as it gets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "Water")
	float Turbidity = 0.0f;
};

/** The weather over a water body, as its level reads it. */
struct FDreamWaterWeather
{
	/** The weather's rain falling there, 0..1. */
	float Rain = 0.0f;
	/** Degrees Celsius. */
	float Temperature = 15.0f;
	/** 0..1. */
	float Cloud = 0.0f;
};

namespace DreamWater::Model
{
	/** The state Hours on: the rain fills it (over the catchment), it drains back toward its usual level, evaporates. */
	DREAMWATER_API FDreamWaterState Advance(const FDreamWaterState& State, const FDreamWaterRules& Rules, const FDreamWaterWeather& Weather, float Hours);

	/** The band an offset stands in, with a couple of cm's hysteresis about the band edges (Current: the band now). */
	DREAMWATER_API EDreamWaterBand BandOf(float LevelOffset, const FDreamWaterRules& Rules, EDreamWaterBand Current);
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamWaterBandEvent, UDreamWaterBodyComponent*, Body, EDreamWaterBand, Previous, EDreamWaterBand, Current);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDreamWaterBandNativeEvent, UDreamWaterBodyComponent* /*Body*/, EDreamWaterBand /*Previous*/, EDreamWaterBand /*Current*/);

/**
 * A water body the weather fills. Put it on any water (a pond's mesh, a lake, a water body of the engine's Water plugin)
 * at its usual surface: the component's height is the usual level, Extent the water's area round it (in the
 * component's yaw), Depth how far down its bottom is. The rain over it -- times its catchment -- raises the level, which
 * drains back to the usual one over DrainHours and evaporates on warm, clear days; what the rain brings in muddies it,
 * settling over ClearHours. It says where its level stands (GetLevelOffset, GetSurfaceHeight), which band (low, normal,
 * high, overflow: OnBandChanged), how muddy, and whether a point is under its water.
 *
 * What it does with the level is the project's: bMoveOwner lifts the actor it is on (a pond's mesh), otherwise read the
 * offset (a Water plugin body: see the README). The underwater look is left to a later water module: the subsystem
 * already says which body the view is in and how deep (DDW_Water).
 */
UCLASS(ClassGroup = (Dream), meta = (BlueprintSpawnableComponent, DisplayName = "Dream Water Body"))
class DREAMWATER_API UDreamWaterBodyComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamWaterBodyComponent();

	/** Half the water's size round the component (cm, in its yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FVector2D Extent = FVector2D(1000.0, 1000.0);

	/** How far below the usual surface its bottom lies, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water", meta = (ClampMin = "0", Units = "cm"))
	float Depth = 200.0f;

	/** How it fills and drains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	FDreamWaterRules Rules;

	/** Lift the actor it is on with the level (a pond's mesh rides on it); off, only the offset is said. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bMoveOwner = false;

	/** Hold the state where it is (cinematics, a level set by hand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water")
	bool bHold = false;

	UPROPERTY(BlueprintAssignable, Category = "Water")
	FDreamWaterBandEvent OnBandChanged;

	FDreamWaterBandNativeEvent OnBandChangedNative;

	/** cm above (or below) the usual level. */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	float GetLevelOffset() const { return State.LevelOffset; }

	/** The surface's height now, world cm. */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	float GetSurfaceHeight() const { return BaseHeight + State.LevelOffset; }

	/** The usual surface's height, world cm (the component's, when it was placed). */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	float GetBaseHeight() const { return BaseHeight; }

	/** The bottom's height, world cm. */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	float GetBottomHeight() const { return BaseHeight - Depth; }

	/** 0 clear .. 1 as muddy as it gets. */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	float GetTurbidity() const { return State.Turbidity; }

	UFUNCTION(BlueprintPure, Category = "Dream Water")
	EDreamWaterBand GetBand() const { return Band; }

	UFUNCTION(BlueprintPure, Category = "Dream Water")
	FDreamWaterState GetState() const { return State; }

	/** Sets the state (a level by hand, a save restored); the band follows. */
	UFUNCTION(BlueprintCallable, Category = "Dream Water")
	void SetState(const FDreamWaterState& NewState);

	/** A point lies within the water's area (any height). */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	bool Covers(FVector Position) const;

	/** How deep a point is under the surface, cm (> 0 under it, between the surface and the bottom, in its area; else 0). */
	UFUNCTION(BlueprintPure, Category = "Dream Water")
	float GetDepthBelowSurface(FVector Position) const;

	/** Moves the state on by Hours in this weather (the subsystem does every frame). */
	void Advance(const FDreamWaterWeather& Weather, float Hours);

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	void ApplyLevel();

	FDreamWaterState State;
	EDreamWaterBand Band = EDreamWaterBand::Normal;
	float BaseHeight = 0.0f;
	float OwnerBaseZ = 0.0f;
	bool bBaseKnown = false;
};
