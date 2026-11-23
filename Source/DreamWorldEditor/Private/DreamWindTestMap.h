// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamSkyStyle;

namespace DreamWorld::Setup
{
	inline const TCHAR* WindTestMapPackage = TEXT("/DreamDynamicWorld/Wind/Maps/Lvl_DreamWindFieldTest");

	/** Where the wind test level's pieces stand (tests, the GPU measurements and the tour read them from here). */
	namespace WindTest
	{
		/** The wind blows along +x at this speed (cm/s), steady: no weather actor drives it. */
		inline const FVector WindDirection = FVector(1.0, 0.0, 0.0);
		inline constexpr float WindSpeed = 900.0f;
		/** A 10 x 8 x 6 m house; downwind of it (+x) its wind shadow, at its corners eddies. */
		inline const FVector HouseCenter = FVector(1500.0, 0.0, 300.0);
		inline const FVector HouseSize = FVector(1000.0, 800.0, 600.0);
		/** A long wall across the wind, 24 m x 0.4 m x 3 m. */
		inline const FVector WallCenter = FVector(-1500.0, 0.0, 150.0);
		inline const FVector WallSize = FVector(40.0, 2400.0, 300.0);
		/** An L of two 12 m walls opening downwind: the corner holds an eddy. */
		inline const FVector CornerPoint = FVector(1200.0, -2400.0, 0.0);
		/** A conical hill, 30 m across and 8 m high: faster over the top, steered round. */
		inline const FVector HillCenter = FVector(-2500.0, 2500.0, 0.0);
		/** The clearing where a radial impulse goes off every few seconds (an explosion's stand-in). */
		inline const FVector ClearingCenter = FVector(-2500.0, -2500.0, 100.0);
		/** The grass: a square this half size around the origin. */
		inline constexpr float GrassExtent = 4000.0f;
		/** The shot camera: upwind and to the side of the house, over the grass. */
		inline const FVector CameraLocation = FVector(-800.0, -2200.0, 250.0);
		inline const FRotator CameraRotation = FRotator(-8.0, 45.0, 0.0);
	}

	/**
	 * The wind field test level (Setup -WindFieldTestMap; beside the hand-built demo level Lvl_DreamWindTest, which it
	 * leaves alone): flat ground with a dense field of wind grass (M_DreamWindGrass), a house, a long wall across the
	 * wind, an L-shaped corner, a hill and a clearing with a periodic radial impulse, under a steady wind along +x; the
	 * wind's terrain map baked over it (the wind shadow today's consumers march through). For the wind's baselines, its
	 * debug views and, later, the fluid wind's acceptance (shadows round the house, eddies in the corner, wakes in the
	 * grass, the impulse pushing the grass).
	 */
	bool BuildWindTestMap(UDreamSkyStyle* Style);
}
