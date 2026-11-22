// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DreamWindForceComponent.generated.h"

class UPrimitiveComponent;

/**
 * Lets the wind push physics bodies: put it on an actor whose primitives simulate physics (a crate, a tarp, a
 * barrel, litter). Every update it samples the wind field at each simulating body (walls, terrain, sources and gusts
 * included), takes the body's own velocity off it and applies aerodynamic drag, F = 1/2 rho Cd A |v| v, with A from
 * the body's bounds facing the wind. bTestBlocking also traces a little way upwind, so a crate behind a wall the
 * wind field does not know of stays put.
 */
UCLASS(ClassGroup = "Dream", meta = (BlueprintSpawnableComponent, DisplayName = "Dream Wind Force"))
class DREAMWIND_API UDreamWindForceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDreamWindForceComponent();

	/** Drag coefficient (a cube about 1, a flat plate about 1.2, a sphere about 0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force", meta = (ClampMin = "0", UIMax = "2"))
	float DragCoefficient = 1.0f;

	/** Scales the force (to taste: a game wind is often stronger than a real one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force", meta = (ClampMin = "0", UIMax = "50"))
	float ForceScale = 1.0f;

	/** The most force it applies to one body, newtons (0 = no limit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force", meta = (ClampMin = "0"))
	float MaxForce = 0.0f;

	/** Only bodies tagged with this (none = every simulating primitive of the actor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force")
	FName ComponentTag;

	/** Trace upwind for a wall the wind field does not know of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force")
	bool bTestBlocking = false;

	/** How far upwind, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force", meta = (EditCondition = "bTestBlocking", ClampMin = "10"))
	float BlockingDistance = 500.0f;

	/** Share of the force left behind such a wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind Force", meta = (EditCondition = "bTestBlocking", ClampMin = "0", ClampMax = "1"))
	float BlockedFactor = 0.15f;

	/** The force applied to a body last update, newtons (world space). */
	UFUNCTION(BlueprintPure, Category = "Wind Force")
	FVector GetLastForce(const UPrimitiveComponent* Body) const;

	/**
	 * Drag in newtons for a relative wind velocity (cm/s) on an area (m^2): pure, for tests and your own bodies.
	 * Air density 1.225 kg/m^3.
	 */
	static FVector Drag(const FVector& RelativeVelocityCmPerSecond, float AreaSquareMetres, float DragCoefficient);

	/** Update now (it runs on its own with the component's tick). */
	void ApplyWind();

	//~ UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	TMap<TWeakObjectPtr<const UPrimitiveComponent>, FVector> LastForces;
};
