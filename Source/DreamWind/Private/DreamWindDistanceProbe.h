// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FSceneView;
class FTextureRenderTargetResource;

namespace DreamWind::DistanceField
{
	/** A horizontal slice of the global distance field to write into a render target (RGBA, writable by a compute pass). */
	struct FProbeRequest
	{
		FTextureRenderTargetResource* Target = nullptr;
		FIntPoint Resolution = FIntPoint(128, 128);
		FVector Center = FVector::ZeroVector;
		float Size = 3000.0f;
	};

	/** Render thread, in a view's post-processing (the global distance field is up to date there): the slice pass. */
	void AddProbePass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FProbeRequest& Request);
}
