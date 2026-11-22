// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindForceComponent.h"
#include "DreamWindSubsystem.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindForceComponent)

UDreamWindForceComponent::UDreamWindForceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

FVector UDreamWindForceComponent::Drag(const FVector& RelativeVelocityCmPerSecond, float AreaSquareMetres, float InDragCoefficient)
{
	constexpr double AirDensity = 1.225;
	const FVector V = RelativeVelocityCmPerSecond / 100.0;   // m/s
	// Newtons; the engine's forces are kg cm / s^2, a hundred to the newton.
	return 0.5 * AirDensity * double(InDragCoefficient) * double(AreaSquareMetres) * V.Size() * V;
}

FVector UDreamWindForceComponent::GetLastForce(const UPrimitiveComponent* Body) const
{
	const FVector* Force = LastForces.Find(Body);
	return Force ? *Force : FVector::ZeroVector;
}

void UDreamWindForceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyWind();
}

void UDreamWindForceComponent::ApplyWind()
{
	AActor* Owner = GetOwner();
	const UDreamWindSubsystem* Wind = UDreamWindSubsystem::Get(this);
	UWorld* World = GetWorld();
	if (!Owner || !Wind || !World)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Bodies(Owner);
	for (UPrimitiveComponent* Body : Bodies)
	{
		if (!Body->IsSimulatingPhysics() || (!ComponentTag.IsNone() && !Body->ComponentHasTag(ComponentTag)))
		{
			continue;
		}
		const FVector Center = Body->Bounds.Origin;
		const FVector Air = Wind->SampleWindVelocity(Center);
		const FVector Relative = Air - Body->GetPhysicsLinearVelocity();
		const FVector Along = Relative.GetSafeNormal();
		if (Along.IsZero())
		{
			LastForces.Add(Body, FVector::ZeroVector);
			continue;
		}
		// The area the body shows the wind: its bounding box seen along the wind.
		const FVector Box = Body->Bounds.BoxExtent * 2.0 / 100.0;   // m
		const float Area = float(Box.Y * Box.Z * FMath::Abs(Along.X) + Box.X * Box.Z * FMath::Abs(Along.Y) + Box.X * Box.Y * FMath::Abs(Along.Z));
		FVector Force = Drag(Relative, Area, DragCoefficient) * double(ForceScale);
		if (bTestBlocking)
		{
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(DreamWindForce), false, Owner);
			if (World->LineTraceSingleByChannel(Hit, Center, Center - Along * double(BlockingDistance), ECC_Visibility, Query))
			{
				Force *= double(BlockedFactor);
			}
		}
		if (MaxForce > 0.0f)
		{
			Force = Force.GetClampedToMaxSize(MaxForce);
		}
		LastForces.Add(Body, Force);
		Body->AddForce(Force * 100.0);
	}
}
