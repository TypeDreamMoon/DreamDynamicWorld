// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The Milky Way the sky dome draws behind the stars (DreamSkyStylized.ush, DSky_MilkyWay), made up here from noise --
 * no photograph -- and baked by Setup into a map in galactic coordinates: longitude across (the galactic centre, in
 * Sagittarius, in the middle; the anticentre, in Auriga, at the edges, where the map wraps), latitude down (+90 at the
 * top). A band of light thickest and brightest toward the centre with the bulge round it, its width and brightness
 * varying along it, clumped into star clouds (Cygnus, Scutum, Sagittarius stand out); dust along its plane, patchy and
 * filamented, with the Great Rift splitting it from Cygnus down to Ophiuchus and dark clouds spilling north of the
 * centre; the two Magellanic Clouds in the south. Warm white toward the centre, a cooler white along the arms.
 *   RGB  its light (sRGB), before the dust
 *   A    the dust's optical depth / 4 (the dome dims the light by it, as strong as the style says)
 */
namespace DreamMilkyWay
{
	/** Bump when the map changes, so Setup builds it again. */
	inline constexpr int32 Version = 1;

	inline constexpr int32 Width = 2048;
	inline constexpr int32 Height = 1024;

	/** The galactic frame in the equatorial one (x toward the centre, z toward the north galactic pole), J2000. */
	FVector3d CentreAxis();
	FVector3d NinetyAxis();
	FVector3d PoleAxis();

	/** The light (linear, before the dust, not yet scaled to the map) and the dust's optical depth at a galactic
	 *  longitude and latitude, degrees. */
	void Evaluate(double Longitude, double Latitude, FLinearColor& OutLight, float& OutDust);

	/** The map, InWidth x InHeight texels (x fastest; FColor as above, RGB already sRGB-encoded). */
	void Build(int32 InWidth, int32 InHeight, TArray<FColor>& OutTexels);
}
