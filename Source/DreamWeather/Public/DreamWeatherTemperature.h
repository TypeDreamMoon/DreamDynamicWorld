// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "DreamWeatherZone.h"
#include "DreamWeatherTemperature.generated.h"

class UBoxComponent;
class USphereComponent;

UENUM(BlueprintType)
enum class EDreamTemperatureMode : uint8
{
	/** Warmer or colder by Temperature degrees: a campfire, a cold cellar. */
	Offset,
	/** The temperature becomes Temperature: a heated room, a cave that keeps the same all year. */
	Replace,
};

UENUM(BlueprintType)
enum class EDreamTemperatureShape : uint8
{
	Sphere,
	Box,
};

/**
 * A place with a temperature of its own: a campfire's warmth, a heated house, a cave. A weather influence like a
 * zone, so every query sees it (SampleWeather, GetTemperatureAt, the characters' weather status): full effect inside
 * the shape, fading out over Falloff around it.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Temperature Volume"))
class DREAMWEATHER_API UDreamTemperatureVolumeComponent : public USceneComponent, public IDreamWeatherInfluence
{
	GENERATED_BODY()

public:
	UDreamTemperatureVolumeComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	EDreamTemperatureMode Mode = EDreamTemperatureMode::Offset;

	/** Degrees Celsius: added (Offset) or the temperature itself (Replace). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	float Temperature = 15.0f;

	/** How much of it applies, 0..1 (Replace at 0.7 is a house that keeps out most of the cold: its insulation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (ClampMin = "0", ClampMax = "1"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	EDreamTemperatureShape Shape = EDreamTemperatureShape::Sphere;

	/** Sphere: X is the radius. Box: half size, cm (turned with the component). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (ClampMin = "1"))
	FVector Extent = FVector(300.0);

	/** Distance outside the shape over which it fades out, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature", meta = (ClampMin = "0"))
	float Falloff = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Temperature")
	int32 Priority = 100;

	/** How much it has its way at a point, 0..Weight. */
	UFUNCTION(BlueprintPure, Category = "Dream Weather")
	float GetWeightAt(FVector Position) const;

	//~ IDreamWeatherInfluence
	virtual void ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const override;
	virtual void ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const override;
	virtual int32 GetWeatherInfluencePriority() const override { return Priority; }
	virtual uint32 GetWeatherInfluenceStableId() const override { return StableId; }

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	uint32 StableId = 0;
};

/** A temperature volume you can drop in a level. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Temperature Volume"))
class DREAMWEATHER_API ADreamTemperatureVolume : public AActor
{
	GENERATED_BODY()

public:
	ADreamTemperatureVolume();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Temperature")
	TObjectPtr<UDreamTemperatureVolumeComponent> Volume;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<USphereComponent> SphereOutline;

	UPROPERTY()
	TObjectPtr<UBoxComponent> BoxOutline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};
