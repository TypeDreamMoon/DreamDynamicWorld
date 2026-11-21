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

	/** How much the zone has its way at a point, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetWeightAt(FVector Position) const;

	/** The value the zone sets for a channel (registry index), or false when it leaves the channel alone. */
	bool GetChannelValue(int32 ChannelIndex, float& OutValue) const;

	/**
	 * The zone as the weather map draws it (three float4, see DreamWeatherShared.h). Only Override zones draw;
	 * returns false for the others and when bDrawIntoWeatherMap is off.
	 */
	bool MakeMapRecord(FVector4f& Z0, FVector4f& Z1, FVector4f& Z2) const;

	//~ IDreamWeatherInfluence
	virtual void ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const override;
	virtual int32 GetWeatherInfluencePriority() const override { return Priority; }
	virtual uint32 GetWeatherInfluenceStableId() const override { return StableId; }
	virtual const UDreamWeatherPreset* GetWeatherInfluencePreset(const FVector& Position, float& OutWeight, EDreamWeatherBlend& OutBlend) const override;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
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
