// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamSkyStyle;

namespace DreamWorld::Setup
{
	inline const TCHAR* ShowcaseMapPackage = TEXT("/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcase");
	inline const TCHAR* ShowcaseDuskMapPackage = TEXT("/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcaseDusk");
	inline const TCHAR* ShowcaseDuskSequencePackage = TEXT("/DreamDynamicWorld/Showcase/LS_DreamWorldShowcaseDusk");
	inline const TCHAR* ShowcaseAuroraPackage = TEXT("/DreamDynamicWorld/Showcase/DSM_ShowcaseAurora");

	/**
	 * The showcase: everything the plugin does in one place, for looking it over as a whole. Rolling ground and a paved
	 * square under the surface materials (realistic) with toon props and a toon figure on it; a hut to step into (a
	 * doorway, a window), a porch before it found by a roof capture box; a pond, a window in the open, an irregular snow
	 * field, a grove (leaves to the rain), a campfire (warm air, no snow round it), crates the wind pushes and a storm that
	 * drifts through. The weather runs a short day: clear - cloudy - rain - storm - after the rain - snow - dust - a clear
	 * night with an aurora (a sky modifier). The dusk level is the same set with a level sequence playing: dusk into night
	 * as the clear sky turns to rain (the time and weather tracks), in cinematic mode.
	 *
	 * Built afresh every time (rerun Setup -WorldShowcase to take in what the plugin has gained since).
	 */
	bool BuildShowcaseMaps(UDreamSkyStyle* Style);
}
