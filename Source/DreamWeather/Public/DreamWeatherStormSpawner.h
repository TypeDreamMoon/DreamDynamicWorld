// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWeatherZone.h"
#include "DreamWeatherStormSpawner.generated.h"

class ADreamWeatherStorm;
class UDreamWeatherPreset;

/** A kind of storm the spawner can make, and how likely it is in each season (relative weights). */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamStormKind
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm")
	TObjectPtr<UDreamWeatherPreset> Preset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm", meta = (ClampMin = "0"))
	float Spring = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm", meta = (ClampMin = "0"))
	float Summer = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm", meta = (ClampMin = "0"))
	float Autumn = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storm", meta = (ClampMin = "0"))
	float Winter = 1.0f;

	float WeightIn(int32 Season) const;
};

/** One storm a slot holds: where it starts, where it goes, how long it lives. */
USTRUCT(BlueprintType)
struct DREAMWEATHER_API FDreamStormPlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	int64 Slot = 0;

	/** World seconds it starts at. */
	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	double StartSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	float LifetimeHours = 0.0f;

	/** Where its centre is at the start, world cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	FVector2D Origin = FVector2D::ZeroVector;

	/** km per game hour. */
	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	FVector2D Velocity = FVector2D::ZeroVector;

	/** Radius, cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	float Radius = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Storm")
	TObjectPtr<UDreamWeatherPreset> Preset;

	double EndSeconds() const { return StartSeconds + double(LifetimeHours) * 3600.0; }
	bool operator==(const FDreamStormPlan& O) const
	{
		return Slot == O.Slot && StartSeconds == O.StartSeconds && LifetimeHours == O.LifetimeHours && Origin == O.Origin && Velocity == O.Velocity
			&& Radius == O.Radius && Preset == O.Preset;
	}
};

/**
 * Storms that come and go by themselves. Time is cut into slots of SlotHours; each slot holds a storm or not, by
 * the season's chance, drawn from the world seed and the slot alone, so every machine, every load and every replay
 * has the same storms at the same moments. A storm starts somewhere in its slot, is of a kind drawn by the season's
 * weights, and crosses the area on a straight line through a point within PassRadius of the spawner, from the
 * prevailing direction give or take DirectionVariation: it builds up, passes over and dies out (ADreamWeatherStorm,
 * spawned while it lives). Place the spawner where storms should pass: the play area.
 *
 * The spawner itself is the weather influence: every query, for now or any past moment, gets the storms alive at
 * that moment, computed from the seed. The storm actors it keeps while a storm lives only draw it into the weather
 * map (so the sky shows it); they do not answer queries.
 */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking, Rendering), meta = (DisplayName = "Dream Weather Storm Spawner"))
class DREAMWEATHER_API ADreamWeatherStormSpawner : public AActor, public IDreamWeatherInfluence
{
	GENERATED_BODY()

public:
	ADreamWeatherStormSpawner();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	TArray<FDreamStormKind> Kinds;

	/** Game hours per slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms", meta = (ClampMin = "1"))
	float SlotHours = 12.0f;

	/** Chance a slot holds a storm, per season (spring, summer, autumn, winter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	FVector4f SeasonChance = FVector4f(0.35f, 0.5f, 0.35f, 0.15f);

	/** Seasons are the southern hemisphere's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	bool bSouthernHemisphere = false;

	/** Game hours a storm lives (from, to). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	FVector2D LifetimeHours = FVector2D(3.0, 6.0);

	/** km per game hour (from, to). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	FVector2D SpeedRange = FVector2D(15.0, 35.0);

	/** Storm radius, cm (from, to). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	FVector2D RadiusRange = FVector2D(600000.0, 1500000.0);

	/** Fading distance around a storm, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms", meta = (ClampMin = "100"))
	float BlendDistance = 800000.0f;

	/** Storms pass over a point this far from the spawner at most, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms", meta = (ClampMin = "0"))
	float PassRadius = 500000.0f;

	/** Where storms come from, degrees (the world yaw they travel along: 0 = toward +X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	float PrevailingYaw = 0.0f;

	/** Degrees either side of the prevailing direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms", meta = (ClampMin = "0", ClampMax = "180"))
	float DirectionVariation = 40.0f;

	/** Added to the world seed (two spawners make different storms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	int32 SeedOffset = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Storms")
	int32 Priority = 20;

	/** How a planned storm moves and fades (its motion as a zone). */
	FDreamWeatherZoneMotion MakeMotion(const FDreamStormPlan& Plan) const;

	/** Where a planned storm's centre is at a world time, cm. */
	static FVector2D CenterAt(const FDreamStormPlan& Plan, double WorldSeconds);

	/** How much the storms alive at WorldSeconds have their way at Position, each (pairs of plan and weight). */
	TArray<TPair<FDreamStormPlan, float>> WeightsAt(const FVector& Position, double WorldSeconds) const;

	/** The storm a slot holds, if any: a pure function of the seed and the slot (and the spawner's settings). */
	bool PlanSlot(int32 WorldSeed, int64 Slot, FDreamStormPlan& OutPlan) const;

	/** Every storm alive at a world time. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	TArray<FDreamStormPlan> GetStormsAt(double WorldSeconds) const;

	/** The storms the spawner keeps in the world now, by slot. */
	const TMap<int64, TWeakObjectPtr<ADreamWeatherStorm>>& GetLiveStorms() const { return Live; }

	/** Bring the live storms up to date with the clock now (it runs every second on its own). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void Refresh();

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;

	//~ IDreamWeatherInfluence
	virtual void ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const override;
	virtual void ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const override;
	virtual int32 GetWeatherInfluencePriority() const override { return Priority; }
	virtual uint32 GetWeatherInfluenceStableId() const override { return GetTypeHash(GetPathName()); }
	virtual const UDreamWeatherPreset* GetWeatherInfluencePreset(const FVector& Position, float& OutWeight, EDreamWeatherBlend& OutBlend) const override;

private:
	TMap<int64, TWeakObjectPtr<ADreamWeatherStorm>> Live;
};
