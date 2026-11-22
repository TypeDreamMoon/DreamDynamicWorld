// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamSkyStyle;

namespace DreamWorld::Setup
{
	/**
	 * Bakes a sky style's sky table for where the GPU cannot build one (mobile, no Shader Model 5): builds the tables for the
	 * style's air, writes the sky table beside the style as <style>_SkyTable (a volume texture) and its summary into the
	 * style's BakedSky, and saves both with bSave. Needs a renderer: without one (-nullrhi) it says so and returns true
	 * (nothing to do there); false when the bake itself failed.
	 */
	bool BakeStyleSky(UDreamSkyStyle* Style, bool bSave);
}
