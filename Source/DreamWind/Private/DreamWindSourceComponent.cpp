// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindSourceComponent.h"
#include "DreamWindSubsystem.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindSourceComponent)

UDreamWindSourceComponent::UDreamWindSourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UDreamWindSourceComponent::OnRegister()
{
	Super::OnRegister();
	if (UWorld* World = GetWorld())
	{
		if (UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
		{
			Wind->RegisterSourceComponent(this);
		}
	}
	bHasLastLocation = false;
}

void UDreamWindSourceComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UDreamWindSubsystem* Wind = World->GetSubsystem<UDreamWindSubsystem>())
		{
			Wind->UnregisterSourceComponent(this);
		}
	}
	Super::OnUnregister();
}

bool UDreamWindSourceComponent::BuildSourceDesc(FDreamWindSourceDesc& OutDesc, float DeltaTime)
{
	const bool bShelter = Type == EDreamWindSourceType::Shelter;
	if (!IsActive() || (!bShelter && Radius <= 0.0f))
	{
		return false;
	}

	const FTransform& Xf = GetComponentTransform();
	const FVector Location = Xf.GetLocation();
	const FVector Forward = Xf.GetUnitAxis(EAxis::X);

	if (bShelter)
	{
		// a shelter is structure: no wake logic, and zero strength is the whole point
		LastLocation = Location;
		bHasLastLocation = true;
		OutDesc.Type = Type;
		OutDesc.Position = Location;
		OutDesc.BoxExtent = BoxExtent.GetAbs() * Xf.GetScale3D().GetAbs();
		OutDesc.BoxRotation = Xf.GetRotation().Rotator();
		OutDesc.Radius = OutDesc.GetBoundingRadius();
		OutDesc.Direction = Forward;
		OutDesc.Strength = Strength;
		OutDesc.InjectRate = InjectRate;
		OutDesc.InnerRadiusFraction = InnerRadiusFraction;
		return OutDesc.Radius > 0.0f;
	}

	float EffectiveStrength = Strength;
	if (VelocityScale > 0.0f)
	{
		FVector Velocity = FVector::ZeroVector;
		if (bHasLastLocation && DeltaTime > KINDA_SMALL_NUMBER)
		{
			Velocity = (Location - LastLocation) / DeltaTime;
		}
		// a wake is only as strong as the motion that makes it
		EffectiveStrength = Strength * FMath::Clamp(static_cast<float>(FVector::DotProduct(Velocity, Forward)) * VelocityScale, 0.0f, 1.0f);
	}
	LastLocation = Location;
	bHasLastLocation = true;

	if (FMath::IsNearlyZero(EffectiveStrength))
	{
		return false;
	}

	OutDesc.Type = Type;
	OutDesc.Position = Location;
	OutDesc.Radius = Radius;
	OutDesc.Stretch = Stretch;
	OutDesc.Direction = Forward;
	OutDesc.Strength = EffectiveStrength;
	OutDesc.InjectRate = InjectRate;
	OutDesc.InnerRadiusFraction = InnerRadiusFraction;
	return true;
}
