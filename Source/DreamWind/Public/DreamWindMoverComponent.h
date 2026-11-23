// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DreamWindTypes.h"
#include "DreamWindMoverComponent.generated.h"

/** What a moving body does to the air. */
UENUM(BlueprintType)
enum class EDreamWindMoverShape : uint8
{
	/** A body (character, vehicle): the air inside it moves with it, and it drags a wake behind. */
	Capsule = 0,
	/** A rotor: a downwash along the component's -Z, spreading out over the ground beneath it. */
	Rotor = 1,
};

/**
 * Something moving through the wind fluid: a character or a vehicle (a capsule standing along the component's Z, centred
 * on it), or a rotor's downwash. It follows the component wherever it goes, its velocity taken from how it moved since
 * last frame. It is the visual wind only: the fluid on the GPU (grass parting where someone walks, a helicopter flattening
 * a ring), not the CPU sample gameplay reads. The nearest MaxMovers to the camera count.
 */
UCLASS(ClassGroup = (DreamWind), meta = (BlueprintSpawnableComponent), HideCategories = (Mobility, LOD, Collision, Physics, Rendering))
class DREAMWIND_API UDreamWindMoverComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDreamWindMoverComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind")
	EDreamWindMoverShape Shape = EDreamWindMoverShape::Capsule;

	/** The capsule's radius, or the rotor disc's, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "1", Units = "cm"))
	float Radius = 40.0f;

	/** The capsule's half height along the component's Z (to the end of its straight part), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", Units = "cm", EditCondition = "Shape == EDreamWindMoverShape::Capsule"))
	float HalfHeight = 50.0f;

	/** The air inside the capsule moves with it: a body standing in the wind has a lee. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (EditCondition = "Shape == EDreamWindMoverShape::Capsule"))
	bool bObstacle = true;

	/** How hard the wake behind pulls the air along: the speed it wants as a fraction of the body's (a character 0.5, a vehicle 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", UIMax = "2", EditCondition = "Shape == EDreamWindMoverShape::Capsule"))
	float Push = 0.5f;

	/** How far behind the wake reaches, in multiples of its width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "1", UIMax = "8", EditCondition = "Shape == EDreamWindMoverShape::Capsule"))
	float WakeLength = 3.0f;

	/** The downwash's speed, cm/s, along the component's -Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "0", Units = "cm/s", EditCondition = "Shape == EDreamWindMoverShape::Rotor"))
	float RotorThrust = 1500.0f;

	/** How far down the downwash reaches before it spreads, cm (past the ground it spreads over the ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamWind", meta = (ClampMin = "1", Units = "cm", EditCondition = "Shape == EDreamWindMoverShape::Rotor"))
	float RotorReach = 1000.0f;

	/** The body's velocity last frame, cm/s (how the component moved). */
	FVector GetMoverVelocity() const { return Velocity; }

	/** This frame's records for the fluid (the body, its wake, or the downwash); advances the velocity. */
	void BuildSources(TArray<FDreamWindSourceDesc>& Out, float DeltaTime);

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	FVector LastLocation = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	bool bHasLastLocation = false;
};
