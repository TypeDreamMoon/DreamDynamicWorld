// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherFXSubsystem.generated.h"

class UNiagaraComponent;
class UDreamWorldSubsystem;
class UDreamWeatherSubsystem;
struct FDreamPrecipitationLayer;
struct FDreamWeatherSample;

/**
 * Rain and snow around the view. Each frame the weather runs (UDreamWeatherSubsystem::OnWeatherFrame), this
 * keeps a buffer of fall paths for each kind of precipitation that is falling — traced against the level,
 * stopped by the weather's shelters, slanted by the wind (FDreamWeatherFallPaths) — and hands them to its
 * effect, which draws the drops along them. Nothing falls indoors, splashes land on what the rain lands on,
 * and an effect only lives while its precipitation falls. What it draws is in UDreamWeatherFXSettings.
 */
UCLASS()
class DREAMWEATHERFX_API UDreamWeatherFXSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDreamWeatherFXSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Rain is layer 0, snow layer 1. */
	static constexpr int32 RainLayer = 0;
	static constexpr int32 SnowLayer = 1;

	/** The fall paths of a layer. */
	const FDreamWeatherFallPaths& GetPaths(int32 Layer) const { return States[FMath::Clamp(Layer, 0, 1)].Paths; }

	/** The effect of a layer, when one is alive. */
	UNiagaraComponent* GetEffect(int32 Layer) const;

	/** Drops and splashes per second being spawned now. */
	float GetDropRate(int32 Layer) const { return States[FMath::Clamp(Layer, 0, 1)].DropRate; }
	float GetSplashRate(int32 Layer) const { return States[FMath::Clamp(Layer, 0, 1)].SplashRate; }

	/** Drops the last fill put in the air at once (when the effect started or the view jumped). */
	float GetFillCount(int32 Layer) const { return States[FMath::Clamp(Layer, 0, 1)].FillCount; }

	/** The drops' velocity now (down and downwind), cm/s. */
	FVector GetFallVelocity(int32 Layer) const { return States[FMath::Clamp(Layer, 0, 1)].FallVelocity; }

	/** Stop spawning; what is falling finishes. */
	void StopPrecipitation();

private:
	struct FLayerState
	{
		FDreamWeatherFallPaths Paths;
		FVector PathOrigin = FVector::ZeroVector;
		FVector FallVelocity = FVector(0.0, 0.0, -900.0);
		uint32 UploadedGeneration = 0;
		float SinceUpload = 0.0f;
		float Idle = 0.0f;
		bool bActivated = false;
		/** Spawn the drops steady precipitation keeps in the air, at once, as soon as the effect runs. */
		bool bFillPending = false;
		/** How many drops the last fill spawned. */
		float FillCount = 0.0f;
		float DropRate = 0.0f;
		float SplashRate = 0.0f;
	};

	void HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime);
	void UpdateLayer(int32 Index, const FDreamPrecipitationLayer& Layer, float Strength, UDreamWeatherSubsystem& Weather,
		const FVector& ViewLocation, const FVector& Forward, const FVector& Wind, float DeltaTime);
	UNiagaraComponent* EnsureEffect(int32 Index, const FDreamPrecipitationLayer& Layer, const FVector& ViewLocation);

	FLayerState States[2];

	/** Rain, snow (created when they first fall). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> Effects;

	TWeakObjectPtr<UDreamWeatherSubsystem> WeatherSubsystem;
	FDelegateHandle FrameHandle;
	FDelegateHandle StoppedHandle;
};
