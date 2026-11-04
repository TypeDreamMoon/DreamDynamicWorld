// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "DreamWorldTypes.h"

class UWorld;
class AActor;

/**
 * Tells indoors from outdoors by casting rays around the view: rings of rays from the horizon up to the zenith,
 * a few per frame, a full round every RoundSeconds. The share of rays that hit something within Distance is
 * how enclosed the view is; the upper rays alone say whether there is a roof overhead, the lower ones which
 * sides are closed. The result is eased so a doorway is a fade, and snaps when the view jumps.
 *
 * Synchronous line traces on the game thread: a handful a frame costs a few microseconds.
 */
class DREAMDYNAMICWORLD_API FDreamViewProbe
{
public:
	struct FParams
	{
		ECollisionChannel Channel = ECC_Visibility;
		float Distance = 4500.0f;
		float RoundSeconds = 0.35f;
		float ResponseSeconds = 0.5f;
		FVector2D InteriorRange = FVector2D(0.2, 0.9);
		float TeleportDistance = 1000.0f;
		/** Usually the player's own pawn, so the camera does not find itself. */
		const AActor* IgnoredActor = nullptr;
	};

	/** The rays, unit vectors from the view, in the order they are cast. */
	static const TArray<FVector>& GetDirections();

	/** Cast this frame's share of rays from Location. bSnap casts them all and skips the easing. */
	void Update(UWorld* World, const FVector& Location, float DeltaTime, const FParams& Params, bool bSnap);

	/** Forget everything: the next update casts a full round. */
	void Reset();

	/** Eased result. */
	const FDreamViewOcclusion& Get() const { return Smoothed; }

	/** What the latest rays say, before easing. */
	const FDreamViewOcclusion& GetTarget() const { return Target; }

	/** Rays cast since the probe was made (for tests and stats). */
	int64 GetRaysCast() const { return RaysCast; }

private:
	void Evaluate(const FParams& Params);

	TArray<int8> Hits;  // per direction: 1 hit, 0 open, -1 not cast since the reset
	int32 Next = 0;
	int32 Known = 0;
	float Budget = 0.0f;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLocation = false;
	FDreamViewOcclusion Target;
	FDreamViewOcclusion Smoothed;
	int64 RaysCast = 0;
};
