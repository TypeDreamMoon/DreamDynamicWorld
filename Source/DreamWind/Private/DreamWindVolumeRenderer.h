// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "DreamWindBackground.h"
#include "DreamWindSubsystem.h"

class FTextureRenderTargetResource;
class FRDGBuilder;

/**
 * Owns the render-thread state of the dynamic wind volume (L2): the previous frame's result and the
 * compute pass that produces the next one into the volume render target asset.
 *
 * Per frame, for every voxel: take last frame's residual at the position the wind carried it from
 * (semi-Lagrangian advection by analytic + residual), decay it, then blend it toward every live
 * source. Cells that were outside last frame's window start from zero, which is how the window
 * follows the camera without a copy pass.
 */
class FDreamWindVolumeRenderer : public TSharedFromThis<FDreamWindVolumeRenderer, ESPMode::ThreadSafe>
{
public:
	struct FUpdateArgs
	{
		FDreamWindFrameData Frame;
		FTextureRenderTargetResource* TargetResource = nullptr;
		FDreamWindBackgroundResources Background;
		FIntVector Resolution = FIntVector(64, 64, 32);
		FVector3f PrevCenter = FVector3f::ZeroVector;
		FVector3f NewCenter = FVector3f::ZeroVector;
		float VoxelSize = 100.0f;
		float DeltaTime = 0.0f;
		float DecayFactor = 1.0f;
		bool bAdvect = true;
		bool bResetHistory = false;
	};

	FDreamWindVolumeRenderer() = default;
	~FDreamWindVolumeRenderer();

	/** Game thread: schedule this frame's update. */
	void EnqueueUpdate(FUpdateArgs&& Args);

	/** Game thread: drop the render-thread history (resolution change, world teardown). */
	void EnqueueReset();

	/** Render thread. */
	void Update_RenderThread(FRDGBuilder& GraphBuilder, const FUpdateArgs& Args);
	void Release_RenderThread();

	uint32 GetFramesRendered_RenderThread() const { return FramesRendered; }

private:
	TRefCountPtr<IPooledRenderTarget> History;
	FIntVector HistoryResolution = FIntVector::ZeroValue;
	bool bHistoryValid = false;
	uint32 FramesRendered = 0;
};
