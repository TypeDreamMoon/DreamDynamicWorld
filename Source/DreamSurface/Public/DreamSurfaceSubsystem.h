// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DreamSurfaceTypes.h"
#include "DreamWeatherTypes.h"
#include "DreamSurfaceSubsystem.generated.h"

class FDreamSurfaceMaps;
class UDreamWeatherSubsystem;
class UDreamWorldSubsystem;

/** The steps the surface maps integrate (DreamSurfaceMap.usf), rebuilt when a step goes by or the view moves away. */
struct FDreamSurfaceMapSteps
{
	/** The step they lead up to (the one in progress). */
	int64 Step = TNumericLimits<int64>::Min();
	/**
	 * Three float4 per whole step, oldest first: the global weather (coverage, darkness, precipitation, snow ratio), the
	 * global channels (temperature offset, dust, wind, -), the air (the view's temperature less its offset, the sun's Z).
	 */
	TArray<FVector4f> Steps;
	/** The step in progress, and how much of it has gone by. */
	FVector4f PartialWeather = FVector4f::Zero();
	FVector4f PartialChannels = FVector4f::Zero();
	FVector4f PartialAir = FVector4f::Zero();
	float PartialHours = 0.0f;
};

/**
 * What the weather has left on the surfaces of a world: wet, standing water, snow, dust.
 *
 * A surface's state is the weather integrated over its past (Project Settings > Dream Surface > Memory Hours), in
 * steps of the weather's own timeline: the same moment gives the same state, whenever and however often it is asked,
 * on any machine, after a load or a jump in time. Nothing of it is saved.
 *
 * The weather of a past step: the schedule is a pure function of time, known for any past moment, but modifiers and
 * forced weather fade in real time and cannot be worked out afterwards. So each step this session runs through records
 * the global weather it had when it began (modifiers included): a modifier counts from the moment it took effect here.
 * A step the session never saw (before it started or was loaded, across a jump in time) takes the schedule's. After a
 * load the modifiers are back from the world snapshot and count from the load on. The zones apply at each step's own
 * time. The formula is DreamSurfaceShared.h: the surface map the materials read integrates the same steps on the GPU.
 */
UCLASS()
class DREAMSURFACE_API UDreamSurfaceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dream Surface", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Dream Surface"))
	static UDreamSurfaceSubsystem* Get(const UObject* WorldContextObject);

	//~ USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** The surfaces follow the weather here (it runs, and the settings have them on). */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	bool IsActive() const;

	/** What the weather has left on a surface at this point now: wet, water, snow, dust. CPU, any time. */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	FDreamSurfaceState SampleSurface(FVector Position) const;

	/** The same at any world time (the past this session saw, or the schedule's). */
	FDreamSurfaceState SampleSurfaceAt(const FVector& Position, double WorldSeconds) const;

	/** Lying snow at a point now, cm. */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	float GetSnowDepthAt(FVector Position) const;

	/** The state in the open at the view this frame (no roof, the weather there): what the materials fall back on. */
	UFUNCTION(BlueprintPure, Category = "Dream Surface")
	const FDreamSurfaceState& GetViewState() const { return ViewState; }

	/** What drives a surface at this point over the step starting at WorldSeconds (bOpenSky: as if nothing covered it). */
	FDreamSurfaceDrive DriveAt(const FVector& Position, double WorldSeconds, bool bOpenSky = false) const;

	/** One step of the shared formula (pure). */
	static FDreamSurfaceState Step(const FDreamSurfaceState& State, const FDreamSurfaceDrive& Drive, const FDreamSurfaceRates& Rates, float Hours);

	/** World seconds per step of the integral. */
	static double StepSeconds();

	/** Steps the integral spans. */
	static int32 MemorySteps();

	/** The global weather the integral takes for a step (what this session saw then, or the schedule's). */
	FDreamWeatherValues GlobalAtStep(int64 StepIndex) const;

	/** Steps whose weather this session saw. */
	int32 GetNumRecordedSteps() const { return History.Num(); }

	/** Runs one frame now (tests; the weather's frame normally does it). */
	void TickForTest(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather) { OnWeatherFrame(Clock, Weather, FDreamWeatherSample(), 0.0f); }

	/** The steps the surface maps integrate this frame. */
	const FDreamSurfaceMapSteps& GetMapSteps() const { return MapSteps; }

	/** The surface maps around the camera (render targets, placement); null before Initialize. */
	const FDreamSurfaceMaps* GetMaps() const { return Maps.Get(); }
	FDreamSurfaceMaps* GetMaps() { return Maps.Get(); }

	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	void OnWeatherFrame(UDreamWorldSubsystem& Clock, UDreamWeatherSubsystem& Weather, const FDreamWeatherSample& View, float RealDeltaSeconds);
	void MapStepRecord(int64 StepIndex, FVector4f& OutWeather, FVector4f& OutChannels, FVector4f& OutAir) const;
	void BuildMapSteps(int64 StepIndex);
	void Record(int64 StepIndex, const FDreamWeatherValues& Global);
	FDreamSurfaceState IntegrateSteps(const FVector& Position, int64 FirstStep, int64 EndStep, const FDreamSurfaceState& Start, bool bOpenSky = false) const;

	/** The global weather at each step this session saw, by step index (world seconds / StepSeconds). */
	TMap<int64, FDreamWeatherValues> History;

	FDreamSurfaceState ViewState;
	/** The view's open-sky state at the start of ViewStep, at ViewAnchor: a frame inside that step only adds its part. */
	FDreamSurfaceState ViewStepStart;
	int64 ViewStep = TNumericLimits<int64>::Min();
	FVector ViewAnchor = FVector(1.0e30);

	FDreamSurfaceMapSteps MapSteps;
	TSharedPtr<FDreamSurfaceMaps> Maps;

	TWeakObjectPtr<UDreamWeatherSubsystem> WeatherSubsystem;
	TWeakObjectPtr<UDreamWorldSubsystem> ClockSubsystem;
	FDelegateHandle FrameHandle;
	FDelegateHandle StoppedHandle;
};
