// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UWorld;

/**
 * Where the rain around the view lands. A few hundred fall paths a second are traced from above the view along
 * the fall direction (down, blown by the wind) until they hit something — the ground, a roof, a leaf — or run
 * out; a shelter that is not geometry (a shelter box, a baked roof) stops them too. The precipitation effects
 * then draw thousands of drops a frame by picking paths at random: a drop falls along its path and dies where
 * the path ends, and a splash is spawned at a path's end with its surface normal. No drop ever needs a
 * collision query of its own, and no rain falls indoors.
 *
 * The paths form a ring buffer; each update replaces the oldest, so a full buffer turns over every
 * RefreshSeconds and follows a moving view.
 */
class DREAMWEATHER_API FDreamWeatherFallPaths
{
public:
	struct FParams
	{
		/** How many paths the buffer holds. */
		int32 Capacity = 1024;
		/** Seconds to replace every path once. */
		float RefreshSeconds = 1.0f;
		/** Paths start inside this radius around the view, cm. */
		float Radius = 3000.0f;
		/** 0 = all around the view .. 1 = mostly in front of it (where the drops are seen). */
		float ForwardBias = 0.6f;
		/** Height above the view where the paths start, cm. */
		float StartHeight = 2500.0f;
		/** A path that hits nothing ends after falling this far below its start, cm (slanted paths are longer). */
		float MaxLength = 8000.0f;
		ECollisionChannel Channel = ECC_Visibility;
	};

	/**
	 * Replace this frame's share of paths. FallVelocity is the drops' velocity (cm/s, down and downwind).
	 * Shelter, when set, says how covered a point is (0 open .. 1 covered) — the weather's shelters and baked
	 * roofs, which have no collision; a path ends where it first passes under cover.
	 */
	void Update(UWorld* World, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity, float DeltaTime,
		const FParams& Params, TFunctionRef<float(const FVector&)> Shelter);

	/** Fill the whole buffer now (first use, a teleport). */
	void Fill(UWorld* World, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity,
		const FParams& Params, TFunctionRef<float(const FVector&)> Shelter);

	void Reset();

	/** Per path: where it ends (xyz) and how many seconds a drop falls along it (w). */
	const TArray<FVector4>& GetEnds() const { return Ends; }

	/** Per path: the normal of what it landed on (zero when it ended in the air). */
	const TArray<FVector>& GetNormals() const { return Normals; }

	/** Paths that ended on something (rather than in the air): the share where splashes belong. */
	int32 CountLanded() const;

	/** Bumped whenever paths changed (the effects re-upload only then). */
	uint32 GetGeneration() const { return Generation; }

	int64 GetTracesCast() const { return TracesCast; }

private:
	void TraceOne(UWorld* World, int32 Index, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity,
		const FParams& Params, TFunctionRef<float(const FVector&)> Shelter);

	TArray<FVector4> Ends;
	TArray<FVector> Normals;
	TArray<uint8> Landed;
	int32 Next = 0;
	float Budget = 0.0f;
	uint32 Generation = 0;
	uint32 Sequence = 0;
	int64 TracesCast = 0;
};
