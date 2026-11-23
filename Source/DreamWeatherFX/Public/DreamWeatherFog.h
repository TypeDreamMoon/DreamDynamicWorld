// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWeatherFog.generated.h"

class UPostProcessComponent;
class UMaterialInstanceDynamic;

namespace DreamWeatherFX::Names
{
	/** xy how far the wind has carried the air at the view (cm, summed frame by frame), zw the wind there now (cm/s). */
	inline const FName MPC_FogDrift(TEXT("DDW_FogDrift"));
}

/**
 * A bank of ground fog: patches of fog (local fog volumes, each hugging the ground) filling a box, each drifting on the
 * wind, wrapping round within the box and coming and going over half a minute, so the bank lies in a dip or along a valley
 * and its fog moves through it in patches. How thick it is follows the weather's fog channel there. The patches draw
 * whether or not the scene has volumetric fog (and go into it when it does).
 */
UCLASS(meta = (DisplayName = "Dream Fog Bank"))
class DREAMWEATHERFX_API ADreamFogBank : public AActor
{
	GENERATED_BODY()

public:
	ADreamFogBank();

	/** Half the box the fog fills, cm (the actor at its floor's centre: the fog rises Extent.Z above it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank")
	FVector Extent = FVector(1500.0, 1200.0, 300.0);

	/** How thick the fog is at full weather fog: a patch's density at its heart (a local fog volume's, per metre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank", meta = (ClampMin = "0", UIMax = "2"))
	float Density = 0.6f;

	/** How much of it stays with no fog in the weather, 0..1 (0: none then; 1: the weather does not matter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank", meta = (ClampMin = "0", ClampMax = "1"))
	float ClearWeatherShare = 0.0f;

	/** A patch's radius, cm (each between three quarters and five quarters of it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank", meta = (ClampMin = "50", Units = "cm"))
	float PatchRadius = 500.0f;

	/** Patches in the box (each a local fog volume: a few dozen at most). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Patches = 20;

	/** How far a patch's fog thins upward, cm: low, the fog lies on the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank", meta = (ClampMin = "10", Units = "cm"))
	float HeightFalloff = 250.0f;

	/** How readily the patches take up the wind, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog Bank", meta = (ClampMin = "0"))
	float WindDrag = 0.4f;

	/** How thick it is now (Density times what the weather's fog makes of it). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetDensityNow() const { return DensityNow; }

	/** Asks the weather again (it does every tick). */
	void Refresh();

	/** Moves the patches on by Seconds on the wind (it does every tick). */
	void Drift(float Seconds);

	/** The patches (their fog volumes) and where each stands. */
	int32 GetPatchCount() const { return Volumes.Num(); }
	FVector GetPatchLocation(int32 Index) const;

	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	struct FPatch
	{
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Radius = 500.0f;
		float Age = 0.0f;
		float Life = 30.0f;
		float Weight = 1.0f;
	};

	void Respawn(FPatch& Patch, bool bAnyAge);
	void Apply();

	UPROPERTY(Transient)
	TArray<TObjectPtr<class ULocalFogVolumeComponent>> Volumes;

	TArray<FPatch> PatchStates;
	FRandomStream Random;
	float DensityNow = 0.0f;
};

/**
 * Fog blown by the wind, low over the ground: a post-process pass (M_DreamWindFog) that marches a slab of wind-stretched,
 * drifting noise from the camera to what it sees, so wisps of fog stream past at knee to head height. It shows as far as
 * the weather has fog and the wind blows (the weather's fog and wind at the view); the slab runs from the actor's height
 * up Thickness.
 */
UCLASS(meta = (DisplayName = "Dream Wind Fog"))
class DREAMWEATHERFX_API ADreamWindFog : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindFog();

	/** The slab's thickness above the actor, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Fog", meta = (ClampMin = "10", Units = "cm"))
	float Thickness = 250.0f;

	/** How thick the wisps get at full fog and wind, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float Amount = 0.6f;

	/** How far the wisps reach from the camera, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Fog", meta = (ClampMin = "100", Units = "cm"))
	float Reach = 4000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Fog", meta = (AllowedClasses = "/Script/Engine.MaterialInterface"))
	FSoftObjectPath Material = FSoftObjectPath(TEXT("/DreamDynamicWorld/Weather/M_DreamWindFog.M_DreamWindFog"));

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wind Fog")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** The pass's material (an instance of Material), once loaded. */
	UMaterialInstanceDynamic* GetPass() const { return Pass; }

	/** Writes the slab and the amount to the pass (it does when they change). */
	void UpdatePass();

	virtual void PostRegisterAllComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Pass;
};
