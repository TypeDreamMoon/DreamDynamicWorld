// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamSkyTypes.h"
#include "DreamWorldSharedTargets.h"
#include "DreamSkySubsystem.generated.h"

class ADreamSky;
class UPostProcessComponent;
class UTexture;
class UTextureRenderTarget2D;
class UDreamSkyModifier;
class UDreamSkyStyle;
class UDreamWorldSubsystem;
class FDreamSkyCloudViewExtension;
struct FDreamWorldSnapshot;

/** Who put a sky modifier in the stack. */
enum class EDreamSkyModifierSource : uint8
{
	/** AddModifier, RemoveModifier, SetModifierAlpha (kept in snapshots). */
	Script,
	/** The weather in effect at the view (UDreamWeatherPreset::SkyModifier), at its weight there. */
	Weather,
	/** The sky actor, by a condition. */
	Condition,
};

/** A sky modifier in the stack (UDreamSkySubsystem::AddModifier and the rest). */
USTRUCT()
struct DREAMSKY_API FDreamSkyActiveModifier
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UDreamSkyModifier> Modifier;

	/** How far along its fade it is, 0..1 (its alpha before the fade's shape and MaxAlpha). */
	float Progress = 0.0f;
	/** Where Progress is heading. */
	float Target = 1.0f;
	/** How fast it heads there, per second (0: it is there at once). */
	float Rate = 0.0f;
	/** Dropped once faded out. */
	bool bRemoving = false;
	/** Driven by SetModifierAlpha: Progress is its alpha as it stands (no fade shape). */
	bool bManual = false;
	/** Who put it in the stack. One not of Script is held anew every update (SetDrivenModifier) or dropped. */
	EDreamSkyModifierSource Source = EDreamSkyModifierSource::Script;
	/** The update it was last held in. */
	uint32 DrivenSerial = 0;
	/** Order it was added in (among modifiers of one priority, the later goes on later). */
	uint32 Order = 0;

	/** Its post process, while it has one. */
	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** How much it takes effect now, 0..1. */
	float GetAlpha() const;
};

/** A sky modifier in the stack as a snapshot keeps it. */
USTRUCT()
struct DREAMSKY_API FDreamSkyModifierState
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath Modifier;
	UPROPERTY()
	float Progress = 0.0f;
	UPROPERTY()
	float Target = 1.0f;
	UPROPERTY()
	float Rate = 0.0f;
	UPROPERTY()
	bool bRemoving = false;
	UPROPERTY()
	bool bManual = false;
	UPROPERTY()
	int32 Order = 0;
};

/** What the sky keeps in a world snapshot (FDreamWorldSnapshot::Extensions): the modifiers, the scripted part of it. */
USTRUCT()
struct DREAMSKY_API FDreamSkySnapshotData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDreamSkyModifierState> Modifiers;
	UPROPERTY()
	int32 NextOrder = 0;
};

/**
 * The lighting director of a world's sky.
 *
 * Runs inside the world clock's tick (UDreamWorldSubsystem::OnWorldTick), right after the sun and moon moved:
 * the active ADreamSky's style turns the celestial state into lighting, which is eased while the clock runs
 * (so a toon ramp never flickers across its threshold) and snapped when the clock jumps; the sky actor's
 * lights, sky light, fog and exposure follow; and the look of the dome plus the lighting are written to the
 * shared parameter collection, where the sky material and any other material read them.
 */
UCLASS()
class DREAMSKY_API UDreamSkySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dream Sky", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Sky"))
	static UDreamSkySubsystem* Get(const UObject* WorldContextObject);

	/** The plugin's default style. */
	static UDreamSkyStyle* GetDefaultStyle();

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterSky(ADreamSky* Sky);
	void UnregisterSky(ADreamSky* Sky);

	/** The sky actor in charge (the first registered that is still alive), or null. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	ADreamSky* GetSky() const;

	/** The lighting as applied this frame: main light, ambient, fog, exposure, day phase. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	const FDreamSkyLighting& GetLighting() const { return Applied; }

	/** The dome's look this frame. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	const FDreamSkyLook& GetLook() const { return Look; }

	/**
	 * How bright the light shafts below the clouds are drawn now (FDreamSkyLightShafts): the look's (style and modifiers)
	 * as the cloud cover breaks the sun into them, gone with the main light under the horizon. 0: none drawn.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetLightShaftStrength() const;

	/**
	 * How wet the camera's lens is, 0..1: gathering while the view is out in the rain facing into it, drying off under
	 * a roof, indoors or once it stops (the style's ScreenDroplets; at once in cinematic updates and on clock jumps).
	 * Kept whether or not the drops are drawn.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetScreenWetness() const { return ScreenWetness; }

	/**
	 * How frosted the camera's lens is, 0..1 of the way in from its rim: growing while the view is out in the snow in
	 * the cold, fading slowly out in the cold once it is over, melting indoors or in the warm (the style's ScreenFrost;
	 * at once in cinematic updates and on clock jumps). Kept whether or not the frost is drawn.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetScreenFrost() const { return ScreenFrost; }

	/**
	 * How strongly hot air shimmers near the horizon, 0..1: the hotter (the style's HeatHaze), the higher the sun, the
	 * clearer and drier the air, the stiller the wind. Kept whether or not the shimmer is drawn.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetHeatHaze() const { return HeatHaze; }

	/**
	 * How strongly a rainbow shows, 0..1: rain in the air (as it falls, and the style's Rainbow.LingerSeconds after),
	 * the main light low (below 42 degrees) and not behind the clouds. Kept whether or not it is drawn.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetRainbow() const { return Rainbow; }

	/**
	 * How much of the Milky Way shows, 0..1 of the darkest night's: the night's darkness, the moon's light, a city's,
	 * the haze, fog and cloud. Kept whether or not it is drawn.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetMilkyWayVisibility() const { return MilkyWayVisibility; }

	/** The meteor shower active on the world's date (the style's showers; none in the gaps), its rate and radiant. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	FDreamSkyShowerActivity GetMeteorShower() const { return MeteorShower; }

	/** Meteors an hour over the whole sky as drawn now (sporadic and the shower's, as the night and the style show
	 *  them: 0 by day). */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	float GetMeteorRate() const { return MeteorRate; }

	/** True once a sky has been applied in this world. */
	UFUNCTION(BlueprintPure, Category = "Dream Sky")
	bool HasLighting() const { return bHasApplied; }

	/** Eases From toward To (colours and intensities; directions and the day phase are taken from To). */
	static FDreamSkyLighting BlendLighting(const FDreamSkyLighting& From, const FDreamSkyLighting& To, float Alpha);

	// ---- Sky modifiers ------------------------------------------------------------------------

	/**
	 * Adds a sky modifier, fading in over FadeInSeconds (below 0: the modifier's own FadeInSeconds). One already in the
	 * stack fades back in from where it stands.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Sky|Modifiers")
	void AddModifier(UDreamSkyModifier* Modifier, float FadeInSeconds = -1.0f);

	/** Fades a sky modifier out over FadeOutSeconds (below 0: its own FadeOutSeconds) and drops it. */
	UFUNCTION(BlueprintCallable, Category = "Dream Sky|Modifiers")
	void RemoveModifier(UDreamSkyModifier* Modifier, float FadeOutSeconds = -1.0f);

	/**
	 * Holds a sky modifier at Alpha (0..1, times its MaxAlpha), reached over Seconds without the fade's shape: for a
	 * modifier something else drives, a player's distance to a place, say. Adds it when it is not in the stack; at 0 it
	 * stays in the stack until removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Sky|Modifiers")
	void SetModifierAlpha(UDreamSkyModifier* Modifier, float Alpha, float Seconds = 0.0f);

	/** Fades every sky modifier out (FadeOutSeconds below 0: each over its own) and drops them. */
	UFUNCTION(BlueprintCallable, Category = "Dream Sky|Modifiers")
	void ClearModifiers(float FadeOutSeconds = -1.0f);

	/** How much a sky modifier takes effect now (0 when it is not in the stack). */
	UFUNCTION(BlueprintPure, Category = "Dream Sky|Modifiers")
	float GetModifierAlpha(const UDreamSkyModifier* Modifier) const;

	/** The sky modifiers in the stack, in the order they go on (fading ones included). */
	UFUNCTION(BlueprintPure, Category = "Dream Sky|Modifiers")
	TArray<UDreamSkyModifier*> GetModifiers() const;

	/**
	 * Advances the modifiers' fades by DeltaTime seconds (with bAtOnce, as in cinematic updates, each is where it is
	 * heading at once) and applies those in effect, lowest priority first.
	 */
	void ApplyModifiers(FDreamSkyLighting& Lighting, FDreamSkyLook& InOutLook, float DeltaTime, bool bAtOnce = false);

	/**
	 * Holds a modifier at Alpha for this update on behalf of Source (the weather, the sky actor's conditions): one not
	 * held again in the next update leaves the stack. Kept apart from the same modifier added by script.
	 */
	void SetDrivenModifier(UDreamSkyModifier* Modifier, float Alpha, EDreamSkyModifierSource Source);

private:
	friend class FDreamSkyCloudViewExtension;

	void Update(UDreamWorldSubsystem& Clock, float DeltaTime);
	/**
	 * The eclipses in the look (as the sun and the moon line up, through the modifiers) onto the lighting and the dome:
	 * the sun's light taken as its disk is covered, the sky's with it near totality; the moon's failing in the umbra. A
	 * coverage a modifier set gets the moon (or the umbra) a place to stand at.
	 */
	void UpdateScreen(const UDreamWorldSubsystem& Clock, const UDreamSkyStyle& Style, float DeltaTime, bool bAtOnce);
	void ApplyEclipses(FDreamSkyLighting& Lighting, FDreamSkyLook& InOutLook, const UDreamSkyStyle& Style, const FDreamCelestialState& Celestial) const;
	/** The modifiers' post processes on the sky actor, weighted by their alpha (made and dropped as they come and go). */
	void UpdateModifierPostProcess(ADreamSky* Sky);
	void CaptureSnapshot(FDreamWorldSnapshot& Snapshot) const;
	void ApplySnapshot(const FDreamWorldSnapshot& Snapshot);
	FDreamSkyActiveModifier* FindModifier(const UDreamSkyModifier* Modifier, EDreamSkyModifierSource Source = EDreamSkyModifierSource::Script);
	FDreamSkyActiveModifier& FindOrAddModifier(UDreamSkyModifier* Modifier, EDreamSkyModifierSource Source = EDreamSkyModifierSource::Script);
	/** The weather's sky modifiers (FDreamWorldEnvironment::SkyModifiers) into the stack. */
	void TakeWeatherModifiers(const FDreamWorldEnvironment& Environment);
	/** bRendererDrawsClouds: the sky cloud renderer draws the clouds (ADreamSky::DrawsClouds). */
	void WriteParameters(UDreamWorldSubsystem& Clock, const UDreamSkyStyle& Style, bool bRendererDrawsClouds);
	void LogLighting(UDreamWorldSubsystem& Clock);
	bool RecaptureIfChanged();

	TArray<TWeakObjectPtr<ADreamSky>> Skies;
	/** The lighting as eased toward the style's (the easing's state). */
	FDreamSkyLighting Eased;
	/** That with the modifiers on it: what the sky applies (and, for the frame, the flash). */
	FDreamSkyLighting Applied;
	FDreamSkyLook Look;
	float ScreenWetness = 0.0f;
	float ScreenFrost = 0.0f;
	float HeatHaze = 0.0f;
	/** The rain in the air, 0..1: as it falls, lingering once it stops. */
	float RainInAir = 0.0f;
	float Rainbow = 0.0f;
	float MilkyWayVisibility = 0.0f;
	FDreamSkyShowerActivity MeteorShower;
	float MeteorRate = 0.0f;
	bool bHasApplied = false;
	/** The sky's clouds are toon or painted (the aurora then draws in their banded tones). */
	bool bStylizedClouds = false;
	uint32 LastJumpSerial = 0;
	FDelegateHandle TickHandle;
	FDelegateHandle CaptureHandle;
	FDelegateHandle ApplyHandle;

	UPROPERTY(Transient)
	TArray<FDreamSkyActiveModifier> Modifiers;
	uint32 NextModifierOrder = 0;
	/** Counts the updates (for the driven modifiers). */
	uint32 UpdateSerial = 0;
	/** The sky actor the modifiers' post processes are on. */
	TWeakObjectPtr<ADreamSky> ModifierPostProcessSky;
	TWeakObjectPtr<UDreamWorldSubsystem> ClockSubsystem;
	TSharedPtr<FDreamSkyCloudViewExtension, ESPMode::ThreadSafe> CloudViewExtension;
	/** What the sky cloud renderer reads: the clouds' noise and the weather map (loaded when it first draws). */
	UPROPERTY(Transient)
	TObjectPtr<UTexture> CloudNoise;
	UPROPERTY(Transient)
	TObjectPtr<UTexture> WeatherMap;
	/** The panorama of the clouds the renderer draws for the dome's sky light capture (RT_DreamCloudPanorama). */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CloudPanorama;
	/** The panorama is shared by every world: this world draws it only while it owns it (it redraws every few frames). */
	DreamWorld::SharedTargets::FWriter PanoramaWriter;

	int32 RecaptureTask = 0;
	FLinearColor CapturedZenith = FLinearColor::Transparent;
	FLinearColor CapturedHorizon = FLinearColor::Transparent;
	float CapturedHaze = 0.0f;
	double LastLightingLog = -1.0e9;
};
