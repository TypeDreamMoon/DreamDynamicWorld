// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Eclipses as the sky shows them: pure functions of the sun's and the moon's directions (from the ground toward them).
 *
 * A disk's own frame on the sky is the one the dome draws the moon in: X = normalize(Up x Direction) with Up the
 * vertical (or +X for a body overhead), Y = Direction x X; positions in it are in the disk's own radii.
 */
namespace DreamSky::Eclipse
{
	/** The sun's angular radius, radians (0.2666 degrees). */
	constexpr double SunRadius = 0.004653;
	/** The moon's, as against the sun's (a little larger, so a central eclipse is total). */
	constexpr double MoonToSun = 1.03;
	/** The earth's umbra at the moon's distance, angular radius (0.70 degrees), and the moon's (0.259 degrees). */
	constexpr double UmbraRadius = 0.01222;
	constexpr double MoonRadius = 0.004520;

	/** Area of the overlap of a circle of radius 1 and one of radius R at distance D, over the first's area (0..1). */
	DREAMSKY_API float DiskOverlap(float D, float R);

	/** The distance (D >= 0) at which DiskOverlap(D, R) is Overlap: the reverse, for an eclipse set by its coverage. */
	DREAMSKY_API float DistanceForOverlap(float Overlap, float R);

	/** A disk's frame on the sky (see the namespace's comment). */
	DREAMSKY_API void DiskFrame(const FVector& Direction, FVector& OutX, FVector& OutY);

	/** Where Point lies in the frame of a disk at Direction, in units of Radius (radians), on the sky. */
	DREAMSKY_API FVector2D OnDisk(const FVector& Direction, const FVector& Point, double Radius);

	/** A solar eclipse: how much of the sun's disk the moon covers, and where the moon's centre stands on it (sun radii). */
	DREAMSKY_API float Solar(const FVector& Sun, const FVector& Moon, FVector2D& OutMoonOnSun);

	/** A lunar eclipse: how much of the moon's disk the earth's umbra covers, and where its centre stands on it (moon radii). */
	DREAMSKY_API float Lunar(const FVector& Sun, const FVector& Moon, FVector2D& OutUmbraOnMoon);
}
