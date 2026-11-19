// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The 3D noise the volumetric cloud material is built from: a cube of voxels that tiles in all three directions.
 *   R  Perlin-Worley, low frequency and smooth (two octaves each): the clouds' base shapes, round blobs of every
 *      size; stretched to fill 0..1 (a cover of 0.3 then clears about 30% of it). The detail comes from G, B, A:
 *      finer octaves here would cut the shapes into sheets and needles.
 *   G  Worley fBm, 8 cells across (the large billows eaten into a cloud)
 *   B  Worley fBm, 16 cells across
 *   A  Worley fBm, 32 cells across (the finest detail, far from the view left out)
 * Worley is inverted: 1 at a cell's feature point (the middle of a billow), 0 between cells.
 */
namespace DreamCloudNoise
{
	/** Bump when the noise changes, so Setup builds the texture again. */
	inline constexpr int32 Version = 3;

	/** Size*Size*Size voxels, x fastest then y then z (FColor: R, G, B, A as above). */
	void Build(int32 Size, TArray<FColor>& OutVoxels);
}
