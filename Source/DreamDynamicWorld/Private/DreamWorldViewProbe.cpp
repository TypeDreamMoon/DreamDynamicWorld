// Copyright DreamMoon. All Rights Reserved.

#include "DreamWorldViewProbe.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"

namespace DreamWorld::Probe
{
	// Rings of rays: elevation (degrees) and count. The low rings find the walls, the high ones the roof.
	struct FRing
	{
		float Elevation;
		int32 Count;
	};
	static constexpr FRing Rings[] = { { 5.0f, 12 }, { 30.0f, 12 }, { 55.0f, 12 }, { 75.0f, 6 }, { 90.0f, 1 } };

	/** At or above this elevation a ray looks for a roof; at or below it, for a wall on one side. */
	static constexpr float OverheadElevation = 50.0f;
	static constexpr float SideElevation = 35.0f;

	static TArray<FVector> MakeDirections()
	{
		TArray<FVector> Dirs;
		int32 RingIndex = 0;
		for (const FRing& Ring : Rings)
		{
			const double Elev = FMath::DegreesToRadians(double(Ring.Elevation));
			// Every other ring turned half a step, so the rays do not line up into columns.
			const double Offset = (RingIndex++ % 2) ? 0.5 : 0.0;
			for (int32 i = 0; i < Ring.Count; ++i)
			{
				const double Az = 2.0 * UE_DOUBLE_PI * (double(i) + Offset) / double(Ring.Count);
				Dirs.Add(FVector(FMath::Cos(Elev) * FMath::Cos(Az), FMath::Cos(Elev) * FMath::Sin(Az), FMath::Sin(Elev)).GetSafeNormal());
			}
		}
		// Interleave the order (a stride coprime with the count), so a partial round still samples all around.
		TArray<FVector> Ordered;
		const int32 Num = Dirs.Num();
		const int32 Stride = 17;
		for (int32 i = 0; i < Num; ++i)
		{
			Ordered.Add(Dirs[(i * Stride) % Num]);
		}
		return Ordered;
	}
}

const TArray<FVector>& FDreamViewProbe::GetDirections()
{
	static const TArray<FVector> Directions = DreamWorld::Probe::MakeDirections();
	return Directions;
}

void FDreamViewProbe::Reset()
{
	Hits.Reset();
	Next = 0;
	Known = 0;
	Budget = 0.0f;
	bHasLocation = false;
	Target = FDreamViewOcclusion();
	Smoothed = FDreamViewOcclusion();
}

void FDreamViewProbe::Update(UWorld* World, const FVector& Location, float DeltaTime, const FParams& Params, bool bSnap)
{
	if (!World)
	{
		return;
	}
	const TArray<FVector>& Dirs = GetDirections();
	const int32 Num = Dirs.Num();
	if (Hits.Num() != Num)
	{
		Hits.Init(-1, Num);
		Known = 0;
	}

	const bool bTeleported = bHasLocation && Params.TeleportDistance > 0.0f
		&& FVector::DistSquared(Location, LastLocation) > FMath::Square(double(Params.TeleportDistance));
	LastLocation = Location;
	bHasLocation = true;
	// A first look, a jump or a teleport sees the whole round at once; otherwise the rays spread over the round.
	const bool bWhole = bSnap || bTeleported || Known < Num;
	int32 Count = Num;
	if (!bWhole)
	{
		Budget += float(Num) * FMath::Max(DeltaTime, 0.0f) / FMath::Max(Params.RoundSeconds, 0.02f);
		Count = FMath::Min(Num, FMath::FloorToInt(Budget));
		Budget -= float(Count);
	}
	else
	{
		Budget = 0.0f;
	}

	if (Count > 0)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(DreamViewProbe), false);
		if (Params.IgnoredActor)
		{
			Query.AddIgnoredActor(Params.IgnoredActor);
		}
		const double Distance = FMath::Max(double(Params.Distance), 1.0);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 Index = Next;
			Next = (Next + 1) % Num;
			const bool bHit = World->LineTraceTestByChannel(Location, Location + Dirs[Index] * Distance, Params.Channel, Query);
			if (Hits[Index] < 0)
			{
				++Known;
			}
			Hits[Index] = bHit ? 1 : 0;
			++RaysCast;
		}
	}

	Evaluate(Params);
	if (bWhole || !Smoothed.bValid || Params.ResponseSeconds <= 0.0f)
	{
		Smoothed = Target;
		return;
	}
	const float Alpha = 1.0f - FMath::Exp(-FMath::Max(DeltaTime, 0.0f) / Params.ResponseSeconds);
	auto Ease = [Alpha](float& Value, float To) { Value = FMath::Lerp(Value, To, Alpha); };
	Ease(Smoothed.Interior, Target.Interior);
	Ease(Smoothed.Overhead, Target.Overhead);
	Ease(Smoothed.PlusX, Target.PlusX);
	Ease(Smoothed.MinusX, Target.MinusX);
	Ease(Smoothed.PlusY, Target.PlusY);
	Ease(Smoothed.MinusY, Target.MinusY);
	Smoothed.bValid = Target.bValid;
}

void FDreamViewProbe::Evaluate(const FParams& Params)
{
	using namespace DreamWorld::Probe;
	const TArray<FVector>& Dirs = GetDirections();
	const float OverheadZ = FMath::Sin(FMath::DegreesToRadians(OverheadElevation)) - 1.0e-3f;
	const float SideZ = FMath::Sin(FMath::DegreesToRadians(SideElevation)) + 1.0e-3f;

	int32 All[2] = { 0, 0 };       // hits, cast
	int32 Over[2] = { 0, 0 };
	int32 Side[4][2] = {};         // +X, -X, +Y, -Y
	for (int32 i = 0; i < Dirs.Num(); ++i)
	{
		if (Hits[i] < 0)
		{
			continue;
		}
		const int32 Hit = Hits[i];
		const FVector& D = Dirs[i];
		All[0] += Hit;
		++All[1];
		if (D.Z >= OverheadZ)
		{
			Over[0] += Hit;
			++Over[1];
		}
		if (D.Z <= SideZ)
		{
			const int32 Quadrant = FMath::Abs(D.X) >= FMath::Abs(D.Y) ? (D.X >= 0.0 ? 0 : 1) : (D.Y >= 0.0 ? 2 : 3);
			Side[Quadrant][0] += Hit;
			++Side[Quadrant][1];
		}
	}
	auto Share = [](const int32 (&Pair)[2]) { return Pair[1] > 0 ? float(Pair[0]) / float(Pair[1]) : 0.0f; };

	const float Low = float(Params.InteriorRange.X);
	const float High = FMath::Max(float(Params.InteriorRange.Y), Low + 1.0e-3f);
	Target.Interior = FMath::Clamp((Share(All) - Low) / (High - Low), 0.0f, 1.0f);
	Target.Overhead = Share(Over);
	Target.PlusX = Share(Side[0]);
	Target.MinusX = Share(Side[1]);
	Target.PlusY = Share(Side[2]);
	Target.MinusY = Share(Side[3]);
	Target.bValid = Known >= Dirs.Num();
}
