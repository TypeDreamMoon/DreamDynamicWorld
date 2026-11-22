// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherTemperature.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherSubsystem.h"
#include "DreamWorldSubsystem.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherTemperature)

UDreamTemperatureVolumeComponent::UDreamTemperatureVolumeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

float UDreamTemperatureVolumeComponent::GetWeightAt(FVector Position) const
{
	const FVector Local = GetComponentTransform().InverseTransformPositionNoScale(Position);
	double Distance = 0.0;
	if (Shape == EDreamTemperatureShape::Sphere)
	{
		Distance = Local.Size() - Extent.X;
	}
	else
	{
		const FVector Q = Local.GetAbs() - Extent;
		Distance = FVector(FMath::Max(Q.X, 0.0), FMath::Max(Q.Y, 0.0), FMath::Max(Q.Z, 0.0)).Size() + FMath::Min(Q.GetMax(), 0.0);
	}
	const float Fade = 1.0f - FMath::SmoothStep(0.0f, FMath::Max(Falloff, 1.0f), float(Distance));
	return FMath::Clamp(Weight, 0.0f, 1.0f) * (Distance <= 0.0 ? 1.0f : Fade);
}

void UDreamTemperatureVolumeComponent::ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const
{
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	ApplyWeatherInfluenceAt(Position, Clock ? Clock->GetWorldSeconds() : 0.0, InOut);
}

void UDreamTemperatureVolumeComponent::ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const
{
	const float W = GetWeightAt(Position);
	if (W <= 0.0f)
	{
		return;
	}
	using C = FDreamWeatherChannels;
	const FDreamWeatherChannelDef& Def = C::Get()[C::Temperature];
	float Offset = InOut.Get(C::Temperature);
	if (Mode == EDreamTemperatureMode::Offset)
	{
		Offset += Temperature * W;
	}
	else if (const UDreamWeatherSubsystem* Weather = UDreamWeatherSubsystem::Get(this))
	{
		// The channel is an offset: the one that makes the air here Temperature, pulled toward by the weight.
		const float Air = Weather->GetBaseTemperatureAt(Position, Offset, InOut.Get(C::CloudCoverage), WorldSeconds);
		Offset += (Temperature - Air) * W;
	}
	InOut.Set(C::Temperature, FMath::Clamp(Offset, Def.Min, Def.Max));
}

void UDreamTemperatureVolumeComponent::OnRegister()
{
	Super::OnRegister();
	StableId = GetTypeHash(GetPathName());
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterInfluence(this);
	}
}

void UDreamTemperatureVolumeComponent::OnUnregister()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterInfluence(this);
	}
	Super::OnUnregister();
}

ADreamTemperatureVolume::ADreamTemperatureVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Volume = CreateDefaultSubobject<UDreamTemperatureVolumeComponent>(TEXT("Volume"));
	RootComponent = Volume;
#if WITH_EDITORONLY_DATA
	SphereOutline = CreateEditorOnlyDefaultSubobject<USphereComponent>(TEXT("SphereOutline"));
	if (SphereOutline)
	{
		SphereOutline->SetupAttachment(Volume);
		SphereOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SphereOutline->SetHiddenInGame(true);
		SphereOutline->ShapeColor = FColor(255, 140, 60);
		SphereOutline->bIsEditorOnly = true;
	}
	BoxOutline = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("BoxOutline"));
	if (BoxOutline)
	{
		BoxOutline->SetupAttachment(Volume);
		BoxOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoxOutline->SetHiddenInGame(true);
		BoxOutline->ShapeColor = FColor(255, 140, 60);
		BoxOutline->bIsEditorOnly = true;
	}
#endif
}

void ADreamTemperatureVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	const bool bSphere = Volume->Shape == EDreamTemperatureShape::Sphere;
	if (SphereOutline)
	{
		SphereOutline->SetSphereRadius(float(Volume->Extent.X), false);
		SphereOutline->SetVisibility(bSphere);
	}
	if (BoxOutline)
	{
		BoxOutline->SetBoxExtent(Volume->Extent, false);
		BoxOutline->SetVisibility(!bSphere);
	}
#endif
}
