// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceMask.h"
#include "DreamSurfaceSubsystem.h"

#include "Components/BoxComponent.h"
#include "DreamWeatherHlslShim.h"
#include "Engine/World.h"

UDreamSurfaceMaskComponent::UDreamSurfaceMaskComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

void UDreamSurfaceMaskComponent::MakeMapRecord(TArray<FVector4f>& Out) const
{
	// The footprint packed as a weather zone's (DreamWeatherShared.h), so its distance is the same function.
	const FVector Location = GetComponentLocation();
	const double Yaw = FMath::DegreesToRadians(GetComponentRotation().Yaw);
	const float S = FMath::Clamp(Strength, 0.0f, 1.0f);
	Out.Add(FVector4f(float(Location.X), float(Location.Y), float(FMath::Cos(Yaw)), float(FMath::Sin(Yaw))));
	Out.Add(FVector4f(float(Extent.X), float(Extent.Y), FadeDistance, Shape == EDreamSurfaceMaskShape::Circle ? 1.0f : 0.0f));
	Out.Add(FVector4f(FMath::Lerp(1.0f, Wetness, S), FMath::Lerp(1.0f, Water, S), FMath::Lerp(1.0f, Snow, S), FMath::Lerp(1.0f, Dust, S)));
}

FVector4f UDreamSurfaceMaskComponent::GetMultipliersAt(const FVector& Position) const
{
	TArray<FVector4f> Record;
	MakeMapRecord(Record);
	using namespace DreamWind::Hlsl;
	const float Distance = DWX_ShapeSignedDistance(float2(float(Position.X), float(Position.Y)),
		float4(Record[0].X, Record[0].Y, Record[0].Z, Record[0].W), float4(Record[1].X, Record[1].Y, Record[1].Z, Record[1].W));
	const float W = 1.0f - smoothstep(0.0f, FMath::Max(FadeDistance, 1.0f), Distance);
	return FVector4f(FMath::Lerp(1.0f, Record[2].X, W), FMath::Lerp(1.0f, Record[2].Y, W), FMath::Lerp(1.0f, Record[2].Z, W), FMath::Lerp(1.0f, Record[2].W, W));
}

void UDreamSurfaceMaskComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamSurfaceSubsystem* Surface = UDreamSurfaceSubsystem::Get(this))
	{
		Surface->RegisterMask(this);
	}
}

void UDreamSurfaceMaskComponent::OnUnregister()
{
	if (UDreamSurfaceSubsystem* Surface = UDreamSurfaceSubsystem::Get(this))
	{
		Surface->UnregisterMask(this);
	}
	Super::OnUnregister();
}

ADreamSurfaceMask::ADreamSurfaceMask()
{
	PrimaryActorTick.bCanEverTick = false;
	Mask = CreateDefaultSubobject<UDreamSurfaceMaskComponent>(TEXT("Mask"));
	RootComponent = Mask;
#if WITH_EDITORONLY_DATA
	Outline = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("Outline"));
	if (Outline)
	{
		Outline->SetupAttachment(Mask);
		Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Outline->SetHiddenInGame(true);
		Outline->ShapeColor = FColor(90, 200, 255);
		Outline->bIsEditorOnly = true;
	}
#endif
}

void ADreamSurfaceMask::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (Outline)
	{
		Outline->SetBoxExtent(FVector(Mask->Extent.X, Mask->Shape == EDreamSurfaceMaskShape::Circle ? Mask->Extent.X : Mask->Extent.Y, 100.0), false);
	}
#endif
}
