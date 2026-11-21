// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherZone.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherHlslShim.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSubsystem.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherZone)

namespace DreamWeather::Zone
{
	/** The zone's footprint as the shared header reads it. */
	static void Pack(const UDreamWeatherZoneComponent& Z, FVector4f& Z0, FVector4f& Z1)
	{
		const FVector Location = Z.GetComponentLocation();
		const double Yaw = FMath::DegreesToRadians(Z.GetComponentRotation().Yaw);
		Z0 = FVector4f(float(Location.X), float(Location.Y), float(FMath::Cos(Yaw)), float(FMath::Sin(Yaw)));
		const float Radius = float(Z.Extent.X);
		Z1 = FVector4f(Radius, float(Z.Extent.Y), Z.BlendDistance, Z.Shape == EDreamWeatherZoneShape::Circle ? 1.0f : 0.0f);
	}
}

UDreamWeatherZoneComponent::UDreamWeatherZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetMobility(EComponentMobility::Movable);
}

float UDreamWeatherZoneComponent::GetWeightAt(FVector Position) const
{
	if (bLimitHeight)
	{
		const double Dz = Position.Z - GetComponentLocation().Z;
		if (Dz < HeightRange.X || Dz > HeightRange.Y)
		{
			return 0.0f;
		}
	}
	FVector4f Z0, Z1;
	DreamWeather::Zone::Pack(*this, Z0, Z1);
	using namespace DreamWind::Hlsl;
	return DWX_ZoneWeight(float2(float(Position.X), float(Position.Y)), ToHlsl(Z0), ToHlsl(Z1), FMath::Clamp(Weight, 0.0f, 1.0f));
}

bool UDreamWeatherZoneComponent::GetChannelValue(int32 ChannelIndex, float& OutValue) const
{
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	if (!Defs.IsValidIndex(ChannelIndex))
	{
		return false;
	}
	if (const float* Own = Channels.Find(Defs[ChannelIndex].Channel))
	{
		OutValue = *Own;
		return true;
	}
	if (Preset)
	{
		OutValue = Preset->GetValue(ChannelIndex);
		return true;
	}
	return false;
}

void UDreamWeatherZoneComponent::ApplyWeatherInfluence(const FVector& Position, FDreamWeatherValues& InOut) const
{
	const float W = GetWeightAt(Position);
	if (W <= 0.0f)
	{
		return;
	}
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	for (int32 i = 0; i < InOut.Values.Num() && i < Defs.Num(); ++i)
	{
		float Value;
		if (GetChannelValue(i, Value))
		{
			InOut.Values[i] = FMath::Clamp(FDreamWeatherChannels::Combine(Blend, InOut.Values[i], Value, W), Defs[i].Min, Defs[i].Max);
		}
	}
}

const UDreamWeatherPreset* UDreamWeatherZoneComponent::GetWeatherInfluencePreset(const FVector& Position, float& OutWeight, EDreamWeatherBlend& OutBlend) const
{
	OutWeight = Preset ? GetWeightAt(Position) : 0.0f;
	OutBlend = Blend;
	return Preset;
}

bool UDreamWeatherZoneComponent::MakeMapRecord(FVector4f& Z0, FVector4f& Z1, FVector4f& Z2) const
{
	if (!bDrawIntoWeatherMap || Blend != EDreamWeatherBlend::Override || Weight <= 0.0f)
	{
		return false;
	}
	DreamWeather::Zone::Pack(*this, Z0, Z1);
	float Coverage = -1.0f, Darkness = -1.0f, Precipitation = -1.0f;
	GetChannelValue(FDreamWeatherChannels::CloudCoverage, Coverage);
	GetChannelValue(FDreamWeatherChannels::CloudDarkness, Darkness);
	GetChannelValue(FDreamWeatherChannels::Precipitation, Precipitation);
	Z2 = FVector4f(Coverage, Darkness, Precipitation, FMath::Clamp(Weight, 0.0f, 1.0f));
	return Coverage >= 0.0f || Darkness >= 0.0f || Precipitation >= 0.0f;
}

void UDreamWeatherZoneComponent::OnRegister()
{
	Super::OnRegister();
	StableId = GetTypeHash(GetPathName());
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterInfluence(this);
	}
}

void UDreamWeatherZoneComponent::OnUnregister()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterInfluence(this);
	}
	Super::OnUnregister();
}

ADreamWeatherZone::ADreamWeatherZone()
{
	PrimaryActorTick.bCanEverTick = false;
	Zone = CreateDefaultSubobject<UDreamWeatherZoneComponent>(TEXT("Zone"));
	RootComponent = Zone;

#if WITH_EDITORONLY_DATA
	// Outlines of the footprint in the editor (no collision, never in game).
	BoxOutline = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("BoxOutline"));
	if (BoxOutline)
	{
		BoxOutline->SetupAttachment(Zone);
		BoxOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BoxOutline->SetHiddenInGame(true);
		BoxOutline->ShapeColor = FColor(80, 140, 255);
		BoxOutline->bIsEditorOnly = true;
	}
	CircleOutline = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("CircleOutline"));
	if (CircleOutline)
	{
		CircleOutline->SetupAttachment(Zone);
		CircleOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CircleOutline->SetHiddenInGame(true);
		CircleOutline->ShapeColor = FColor(80, 140, 255);
		CircleOutline->bIsEditorOnly = true;
	}
#endif
}

void ADreamWeatherZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
#if WITH_EDITORONLY_DATA
	const float HalfHeight = Zone->bLimitHeight ? float(FMath::Max(1.0, (Zone->HeightRange.Y - Zone->HeightRange.X) * 0.5)) : 20000.0f;
	const bool bCircle = Zone->Shape == EDreamWeatherZoneShape::Circle;
	if (BoxOutline)
	{
		BoxOutline->SetBoxExtent(FVector(Zone->Extent.X, Zone->Extent.Y, HalfHeight), false);
		BoxOutline->SetVisibility(!bCircle);
	}
	if (CircleOutline)
	{
		CircleOutline->SetCapsuleSize(float(Zone->Extent.X), FMath::Max(HalfHeight, float(Zone->Extent.X)), false);
		CircleOutline->SetVisibility(bCircle);
	}
#endif
}
