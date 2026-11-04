// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherShelter.h"
#include "DreamWeatherSubsystem.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherShelter)

UDreamWeatherShelterComponent::UDreamWeatherShelterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UDreamWeatherShelterComponent::GetShelterAt(const FVector& Position) const
{
	// Distance from the box in its own frame (unscaled extent), 0 inside.
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(Position);
	const FVector Outside(FMath::Max(FMath::Abs(Local.X) - Extent.X, 0.0), FMath::Max(FMath::Abs(Local.Y) - Extent.Y, 0.0), FMath::Max(FMath::Abs(Local.Z) - Extent.Z, 0.0));
	const double Distance = Outside.Size();
	if (Distance <= 0.0)
	{
		return FMath::Clamp(Strength, 0.0f, 1.0f);
	}
	return FadeDistance > 0.0f ? FMath::Clamp(Strength, 0.0f, 1.0f) * (1.0f - FMath::SmoothStep(0.0f, FadeDistance, float(Distance))) : 0.0f;
}

void UDreamWeatherShelterComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterShelter(this);
	}
}

void UDreamWeatherShelterComponent::OnUnregister()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterShelter(this);
	}
	Super::OnUnregister();
}

ADreamWeatherShelter::ADreamWeatherShelter()
{
	PrimaryActorTick.bCanEverTick = false;
	Shelter = CreateDefaultSubobject<UDreamWeatherShelterComponent>(TEXT("Shelter"));
	RootComponent = Shelter;
#if WITH_EDITORONLY_DATA
	Outline = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("Outline"));
	if (Outline)
	{
		Outline->SetupAttachment(Shelter);
		Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Outline->SetHiddenInGame(true);
		Outline->ShapeColor = FColor(255, 170, 60);
		Outline->bIsEditorOnly = true;
	}
#endif
}

void ADreamWeatherShelter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	if (Outline)
	{
		Outline->SetBoxExtent(Shelter->Extent, false);
	}
#endif
}

UDreamLightningRodComponent::UDreamLightningRodComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDreamLightningRodComponent::OnRegister()
{
	Super::OnRegister();
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterLightningRod(this);
	}
}

void UDreamLightningRodComponent::OnUnregister()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterLightningRod(this);
	}
	Super::OnUnregister();
}
