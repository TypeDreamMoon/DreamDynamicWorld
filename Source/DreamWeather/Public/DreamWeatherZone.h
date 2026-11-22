// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "DreamWeatherTypes.h"
#include "DreamWeatherZone.generated.h"

class UBoxComponent;
class UCapsuleComponent;
class UDreamWeatherPreset;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamWeatherInfluence : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that bends the weather where it is: a zone, a campfire that warms the air, a dome that keeps the
 * rain out, a spell. Register the object with UDreamWeatherSubsystem::RegisterInfluence; every point query
 * then passes through it, in priority order (equal priorities by stable id). Zones additionally draw into the
 * weather map the sky reads; other influences are seen by queries only.
 */
class DREAMWEATHER_API IDreamWeatherInfluence
{
	GENERATED_BODY()

public:
	/** Bend the channel values at Position. */
	virtual void ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const = 0;

	/**
	 * Bend the channel values at Position at a world time (now, or a past moment the surfaces integrate over). An
	 * influence that moves with time (a travelling storm) places itself for WorldSeconds here; one that stands still
	 * needs nothing more than ApplyWeatherInfluence.
	 */
	virtual void ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const
	{
		ApplyWeatherInfluence(Position, InOut);
	}

	/** Higher applies later (wins). */
	virtual int32 GetWeatherInfluencePriority() const { return 0; }

	/** A number that orders influences of equal priority the same way on every run. */
	virtual uint32 GetWeatherInfluenceStableId() const = 0;

	/**
	 * The weather preset it brings at Position, if it brings one (the sky takes on the preset's sky modifier by it):
	 * how much it has its way there and how it blends. Null when it brings none.
	 */
	virtual const UDreamWeatherPreset* GetWeatherInfluencePreset(const FVector& Position, float& OutWeight, EDreamWeatherBlend& OutBlend) const { return nullptr; }
};

UENUM(BlueprintType)
enum class EDreamWeatherZoneShape : uint8
{
	Box,
	Circle,
};

/**
 * How a zone moves and lives, as a pure function of the world time from StartSeconds: the same moment puts a storm in
 * the same place on every machine, after a load, in a replay, and for a past moment the surfaces integrate over.
 * Times are game time (the weather's own clock), so a storm crosses the map in the same part of the day at any day
 * length.
 */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamWeatherZoneMotion
{
	GENERATED_BODY()

	/** The zone moves and has a lifetime (otherwise it stands where it is, always). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	bool bEnabled = false;

	/**
	 * World seconds it starts at, from where the zone was placed. Below zero it has not started: it has no effect until
	 * StartMotion (or a time is set here).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled"))
	double StartSeconds = -1.0;

	/** Drift, km per game hour (x and y in world axes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled"))
	FVector2D Velocity = FVector2D(20.0, 0.0);

	/** Follow the first spline of the zone's actor instead (at PathSpeed), from its start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled"))
	bool bFollowPath = false;

	/** km per game hour along the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled && bFollowPath", ClampMin = "0"))
	float PathSpeed = 20.0f;

	/** Back to the path's start at its end (otherwise it stays at the end). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled && bFollowPath"))
	bool bLoopPath = false;

	/** Game hours it lives; zero = for ever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled", ClampMin = "0"))
	float LifetimeHours = 6.0f;

	/** Game hours it takes to build up, and to die out at the end of its life. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled", ClampMin = "0"))
	float FadeInHours = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled", ClampMin = "0"))
	float FadeOutHours = 0.75f;

	/** Destroy the zone's actor once its life is over (game worlds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion", meta = (EditCondition = "bEnabled"))
	bool bDestroyWhenDone = false;

	/** Strength of a zone with this motion Elapsed game seconds after its start, 0..1 (1 without motion). */
	float FadeAt(double Elapsed) const;

	/** Its life is over Elapsed game seconds after its start. */
	bool IsOver(double Elapsed) const { return bEnabled && LifetimeHours > 0.0f && Elapsed >= double(LifetimeHours) * 3600.0; }
};

/**
 * A region of the world with weather of its own: a storm over the bay, a valley that is always foggy, a
 * desert. The footprint is a box or a circle seen from above (turned with the component's yaw), standing on
 * the whole height unless bLimitHeight; the zone has full effect inside and fades over BlendDistance outside.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Weather Zone"))
class DREAMWEATHER_API UDreamWeatherZoneComponent : public USceneComponent, public IDreamWeatherInfluence
{
	GENERATED_BODY()

public:
	UDreamWeatherZoneComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	EDreamWeatherZoneShape Shape = EDreamWeatherZoneShape::Box;

	/** Half size of the footprint, cm (a circle uses X as its radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone", meta = (ClampMin = "100"))
	FVector2D Extent = FVector2D(100000.0, 100000.0);

	/** Distance outside the footprint over which the zone fades out, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone", meta = (ClampMin = "100"))
	float BlendDistance = 30000.0f;

	/** Only between these heights relative to the component (otherwise from the ground to the sky). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	bool bLimitHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone", meta = (EditCondition = "bLimitHeight"))
	FVector2D HeightRange = FVector2D(-10000.0, 10000.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone", meta = (ClampMin = "0", ClampMax = "1"))
	float Weight = 1.0f;

	/** The zone's weather (optional); Channels are applied on top of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> Preset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (Categories = "Dream.Weather.Channel"))
	TMap<FGameplayTag, float> Channels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	EDreamWeatherBlend Blend = EDreamWeatherBlend::Override;

	/** Draw into the weather map, so the sky shows the zone's clouds from afar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bDrawIntoWeatherMap = true;

	/** A storm that drifts or follows a path, builds up and dies out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motion")
	FDreamWeatherZoneMotion Motion;

	/**
	 * Seen by weather queries (otherwise it only draws into the weather map: an influence that applies the same weather
	 * itself, like a storm spawner, keeps its storms' actors for the sky).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bAffectsQueries = true;

	/** How much the zone has its way at a point now, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetWeightAt(FVector Position) const;

	/** ...at a world time. */
	float GetWeightAtTime(const FVector& Position, double WorldSeconds) const;

	/** Where the footprint's centre is now (it moves with Motion; the component itself stays where it was placed). */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	FVector GetFootprintCenter() const { return GetFootprintCenterAt(NowSeconds()); }

	/** ...at a world time. */
	FVector GetFootprintCenterAt(double WorldSeconds) const;

	/** Weight times the motion's fade now. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetCurrentStrength() const { return GetStrengthAt(NowSeconds()); }

	/** ...at a world time. */
	float GetStrengthAt(double WorldSeconds) const;

	/** Start the motion now (or at AtWorldSeconds), from where the zone stands: a storm spawned by a script. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void StartMotion(double AtWorldSeconds = -1.0);

	/** The value the zone sets for a channel (registry index), or false when it leaves the channel alone. */
	bool GetChannelValue(int32 ChannelIndex, float& OutValue) const;

	/**
	 * The zone as the weather map draws it (three float4, see DreamWeatherShared.h). Only Override zones draw;
	 * returns false for the others and when bDrawIntoWeatherMap is off.
	 */
	bool MakeMapRecord(FVector4f& Z0, FVector4f& Z1, FVector4f& Z2) const { return MakeMapRecordAt(NowSeconds(), Z0, Z1, Z2); }

	/** ...where and how strong it is at a world time. */
	bool MakeMapRecordAt(double WorldSeconds, FVector4f& Z0, FVector4f& Z1, FVector4f& Z2) const;

	//~ IDreamWeatherInfluence
	virtual void ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const override;
	virtual void ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const override;
	virtual int32 GetWeatherInfluencePriority() const override { return Priority; }
	virtual uint32 GetWeatherInfluenceStableId() const override { return StableId; }
	virtual const UDreamWeatherPreset* GetWeatherInfluencePreset(const FVector& Position, float& OutWeight, EDreamWeatherBlend& OutBlend) const override;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** The world clock's time (zero without one). */
	double NowSeconds() const;

	uint32 StableId = 0;
};

/** A weather zone you can drop in a level. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Weather Zone"))
class DREAMWEATHER_API ADreamWeatherZone : public AActor
{
	GENERATED_BODY()

public:
	ADreamWeatherZone();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zone")
	TObjectPtr<UDreamWeatherZoneComponent> Zone;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> BoxOutline;

	UPROPERTY()
	TObjectPtr<UCapsuleComponent> CircleOutline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};

/**
 * A storm that moves: a round zone of storm weather that builds up, drifts with its Motion (or along its Path when
 * Motion.bFollowPath) and dies out, its clouds and lightning going with it. Spawn one and call StartMotion, set
 * Motion.StartSeconds, or tick bStartOnBeginPlay.
 */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Weather Storm"))
class DREAMWEATHER_API ADreamWeatherStorm : public ADreamWeatherZone
{
	GENERATED_BODY()

public:
	ADreamWeatherStorm();

	/** The way it goes when Motion.bFollowPath (world space, from its first point). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zone")
	TObjectPtr<class USplineComponent> Path;

	/** Start the motion when play begins (when it has no start time of its own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone")
	bool bStartOnBeginPlay = false;

	virtual void BeginPlay() override;
};
