// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWorldTypes.h"
#include "DreamSkyTypes.h"
#include "DreamSkyOverrides.h"
#include "DreamSkyConditions.h"
#include "DreamSky.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UPostProcessComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UDreamSkyStyle;
class UDreamSkySubsystem;

/**
 * The sky of a level: drop one in (and remove the level's own directional lights, sky light and height fog).
 *
 * It owns the lights the sky drives -- the sun and the moon (two directional lights; the atmosphere lights 0 and
 * 1), a sky light that captures the dome, a height fog and an unbound post process for exposure -- a dome drawn by
 * the DreamShader sky material, and the clouds, which the sky cloud renderer draws (see CloudLook). It also carries the level's clock:
 * the date and hour the level starts at, how long a day lasts, and how the sun and moon move.
 *
 * The lighting itself is decided every frame by UDreamSkySubsystem from the world clock and Style. The panel shows
 * only what is worth setting: the style, the clock, the clouds, and per-level overrides of the style and of the
 * lights' and fog's own settings (the components it drives are still editable from the component tree, though the
 * sky rewrites most of what they hold every frame).
 */
UCLASS(ClassGroup = "Dream", meta = (DisplayName = "Dream Sky"), hidecategories = (Collision, Physics, Input, Replication, HLOD,
	Cooking, Networking, Rendering, Actor, LOD, WorldPartition, DataLayers, Events, Navigation, AssetUserData, LevelInstance,
	Tags, Activation, Components))
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

	/**
	 * Radius of the dome around the camera, cm. Everything that should be seen must be nearer than this: the dome is
	 * drawn into the depth buffer like any mesh and hides what stands behind it. The sky it shows depends only on the
	 * view direction, so it can stand as far off as the terrain reaches and further (by default 1000 km).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (ClampMin = "10000", UIMax = "200000000"))
	float DomeRadius = 100000000.0f;

	/** The sky light recaptures the dome continuously (time-sliced by the engine). Off = recaptured when the sky changed enough. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky Light and Fog")
	bool bRealTimeSkyCapture = true;

	/**
	 * How the sky cloud renderer draws the clouds: toon (the surface, in the style's few tones), realistic or painted
	 * (the density, lit by scattering or painted in the style's tones). The realistic and painted looks take their
	 * settings from the style (its RealisticClouds and PaintedClouds), with the cloud overrides over them. The console
	 * variable DreamSky.CloudRender.Look overrides it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	EDreamSkyCloudLook CloudLook = EDreamSkyCloudLook::Toon;

	/**
	 * How far each sample of the realistic and painted clouds looks toward the sun for its shadow, km. Its samples crowd
	 * toward the start: shorter = finer near the cloud's surface, so the sunlit faces stay lit; a tower's shadow on
	 * clouds further than this is left out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.5", UIMax = "15"))
	float CloudShadowTracingDistance = 6.0f;

	/**
	 * How strongly the sun's and the moon's light scatters in the realistic and painted clouds. Their multiple
	 * scattering is an approximation that loses light: at 1 sunlit cloud faces come out light grey where a real cumulus
	 * is near white.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "5"))
	float CloudSunScattering = 2.5f;

	/** This level's cloud settings over the style's (tick one to use it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ShowOnlyInnerProperties))
	FDreamSkyCloudOverrides CloudOverrides;

	/** This level's sun and moon settings over the style's and the lights' own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun and Moon", meta = (ShowOnlyInnerProperties))
	FDreamSkyLightOverrides LightOverrides;

	/** This level's sky light and fog settings over the style's and the fog's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky Light and Fog", meta = (ShowOnlyInnerProperties))
	FDreamSkyAmbientOverrides AmbientOverrides;

	/** This level's exposure settings over the style's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure", meta = (ShowOnlyInnerProperties))
	FDreamSkyExposureOverrides ExposureOverrides;

	/**
	 * Post processes the sky blends in by the hour, the weather and whether the view is indoors (a cold grade on snowy
	 * nights, a warm one at dusk, a bloom in the fog): each weighted by how far its condition holds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Post Process")
	TArray<FDreamSkyConditionalPostProcess> PostProcesses;

	/**
	 * Sky modifiers the sky holds at the strength their condition holds to (a moonlit night's blue at night, a heavier
	 * gloom in a storm). Leave a modifier listed here to the sky: added or removed from code as well, the two fight.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Modifiers")
	TArray<FDreamSkyConditionalModifier> ConditionalModifiers;

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

	/** The style in effect: Style (or the default) with this level's overrides written over it. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	UDreamSkyStyle* GetEffectiveStyle() const;

	/**
	 * Takes in changed overrides (and a changed style): rebuilds the effective style and sets the lights' and the fog's
	 * own settings. Called when they are edited; call it after changing the override structs from code.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Sky")
	void RefreshOverrides();

	/** Scales the style's lighting by this level's overrides (sun, moon, sky light, fog, exposure compensation). */
	void ApplyLightingOverrides(FDreamSkyLighting& Lighting) const;

	/**
	 * Whether the sky cloud renderer draws this sky's clouds: it needs a renderer of Shader Model 5 or above (not the
	 * mobile renderer, not -nullrhi), and DreamSky.CloudRender.Enable on (a device profile can turn it off for a
	 * platform). Where it does not, the sky has no cumulus; the sky itself, the high cloud and the rain shafts are drawn
	 * as ever. Saying so once in the log and the editor when it turns off.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	bool DrawsClouds() const;

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

	/**
	 * Weighs the conditional post processes and modifiers for the frame: Hours is the time of day, the weather and the
	 * view occlusion are the clock's. Each weight follows its condition over its BlendSeconds (at once on a snap); the
	 * modifiers' go to Subsystem's stack. Game thread, each frame (UDreamSkySubsystem).
	 */
	void UpdateConditions(UDreamSkySubsystem& Subsystem, float Hours, const FDreamWorldEnvironment& Environment, const FDreamViewOcclusion& View,
		float DeltaTime, bool bSnap);

	/** How much a conditional post process (an index into PostProcesses) is blended in now. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetPostProcessWeight(int32 Index) const;

	/**
	 * The sky cloud renderer's clouds on the ground: while it draws the clouds (DrawsClouds) the sun light
	 * takes a light function (M_DreamCloudShadow) that reads the clouds' transmittance toward the sun from a texture the
	 * renderer writes round the view; otherwise the light function is taken off again. Game thread, each frame.
	 */
	void UpdateCloudShadow(const FVector& ViewLocation);

	/** CloudLook, or the console's DreamSky.CloudRender.Look. */
	EDreamSkyCloudLook GetEffectiveCloudLook() const;

	/** The settings the sky cloud renderer's realistic and painted looks are drawn with this frame. */
	void GetCloudLookParameters(FDreamSkyCloudLookParameters& Out) const;

	/** The texture the renderer writes the clouds' shadow into (null while it does not draw the clouds). */
	UTextureRenderTarget2D* GetCloudShadowTarget() const { return CloudShadowTarget; }

	/** Where that texture lies: its middle x, y (world cm), half its width (cm), how much sun the shadows take. */
	const FVector4f& GetCloudShadowPlacement() const { return CloudShadowPlacement; }

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void BeginPlay() override;
	//~ UObject
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	friend class FDreamSkyCloudMigrationTest;
	/** The style with the overrides written over it (null while no style override is ticked). */
	UPROPERTY(Transient)
	TObjectPtr<UDreamSkyStyle> OverriddenStyle;

	/** What drew the clouds in a level saved before the sky cloud renderer drew them all: read once on load (CloudLook). */
	UPROPERTY()
	EDreamSkyClouds Clouds_DEPRECATED = EDreamSkyClouds::Toon;

	/** The engine's volumetric cloud material such a level chose: read with it, to pick the renderer's look. */
	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> VolumetricCloudMaterial_DEPRECATED;

	/** A level saved before: its clouds as it chose them, in the look of the sky cloud renderer's that draws them. */
	void MigrateSavedClouds();

	/** The sky cloud renderer's shadow on the ground (UpdateCloudShadow). */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CloudShadowTarget;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudShadowFunction;
	FVector4f CloudShadowPlacement = FVector4f(0.0f, 0.0f, 1.0f, 0.0f);

	/** The conditional post processes' components and weights (UpdateConditions), and the conditional modifiers' weights. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPostProcessComponent>> ConditionalPostProcessComponents;
	TArray<float> ConditionalPostProcessWeights;
	TArray<float> ConditionalModifierWeights;

	/** Why the clouds are not drawn (DreamSky::Actor::ECloudsOff; none while they are), and what was said last. */
	uint8 WhyNoClouds() const;
	uint8 ToldCloudsOff = 0;
	void TellCloudsOff(uint8 Why);

#if WITH_EDITOR
	/** The style asset was edited: the overridden copy is rebuilt. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	FDelegateHandle StyleChangedHandle;
#endif

	void RebuildOverriddenStyle();
	/** Sets the lights' and the fog's ticked settings; bRestoreUnticked puts the unticked ones back to the sky's defaults. */
	void ApplyComponentOverrides(bool bRestoreUnticked);
	void ApplyDome();
	void PushClockToWorld(bool bResetTime);
	static void ApplyDirectional(UDirectionalLightComponent* Light, const FVector& Direction, const FLinearColor& Color, float Lux, float ThresholdDeg, bool bShadows, bool bSnap);
};
