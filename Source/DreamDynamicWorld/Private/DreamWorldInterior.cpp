// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldInterior.h"
#include "DreamWorldSubsystem.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWorldInterior)

// ---- The room ------------------------------------------------------------------------------------------------------

UDreamInteriorComponent::UDreamInteriorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UDreamInteriorComponent::GetWeightAt(const FVector& Position) const
{
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(Position);
	const FVector Ext = Extent.GetAbs();
	const double Depth = FMath::Min3(Ext.X - FMath::Abs(Local.X), Ext.Y - FMath::Abs(Local.Y), Ext.Z - FMath::Abs(Local.Z));
	if (Depth <= 0.0)
	{
		return 0.0f;
	}
	return FadeDistance > 0.0f ? FMath::SmoothStep(0.0f, FadeDistance, float(Depth)) : 1.0f;
}

bool UDreamInteriorComponent::HasPortal(const UDreamPortalComponent& Portal, float Margin) const
{
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(Portal.GetComponentLocation());
	const FVector Reach = Extent.GetAbs() + FVector(Margin);
	return FMath::Abs(Local.X) <= Reach.X && FMath::Abs(Local.Y) <= Reach.Y && FMath::Abs(Local.Z) <= Reach.Z;
}

void UDreamInteriorComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
	{
		Clock->RegisterInterior(this);
	}
}

void UDreamInteriorComponent::OnUnregister()
{
	if (UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
	{
		Clock->UnregisterInterior(this);
	}
	Super::OnUnregister();
}

// ---- Its openings --------------------------------------------------------------------------------------------------

UDreamPortalComponent::UDreamPortalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDreamPortalComponent::SetOpenness(float NewOpenness)
{
	NewOpenness = FMath::Clamp(NewOpenness, 0.0f, 1.0f);
	if (NewOpenness != Openness)
	{
		Openness = NewOpenness;
		NotifyChanged();
	}
}

void UDreamPortalComponent::SetVentilate(bool bNewVentilate)
{
	if (bNewVentilate != bVentilate)
	{
		bVentilate = bNewVentilate;
		NotifyChanged();
	}
}

FBox UDreamPortalComponent::GetBounds() const
{
	const FVector Half(Depth * 0.5, Width * 0.5, Height * 0.5);
	return FBox(-Half, Half).TransformBy(FTransform(GetComponentQuat(), GetComponentLocation()));
}

void UDreamPortalComponent::NotifyChanged()
{
	if (UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
	{
		Clock->NotifyPortalChanged(GetBounds());
	}
}

void UDreamPortalComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
	{
		Clock->RegisterPortal(this);
		Clock->NotifyPortalChanged(GetBounds());
	}
}

void UDreamPortalComponent::OnUnregister()
{
	if (UDreamWorldSubsystem* Clock = GetWorld() ? GetWorld()->GetSubsystem<UDreamWorldSubsystem>() : nullptr)
	{
		Clock->UnregisterPortal(this);
		Clock->NotifyPortalChanged(GetBounds());
	}
	Super::OnUnregister();
}

// ---- Actors --------------------------------------------------------------------------------------------------------

namespace DreamWorldInteriorImpl
{
#if WITH_EDITORONLY_DATA
	static UBoxComponent* MakeOutline(AActor& Owner, USceneComponent* Parent, const FColor& Color)
	{
		UBoxComponent* Outline = Owner.CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("Outline"));
		if (Outline)
		{
			Outline->SetupAttachment(Parent);
			Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Outline->SetHiddenInGame(true);
			Outline->ShapeColor = Color;
			Outline->bIsEditorOnly = true;
		}
		return Outline;
	}
#endif
}

ADreamInterior::ADreamInterior()
{
	PrimaryActorTick.bCanEverTick = false;
	Interior = CreateDefaultSubobject<UDreamInteriorComponent>(TEXT("Interior"));
	RootComponent = Interior;
#if WITH_EDITORONLY_DATA
	Outline = DreamWorldInteriorImpl::MakeOutline(*this, Interior, FColor(120, 200, 255));
#endif
}

void ADreamInterior::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (Outline)
	{
		Outline->SetBoxExtent(Interior->Extent, false);
	}
#endif
}

ADreamPortal::ADreamPortal()
{
	PrimaryActorTick.bCanEverTick = false;
	Portal = CreateDefaultSubobject<UDreamPortalComponent>(TEXT("Portal"));
	RootComponent = Portal;
#if WITH_EDITORONLY_DATA
	Outline = DreamWorldInteriorImpl::MakeOutline(*this, Portal, FColor(120, 255, 160));
#endif
}

void ADreamPortal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (Outline)
	{
		Outline->SetBoxExtent(FVector(Portal->Depth * 0.5, Portal->Width * 0.5, Portal->Height * 0.5), false);
	}
#endif
}

// ---- The view inside -----------------------------------------------------------------------------------------------

float DreamWorld::Interiors::PortalView(const UDreamPortalComponent& Portal, const FVector& View)
{
	// The share of the view an opening fills, about twice its solid angle over the hemisphere (it saturates when the
	// view stands in it); a metre is as near as it counts.
	const double Distance = FMath::Max(FVector::Dist(View, Portal.GetComponentLocation()), 100.0);
	const double Fill = 2.0 * double(Portal.GetArea()) / (UE_DOUBLE_PI * Distance * Distance);
	return FMath::Clamp(Portal.Openness, 0.0f, 1.0f) * float(FMath::Min(Fill, 1.0));
}

FDreamViewOcclusion DreamWorld::Interiors::Apply(const FDreamViewOcclusion& Probe, const FVector& View,
	TConstArrayView<const UDreamInteriorComponent*> Rooms, TConstArrayView<const UDreamPortalComponent*> Portals)
{
	const UDreamInteriorComponent* Room = nullptr;
	float Weight = 0.0f;
	for (const UDreamInteriorComponent* R : Rooms)
	{
		const float W = R ? R->GetWeightAt(View) : 0.0f;
		if (W > Weight)
		{
			Weight = W;
			Room = R;
		}
	}
	if (!Room)
	{
		return Probe;
	}

	// The room: a roof, walls on every side, but where its open doors and windows are (each by how much of the view it
	// fills, toward it); a skylight opens the roof.
	float Open[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // +X, -X, +Y, -Y
	float Up = 0.0f;
	float Sum = 0.0f;
	FVector Opening = FVector::ZeroVector;
	for (const UDreamPortalComponent* P : Portals)
	{
		if (!P || !Room->HasPortal(*P))
		{
			continue;
		}
		const float Seen = PortalView(*P, View);
		if (Seen <= 0.0f)
		{
			continue;
		}
		Sum += Seen;
		if (P->GetOutward().Z > 0.7)
		{
			Up += Seen;
			continue;
		}
		FVector2D To(P->GetComponentLocation() - View);
		if (!To.Normalize())
		{
			To = FVector2D(P->GetOutward());
			To.Normalize();
		}
		Open[0] += Seen * float(FMath::Max(To.X, 0.0));
		Open[1] += Seen * float(FMath::Max(-To.X, 0.0));
		Open[2] += Seen * float(FMath::Max(To.Y, 0.0));
		Open[3] += Seen * float(FMath::Max(-To.Y, 0.0));
		Opening += FVector(To.X, To.Y, 0.0) * Seen;
	}
	FDreamViewOcclusion Inside;
	Inside.bValid = true;
	Inside.Interior = 1.0f - 0.6f * FMath::Clamp(Sum, 0.0f, 1.0f);
	Inside.Overhead = 1.0f - FMath::Clamp(Up, 0.0f, 1.0f);
	Inside.PlusX = 1.0f - FMath::Clamp(2.0f * Open[0], 0.0f, 1.0f);
	Inside.MinusX = 1.0f - FMath::Clamp(2.0f * Open[1], 0.0f, 1.0f);
	Inside.PlusY = 1.0f - FMath::Clamp(2.0f * Open[2], 0.0f, 1.0f);
	Inside.MinusY = 1.0f - FMath::Clamp(2.0f * Open[3], 0.0f, 1.0f);
	Inside.Opening = Opening.GetClampedToMaxSize(1.0);

	if (!Probe.bValid)
	{
		Weight = 1.0f;   // (no rays yet: the room alone)
	}
	FDreamViewOcclusion Out;
	Out.bValid = true;
	Out.Interior = FMath::Lerp(Probe.Interior, Inside.Interior, Weight);
	Out.Overhead = FMath::Lerp(Probe.Overhead, Inside.Overhead, Weight);
	Out.PlusX = FMath::Lerp(Probe.PlusX, Inside.PlusX, Weight);
	Out.MinusX = FMath::Lerp(Probe.MinusX, Inside.MinusX, Weight);
	Out.PlusY = FMath::Lerp(Probe.PlusY, Inside.PlusY, Weight);
	Out.MinusY = FMath::Lerp(Probe.MinusY, Inside.MinusY, Weight);
	Out.Opening = Inside.Opening * double(Weight);
	return Out;
}
