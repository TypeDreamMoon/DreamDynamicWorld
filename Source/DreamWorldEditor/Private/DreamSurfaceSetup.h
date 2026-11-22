// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamSkyStyle;

namespace DreamWorld::Setup
{
	inline const TCHAR* SurfaceTestMapPackage = TEXT("/DreamDynamicWorld/Surface/Maps/Lvl_DreamSurfaceTest");

	/**
	 * The surface maps' render targets (Project Settings > Dream Surface > Surface Maps): the near and far state maps
	 * (RGBA16F, 256 and 128 texels) and the cover map (RG32F, 256), all writable by a compute pass.
	 */
	bool EnsureSurfaceMaps();

	/**
	 * The surface test level, around a camera at the origin looking along +x under a clear schedule (force the weather
	 * to see it change):
	 *   ahead, near      a stone pavement (puddles gather in its hollows) under a canopy on four pillars with a shelter
	 *                    box below the roof (dry beneath it), realistic props (stone, metal, wood) and toon props and a
	 *                    toon figure on it;
	 *   around, further  rolling ground with the terrain material (snow stands up off it);
	 *   ahead right, far a snow zone: its border shows on the ground from the camera.
	 * The materials are the DreamSurface demo materials and instances of them.
	 */
	bool BuildSurfaceTestMap(UDreamSkyStyle* Style);
}
