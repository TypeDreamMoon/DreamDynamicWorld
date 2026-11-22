// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherZone.h"
#include "DreamWeatherChannels.h"
#include "DreamWeatherHlslShim.h"
#include "DreamWeatherPreset.h"
#include "DreamWeatherSubsystem.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SplineComponent.h"
#include "DreamWorldSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherZone)

namespace DreamWeather::Zone
{
	/** The zone's footprint as the shared header reads it. */
	static void Pack(const UDreamWeatherZoneComponent& Z, FVector4f& Z0, FVector4f& Z1, double WorldSeconds)
	{
		const FVector Location = Z.GetFootprintCenterAt(WorldSeconds);
		const double Yaw = FMath::DegreesToRadians(Z.GetComponentRotation().Yaw);
		Z0 = FVector4f(float(Location.X), float(Location.Y), float(FMath::Cos(Yaw)), float(FMath::Sin(Yaw)));
		const float Radius = float(Z.Extent.X);
		Z1 = FVector4f(Radius, float(Z.Extent.Y), Z.BlendDistance, Z.Shape == EDreamWeatherZoneShape::Circle ? 1.0f : 0.0f);
	}
}

float FDreamWeatherZoneMotion::FadeAt(double Elapsed) const
{
	if (!bEnabled)
	{
		return 1.0f;
	}
	if (Elapsed < 0.0 || IsOver(Elapsed))
	{
		return 0.0f;
	}
	const double In = FadeInHours > 0.0f ? FMath::Clamp(Elapsed / (double(FadeInHours) * 3600.0), 0.0, 1.0) : 1.0;
	double Out = 1.0;
	if (LifetimeHours > 0.0f && FadeOutHours > 0.0f)
	{
		Out = FMath::Clamp((double(LifetimeHours) * 3600.0 - Elapsed) / (double(FadeOutHours) * 3600.0), 0.0, 1.0);
	}
	// Smooth at both ends: a storm gathers and breaks up rather than switching on.
	return FMath::SmoothStep(0.0f, 1.0f, float(FMath::Min(In, Out)));
}

UDreamWeatherZoneComponent::UDreamWeatherZoneComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 1.0f;
	SetMobility(EComponentMobility::Movable);
}

double UDreamWeatherZoneComponent::NowSeconds() const
{
	const UDreamWorldSubsystem* Clock = UDreamWorldSubsystem::Get(this);
	return Clock ? Clock->GetWorldSeconds() : 0.0;
}

void UDreamWeatherZoneComponent::StartMotion(double AtWorldSeconds)
{
	Motion.bEnabled = true;
	Motion.StartSeconds = AtWorldSeconds >= 0.0 ? AtWorldSeconds : NowSeconds();
	const UWorld* World = GetWorld();
	SetComponentTickEnabled(Motion.bDestroyWhenDone && World && World->IsGameWorld());
}

FVector UDreamWeatherZoneComponent::GetFootprintCenterAt(double WorldSeconds) const
{
	const FVector Origin = GetComponentLocation();
	if (!Motion.bEnabled || Motion.StartSeconds < 0.0)
	{
		return Origin;
	}
	const double Hours = FMath::Max(WorldSeconds - Motion.StartSeconds, 0.0) / 3600.0;
	if (Motion.bFollowPath)
	{
		if (const USplineComponent* Path = GetOwner() ? GetOwner()->FindComponentByClass<USplineComponent>() : nullptr)
		{
			const double Length = Path->GetSplineLength();
			double Along = Hours * double(Motion.PathSpeed) * 100000.0;
			Along = Motion.bLoopPath && Length > 1.0 ? FMath::Fmod(Along, Length) : FMath::Min(Along, Length);
			const FVector OnPath = Path->GetLocationAtDistanceAlongSpline(float(Along), ESplineCoordinateSpace::World);
			return FVector(OnPath.X, OnPath.Y, Origin.Z);
		}
	}
	return Origin + FVector(Motion.Velocity.X, Motion.Velocity.Y, 0.0) * (Hours * 100000.0);
}

float UDreamWeatherZoneComponent::GetStrengthAt(double WorldSeconds) const
{
	const float Base = FMath::Clamp(Weight, 0.0f, 1.0f);
	if (!Motion.bEnabled)
	{
		return Base;
	}
	// Not started yet: no effect until it is.
	return Motion.StartSeconds < 0.0 ? 0.0f : Base * Motion.FadeAt(WorldSeconds - Motion.StartSeconds);
}

void UDreamWeatherZoneComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Motion.bDestroyWhenDone && Motion.StartSeconds >= 0.0 && Motion.IsOver(NowSeconds() - Motion.StartSeconds))
	{
		SetComponentTickEnabled(false);
		if (AActor* Owner = GetOwner())
		{
			Owner->Destroy();
		}
	}
}

float UDreamWeatherZoneComponent::GetWeightAt(FVector Position) const
{
	return GetWeightAtTime(Position, NowSeconds());
}

float UDreamWeatherZoneComponent::GetWeightAtTime(const FVector& Position, double WorldSeconds) const
{
	if (bLimitHeight)
	{
		const double Dz = Position.Z - GetComponentLocation().Z;
		if (Dz < HeightRange.X || Dz > HeightRange.Y)
		{
			return 0.0f;
		}
	}
	const float Strength = GetStrengthAt(WorldSeconds);
	if (Strength <= 0.0f)
	{
		return 0.0f;
	}
	FVector4f Z0, Z1;
	DreamWeather::Zone::Pack(*this, Z0, Z1, WorldSeconds);
	using namespace DreamWind::Hlsl;
	return DWX_ZoneWeight(float2(float(Position.X), float(Position.Y)), ToHlsl(Z0), ToHlsl(Z1), Strength);
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
	ApplyWeatherInfluenceAt(Position, NowSeconds(), InOut);
}

void UDreamWeatherZoneComponent::ApplyWeatherInfluenceAt(const FVector& Position, double WorldSeconds, FDreamWeatherValues& InOut) const
{
	const float W = bAffectsQueries ? GetWeightAtTime(Position, WorldSeconds) : 0.0f;
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
	OutWeight = Preset && bAffectsQueries ? GetWeightAt(Position) : 0.0f;
	OutBlend = Blend;
	return Preset;
}

bool UDreamWeatherZoneComponent::MakeMapRecordAt(double WorldSeconds, FVector4f& Z0, FVector4f& Z1, FVector4f& Z2) const
{
	const float Strength = GetStrengthAt(WorldSeconds);
	if (!bDrawIntoWeatherMap || Blend != EDreamWeatherBlend::Override || Strength <= 0.0f)
	{
		return false;
	}
	DreamWeather::Zone::Pack(*this, Z0, Z1, WorldSeconds);
	float Coverage = -1.0f, Darkness = -1.0f, Precipitation = -1.0f;
	GetChannelValue(FDreamWeatherChannels::CloudCoverage, Coverage);
	GetChannelValue(FDreamWeatherChannels::CloudDarkness, Darkness);
	GetChannelValue(FDreamWeatherChannels::Precipitation, Precipitation);
	Z2 = FVector4f(Coverage, Darkness, Precipitation, Strength);
	return Coverage >= 0.0f || Darkness >= 0.0f || Precipitation >= 0.0f;
}

void UDreamWeatherZoneComponent::OnRegister()
{
	Super::OnRegister();
	StableId = GetTypeHash(GetPathName());
	const UWorld* World = GetWorld();
	SetComponentTickEnabled(Motion.bEnabled && Motion.bDestroyWhenDone && World && World->IsGameWorld());
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

ADreamWeatherStorm::ADreamWeatherStorm()
{
	Zone->Shape = EDreamWeatherZoneShape::Circle;
	// 15 km in radius, fading out over 10 km around it.
	Zone->Extent = FVector2D(1500000.0, 1500000.0);
	Zone->BlendDistance = 1000000.0f;
	Zone->Priority = 10;
	Zone->Motion.bEnabled = true;
	Zone->Motion.bDestroyWhenDone = true;
	static ConstructorHelpers::FObjectFinderOptional<UDreamWeatherPreset> StormPreset(TEXT("/DreamDynamicWorld/Weather/Presets/WP_Storm.WP_Storm"));
	Zone->Preset = StormPreset.Get();
	Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
	Path->SetupAttachment(Zone);
	Path->ClearSplinePoints(false);
	Path->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
	Path->AddSplinePoint(FVector(2000000.0, 0.0, 0.0), ESplineCoordinateSpace::Local, true);
}

void ADreamWeatherStorm::BeginPlay()
{
	Super::BeginPlay();
	if (bStartOnBeginPlay && Zone->Motion.StartSeconds < 0.0)
	{
		Zone->StartMotion();
	}
}
