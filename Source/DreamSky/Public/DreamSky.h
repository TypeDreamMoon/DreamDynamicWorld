// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWorldTypes.h"
#include "DreamSkyTypes.h"
#include "DreamSkyOverrides.h"
#include "DreamSky.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
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

	/** Radius of the dome around the camera, cm. Everything that should be seen must be nearer than this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (ClampMin = "10000", UIMax = "5000000"))
	float DomeRadius = 500000.0f;

	/** The sky light recaptures the dome continuously (time-sliced by the engine). Off = recaptured when the sky changed enough. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky Light and Fog")
	bool bRealTimeSkyCapture = true;

	/** What draws the clouds. The console variable DreamSky.Clouds overrides it (0 toon, 1 volumetric). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	EDreamSkyClouds Clouds = EDreamSkyClouds::Toon;

	/**
	 * Material of the engine's volumetric cloud layer (a Volume domain material), for Clouds = Volumetric. The plugin
	 * ships two over the same clouds: M_DreamVolumetricCloud (realistic: soft, lit by scattering) and
	 * M_DreamVolumetricCloud_Painted (an anime background's: hard-edged, in the style's three painted tones). The
	 * console variable DreamSky.CloudMaterial overrides it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	TSoftObjectPtr<UMaterialInterface> VolumetricCloudMaterial;

	/**
	 * How tall the volumetric cloud layer stands above its base (the style's CloudLayerHeight), km. Fair-weather and
	 * overcast clouds keep to its lowest 4 km; a storm's cumulonimbus climb to 10 km.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "1", UIMax = "15"))
	float VolumetricLayerThickness = 10.0f;

	/** How far the volumetric clouds are traced, km. The dome is pushed out beyond it (it would hide clouds behind it),
	 *  and the clouds' shadow map reaches as far (the plugin's cloud materials fade their shadows out short of it:
	 *  their ShadowReach parameter, set from this when the material overrides make an instance of them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "5", UIMax = "200"))
	float VolumetricTracingDistance = 60.0f;

	/**
	 * Samples along each view ray of the volumetric clouds, relative to the engine's default: more keeps small
	 * far clouds from being sliced into layers near the horizon, at a cost.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.25", UIMax = "8"))
	float VolumetricSampleCountScale = 2.0f;

	/**
	 * How much of the sky light the volumetric clouds' bases are shut off from (the cloud above hides the sky):
	 * 0 = lit by the whole sky like their tops .. 1 = none. Higher = darker, flatter bases.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float VolumetricBaseSkyOcclusion = 0.85f;

	/**
	 * How far each sample of the volumetric clouds looks toward the sun for its shadow, km (the engine's march, for a
	 * material that marches it). Its samples crowd toward the start: shorter = finer near the cloud's surface, so the
	 * sunlit faces stay lit; a tower's shadow on clouds further than this is left out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.5", UIMax = "15"))
	float VolumetricShadowTracingDistance = 6.0f;

	/**
	 * Resolution of the maps the volumetric clouds are shadowed through -- the sun's cloud shadow map (their shadows
	 * on the ground, and their own sunlight where a material reads it) and the sky light's cloud occlusion map --
	 * relative to the engine's 512 texels. Both cover the traced distance, not the engine's 150 km, so they stay as
	 * fine at a lower resolution; each costs about its resolution squared.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.25", UIMax = "2"))
	float VolumetricShadowResolution = 0.75f;

	/**
	 * Samples each texel of the sun's cloud shadow map marches through the clouds, relative to the engine's 16. The map
	 * is soft (a texel is some 300 m) and its cost goes with this; storms, whose towers fill the layer, cost the most.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.25", UIMax = "2"))
	float VolumetricShadowSamples = 0.5f;

	/**
	 * How strongly the sun's and the moon's light scatters in the volumetric clouds, relative to the engine's. Its
	 * multiple scattering is an approximation that loses light: at 1 sunlit cloud faces come out light grey where a
	 * real cumulus is near white. (The moon is left out of the clouds' tracing while it is dark: a second light costs a
	 * second march toward it at every lit sample.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "5"))
	float VolumetricSunScattering = 2.5f;

	/**
	 * Trace the realistic volumetric clouds at half resolution every frame (r.VolumetricRenderTarget.Mode 1) instead of
	 * the engine's default, a quarter of the pixels each frame rebuilt from the frames before, which keeps a standing
	 * grain over dense clouds and comes out in stripes for some frames after a lightning flash. It costs the clouds
	 * 0.2 to 0.9 ms more at 1080p, about 2 ms in a storm: in a storm they go back to the default (the rain hides the
	 * grain) except through each lightning flash. The painted clouds need none of it and keep the default. Off: always
	 * the default. A project or console setting of the variable is left alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	bool bVolumetricHalfResTracing = true;

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

	/** The style in effect: Style (or the default) with this level's overrides written over it. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	UDreamSkyStyle* GetEffectiveStyle() const;

	/**
	 * Takes in changed overrides (and a changed style): rebuilds the effective style, sets the lights' and the fog's
	 * own settings, and refreshes the cloud material's. Called when they are edited; call it after changing the
	 * override structs from code.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Sky")
	void RefreshOverrides();

	/** Scales the style's lighting by this level's overrides (sun, moon, sky light, fog, exposure compensation). */
	void ApplyLightingOverrides(FDreamSkyLighting& Lighting) const;

	/** The clouds in effect: Clouds, or the console variable DreamSky.Clouds when it is set. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	EDreamSkyClouds GetEffectiveClouds() const;

	/**
	 * Whether the clouds hide the sun themselves: the volumetric clouds are in effect and the sun casts cloud shadows
	 * at some strength. The style then does not dim the sun for an overcast sky (UDreamSkyStyle::ApplyWeather).
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	bool CloudsShadowTheSun() const;

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
	/** The style with the overrides written over it (null while no style override is ticked). */
	UPROPERTY(Transient)
	TObjectPtr<UDreamSkyStyle> OverriddenStyle;

	/** The volumetric cloud material with the material overrides set on it (null while none is ticked). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterialInstance;

	/** Set when the material overrides changed: the instance takes them on its next ApplyClouds. */
	bool bCloudMaterialDirty = true;

	/** Picks how the volumetric clouds are traced (half or quarter resolution; through a lightning flash, without the
	 *  frame history) for the material in use and the weather. */
	void UpdateCloudTracing(bool bPainted);
	/** The lightning flash at the last ApplyClouds, the frame its level last jumped, the last frame it was lit, whether
	 *  the history is held for it, whether the weather counts as a storm. */
	float LastCloudFlash = 0.0f;
	uint64 CloudFlashJumpFrame = 0;
	uint64 CloudFlashLitFrame = 0;
	bool bHoldingCloudHistory = false;
	bool bCloudStorm = false;

#if WITH_EDITOR
	/** The style asset was edited: the overridden copy is rebuilt. */
	void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
	FDelegateHandle StyleChangedHandle;
#endif

	void RebuildOverriddenStyle();
	/** Sets the lights' and the fog's ticked settings; bRestoreUnticked puts the unticked ones back to the sky's defaults. */
	void ApplyComponentOverrides(bool bRestoreUnticked);
	void ApplyDome();
	/** The dome's radius: DomeRadius, pushed beyond the traced clouds while the volumetric clouds are on. */
	float GetEffectiveDomeRadius() const;
	void PushClockToWorld(bool bResetTime);
	static void ApplyDirectional(UDirectionalLightComponent* Light, const FVector& Direction, const FLinearColor& Color, float Lux, float ThresholdDeg, bool bShadows, bool bSnap);
};
