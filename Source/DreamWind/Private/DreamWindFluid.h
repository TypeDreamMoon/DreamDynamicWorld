// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWindBackground.h"
#include "DreamWindSubsystem.h"
#include "RenderGraphResources.h"

class FRDGBuilder;
class FSceneView;
class FTextureRenderTargetResource;

/**
 * The wind fluid (L2): a near and a far grid around the camera, each solved every frame on the GPU and written into its
 * shared volume render target as the deviation from the background wind (DreamWindFluid.usf has the steps). Owns the
 * render-thread state: last frame's deviation, the pressure (each frame starts from the last one's) and the walls the
 * global distance field gave, all in the grids' own pooled textures (per world, not shared assets).
 *
 * The walls from the distance field are looked up in a view's post-processing (AddWallPasses_RenderThread, from the
 * wind's view extension), where the renderer has the field up to date; the step itself runs in its own graph before
 * the scene, so this frame's consumers read this frame's fluid with the walls of the frame before.
 */
class FDreamWindFluid : public TSharedFromThis<FDreamWindFluid, ESPMode::ThreadSafe>
{
public:
	struct FLevelArgs
	{
		bool bEnabled = false;
		FTextureRenderTargetResource* Target = nullptr;
		FIntVector Resolution = FIntVector(64, 64, 32);
		float CellSize = 100.0f;
		/** The grid's lowest corner, world space (whole cells). */
		FVector3f Origin = FVector3f::ZeroVector;
		/** The window as consumers read it (DW_UnpackVolume); the near level reads the far one through it. */
		FVector4f V0 = FVector4f::Zero();
		FVector4f V1 = FVector4f::Zero();
		int32 PressureIterations = 8;
		float Vorticity = 0.0f;
		/** Second-order advection (a corrective pass): far less smearing. */
		bool bMacCormack = false;
		/** This level's time step, s (a level stepped every other frame takes two frames' time). */
		float DeltaTime = 0.0f;
		/** Per step toward the cells beside (the checkerboard's damping over this level's step). */
		float Smoothing = 0.1f;
		/** A surface within this fraction of a cell of a cell's centre makes it a wall. */
		float WallThickness = 0.45f;
		/** False: the level holds this frame (its volume as the last step left it, still read by the near level). */
		bool bStep = true;
	};

	struct FStepArgs
	{
		FDreamWindFrameData Frame;
		FDreamWindBackgroundResources Background;
		FLevelArgs Near;
		FLevelArgs Far;
		float Relaxation = 1.5f;
		float NudgeTime = 3.0f;
		float SpongeTime = 0.1f;
		int32 SpongeCells = 4;
		float MaxSpeed = 8000.0f;
		/** Walls from the global distance field (else the terrain map's heights everywhere). */
		bool bDistanceField = true;
		/** The volumes' w holds the residual divergence instead of the walls (debug). */
		bool bShowDivergence = false;
		/** Start afresh (the world got the volumes back, a jump of the camera). */
		bool bReset = false;
		/** Openings carved out of the walls (doors, windows letting the air through): three float4 each -- centre, half
		 *  extent (through the wall, across, up), rotation (quaternion) -- at most MaxPortals. */
		TArray<FVector4f> Portals;
	};

	static constexpr int32 MaxPortals = 16;

	/** Game thread: schedule this frame's step. */
	void EnqueueStep(FStepArgs&& Args);

	/** Game thread: drop the render-thread state. */
	void EnqueueReset();

	/** Render thread, in a perspective view's post-processing: the grids' walls from the global distance field. */
	void AddWallPasses_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View);

	/** Steps run and wall passes written so far (any thread). */
	uint32 GetStepCount() const { return StepCount.load(); }
	uint32 GetWallPassCount() const { return WallPassCount.load(); }

private:
	struct FLevel
	{
		TRefCountPtr<IPooledRenderTarget> D;
		TRefCountPtr<IPooledRenderTarget> Theta[2];
		TRefCountPtr<IPooledRenderTarget> Pressure[2];
		int32 Current = 0;
		FIntVector Resolution = FIntVector::ZeroValue;
		float CellSize = 0.0f;
		FVector3f Origin = FVector3f::ZeroVector;
		/** A surface within this fraction of a cell of a cell's centre makes it a wall (this level's cells). */
		float WallThickness = 0.45f;
		bool bValid = false;

		void Release();
	};

	void Step_RenderThread(FRDGBuilder& GraphBuilder, const FStepArgs& Args);
	/** One level's step; returns its output volume (registered), or null when it did not run. */
	FRDGTextureRef StepLevel(FRDGBuilder& GraphBuilder, FLevel& Level, const FLevelArgs& LevelArgs, const FStepArgs& Args,
		const FDreamWindBackgroundParameters& Background, FRDGBufferSRVRef Sources, TConstArrayView<DreamWind::Hlsl::FDreamWindSourceData> Records,
		FRDGTextureRef FarVolume, const FLevelArgs* FarArgs);
	void AddWallPass(FRDGBuilder& GraphBuilder, const FSceneView& View, FLevel& Level);

	FLevel Near;
	FLevel Far;
	/** Render thread: whether the last step took its walls from the distance field. */
	bool bDistanceFieldWalls = false;
	uint32 LastWallFrame = ~0u;
	int32 WallPhase = 0;
	std::atomic<uint32> StepCount{ 0 };
	std::atomic<uint32> WallPassCount{ 0 };
};
