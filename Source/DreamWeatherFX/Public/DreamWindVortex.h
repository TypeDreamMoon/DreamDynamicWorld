// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWindVortex.generated.h"

class UDreamWindSourceComponent;
class UNiagaraComponent;
class UNiagaraSystem;

/** What kind of whirlwind. */
UENUM(BlueprintType)
enum class EDreamVortexKind : uint8
{
	/** A dust devil: some metres across, tens of metres high, on a hot calm afternoon; no funnel cloud. */
	DustDevil,
	/** A tornado: a hundred metres and more across under a storm's cloud base, a funnel down to the ground. */
	Tornado,
};

/** One whirlwind a spawner's slot holds: when, where it goes, how strong. A pure function of the seed and the slot. */
USTRUCT(BlueprintType)
struct DREAMWEATHERFX_API FDreamVortexPlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	int64 Slot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	EDreamVortexKind Kind = EDreamVortexKind::DustDevil;

	/** World seconds it touches down at, and how long it lives (game seconds). */
	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	double StartSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	float LifetimeSeconds = 0.0f;

	/** Where its foot is at the start (cm) and how it moves (cm per game second). */
	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	FVector2D Velocity = FVector2D::ZeroVector;

	/** Its size against its kind's (0.7 .. 1.3), and which way it turns (+1 counter-clockwise seen from above). */
	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	float Scale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Vortex")
	int32 Spin = 1;

	double EndSeconds() const { return StartSeconds + double(LifetimeSeconds); }
	bool IsAlive(double WorldSeconds) const { return WorldSeconds >= StartSeconds && WorldSeconds < EndSeconds(); }
	FVector2D FootAt(double WorldSeconds) const { return Origin + Velocity * (WorldSeconds - StartSeconds); }
	/** How strong it is, 0..1: it spins up over its first sixth, holds, and dies out over its last quarter. */
	float StrengthAt(double WorldSeconds) const;

	bool operator==(const FDreamVortexPlan& O) const
	{
		return Slot == O.Slot && Kind == O.Kind && StartSeconds == O.StartSeconds && LifetimeSeconds == O.LifetimeSeconds && Origin == O.Origin
			&& Velocity == O.Velocity && Scale == O.Scale && Spin == O.Spin;
	}
};

/**
 * A whirlwind: a dust devil or a tornado standing on the ground. It is the wind's (a Twister source: the air turns round
 * its core, rises up it and is drawn in along the ground, on the CPU as in the wind's fluid, so whatever the wind moves
 * -- grass, leaves, rain, forces -- feels it), its dust and debris are particles the wind carries (NS_DreamWindVortex),
 * and a tornado's funnel is the sky's (the clock's vortex register). Placed by hand it stands where it is at Intensity;
 * a spawner (ADreamWindVortexSpawner) gives it a plan: it then moves, spins up and dies out on it.
 */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Wind Vortex"))
class DREAMWEATHERFX_API ADreamWindVortex : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindVortex();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vortex")
	TObjectPtr<UDreamWindSourceComponent> Wind;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vortex")
	TObjectPtr<UNiagaraComponent> Effect;

	/** Its kind (SetKind fills the sizes and speeds below with the kind's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex")
	EDreamVortexKind Kind = EDreamVortexKind::DustDevil;

	/** How far out the air feels it (cm), its core (cm: turning fastest at its edge), how high it reaches (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "50"))
	float Radius = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "10"))
	float CoreRadius = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "100"))
	float Height = 4000.0f;

	/** The wind round the core's edge, up it, in along the ground (cm/s, at full strength). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "0"))
	float TangentialSpeed = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "0"))
	float Updraft = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "0"))
	float Inflow = 300.0f;

	/** How strong it is now, 0..1 (a plan sets it; placed by hand, set it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex", meta = (ClampMin = "0", ClampMax = "1"))
	float Intensity = 1.0f;

	/** Turning clockwise seen from above (most dust devils either way, most northern tornadoes counter-clockwise). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex")
	bool bClockwise = false;

	/** Its dust and debris: puffs and pieces a second at full strength, their sizes and colours. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex|Look", meta = (ClampMin = "0"))
	float DustRate = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex|Look", meta = (ClampMin = "1"))
	float PuffSize = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex|Look")
	FLinearColor DustColor = FLinearColor(0.62f, 0.52f, 0.40f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex|Look", meta = (ClampMin = "0"))
	float DebrisRate = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex|Look", meta = (ClampMin = "1"))
	float DebrisSize = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortex|Look")
	FLinearColor DebrisColor = FLinearColor(0.45f, 0.33f, 0.20f, 1.0f);

	/** Fill the sizes, speeds and look with a kind's (a plan's Scale scales the sizes). */
	UFUNCTION(BlueprintCallable, Category = "Vortex")
	void SetKind(EDreamVortexKind NewKind, float Scale = 1.0f);

	/** Follow a plan: its foot moves on it, its strength rises and falls with it (the clock's time). */
	void SetPlan(const FDreamVortexPlan& NewPlan);
	const FDreamVortexPlan* GetPlan() const { return bHasPlan ? &Plan : nullptr; }

	/** Where the vortex is in the clock's register (the sky's funnel), 0 when not registered. */
	int32 GetVortexId() const { return VortexId; }

	/** Bring the source, the effect and the register up to date now (it runs every frame on its own). */
	void Refresh();

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	/** The ground under a point (cm), traced down from high above; the actor's own height where nothing is hit. */
	double GroundAt(const FVector2D& Foot) const;
	void Unregister();

	FDreamVortexPlan Plan;
	bool bHasPlan = false;
	int32 VortexId = 0;
};

/**
 * Whirlwinds that come and go by themselves, as the storms do (ADreamWeatherStormSpawner): time is cut into slots of
 * SlotHours; each slot holds a whirlwind or not by the season's chances (a tornado, a dust devil), drawn from the world
 * seed and the slot alone -- every machine, load and replay has the same whirlwinds at the same moments. A dust devil
 * only starts in the afternoon's hours (DustDevilHours). A whirlwind starts somewhere in its slot, crosses the area on
 * a straight line through a point within PassRadius of the spawner (from the prevailing direction give or take
 * DirectionVariation), spins up, wanders and dies out (an ADreamWindVortex, spawned while it lives).
 */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking, Rendering), meta = (DisplayName = "Dream Wind Vortex Spawner"))
class DREAMWEATHERFX_API ADreamWindVortexSpawner : public AActor
{
	GENERATED_BODY()

public:
	ADreamWindVortexSpawner();

	/** Game hours per slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices", meta = (ClampMin = "0.25"))
	float SlotHours = 2.0f;

	/** Chance a slot holds a dust devil / a tornado, per season (spring, summer, autumn, winter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector4f DustDevilChance = FVector4f(0.2f, 0.5f, 0.2f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector4f TornadoChance = FVector4f(0.05f, 0.12f, 0.03f, 0.0f);

	/** Seasons are the southern hemisphere's. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	bool bSouthernHemisphere = false;

	/** The hours of the day a dust devil may start in (the ground hottest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector2D DustDevilHours = FVector2D(11.0, 17.0);

	/** Game minutes a dust devil / a tornado lives (from, to). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector2D DustDevilMinutes = FVector2D(3.0, 12.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector2D TornadoMinutes = FVector2D(8.0, 30.0);

	/** How fast they wander, cm per game second (from, to). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector2D DustDevilSpeed = FVector2D(100.0, 400.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	FVector2D TornadoSpeed = FVector2D(600.0, 1500.0);

	/** They pass this far from the spawner at most, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices", meta = (ClampMin = "0"))
	float PassRadius = 20000.0f;

	/** The world yaw they travel along (0 = toward +X), and degrees either side of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	float PrevailingYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices", meta = (ClampMin = "0", ClampMax = "180"))
	float DirectionVariation = 60.0f;

	/** Added to the world seed (two spawners make different whirlwinds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vortices")
	int32 SeedOffset = 0;

	/** The whirlwind a slot holds, if any: a pure function of the seed, the slot and the spawner's settings. */
	bool PlanSlot(int32 WorldSeed, int64 Slot, FDreamVortexPlan& OutPlan) const;

	/** Every whirlwind alive at a world time. */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	TArray<FDreamVortexPlan> GetVorticesAt(double WorldSeconds) const;

	/** The whirlwinds the spawner keeps in the world now, by slot. */
	const TMap<int64, TWeakObjectPtr<ADreamWindVortex>>& GetLiveVortices() const { return Live; }

	/** Bring the live whirlwinds up to date with the clock now (it runs every half second on its own). */
	UFUNCTION(BlueprintCallable, Category = "Dream Weather")
	void Refresh();

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	TMap<int64, TWeakObjectPtr<ADreamWindVortex>> Live;
	float SinceRefresh = 0.0f;
};
