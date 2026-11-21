// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWeatherTypes.h"
#include "DreamWorldTypes.h"
#include "DreamWeatherSubsystem.generated.h"

class ADreamWeather;
class FDreamWeatherMapRenderer;
class IDreamWeatherInfluence;
class UDreamWeatherPreset;
class UDreamWeatherSelector;
class UDreamWeatherSelector_Fixed;
class UDreamWorldSubsystem;
class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamWeatherChangedEvent, UDreamWeatherPreset*, Previous, UDreamWeatherPreset*, Current);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamWeatherEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamWeatherTagsEvent, const FGameplayTagContainer&, Tags);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamLightningEvent, const FDreamLightningStrike&, Strike);

class UDreamLightningRodComponent;
class UDreamWeatherSubsystem;

DECLARE_MULTICAST_DELEGATE_FourParams(FOnDreamWeatherFrame, UDreamWorldSubsystem& /*Clock*/, UDreamWeatherSubsystem& /*Weather*/, const FDreamWeatherSample& /*View*/, float /*RealDeltaSeconds*/);

/**
 * The weather of a world.
 *
 * Every frame, in the world clock's pre-tick (before the sky):
 *  1. the selector of the level's ADreamWeather gives, per channel, the weather slot at the time shifted by that
 *     channel's lead, and the channel eases from the previous slot's value to the current one over its
 *     transition time - a pure function of the world time and seed;
 *  2. the modifier stack bends it (scripts, spells, forced weather), in priority order;
 *  3. at any point, the influences there (zones...) bend it further - SampleWeather does this anywhere, and
 *     for the view it becomes the world clock's environment (what the sky shows) and the shared material
 *     parameters; the zones are also drawn into the weather map around the camera;
 *  4. the wind follows, the precipitation around the view is updated, and the events fire.
 *
 * The weather only runs where the level has an ADreamWeather (or the settings say to run anyway).
 */
UCLASS()
class DREAMWEATHER_API UDreamWeatherSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dream Weather", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Weather"))
	static UDreamWeatherSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- What runs ----------------------------------------------------------------------------

	void RegisterWeatherActor(ADreamWeather* Actor);
	void UnregisterWeatherActor(ADreamWeather* Actor);

	/** Register an object implementing IDreamWeatherInfluence (zones register themselves). */
	void RegisterInfluence(UObject* Influence);
	void UnregisterInfluence(UObject* Influence);

	/** Register an object implementing IDreamWeatherShelterProvider (shelter components register themselves). */
	void RegisterShelter(UObject* Shelter);
	void UnregisterShelter(UObject* Shelter);

	void RegisterLightningRod(UDreamLightningRodComponent* Rod);
	void UnregisterLightningRod(UDreamLightningRodComponent* Rod);

	/**
	 * Every frame the weather runs, once the weather at the view is known: what follows the view (the rain and
	 * snow effects of DreamWeatherFX) updates here, in the same frame and order as the sky.
	 */
	FOnDreamWeatherFrame OnWeatherFrame;

	/** The weather stopped running in this world (its last weather actor went): stop what OnWeatherFrame started. */
	FSimpleMulticastDelegate OnWeatherStopped;

	/** The weather runs in this world. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	bool IsActive() const;

	/** The selector in charge (the level's, or the default when the settings run the weather anyway). */
	const UDreamWeatherSelector* GetSelector() const;

	// ---- Queries -------------------------------------------------------------------------------

	/** The weather at a point: scheduled weather, modifiers, then every influence there. CPU only, any time. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	FDreamWeatherSample SampleWeather(FVector Position) const;

	/**
	 * The weather at a point for given weather everywhere at a given world time: the influences there and the cloud
	 * gates applied to Global, the temperature of that hour and season. SampleWeather is this for now; what integrates
	 * the weather over time (the surfaces) asks it of the past.
	 */
	FDreamWeatherSample SampleWeatherAt(FVector Position, const FDreamWeatherValues& Global, double WorldSeconds) const;

	/** One channel at a point. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetChannelAt(FVector Position, FGameplayTag Channel) const;

	/** Air temperature at a point, degrees Celsius. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetTemperatureAt(FVector Position) const;

	/** How open a point is to the sky, 0 under a roof .. 1 in the open (shelters and the wind's baked level heights). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetExposureAt(FVector Position) const;

	/** Exposure below one half: a roof keeps the rain off. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	bool IsSheltered(FVector Position) const { return GetExposureAt(Position) < 0.5f; }

	/** The weather everywhere before influences (schedule and modifiers), registry order; raw, before the cloud gates on rain and lightning (SampleWeather applies them). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	const FDreamWeatherValues& GetGlobalValues() const { return GlobalValues; }

	/** What the weather is at the view this frame. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	const FDreamWeatherSample& GetViewSample() const { return ViewSample; }

	/** The weather the schedule is in now (the slot of a channel without lead), or null. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	UDreamWeatherPreset* GetCurrentWeather() const;

	/** The scheduled weather from now on, every StepHours game hours for Hours game hours. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	TArray<FDreamWeatherForecastEntry> GetForecast(float Hours = 24.0f, float StepHours = 3.0f) const;

	/** The scheduled values (before modifiers) at any world time: a pure function of the time and seed. */
	FDreamWeatherValues EvaluateSchedule(double WorldSeconds) const;

	// ---- Modifiers -----------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	FDreamWeatherModifierHandle PushModifier(const FDreamWeatherModifierDesc& Desc);

	/** Fade a modifier out over BlendOutSeconds (real) and drop it. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void RemoveModifier(FDreamWeatherModifierHandle Handle, float BlendOutSeconds = 5.0f);

	/** Change a modifier's strength over Seconds. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void SetModifierWeight(FDreamWeatherModifierHandle Handle, float Weight, float Seconds = 2.0f);

	/**
	 * Change what a modifier applies (channels, preset, blend, priority) in place; its strength and fades carry on
	 * (Desc's Weight and BlendInSeconds are not used). False when the handle is gone or fading out.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	bool UpdateModifier(FDreamWeatherModifierHandle Handle, const FDreamWeatherModifierDesc& Desc);

	/** Whether a modifier is alive (and not fading out). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	bool HasModifier(FDreamWeatherModifierHandle Handle) const;

	/**
	 * Make the weather a preset, everywhere the zones allow, until ClearForcedWeather. Replaces an earlier forcing:
	 * the new weather blends in over the old one, which holds until then (the schedule never shows through between).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	FDreamWeatherModifierHandle ForceWeather(UDreamWeatherPreset* Preset, float TransitionSeconds = 10.0f);

	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void ClearForcedWeather(float TransitionSeconds = 10.0f);

	/** Modifiers alive now (fading ones included). */
	int32 GetNumModifiers() const { return Modifiers.Num(); }

	// ---- Events --------------------------------------------------------------------------------

	/** The schedule moved to another weather (at the slot boundary). */
	UPROPERTY(BlueprintAssignable, Category = "Dream Weather|Events")
	FDreamWeatherChangedEvent OnWeatherChanged;

	/** Rain or snow began at the view. */
	UPROPERTY(BlueprintAssignable, Category = "Dream Weather|Events")
	FDreamWeatherEvent OnPrecipitationStarted;

	/** Rain or snow stopped at the view. */
	UPROPERTY(BlueprintAssignable, Category = "Dream Weather|Events")
	FDreamWeatherEvent OnPrecipitationStopped;

	/** The Dream.Weather.State.* tags at the view changed. */
	UPROPERTY(BlueprintAssignable, Category = "Dream Weather|Events")
	FDreamWeatherTagsEvent OnWeatherTagsChanged;

	/** Lightning struck (near or far; ThunderDelay says when the view hears it). */
	UPROPERTY(BlueprintAssignable, Category = "Dream Weather|Events")
	FDreamLightningEvent OnLightningStrike;

	/** Strikes so far in this world. */
	int32 GetStrikeCount() const { return StrikeCount; }

	/** The latest strike (zero when none yet). */
	const FDreamLightningStrike& GetLastStrike() const { return LastStrike; }

	/** The flash now, 0..1: a few bright strokes, then a fading afterglow. */
	float GetLightningFlash() const { return Flash; }

	/** The flash now as a share of its own peak (its strokes and afterglow, whatever the strike's strength), 0..1. */
	float GetLightningFlashShape() const { return FlashPeak > 1.0e-4f ? FMath::Clamp(Flash / FlashPeak, 0.0f, 1.0f) : 0.0f; }

	/**
	 * Strike lightning here now (a scripted strike: a spell, a story beat), whatever the weather. It flashes,
	 * is drawn by a lightning rod in reach and fires OnLightningStrike like any other. Intensity 0..1.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather|Lightning")
	void StrikeLightning(FVector Location, float Intensity = 1.0f);

	/**
	 * Brightness of a flash Seconds after its first stroke, 0..1, for a flash with these strokes and duration.
	 * StrokeTimes / StrokeLevels hold up to four strokes (the first at 0 and 1). Pure, for tests and previews.
	 */
	static float FlashEnvelope(float Seconds, float Duration, int32 Strokes, const float* StrokeTimes, const float* StrokeLevels);

	// ---- Weather map ---------------------------------------------------------------------------

	/** The map render target in use, or null. */
	UTextureRenderTarget2D* GetWeatherMap() const { return WeatherMap; }

	/** World centre of the map this frame. */
	FVector2D GetWeatherMapCenter() const { return MapCenter; }

	/** The map's records of the influences drawn into it this frame (three float4 per zone), for tests. */
	const TArray<FVector4f>& GetMapZones() const { return MapZones; }

	/** Maps drawn so far. */
	uint32 GetWeatherMapDrawCount() const;

	/** Runs one frame of the weather now (tests; the world clock normally does it). */
	void TickForTest(UDreamWorldSubsystem& Clock, float RealDeltaSeconds) { PreTick(Clock, RealDeltaSeconds); }

private:
	struct FModifier
	{
		int32 Id = 0;
		FDreamWeatherModifierDesc Desc;
		float Current = 0.0f;
		float Target = 0.0f;
		float FadeSeconds = 0.0f;
		bool bRemoving = false;
		/** See FDreamWeatherModifierState::SupersededBy. */
		int32 SupersededBy = 0;
	};

	void PreTick(UDreamWorldSubsystem& Clock, float DeltaTime);
	void AdvanceModifiers(float DeltaTime);
	void ApplyModifiers(FDreamWeatherValues& InOut) const;
	void ApplyInfluences(const FVector& Position, FDreamWeatherValues& InOut, double WorldSeconds) const;
	FDreamWeatherSample MakeSample(const FVector& Position, const FDreamWeatherValues& Values, double WorldSeconds) const;
	float TemperatureFor(const FVector& Position, float Offset, float Coverage, double WorldSeconds) const;
	void UpdateWind(const FDreamWeatherSample& View);
	void UpdateWeatherMap(UDreamWorldSubsystem& Clock);
	void FireEvents(const FDreamWeatherSample& View);
	void ProcessLightning(float DeltaTime);
	void StrikeAt(const FVector& Location, float Intensity, int32 WorldSeed, int64 Key);
	void UpdateFlashNow();
	float ExposureAt(const FVector& Position) const;
	void CaptureSnapshot(FDreamWorldSnapshot& Snapshot) const;

	/**
	 * The weathers in effect at the view and how much of each, by the rules the channels are blended by (the schedule's
	 * transition, then forced or pushed presets, then the influences at the view; an Override pulls everything under it
	 * back by its weight, the other blends add theirs), into the sky modifiers those weathers carry.
	 */
	void GatherSkyModifiers(TArray<FDreamWorldWeightedObject>& Out) const;
	void ApplySnapshot(const FDreamWorldSnapshot& Snapshot);
	TArray<IDreamWeatherInfluence*> SortedInfluences() const;
	double NowSeconds() const;
	int32 Seed() const;

	TArray<TWeakObjectPtr<ADreamWeather>> WeatherActors;
	TArray<TWeakObjectPtr<UObject>> Influences;
	TArray<TWeakObjectPtr<UObject>> Shelters;
	TArray<TWeakObjectPtr<UDreamLightningRodComponent>> Rods;

	// lightning (on its own real-time clock, so a storm looks the same at any day length)
	double LightningClock = 0.0;
	int64 LastLightningBucket = -1;
	float Flash = 0.0f;
	FVector FlashDirection = FVector::UpVector;
	// the flash in progress: when it started, how bright at most, how long, and its strokes
	double FlashStart = -1000.0;
	float FlashPeak = 0.0f;
	float FlashLength = 1.0f;
	int32 FlashStrokeCount = 0;
	float FlashStrokeTimes[4] = {};
	float FlashStrokeLevels[4] = {};
	int32 StrikeCount = 0;
	int32 ScriptedStrikes = 0;
	FDreamLightningStrike LastStrike;
	TArray<FModifier> Modifiers;
	int32 NextModifierId = 1;
	int32 ForcedModifierId = 0;

	FDreamWeatherValues GlobalValues;
	FDreamWeatherSample ViewSample;
	FVector ViewLocation = FVector::ZeroVector;
	/** Where the view looks (zero before the first view report). */
	FVector ViewForward = FVector::ZeroVector;
	bool bWasActive = false;

	TWeakObjectPtr<UDreamWorldSubsystem> ClockSubsystem;
	FDelegateHandle PreTickHandle;
	FDelegateHandle CaptureHandle;
	FDelegateHandle ApplyHandle;

	/** Stands in for the level's selector when the editor previews a preset or no weather actor exists. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamWeatherSelector_Fixed> FallbackSelector;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWeatherPreset> DefaultPreset;

	// events
	TWeakObjectPtr<UDreamWeatherPreset> LastWeather;
	bool bEventsPrimed = false;
	bool bPrecipitating = false;
	FGameplayTagContainer LastTags;

	// wind
	float LastWindSpeed = -1.0f;
	float LastWindGust = -1.0f;

	// weather map
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> WeatherMap;
	TSharedPtr<FDreamWeatherMapRenderer, ESPMode::ThreadSafe> MapRenderer;
	FVector2D MapCenter = FVector2D::ZeroVector;
	TArray<FVector4f> MapZones;
	FVector4f LastMapGlobal = FVector4f::Zero();
	TArray<FVector4f> LastMapZones;
	FVector2D LastMapCenter = FVector2D(1.0e30, 1.0e30);
	double LastMapDraw = -1.0e30;
	double LastMapResolve = -1.0e30;
};
