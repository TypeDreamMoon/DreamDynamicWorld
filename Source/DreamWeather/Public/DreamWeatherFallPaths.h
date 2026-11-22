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
 * collision query of its own, and no rain falls indoors. A drop runs from its path's own start to its own end, never
 * from a start worked out again from the wind of the moment: a path traced a second ago in other wind still ends on
 * the ground outside, and its drop must not cut through the roof beside it.
 *
 * The paths form a ring buffer; each update replaces the oldest, so a full buffer turns over every
 * RefreshSeconds and follows a moving view.
 */
class DREAMWEATHER_API FDreamWeatherFallPaths
{
public:
	/**
	 * What a path ended on, for sounds that depend on it (rain on leaves, on a roof, on the ground). A hit on a
	 * component or actor tagged DreamWeather.Leaves, or on foliage painted with the foliage tool, is Leaves; one
	 * tagged DreamWeather.Roof, a shelter without geometry, or any surface more than RoofHeight above the view, is
	 * a Roof; anything else it lands on is Ground.
	 */
	enum class ESurface : uint8
	{
		Air,
		Ground,
		Roof,
		Leaves,
		Num
	};

	/** The tags that classify what a path lands on. */
	static const FName LeavesTag;
	static const FName RoofTag;

	/** A surface this far above the view (cm) counts as a roof. */
	static constexpr double RoofHeight = 100.0;

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

	/** Per path: where it starts. A drop falls from here to the end in the end's w seconds, along a straight line. */
	const TArray<FVector>& GetStarts() const { return Starts; }

	/** Per path: the normal of what it landed on (zero when it ended in the air). */
	const TArray<FVector>& GetNormals() const { return Normals; }

	/** Per path: what it ended on (ESurface). */
	const TArray<uint8>& GetSurfaces() const { return Surfaces; }

	/** Paths that ended on something (rather than in the air): the share where splashes belong. */
	int32 CountLanded() const;

	/**
	 * How the paths ending near a point split between what they landed on: each path weighs 1 / (1 + d^2 / R^2)
	 * by its horizontal distance d to Location. OutShares[ESurface] sums to 1 (all Air when the buffer is empty).
	 */
	void GetSurfaceShares(const FVector& Location, double Radius, float OutShares[(int32)ESurface::Num]) const;

	/** Bumped whenever paths changed (the effects re-upload only then). */
	uint32 GetGeneration() const { return Generation; }

	int64 GetTracesCast() const { return TracesCast; }

private:
	void TraceOne(UWorld* World, int32 Index, const FVector& ViewLocation, const FVector& ViewForward, const FVector& FallVelocity,
		const FParams& Params, TFunctionRef<float(const FVector&)> Shelter);

	TArray<FVector4> Ends;
	TArray<FVector> Starts;
	TArray<FVector> Normals;
	TArray<uint8> Landed;
	TArray<uint8> Surfaces;
	int32 Next = 0;
	float Budget = 0.0f;
	uint32 Generation = 0;
	uint32 Sequence = 0;
	int64 TracesCast = 0;
};
