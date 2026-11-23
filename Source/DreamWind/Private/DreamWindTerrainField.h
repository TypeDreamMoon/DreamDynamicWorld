// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class AActor;
class FReferenceCollector;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UWorld;

/**
 * The runtime terrain wind map (L1), game thread side: two levels around the camera, each a ring buffer over the world
 * (DreamWindShared.h, "The runtime terrain wind map"). A top-down depth capture of each level's newly exposed strips
 * (and, a strip every few frames, of the whole level in turn, so moving buildings and streamed-in cells show up; one
 * capture a frame at most) goes into its
 * height map; whenever a level's heights or the wind's direction change, compute passes make its derived map: the speed
 * multiplier and steering from the blurred heights (far level), and the wind shadow height swept along the wind.
 * Only the world that owns the shared render targets runs it (DreamWorldSharedTargets).
 */
class FDreamWindTerrainField : public FGCObject
{
public:
	FDreamWindTerrainField();
	virtual ~FDreamWindTerrainField() override;

	/**
	 * Every tick of the owning world: follow the view, capture what is due, derive what changed. WindDir is the global
	 * wind's direction; bRedrawAll throws everything away (the targets were another world's).
	 */
	void Update(UWorld& World, const FVector& ViewLocation, const FVector& WindDir, bool bRedrawAll);

	/** Stop: the levels are off (the material parameters say so) until Update runs again. */
	void Stop();

	/** The packed level / parameter vectors (DW_UnpackL1Level, DW_UnpackL1Params); level w = 0 when off. */
	FVector4f GetNearLevel() const { return Near.Packed(); }
	FVector4f GetFarLevel() const { return Far.Packed(); }
	FVector4f GetParams() const;

	/** Captures and derivations done so far (tests, stats). */
	uint32 GetCaptureCount() const { return CaptureCount; }
	uint32 GetDeriveCount() const { return DeriveCount; }

	/** The next update captures this box again on both levels (a building moved, a wall fell). */
	void MarkDirty(const FBox& Bounds);

	UTextureRenderTarget2D* GetNearDerived() const { return Near.Derived; }
	UTextureRenderTarget2D* GetFarDerived() const { return Far.Derived; }
	UTextureRenderTarget2D* GetNearHeight() const { return Near.Height; }
	UTextureRenderTarget2D* GetFarHeight() const { return Far.Height; }

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDreamWindTerrainField"); }

private:
	struct FLevel
	{
		int32 Size = 256;
		float TexelSize = 100.0f;
		float ShadowRange = 3000.0f;
		bool bShape = false;
		/** The window's first cell, and whether anything is in the maps yet. */
		FIntPoint WindowMin = FIntPoint::ZeroValue;
		bool bValid = false;
		/** Cell rectangles waiting to be captured, oldest first. */
		TArray<FIntRect> Pending;
		int32 RefreshNext = 0;
		bool bDerivePending = false;
		TObjectPtr<UTextureRenderTarget2D> Derived;
		TObjectPtr<UTextureRenderTarget2D> Height;
		TObjectPtr<USceneCaptureComponent2D> Capture;
		/** Capture targets by size (the whole level, strips across x and across y). */
		TMap<FIntPoint, TObjectPtr<UTextureRenderTarget2D>> CaptureTargets;

		FVector4f Packed() const;
	};

	bool Resolve(FLevel& Level, const FSoftObjectPath& DerivedPath, const FSoftObjectPath& HeightPath, int32 Size, float Extent);
	void Follow(FLevel& Level, const FVector& ViewLocation);
	/** Captures the level's next rectangle that came into view (or, with bRefresh, its next refresh strip); false if none. */
	bool CaptureNext(UWorld& World, FLevel& Level, double ViewZ, bool bRefresh);
	void Capture(UWorld& World, FLevel& Level, const FIntRect& Rect, double ViewZ);
	void Derive(FLevel& Level, const FVector2D& WindDir2);
	void RefreshHiddenActors(UWorld& World);

	FLevel Near;
	FLevel Far;
	FVector2D DerivedWindDir = FVector2D::ZeroVector;
	TArray<TWeakObjectPtr<AActor>> Hidden;
	double LastHiddenScan = -1.0e30;
	uint32 CaptureCount = 0;
	uint32 DeriveCount = 0;
	int32 FramesSinceRefresh = 0;
	bool bRefreshFar = false;
	bool bActive = false;
};
