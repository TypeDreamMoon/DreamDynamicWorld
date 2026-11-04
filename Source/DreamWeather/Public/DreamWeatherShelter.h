// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "DreamWeatherTypes.h"
#include "DreamWeatherShelter.generated.h"

class UBoxComponent;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamWeatherShelterProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything that keeps the sky off a space: a roof, a cave, a tent, a magic dome. Register the object with
 * UDreamWeatherSubsystem::RegisterShelter; every query then sees the shelter in its Exposure, and rain and
 * snow stop landing there. Shelter components register themselves.
 */
class DREAMWEATHER_API IDreamWeatherShelterProvider
{
	GENERATED_BODY()

public:
	/** How covered a point is, 0 open .. 1 fully sheltered. */
	virtual float GetShelterAt(const FVector& Position) const = 0;
};

/**
 * A box that keeps the weather out: put it inside a house, under a bridge, in a cave mouth. Full shelter inside,
 * fading out over FadeDistance around the box. Oriented by the component's transform; the extent is unscaled.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Weather Shelter"))
class DREAMWEATHER_API UDreamWeatherShelterComponent : public USceneComponent, public IDreamWeatherShelterProvider
{
	GENERATED_BODY()

public:
	UDreamWeatherShelterComponent();

	/** Half size of the box, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shelter", meta = (ClampMin = "1"))
	FVector Extent = FVector(300.0, 300.0, 200.0);

	/** Distance outside the box over which the shelter fades out, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shelter", meta = (ClampMin = "0"))
	float FadeDistance = 100.0f;

	/** 0..1: how much the box keeps out (a thin canvas might keep out 0.8). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shelter", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	//~ IDreamWeatherShelterProvider
	virtual float GetShelterAt(const FVector& Position) const override;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

/** A shelter box you can drop in a level. */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking), meta = (DisplayName = "Dream Weather Shelter"))
class DREAMWEATHER_API ADreamWeatherShelter : public AActor
{
	GENERATED_BODY()

public:
	ADreamWeatherShelter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Shelter")
	TObjectPtr<UDreamWeatherShelterComponent> Shelter;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBoxComponent> Outline;
#endif

	virtual void OnConstruction(const FTransform& Transform) override;
};

/**
 * Draws lightning: a strike that lands within AttractRadius may be pulled to the component (with AttractChance)
 * and then reports it here. Put one on a lightning rod, a metal sword held high, a tall tree.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Lightning Rod"))
class DREAMWEATHER_API UDreamLightningRodComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamLightningRodComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FStruck, const FDreamLightningStrike&, Strike);

	/** Strikes landing within this distance (cm) may be drawn here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0"))
	float AttractRadius = 3000.0f;

	/** Chance a strike within the radius comes here, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "1"))
	float AttractChance = 0.8f;

	/** This rod was struck. */
	UPROPERTY(BlueprintAssignable, Category = "Lightning")
	FStruck OnStruck;

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};
