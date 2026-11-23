// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceVehicleGlass.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UDreamSurfaceVehicleGlassComponent::UDreamSurfaceVehicleGlassComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bTickInEditor = true;
}

void UDreamSurfaceVehicleGlassComponent::BeginPlay()
{
	Super::BeginPlay();
	bHasLast = false;
	if (bWipers)
	{
		SetWipers(true);
	}
}

void UDreamSurfaceVehicleGlassComponent::SetWipers(bool bOn)
{
	if (bOn && (!bWipers || WiperStart <= 0.0))
	{
		const UWorld* World = GetWorld();
		WiperStart = World ? World->GetTimeSeconds() : 0.0;
	}
	bWipers = bOn;
	UpdateGlass();
}

TArray<UPrimitiveComponent*> UDreamSurfaceVehicleGlassComponent::GetGlass() const
{
	TArray<UPrimitiveComponent*> All;
	TArray<UPrimitiveComponent*> Tagged;
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetComponents<UPrimitiveComponent>(All);
		if (!GlassTag.IsNone())
		{
			for (UPrimitiveComponent* Primitive : All)
			{
				if (Primitive->ComponentHasTag(GlassTag))
				{
					Tagged.Add(Primitive);
				}
			}
		}
	}
	return Tagged.Num() > 0 ? Tagged : All;
}

void UDreamSurfaceVehicleGlassComponent::UpdateGlass()
{
	for (UPrimitiveComponent* Glass : GetGlass())
	{
		// The velocity in the mesh's own frame (direction only: the material reads it against the mesh's local axes).
		const FVector Local = Glass->GetComponentTransform().InverseTransformVectorNoScale(Velocity);
		Glass->SetCustomPrimitiveDataVector4(DataIndex, FVector4(Local.X, Local.Y, Local.Z, 0.0));
		Glass->SetCustomPrimitiveDataVector4(DataIndex + 4, FVector4(bWipers ? FMath::Max(0.3f, WiperPeriod) : 0.0f, WiperStart, FMath::Clamp(Heater, 0.0f, 1.0f), 0.0));
	}
}

void UDreamSurfaceVehicleGlassComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	// The vehicle's own velocity: its physics', else how far it moved this frame.
	const FVector Location = Owner->GetActorLocation();
	FVector Own = Owner->GetVelocity();
	if (Own.IsNearlyZero() && bHasLast && DeltaTime > 1.0e-4f)
	{
		Own = (Location - LastLocation) / DeltaTime;
	}
	LastLocation = Location;
	bHasLast = true;
	const FVector Next = Own + Owner->GetActorTransform().TransformVectorNoScale(ExtraVelocity);
	// Written only when something changed (each write marks the meshes' render state dirty).
	const bool bChanged = !Next.Equals(Velocity, 5.0f);
	Velocity = Next;
	if (bChanged || GetGlass().ContainsByPredicate([this](const UPrimitiveComponent* Glass)
		{
			const TArray<float>& Data = Glass->GetCustomPrimitiveData().Data;
			return Data.Num() <= DataIndex + 4 || !FMath::IsNearlyEqual(Data[DataIndex + 4], bWipers ? FMath::Max(0.3f, WiperPeriod) : 0.0f)
				|| !FMath::IsNearlyEqual(Data[DataIndex + 6], FMath::Clamp(Heater, 0.0f, 1.0f));
		}))
	{
		UpdateGlass();
	}
}
