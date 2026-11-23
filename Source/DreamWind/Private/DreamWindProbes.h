// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FRHIGPUBufferReadback;
class FTextureRenderTargetResource;

/**
 * The wind fluid at a few points, for the CPU: each frame up to MaxProbes positions (keyed by who asks) are read from
 * the volumes on the GPU and read back asynchronously, so their answers arrive two or three frames later. Visual only
 * (sound, the camera, decorative physics): late, and not the same on every machine; a server has none.
 */
class FDreamWindFluidProbes : public TSharedFromThis<FDreamWindFluidProbes, ESPMode::ThreadSafe>
{
public:
	static constexpr int32 MaxProbes = 16;

	struct FRequest
	{
		TArray<FName> Keys;
		TArray<FVector4f> Points;
		FTextureRenderTargetResource* Near = nullptr;
		FTextureRenderTargetResource* Far = nullptr;
		FVector4f V0, V1, V2, V3;
	};

	~FDreamWindFluidProbes();

	/** Game thread: read these points from this frame's volumes. */
	void Enqueue(FRequest&& Request);

	/** Game thread: the latest answer for a key (deviation cm/s, w = how much the volumes covered the point); false if none yet. */
	bool Get(FName Key, FVector4f& Out) const;

	/** Game thread: forget every answer (the volumes started afresh). */
	void Clear();

private:
	struct FSlot
	{
		FRHIGPUBufferReadback* Readback = nullptr;
		TArray<FName> Keys;
		bool bPending = false;
	};

	void Poll_RenderThread();
	void Dispatch_RenderThread(FRHICommandListImmediate& RHICmdList, const FRequest& Request);

	/** Render thread. */
	FSlot Slots[4];
	int32 NextSlot = 0;

	mutable FCriticalSection Lock;
	TMap<FName, FVector4f> Results;
};
