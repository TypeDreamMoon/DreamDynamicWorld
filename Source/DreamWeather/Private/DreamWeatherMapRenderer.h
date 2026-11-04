// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <atomic>

class FTextureRenderTargetResource;
class FRDGBuilder;

/**
 * Draws the weather map (DreamWeatherMap.usf) into its render target asset: the global weather with every
 * map zone applied, over a square around the camera. Stateless between frames.
 */
class FDreamWeatherMapRenderer : public TSharedFromThis<FDreamWeatherMapRenderer, ESPMode::ThreadSafe>
{
public:
	struct FUpdateArgs
	{
		FTextureRenderTargetResource* Target = nullptr;
		FIntPoint Resolution = FIntPoint(128, 128);
		FVector2f Center = FVector2f::ZeroVector;
		float Extent = 4000000.0f;
		/** Coverage, darkness, precipitation, unused. */
		FVector4f Global = FVector4f::Zero();
		/** DWX_ZONE_FLOAT4S per zone, lowest priority first. */
		TArray<FVector4f> Zones;
	};

	/** Game thread: draw the map this frame. */
	void EnqueueUpdate(FUpdateArgs&& Args);

	/** Render thread. */
	void Update_RenderThread(FRDGBuilder& GraphBuilder, const FUpdateArgs& Args);

	/** Maps drawn so far (any thread; for tests and stats). */
	uint32 GetDrawCount() const { return DrawCount.load(); }

private:
	std::atomic<uint32> DrawCount { 0 };
};
