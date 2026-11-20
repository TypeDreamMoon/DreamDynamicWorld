// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamSkyStyle;
class UDreamWeatherPreset;
class UStaticMesh;

namespace DreamWorld::Setup
{
	inline const TCHAR* CloudTestMapPackage = TEXT("/DreamDynamicWorld/Cloud/Maps/Lvl_DreamCloudVolumeTest");
	inline const TCHAR* CloudSculptPackage = TEXT("/DreamDynamicWorld/Cloud/SM_DreamCloudSculpt");

	/** A mesh merged from the engine's cube, sphere and cylinder overlapping (the cloud volumes' merged-mesh example),
	 *  allowing CPU access so a game can bake it too. Built again when bForce or missing. */
	UStaticMesh* EnsureCloudSculptMesh(bool bForce);

	/**
	 * The cloud volume test level, around a camera at the origin looking along +x:
	 *   ahead (+x)      the three looks side by side: realistic, toon, painted;
	 *   right (+y)      a mesh cube, a mesh sphere and the merged sculpt as clouds;
	 *   behind (-x)     a toon cloud with a tunnel through it at the camera's height, and a realistic cloud beside it to fly
	 *                   through (and carve);
	 *   left (-y), high up  a painted castle in the sky.
	 * The sky stands at 15:00 under clear weather.
	 */
	bool BuildCloudTestMap(UDreamSkyStyle* Style, UDreamWeatherPreset* Weather);
}
