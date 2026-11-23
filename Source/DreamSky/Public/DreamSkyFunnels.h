// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDreamWorldVortex;

/**
 * The funnel clouds the sky draws (DreamSkyStylized.ush, DSky_Funnels): the tornadoes registered with the world clock
 * (UDreamWorldSubsystem::SetVortex) nearest the view, packed for the shared parameter collection, which the cloud
 * renderer reads.
 *   DDW_FunnelN       = (x, y of the root (cm), radius at the cloud base (cm), strength 0..1 times the spin's sign)
 *   DDW_FunnelShapeN  = (how far it has turned (radians, 0..2 pi), the ground's height at the root (cm), the lean x, y (cm))
 *   DDW_FunnelDecay   = (how far each has roped out as it dies, 0..1: x funnel 0 .. w funnel 3)
 * A funnel roping out is packed all the way down (strength 1) and leaning further with the wind aloft (RopeLean); the
 * renderer thins it, fades its wall cloud and dust and breaks it by its decay.
 */
namespace DreamSky::Funnels
{
	constexpr int32 MaxFunnels = 4;

	/**
	 * The funnels to draw from these whirlwinds: those with a funnel and a strength, the nearest to View first, up to
	 * MaxFunnels; the rest of the slots zero. How far one has turned is its wall's speed over its radius times the time
	 * since it formed, in real seconds at the clock's rate (TimeScale game seconds a second), so the same moment looks
	 * the same however it is reached. Returns how many.
	 */
	DREAMSKY_API int32 Pack(const TMap<int32, FDreamWorldVortex>& Vortices, const FVector& View, double WorldSeconds, float TimeScale,
		FLinearColor OutFunnel[MaxFunnels], FLinearColor OutShape[MaxFunnels], FLinearColor& OutDecay);

	/** How much further a funnel roping out leans than its Lean (1 not roping out, 3 at the most). */
	DREAMSKY_API float RopeLean(float Decay);

	/** How far a funnel has turned (radians, wrapped to 0..2 pi): see Pack. */
	DREAMSKY_API float Turned(const FDreamWorldVortex& Vortex, double WorldSeconds, float TimeScale);
}
