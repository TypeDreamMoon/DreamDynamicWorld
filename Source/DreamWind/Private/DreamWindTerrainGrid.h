// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWindSubsystem.h"
#include "WorldCollision.h"

class AActor;
class UWorld;

/**
 * Builds FDreamWindTerrainGridData: the runtime terrain wind map's CPU grid around the camera. Each cell's height comes
 * from an asynchronous box sweep (a cell-sized box, so a wall thinner than a cell still registers) down through the
 * level's collision; new cells as the window follows the camera first, then one row a frame again (moving buildings,
 * streamed-in cells). Whenever heights arrive or the wind turns, the grid is derived afresh on the game thread and
 * published as a new snapshot. Runs wherever there is collision (dedicated servers too); not a shared resource.
 */
class FDreamWindTerrainGrid : public TSharedFromThis<FDreamWindTerrainGrid, ESPMode::ThreadSafe>
{
public:
	/** Every tick of its world. */
	void Update(UWorld& World, const FVector& ViewLocation, const FVector& WindDir);

	/** The latest snapshot (null until the first derivation). */
	TSharedPtr<const FDreamWindTerrainGridData, ESPMode::ThreadSafe> GetSnapshot() const { return Snapshot; }

	/** Sweep a box's cells again (a building moved). */
	void MarkDirty(const FBox& Bounds);

	/** Sweeps issued and results received so far (tests). */
	uint32 GetSweepCount() const { return SweepCount; }
	uint32 GetResultCount() const { return ResultCount; }

private:
	void Follow(const FVector& ViewLocation);
	void IssueSweeps(UWorld& World, double ViewZ);
	void OnSweep(const FTraceHandle& Handle, FTraceDatum& Datum);
	void Derive(const FVector2D& WindDir2);
	int32 RingIndex(const FIntPoint& Cell) const;
	void RefreshIgnored(UWorld& World);

	int32 Size = 64;
	float CellSize = 400.0f;
	FIntPoint WindowMin = FIntPoint::ZeroValue;
	bool bValid = false;
	/** Ring buffer over the window: heights, and the cell each slot holds (to drop results for cells that left). */
	TArray<float> Heights;
	TArray<FIntPoint> SlotCell;
	TArray<FIntPoint> Pending;
	int32 RefreshRow = 0;
	bool bChanged = false;
	FVector2D DerivedWindDir = FVector2D::ZeroVector;
	TArray<TWeakObjectPtr<AActor>> Ignored;
	double LastIgnoredScan = -1.0e30;
	TSharedPtr<const FDreamWindTerrainGridData, ESPMode::ThreadSafe> Snapshot;
	uint32 SweepCount = 0;
	uint32 ResultCount = 0;
};
