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
	inline const TCHAR* ShowcaseHaloPackage = TEXT("/DreamDynamicWorld/Showcase/DSM_ShowcaseHalo");
	inline const TCHAR* ShowcaseNoctilucentPackage = TEXT("/DreamDynamicWorld/Showcase/DSM_ShowcaseNoctilucent");
	inline const TCHAR* ShowcaseMeteorsPackage = TEXT("/DreamDynamicWorld/Showcase/DSM_ShowcaseMeteors");
	inline const TCHAR* ShowcaseCloudPaintPackage = TEXT("/DreamDynamicWorld/Showcase/CP_ShowcaseClouds");
	inline const TCHAR* ShowcasePondPackage = TEXT("/DreamDynamicWorld/Showcase/Meshes/SM_DreamShowcasePond");

	/**
	 * The showcase pond: its middle, its usual water level (cm), and how high its water can rise (its water body's brim):
	 * the water's mesh reaches out under the bank to where the bank stands that high, so the risen water still meets it.
	 */
	inline const FVector2D ShowcasePondCenter(900.0, -1100.0);
	constexpr double ShowcasePondLevel = -55.0;
	constexpr double ShowcasePondRise = 30.0;
	constexpr double ShowcasePondReach = 40.0;

	/** The clear basin in the sun (its caustics): its middle and its inner size (cm). */
	inline const FVector2D ShowcaseBasinCenter(2700.0, -1500.0);
	inline const FVector2D ShowcaseBasinSize(300.0, 200.0);

	/**
	 * The showcase's tornado, in the afternoon's storm: a spawner's (ADreamWindVortexSpawner) whose path crosses this
	 * point, 2.5 km east of the hut, heading north; it lives ShowcaseTornadoMinutes and is half way through its life at
	 * ShowcaseTornadoMidHour on the showcase's day (the schedule tour's storm), so its stages fall at fixed hours: forming
	 * at 14.074, touching down 14.14, moving on 14.2, roping out 14.317, breaking 14.328.
	 */
	inline const FVector2D ShowcaseTornadoPass(252450.0, 700.0);
	constexpr double ShowcaseTornadoMidHour = 14.2;
	constexpr float ShowcaseTornadoMinutes = 18.0f;

	/** The far land round the set (near hills 5-7.5 km out, mid mountains 12-16 km, a far range 24-30 km, to 2 km high). */
	inline const TCHAR* ShowcaseFarLandPackage = TEXT("/DreamDynamicWorld/Showcase/Meshes/SM_DreamShowcaseFarLand");
	inline const FVector2D ShowcaseFarCenter(2500.0, 0.0);

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
