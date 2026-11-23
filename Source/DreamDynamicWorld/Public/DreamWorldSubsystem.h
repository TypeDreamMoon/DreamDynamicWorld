// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamWorldTypes.h"
#include "DreamWorldScheduler.h"
#include "DreamWorldCalendar.h"
#include "DreamWorldViewProbe.h"
#include "DreamWorldSubsystem.generated.h"

class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class FDreamWorldViewExtension;
class UDreamWorldSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamWorldHourEvent, int32, Hour);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamWorldDateEvent, FDateTime, Date);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamWorldEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamWorldMinuteEvent, int32, Hour, int32, Minute);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamWorldTimeEvent, FName, Name, float, Hour);

/** Fired every frame after the clock moved and the sun and moon were placed; consumers update in this order. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDreamWorldTick, UDreamWorldSubsystem& /*World*/, float /*RealDeltaSeconds*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDreamWorldCaptureSnapshot, FDreamWorldSnapshot& /*Snapshot*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDreamWorldApplySnapshot, const FDreamWorldSnapshot& /*Snapshot*/);

/**
 * The clock of a world and the sun and moon it moves.
 *
 * World time is a double count of seconds in the world's own calendar (see DreamWorldCelestial.h), advanced
 * by real time times the time scale. Everything else follows from it as a pure function, so setting the time
 * (or applying a snapshot) puts the whole sky exactly where it would have been.
 *
 * Each frame: the clock moves, the sun and moon are placed, the clock's events fire, the shared Material
 * Parameter Collection gets DDW_Time / DDW_SunDir / DDW_MoonDir, OnWorldTick runs the other subsystems
 * (the sky first), and the scheduler spends what is left of its budget.
 */
UCLASS()
class DREAMDYNAMICWORLD_API UDreamWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The world clock of the world the object lives in, or null. */
	UFUNCTION(BlueprintPure, Category = "Dream World", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream World"))
	static UDreamWorldSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableInEditor() const override;
	virtual TStatId GetStatId() const override;

	// ---- Clock --------------------------------------------------------------------------------

	/** Use a clock configuration (from the level's sky actor). bResetTime also moves the clock to its start time. */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void ApplyClockConfig(const FDreamWorldClockConfig& Config, bool bResetTime = true);

	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	const FDreamWorldClockConfig& GetClockConfig() const { return Clock; }

	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	FDateTime GetDateTime() const;

	/** Hours since midnight, 0..24. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	float GetTimeOfDay() const;

	/** Move to an hour of the current day (snaps; no events for the hours skipped). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetTimeOfDay(float Hours);

	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetDateTime(FDateTime DateTime);

	/**
	 * Run the clock to Hours over Duration real seconds. bForward runs into the next day when Hours is earlier
	 * than now; otherwise the clock takes the shorter way, backwards if need be.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void TransitionToTimeOfDay(float Hours, float Duration = 3.0f, bool bForward = true);

	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	bool IsTransitioning() const { return bTransitionActive; }

	/** Real minutes per in-game day. */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetDayLengthMinutes(float Minutes);

	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	float GetDayLengthMinutes() const { return Clock.DayLengthMinutes; }

	/** In-game seconds per real second. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	float GetTimeScale() const;

	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetPaused(bool bInPaused) { bPaused = bInPaused; }

	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	bool IsPaused() const { return bPaused; }

	/** Whether the clock runs in this world right now (game worlds: bAnimate; editor worlds: bAnimateInEditor; never while paused). */
	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	bool IsClockRunning() const;

	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetAnimateInEditor(bool bAnimate) { Clock.bAnimateInEditor = bAnimate; }

	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	int32 GetSeed() const { return Clock.Seed; }

	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetSeed(int32 NewSeed) { Clock.Seed = NewSeed; }

	double GetWorldSeconds() const { return WorldSeconds; }
	void SetWorldSeconds(double Seconds);

	/** Increases every time the clock jumps (set, snapshot, editor scrub) rather than runs; consumers snap instead of easing. */
	uint32 GetTimeJumpSerial() const { return TimeJumpSerial; }

	// ---- Driven from outside ------------------------------------------------------------------

	/**
	 * Something else drives the time from now on (a Sequencer time track): the clock stops running on its own and
	 * follows DriveWorldSeconds until every driver has ended. Drivers count by object; the same one twice is one.
	 */
	void BeginExternalDrive(const UObject* Driver);
	void EndExternalDrive(const UObject* Driver);

	/** A driver holds the clock. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	bool IsDrivenExternally() const { return ExternalDrivers.Num() > 0; }

	/**
	 * Move the clock as its driver says. A small step forward (under MaxRunSeconds of world time) runs the clock
	 * there, so the hour's events fire as if it had run; anything else is a jump (a scrub, a cut).
	 */
	void DriveWorldSeconds(double Seconds, double MaxRunSeconds = 3600.0);

	/**
	 * Cinematic updates: every frame a full update with no easing anywhere (the sky's lighting snaps, the view probe
	 * casts its whole round), so every frame of an offline render stands on its own. DreamWorld.CinematicMode -1 (the
	 * default) turns it on while a Movie Render Queue render drives the engine's frames, 1 always, 0 never.
	 */
	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	bool IsCinematicMode() const;

	/** Force cinematic updates on (or let the console variable decide again with false). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Clock")
	void SetCinematicModeOverride(bool bCinematic) { bCinematicOverride = bCinematic; }

	// ---- Sun and moon -------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Dream World|Celestial")
	void SetCelestialSettings(const FDreamCelestialSettings& Settings);

	UFUNCTION(BlueprintPure, Category = "Dream World|Celestial")
	const FDreamCelestialSettings& GetCelestialSettings() const { return Clock.Celestial; }

	UFUNCTION(BlueprintPure, Category = "Dream World|Celestial")
	const FDreamCelestialState& GetCelestial() const { return Celestial; }

	/** Day / twilight / night weights from the sun's elevation, with the default thresholds. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Celestial")
	FDreamDayPhase GetDayPhase() const;

	/** The sun is above the horizon. */
	UFUNCTION(BlueprintPure, Category = "Dream World|Celestial")
	bool IsDaytime() const { return Celestial.SunElevation > 0.0f; }

	// ---- Events -------------------------------------------------------------------------------

	/** The hour changed while the clock ran (not on jumps). */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldHourEvent OnHourChanged;

	/** The date changed while the clock ran. */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldDateEvent OnDateChanged;

	/** The sun rose above the horizon while the clock ran. */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldEvent OnSunrise;

	/** The sun went below the horizon while the clock ran. */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldEvent OnSunset;

	/** The clock jumped (set, scrubbed, snapshot applied). Hour/date/sun events are not fired for what was skipped. */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldEvent OnTimeJumped;

	/** A new game minute began (once a frame at most: a fast clock skips minutes, it reports the one it is in). */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldMinuteEvent OnMinuteChanged;

	/** The clock passed a time registered with RegisterTimeEvent (each one it passed, in the frame it passed it). */
	UPROPERTY(BlueprintAssignable, Category = "Dream World|Events")
	FDreamWorldTimeEvent OnTimeEvent;

	/** Have OnTimeEvent fire with Name each day the clock passes Hour (0..24; a name registered again moves it). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Events")
	void RegisterTimeEvent(FName Name, float Hour);

	UFUNCTION(BlueprintCallable, Category = "Dream World|Events")
	void UnregisterTimeEvent(FName Name);

	/** The date as the clock's calendar tells it (its custom calendar, or the Gregorian). */
	UFUNCTION(BlueprintPure, Category = "Dream World|Clock")
	FDreamCalendarDate GetCalendarDate() const;

	/** Native per-frame hook that runs first, after the clock moved: what others depend on (the weather) updates here. */
	FOnDreamWorldTick OnWorldPreTick;

	/** Native per-frame hook for the other subsystems, after the clock, the sun and moon and the pre-tick are up to date. */
	FOnDreamWorldTick OnWorldTick;

	// ---- Environment ----------------------------------------------------------------------------

	/** The weather where the world is looked at from (see FDreamWorldEnvironment). Set by the weather in its pre-tick. */
	void SetEnvironment(const FDreamWorldEnvironment& InEnvironment) { Environment = InEnvironment; }

	UFUNCTION(BlueprintPure, Category = "Dream World|Environment")
	const FDreamWorldEnvironment& GetEnvironment() const { return Environment; }

	// ---- Snapshots ----------------------------------------------------------------------------

	/** Everything needed to restore the clock (plus what other subsystems add through OnCaptureSnapshot). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Snapshot")
	FDreamWorldSnapshot CaptureSnapshot() const;

	UFUNCTION(BlueprintCallable, Category = "Dream World|Snapshot")
	void ApplySnapshot(const FDreamWorldSnapshot& Snapshot);

	FOnDreamWorldCaptureSnapshot OnCaptureSnapshot;
	FOnDreamWorldApplySnapshot OnApplySnapshot;

	// ---- Shared material parameters -------------------------------------------------------------

	/** Writes a vector of the shared collection (skipped when unchanged). Names come from FDreamWorldParameters. */
	void SetVectorParameter(FName Name, const FLinearColor& Value);

	/** The shared collection, or null before the Setup step created it. */
	UMaterialParameterCollection* GetParameterCollection() const { return ParameterCollection; }

	// ---- Scheduler and view -------------------------------------------------------------------

	FDreamWorldScheduler& GetScheduler() { return Scheduler; }

	/** Where the world was last looked at from: the player camera in game worlds, the editor viewport otherwise. */
	FVector GetViewLocation() const { return ViewLocation; }
	bool HasViewLocation() const { return bHasViewLocation; }

	/** Which way the view looks (when a view reported it). */
	FRotator GetViewRotation() const { return ViewRotation; }
	bool HasViewRotation() const { return bHasViewRotation; }

	/** Called by the view extension with every view of this world (game thread). */
	void ReportView(const FVector& Location, const FRotator& Rotation, bool bIsGameView);

	/** The same without a direction (tests, scripted views). */
	void ReportViewLocation(const FVector& Location, bool bIsGameView);

	// ---- View occlusion -------------------------------------------------------------------------

	/** How enclosed the view is (indoors / roof overhead / closed sides), from the view probe. */
	UFUNCTION(BlueprintPure, Category = "Dream World|View")
	const FDreamViewOcclusion& GetViewOcclusion() const { return ViewOcclusion; }

	/** Probe the whole round of rays now, without easing (after moving the view by script). */
	UFUNCTION(BlueprintCallable, Category = "Dream World|View")
	void RefreshViewOcclusion();

	const FDreamViewProbe& GetViewProbe() const { return ViewProbe; }

	// ---- Rooms and their openings -----------------------------------------------------------------

	/** The rooms (UDreamInteriorComponent) and doors and windows (UDreamPortalComponent) of this world; they register themselves. */
	void RegisterInterior(const class UDreamInteriorComponent* Interior);
	void UnregisterInterior(const class UDreamInteriorComponent* Interior);
	void RegisterPortal(const class UDreamPortalComponent* Portal);
	void UnregisterPortal(const class UDreamPortalComponent* Portal);
	TArray<const class UDreamInteriorComponent*> GetInteriors() const;
	TArray<const class UDreamPortalComponent*> GetPortals() const;

	/** The rooms' weight at a point (UDreamInteriorComponent::GetWeightAt, the largest); bWeatherOnly: the rooms that keep the weather out, 1 anywhere inside them. */
	float GetInteriorAt(const FVector& Position, bool bWeatherOnly = false) const;

	/** A portal opened, closed or moved: its bounds (the wind looks at its walls there again). */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPortalChanged, const FBox& /*Bounds*/);
	FOnPortalChanged OnPortalChanged;
	void NotifyPortalChanged(const FBox& Bounds) { OnPortalChanged.Broadcast(Bounds); }

	/** Game-thread cost of the last tick, including every OnWorldTick listener, in milliseconds. */
	double GetLastTickMs() const { return LastTickMs; }
	/** Exponential average of the same (about the last 100 frames). */
	double GetAverageTickMs() const { return AverageTickMs; }

	// ---- Whirlwinds ---------------------------------------------------------------------------

	/**
	 * Registers or updates a whirlwind (a tornado, a dust devil) for whatever draws or hears it (the sky's funnel
	 * clouds). Id 0 registers a new one; returns its id, to update it with every frame and remove it with.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dream World|Vortex")
	int32 SetVortex(int32 Id, const FDreamWorldVortex& Vortex);

	UFUNCTION(BlueprintCallable, Category = "Dream World|Vortex")
	void RemoveVortex(int32 Id);

	/** Every whirlwind registered, by id. */
	const TMap<int32, FDreamWorldVortex>& GetVortices() const { return Vortices; }

private:
	void UpdateCelestial();
	void UpdateViewProbe(float DeltaTime, bool bSnap);
	void FireClockEvents(double PrevSeconds, bool bJumped);
	void WriteClockParameters();
	void ResolveParameterCollection();
	void NoteJump();

	FDreamWorldClockConfig Clock;
	double WorldSeconds = 0.0;
	TMap<int32, FDreamWorldVortex> Vortices;
	int32 NextVortexId = 1;
	bool bPaused = false;

	bool bTransitionActive = false;
	double TransitionFrom = 0.0;
	double TransitionTo = 0.0;
	float TransitionDuration = 0.0f;
	float TransitionElapsed = 0.0f;

	FDreamCelestialState Celestial;
	FDreamWorldEnvironment Environment;
	double LastEventSeconds = 0.0;
	TMap<FName, float> TimeEvents;
	float LastSunElevation = 0.0f;
	bool bEventsPrimed = false;
	bool bJumpPending = false;
	uint32 TimeJumpSerial = 0;

	FDreamWorldScheduler Scheduler;

	TArray<TWeakObjectPtr<const UObject>> ExternalDrivers;
	bool bCinematicOverride = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> ParameterCollection;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollectionInstance> ParameterCollectionInstance;
	TMap<FName, FLinearColor> WrittenParameters;
	double LastCollectionResolve = -1000.0;

	TSharedPtr<FDreamWorldViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	bool bHasViewLocation = false;
	bool bHasViewRotation = false;
	bool bHasGameViewLocation = false;

	FDreamViewProbe ViewProbe;
	FDreamViewOcclusion ViewOcclusion;
	TArray<TWeakObjectPtr<const class UDreamInteriorComponent>> Interiors;
	TArray<TWeakObjectPtr<const class UDreamPortalComponent>> Portals;

	double LastTickMs = 0.0;
	double AverageTickMs = 0.0;
};
