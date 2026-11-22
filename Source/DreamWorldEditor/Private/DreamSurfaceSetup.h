// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DreamWorld::Setup
{
	/**
	 * The surface maps' render targets (Project Settings > Dream Surface > Surface Maps): the near and far state maps
	 * (RGBA16F, 256 and 128 texels) and the cover map (RG32F, 256), all writable by a compute pass.
	 */
	bool EnsureSurfaceMaps();
}
