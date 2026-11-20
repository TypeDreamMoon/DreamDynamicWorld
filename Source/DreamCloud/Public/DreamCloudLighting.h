// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDreamCloudField;
class FTextureResource;
class FTextureRenderTargetResource;

/**
 * A cloud volume's light map: the transmittance toward the light from each texel of a small grid over its field's box,
 * through the smooth shape less what has been carved out of it. A compute shader (DreamCloudLightMap.usf) draws it
 * whenever the shape, the carving or the light's direction in the volume change enough; ComputeTransmittance is the
 * same formula on the CPU (for tests and queries).
 */
namespace DreamCloudLighting
{
	struct FLightMapParams
	{
		/** Toward the light, unit, in the shape's space. */
		FVector3f LightDir = FVector3f(0.0f, 0.0f, 1.0f);
		/** Extinction per cm of the shape's space toward the light. */
		float SigmaPerUnit = 0.005f;
		/** The shape grown by Inflate cm, its density rising over Edge cm (the shape's space). */
		float Inflate = 0.0f;
		float Edge = 50.0f;
		FIntVector Resolution = FIntVector(32);
		int32 Steps = 48;
	};

	/** The light map's resolution for a field box of that size with Longest texels along its longest axis. */
	DREAMCLOUD_API FIntVector ResolutionFor(const FVector& BoxSize, int32 Longest);

	/** The transmittance from one point toward the light, as the light map's texels hold it. Holes (may be empty):
	 *  carving 0..1 on a grid of HoleResolution over the same box. */
	DREAMCLOUD_API float TransmittanceAt(const FDreamCloudField& Field, TConstArrayView<float> Holes, const FIntVector& HoleResolution,
		const FLightMapParams& Params, const FVector& Point);

	/** The whole light map on the CPU, x fastest. */
	DREAMCLOUD_API void ComputeTransmittance(const FDreamCloudField& Field, TConstArrayView<float> Holes, const FIntVector& HoleResolution,
		const FLightMapParams& Params, TArray<float>& Out);

	/** Samples a grid over the box (trilinear, texel centres at (i + 0.5) / N), for the holes. */
	DREAMCLOUD_API float SampleGrid(TConstArrayView<float> Grid, const FIntVector& Resolution, const FVector& BoxSize, const FVector& Point);

	/** Game thread: draws the light map into Target (a UAV volume render target of Params.Resolution) on the GPU. */
	DREAMCLOUD_API void EnqueueLightMap(FTextureResource* Field, FTextureRenderTargetResource* Holes, FTextureRenderTargetResource* Target,
		const FVector& BoxSize, const FLightMapParams& Params);

	/** Game thread: copies carving (0..1, x fastest) into a volume render target of that size (8 bits a texel). */
	DREAMCLOUD_API void EnqueueHoleUpload(FTextureRenderTargetResource* Target, const FIntVector& Resolution, TArray<uint8>&& Texels);
}
