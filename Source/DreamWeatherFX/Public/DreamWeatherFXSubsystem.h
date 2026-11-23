// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWeatherFallPaths.h"
#include "DreamWeatherFXSubsystem.generated.h"

class UNiagaraComponent;
class UDreamWorldSubsystem;
class UDreamWeatherSubsystem;
struct FDreamLightningStrike;
struct FDreamPrecipitationLayer;
struct FDreamWeatherSample;

/**
 * Rain, snow and lightning bolts around the view. Each frame the weather runs (UDreamWeatherSubsystem::
 * OnWeatherFrame), this keeps a buffer of fall paths for each kind of precipitation that is falling — traced
 * against the level, stopped by the weather's shelters, slanted by the wind (FDreamWeatherFallPaths) — and hands
 * them to its effect, which draws the drops along them. Nothing falls indoors, splashes land on what the rain
 * lands on, and an effect only lives while its precipitation falls. Where lightning strikes within reach, a bolt
 * comes down from the cloud base and flickers with the flash. What it draws is in UDreamWeatherFXSettings.
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

	/** Rain is layer 0, snow layer 1, blown dust (and the debris on its paths) layer 2. */
	static constexpr int32 RainLayer = 0;
	static constexpr int32 SnowLayer = 1;
	static constexpr int32 DustLayer = 2;
	static constexpr int32 HailLayer = 3;
	static constexpr int32 SleetLayer = 4;
	static constexpr int32 NumLayers = 5;

	/** The fall paths of a layer. */
	const FDreamWeatherFallPaths& GetPaths(int32 Layer) const { return States[FMath::Clamp(Layer, 0, NumLayers - 1)].Paths; }

	/** The effect of a layer, when one is alive. */
	UNiagaraComponent* GetEffect(int32 Layer) const;

	/** Drops and splashes per second being spawned now. */
	float GetDropRate(int32 Layer) const { return States[FMath::Clamp(Layer, 0, NumLayers - 1)].DropRate; }
	float GetSplashRate(int32 Layer) const { return States[FMath::Clamp(Layer, 0, NumLayers - 1)].SplashRate; }

	/** How much of the layer's dust is still in the air (0..1): what is already flying fades with it (the dust layer). */
	float GetPresence(int32 Layer) const { return States[FMath::Clamp(Layer, 0, NumLayers - 1)].Presence; }

	/** Drops the last fill put in the air at once (when the effect started or the view jumped). */
	float GetFillCount(int32 Layer) const { return States[FMath::Clamp(Layer, 0, NumLayers - 1)].FillCount; }

	/** The drops' velocity now (down and downwind), cm/s. */
	FVector GetFallVelocity(int32 Layer) const { return States[FMath::Clamp(Layer, 0, NumLayers - 1)].FallVelocity; }

	/** The wind debris effect, when one is alive, and the pieces it spawns per second now. */
	UNiagaraComponent* GetDebrisEffect() const { return DebrisEffect; }
	float GetDebrisRate() const { return DebrisRate; }

	/** How much debris the season brings now (spring .. winter blended), and its colour. */
	static float SeasonDebris(const FDateTime& Date, bool bSouthernHemisphere, FLinearColor& OutColor);

	/** Stop spawning; what is falling finishes. */
	void StopPrecipitation();

	/** The bolts alive now, newest last. */
	TArray<UNiagaraComponent*> GetBolts() const;

	/**
	 * How far the wind has carried the air at the view, cm: the wind summed frame by frame (so a change of wind never
	 * jumps it), what the fog's noise drifts by (DDW_FogDrift).
	 */
	FVector2D GetFogDrift() const { return FogDrift; }

private:
	FVector2D FogDrift = FVector2D::ZeroVector;

	/** A lightning strike: draw its bolt if it is within reach. */
	UFUNCTION()
	void HandleLightningStrike(const FDreamLightningStrike& Strike);

	/** The newest bolt follows the flash; older ones fade out. */
	void UpdateBolts(const UDreamWeatherSubsystem& Weather, float DeltaTime);

	struct FBolt
	{
		TWeakObjectPtr<UNiagaraComponent> Effect;
		float Glow = 1.0f;
		float Age = 0.0f;
	};
	TArray<FBolt> Bolts;
	int32 BoltsDrawn = 0;

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
		/** How much of the dust is still in the air (the dust layer's particles fade with it). */
		float Presence = 0.0f;
	};

	void HandleWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float DeltaTime);
	void UpdateLayer(int32 Index, const FDreamPrecipitationLayer& Layer, float Strength, UDreamWeatherSubsystem& Weather,
		const FVector& ViewLocation, const FVector& Forward, const FVector& Wind, float DeltaTime, float DropShare = 1.0f);
	void UpdateDebris(UDreamWorldSubsystem& Clock, float Strength, const FVector& ViewLocation);
	UNiagaraComponent* EnsureEffect(int32 Index, const FDreamPrecipitationLayer& Layer, const FVector& ViewLocation);

	FLayerState States[NumLayers];

	/** Rain, snow (created when they first fall). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> Effects;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> DebrisEffect;
	float DebrisRate = 0.0f;
	uint32 DebrisUploaded = 0;

	TWeakObjectPtr<UDreamWeatherSubsystem> WeatherSubsystem;
	FDelegateHandle FrameHandle;
	FDelegateHandle StoppedHandle;
};
