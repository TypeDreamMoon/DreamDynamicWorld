// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWorldTypes.h"
#include "DreamSkyTypes.h"
#include "DreamSky.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInterface;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UVolumetricCloudComponent;
class UDreamSkyStyle;

/**
 * The sky of a level: drop one in (and remove the level's own directional lights, sky light and height fog).
 *
 * It owns the lights the sky drives -- the sun and the moon (two directional lights; the atmosphere lights 0 and
 * 1), a sky light that captures the dome, a height fog and an unbound post process for exposure -- a dome drawn by
 * the DreamShader sky material, and the engine's volumetric cloud layer for when the clouds are volumetric
 * rather than the dome's own toon clouds (see Clouds). It also carries the level's clock:
 * the date and hour the level starts at, how long a day lasts, and how the sun and moon move.
 *
 * The lighting itself is decided every frame by UDreamSkySubsystem from the world clock and Style.
 */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Sky"))
class DREAMSKY_API ADreamSky : public AActor
{
	GENERATED_BODY()

public:
	ADreamSky();

	/** How the sky looks and lights the scene. Empty = the plugin's default stylized style. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	TObjectPtr<UDreamSkyStyle> Style;

	/** The hour the level starts at (and, in the editor, the hour shown). Scrubbing it previews the day. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time", meta = (ClampMin = "0", ClampMax = "23.99", UIMin = "0", UIMax = "24"))
	float TimeOfDay = 9.0f;

	/** The level's clock. Its start time's hour is replaced by TimeOfDay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time")
	FDreamWorldClockConfig Clock;

	/** Material of the dome. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome")
	TSoftObjectPtr<UMaterialInterface> SkyMaterial;

	/** Radius of the dome around the camera, cm. Everything that should be seen must be nearer than this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (ClampMin = "10000", UIMax = "5000000"))
	float DomeRadius = 500000.0f;

	/** The sky light recaptures the dome continuously (time-sliced by the engine). Off = recaptured when the sky changed enough. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lighting")
	bool bRealTimeSkyCapture = true;

	/** What draws the clouds. The console variable DreamSky.Clouds overrides it (0 toon, 1 volumetric). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	EDreamSkyClouds Clouds = EDreamSkyClouds::Toon;

	/** Material of the engine's volumetric cloud layer (a Volume domain material), for Clouds = Volumetric. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	TSoftObjectPtr<UMaterialInterface> VolumetricCloudMaterial;

	/** How tall the volumetric cloud layer stands above its base (the style's CloudLayerHeight), km: room for storm towers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "1", UIMax = "15"))
	float VolumetricLayerThickness = 7.0f;

	/** How far the volumetric clouds are traced, km. The dome is pushed out beyond it (it would hide clouds behind it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "5", UIMax = "200"))
	float VolumetricTracingDistance = 60.0f;

	/**
	 * Samples along each view ray of the volumetric clouds, relative to the engine's default: more keeps small
	 * far clouds from being sliced into layers near the horizon, at a cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.25", UIMax = "8"))
	float VolumetricSampleCountScale = 2.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SkyDome;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Lights the scene from a lightning strike for the length of the flash; dark (not rendered) otherwise. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> LightningLight;

	/** The engine's volumetric cloud layer: shown only while the volumetric clouds are in effect. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UVolumetricCloudComponent> VolumetricClouds;

	/** The style in effect (Style, or the default). */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	UDreamSkyStyle* GetEffectiveStyle() const;

	/** The clouds in effect: Clouds, or the console variable DreamSky.Clouds when it is set. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	EDreamSkyClouds GetEffectiveClouds() const;

	/**
	 * Shows the clouds in effect: the volumetric layer on (its material, its altitude from the style) or off, and
	 * the dome far enough out not to hide it. Cheap when nothing changed; the sky subsystem calls it every update.
	 */
	void ApplyClouds(const UDreamSkyStyle& InStyle);

	/** The clock this level starts with. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	FDreamWorldClockConfig MakeClockConfig() const;

	/**
	 * Sets TimeOfDay and moves the world clock there: what scrubbing the time in the editor does. With
	 * bMarkModified the actor is Modify()'d first (undoable inside the caller's transaction, level marked dirty).
	 */
	void SetTimeOfDayAndClock(float Hours, bool bMarkModified);

	/** Applies the lighting to the lights, sky light, fog and exposure. bSnap ignores the change thresholds. */
	void ApplyLighting(const FDreamSkyLighting& Lighting, const UDreamSkyStyle& InStyle, bool bSnap);

	/** Keeps the dome around the view. */
	void UpdateDome(const FVector& ViewLocation, bool bHasView);

	/** Requests a sky light recapture (when real-time capture is off). */
	void RecaptureSkyLight();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void ApplyDome();
	/** The dome's radius: DomeRadius, pushed beyond the traced clouds while the volumetric clouds are on. */
	float GetEffectiveDomeRadius() const;
	void PushClockToWorld(bool bResetTime);
	static void ApplyDirectional(UDirectionalLightComponent* Light, const FVector& Direction, const FLinearColor& Color, float Lux, float ThresholdDeg, bool bShadows, bool bSnap);
};
