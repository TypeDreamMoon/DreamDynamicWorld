// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyFunnels.h"
#include "DreamWorldTypes.h"

float DreamSky::Funnels::Turned(const FDreamWorldVortex& Vortex, double WorldSeconds, float TimeScale)
{
	const double RealSeconds = (WorldSeconds - Vortex.BornSeconds) / FMath::Max(double(TimeScale), 1.0e-3);
	const double Rate = double(Vortex.TangentialSpeed) / FMath::Max(double(Vortex.Radius), 100.0);
	// (The funnel's pattern is turned whole, a full turn the same as none: wrapped, it stays exact however long it lives.)
	const double Angle = FMath::Fmod(Rate * RealSeconds, UE_DOUBLE_TWO_PI);
	return float(Angle < 0.0 ? Angle + UE_DOUBLE_TWO_PI : Angle);
}

float DreamSky::Funnels::RopeLean(float Decay)
{
	return 1.0f + 2.0f * FMath::SmoothStep(0.0f, 0.7f, FMath::Clamp(Decay, 0.0f, 1.0f));
}

int32 DreamSky::Funnels::Pack(const TMap<int32, FDreamWorldVortex>& Vortices, const FVector& View, double WorldSeconds, float TimeScale,
	FLinearColor OutFunnel[MaxFunnels], FLinearColor OutShape[MaxFunnels], FLinearColor& OutDecay)
{
	OutDecay = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	TArray<const FDreamWorldVortex*, TInlineAllocator<16>> Shown;
	for (const TPair<int32, FDreamWorldVortex>& Pair : Vortices)
	{
		const FDreamWorldVortex& V = Pair.Value;
		if (V.bFunnel && V.Strength > 0.0f && V.Radius >= 100.0f)
		{
			Shown.Add(&V);
		}
	}
	Shown.Sort([&View](const FDreamWorldVortex& A, const FDreamWorldVortex& B)
	{
		return FVector::DistSquared2D(A.Location, View) < FVector::DistSquared2D(B.Location, View);
	});
	const int32 Count = FMath::Min(Shown.Num(), MaxFunnels);
	for (int32 i = 0; i < MaxFunnels; ++i)
	{
		if (i < Count)
		{
			const FDreamWorldVortex& V = *Shown[i];
			// (Roping out it stays down to the ground, however its strength falls, leaning further: so far the bounds the
			// renderer marches by take it in.)
			const float Decay = FMath::Clamp(V.Decay, 0.0f, 1.0f);
			const float Strength = (Decay > 0.0f ? 1.0f : FMath::Clamp(V.Strength, 0.0f, 1.0f)) * (V.Spin < 0.0f ? -1.0f : 1.0f);
			const FVector2D Lean = V.Lean * double(RopeLean(Decay));
			OutFunnel[i] = FLinearColor(float(V.Location.X), float(V.Location.Y), V.Radius, Strength);
			OutShape[i] = FLinearColor(Turned(V, WorldSeconds, TimeScale), float(V.Location.Z), float(Lean.X), float(Lean.Y));
			OutDecay.Component(i) = Decay;
		}
		else
		{
			OutFunnel[i] = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			OutShape[i] = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		}
	}
	return Count;
}
