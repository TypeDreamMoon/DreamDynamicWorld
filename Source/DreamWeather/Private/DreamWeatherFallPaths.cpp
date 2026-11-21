// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherFallPaths.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"

namespace DreamWeather::FallPaths
{
	/** A well-spread pseudo random number in [0, 1) from a sequence number and a salt. */
	static float Rand(uint32 Seq, uint32 Salt)
	{
		uint32 H = Seq * 0x9E3779B9u ^ (Salt * 0x85EBCA6Bu);
		H ^= H >> 16;
		H *= 0x7FEB352Du;
		H ^= H >> 15;
		H *= 0x846CA68Bu;
		H ^= H >> 16;
		return float(H >> 8) * (1.0f / 16777216.0f);
	}

	/** Foliage painted with the foliage tool, recognised by class name (no dependency on the Foliage module). */
	static bool IsPaintedFoliage(const UPrimitiveComponent* Component)
	{
		static const FName FoliageClass(TEXT("FoliageInstancedStaticMeshComponent"));
		for (const UClass* Class = Component->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == FoliageClass)
			{
				return true;
			}
		}
		return false;
	}

	static FDreamWeatherFallPaths::ESurface Classify(const FHitResult& Hit, const FVector& ViewLocation)
	{
		using ESurface = FDreamWeatherFallPaths::ESurface;
		if (const UPrimitiveComponent* Component = Hit.GetComponent())
		{
			const AActor* Owner = Component->GetOwner();
			if (Component->ComponentHasTag(FDreamWeatherFallPaths::LeavesTag) || (Owner && Owner->ActorHasTag(FDreamWeatherFallPaths::LeavesTag))
				|| IsPaintedFoliage(Component))
			{
				return ESurface::Leaves;
			}
			if (Component->ComponentHasTag(FDreamWeatherFallPaths::RoofTag) || (Owner && Owner->ActorHasTag(FDreamWeatherFallPaths::RoofTag)))
			{
				return ESurface::Roof;
			}
		}
		return Hit.ImpactPoint.Z > ViewLocation.Z + FDreamWeatherFallPaths::RoofHeight ? ESurface::Roof : ESurface::Ground;
	}
}

const FName FDreamWeatherFallPaths::LeavesTag(TEXT("DreamWeather.Leaves"));
const FName FDreamWeatherFallPaths::RoofTag(TEXT("DreamWeather.Roof"));

void FDreamWeatherFallPaths::Reset()
{
	Ends.Reset();
	Normals.Reset();
	Landed.Reset();
	Surfaces.Reset();
	Next = 0;
	Budget = 0.0f;
	++Generation;
}

int32 FDreamWeatherFallPaths::CountLanded() const
{
	int32 Count = 0;
	for (const uint8 L : Landed)
	{
		Count += L;
	}
	return Count;
}

void FDreamWeatherFallPaths::Fill(UWorld* World, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity,
	const FParams& Params, TFunctionRef<float(const FVector&)> Shelter)
{
	const int32 Capacity = FMath::Max(Params.Capacity, 1);
	Ends.SetNum(Capacity);
	Normals.SetNum(Capacity);
	Landed.SetNumZeroed(Capacity);
	Surfaces.SetNumZeroed(Capacity);
	for (int32 i = 0; i < Capacity; ++i)
	{
		TraceOne(World, i, ViewLocation, ViewForward, FallVelocity, Params, Shelter);
	}
	Next = 0;
	Budget = 0.0f;
	++Generation;
}

void FDreamWeatherFallPaths::Update(UWorld* World, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity, float DeltaTime,
	const FParams& Params, TFunctionRef<float(const FVector&)> Shelter)
{
	const int32 Capacity = FMath::Max(Params.Capacity, 1);
	if (Ends.Num() != Capacity)
	{
		Fill(World, ViewLocation, ViewForward, FallVelocity, Params, Shelter);
		return;
	}
	Budget += float(Capacity) * FMath::Max(DeltaTime, 0.0f) / FMath::Max(Params.RefreshSeconds, 0.05f);
	const int32 Count = FMath::Min(Capacity, FMath::FloorToInt(Budget));
	Budget -= float(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		TraceOne(World, Next, ViewLocation, ViewForward, FallVelocity, Params, Shelter);
		Next = (Next + 1) % Capacity;
	}
	if (Count > 0)
	{
		++Generation;
	}
}

void FDreamWeatherFallPaths::TraceOne(UWorld* World, int32 Index, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity,
	const FParams& Params, TFunctionRef<float(const FVector&)> Shelter)
{
	using namespace DreamWeather::FallPaths;
	const uint32 Seq = ++Sequence;

	// Where the drop lands, roughly: a point in a disc around the view, pulled forward (a drop behind the camera
	// is never seen). Uniform over the disc's area, then the forward half is favoured by folding.
	const float Radius = Params.Radius * FMath::Sqrt(Rand(Seq, 1u));
	float Angle = 2.0f * UE_PI * Rand(Seq, 2u);
	FVector2D Forward(ViewForward.X, ViewForward.Y);
	if (Forward.Normalize() && Rand(Seq, 3u) < FMath::Clamp(Params.ForwardBias, 0.0f, 1.0f))
	{
		// Fold the angle into the half disc ahead of the view.
		const float Ahead = float(FMath::Atan2(Forward.Y, Forward.X));
		Angle = Ahead + (Rand(Seq, 4u) - 0.5f) * UE_PI;
	}
	const FVector Ground(ViewLocation.X + Radius * FMath::Cos(Angle), ViewLocation.Y + Radius * FMath::Sin(Angle), ViewLocation.Z);

	// The path starts above it, upwind, so that it falls onto that point along the fall direction.
	FVector Dir = FallVelocity.GetSafeNormal();
	if (Dir.Z > -0.05)
	{
		Dir = FVector(0.0, 0.0, -1.0);
	}
	const double Height = Params.StartHeight;
	const FVector Start = Ground - Dir * (Height / -Dir.Z);
	const FVector Far = Start + Dir * (double(Params.MaxLength) / -Dir.Z);

	FVector End = Far;
	FVector Normal = FVector::ZeroVector;   // stays zero for a path that ends in the air: no splash there
	bool bLanded = false;
	ESurface Surface = ESurface::Air;
	if (World)
	{
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DreamWeatherFallPath), false);
		++TracesCast;
		if (World->LineTraceSingleByChannel(Hit, Start, Far, Params.Channel, Query))
		{
			End = Hit.ImpactPoint;
			Normal = Hit.ImpactNormal;
			bLanded = true;
			Surface = Classify(Hit, ViewLocation);
		}
	}

	// Shelters without geometry: the path ends where it first passes under cover (halving the segment).
	if (Shelter(End + Normal * 5.0) >= 0.5f)
	{
		FVector Open = Start;
		FVector Covered = End;
		for (int32 Step = 0; Step < 10; ++Step)
		{
			const FVector Mid = (Open + Covered) * 0.5;
			if (Shelter(Mid) >= 0.5f)
			{
				Covered = Mid;
			}
			else
			{
				Open = Mid;
			}
		}
		End = Covered;
		Normal = FVector::UpVector;
		bLanded = true;
		Surface = ESurface::Roof;
	}

	const double Speed = FMath::Max(FallVelocity.Size(), 1.0);
	Ends[Index] = FVector4(End.X, End.Y, End.Z, FVector::Dist(Start, End) / Speed);
	Normals[Index] = Normal;
	Landed[Index] = bLanded ? 1 : 0;
	Surfaces[Index] = uint8(Surface);
}

void FDreamWeatherFallPaths::GetSurfaceShares(const FVector& Location, double Radius, float OutShares[(int32)ESurface::Num]) const
{
	double Weights[(int32)ESurface::Num] = {};
	double Total = 0.0;
	const double InvR2 = 1.0 / FMath::Max(Radius * Radius, 1.0);
	const int32 Count = FMath::Min(Ends.Num(), Surfaces.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const double D2 = FVector2D(Ends[i].X - Location.X, Ends[i].Y - Location.Y).SizeSquared();
		const double W = 1.0 / (1.0 + D2 * InvR2);
		Weights[FMath::Min<int32>(Surfaces[i], (int32)ESurface::Num - 1)] += W;
		Total += W;
	}
	for (int32 s = 0; s < (int32)ESurface::Num; ++s)
	{
		OutShares[s] = Total > 0.0 ? float(Weights[s] / Total) : (s == (int32)ESurface::Air ? 1.0f : 0.0f);
	}
}
