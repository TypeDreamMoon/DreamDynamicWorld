// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWindSettings;

/**
 * What the wind field runs at one quality level (r.DreamWind.Quality, through UDreamWorldQuality's Wind group). Epic --
 * the engine's scalability where nothing has detected the machine's -- is the project settings as they stand, as the
 * sky's epic is its tuned defaults; the other levels are drawn from them:
 *   0 low       the analytic wind (L0) and the level's baked terrain map without its wind shadow on the GPU; no
 *               runtime terrain map, no volume.
 *   1 medium    the runtime terrain map; one fluid grid at the far level's cells (64 x 64 x 16 at 4 m), no far level.
 *   2 high      the settings' grids, a quarter fewer pressure iterations near, the far level every third frame.
 *   3 epic      the settings: the near fluid (1 m) and the far one (4 m, stepped every other frame).
 *   4 cinematic a finer near grid (128 x 128 x 64 at 50 cm), twice the iterations; a wider far level (96 x 96 x 48 at
 *               4 m), every frame.
 * Only the GPU's field changes with the level: the CPU's wind (SampleWind, forces, gameplay) is the same at every one.
 * Without compute shaders (mobile) or a renderer the wind runs as low.
 */
struct DREAMWIND_API FDreamWindQualityTier
{
	int32 Level = 3;
	/** The runtime terrain map (else the baked one). */
	bool bRuntimeTerrain = true;
	/** The GPU's consumers march the baked map's wind shadow (25 texture reads a vertex): off at low. */
	bool bBakedShadow = true;
	/** The dynamic volume (L2) at all. */
	bool bVolume = true;
	bool bFar = true;
	FIntVector NearResolution = FIntVector(64, 64, 32);
	FIntVector FarResolution = FIntVector(64, 64, 32);
	float NearCell = 100.0f;
	float FarCell = 400.0f;
	int32 NearIterations = 8;
	int32 FarIterations = 4;
	bool bMacCormack = true;
	float Vorticity = 0.4f;
	/** The far level steps every this many frames (its time step as long). */
	int32 FarInterval = 1;

	/** Whether switching from Other to this one needs new grids (the volume fades out, the grids change, it fades in). */
	bool SameLayout(const FDreamWindQualityTier& Other) const
	{
		return bVolume == Other.bVolume && bFar == Other.bFar && NearResolution == Other.NearResolution && FarResolution == Other.FarResolution
			&& NearCell == Other.NearCell && FarCell == Other.FarCell;
	}

	/** The tier of a level (0 low .. 4 cinematic) for these settings, on this machine. */
	static FDreamWindQualityTier Resolve(const UDreamWindSettings& Settings, int32 Level);

	/** The level the wind runs at now (UDreamWorldQuality's Wind group). */
	static int32 CurrentLevel();
};
