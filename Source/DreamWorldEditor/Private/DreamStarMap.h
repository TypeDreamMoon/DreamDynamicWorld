// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The real stars the sky dome can draw (DreamSkyStylized.ush, DSky_CatalogueStars), from the Yale Bright Star
 * Catalogue, 5th revised edition (Hoffleit & Warren, 1991): its 9110 records, some 9096 of them stars with a place.
 * Setup reads the catalogue's file (gzipped, as it is distributed) and builds a small map of them on the celestial
 * sphere: an octahedral map of Size x Size cells in the equatorial frame (x toward the March equinox, y toward 6 h of
 * right ascension, z toward the north celestial pole), each cell holding the brightest star of those nearest it --
 *   R, G  where the star lies, from the cell's middle, -1.5 .. 1.5 cells across and down (a star whose own cell was
 *         taken goes to a free neighbour)
 *   B     how bright: 0 an empty cell; 1 .. 255 magnitude 8 .. -2
 *   A     its colour: B-V -0.4 .. 2.0 (the reddest held at 2.0)
 * The dome reads the cells round a pixel's and draws each star a point of light as bright as its magnitude.
 */
namespace DreamStarMap
{
	/** Bump when the map changes, so Setup builds it again. */
	inline constexpr int32 Version = 1;

	/** Cells across the map (some 0.4 degrees each). */
	inline constexpr int32 Size = 512;

	struct FStar
	{
		/** Radians, J2000. */
		double RightAscension = 0.0;
		double Declination = 0.0;
		/** Visual magnitude. */
		float Magnitude = 0.0f;
		/** B-V colour index (0.6, the sun's, where the catalogue gives none). */
		float ColorIndex = 0.6f;
	};

	/** Inflates a gzip file; false (with why) when it is not one or does not inflate. */
	bool Unzip(const TArray<uint8>& Compressed, TArray<uint8>& OutData, FString& OutError);

	/**
	 * Reads the catalogue's fixed-width records (right ascension bytes 76-83, declination 84-90, V 103-107, B-V
	 * 110-114). Records with no place (the novae and the like) are left out. False, with why, when the text does not
	 * look like the catalogue: far from 9110 records, or a field out of its range.
	 */
	bool Parse(const FString& Text, TArray<FStar>& OutStars, FString& OutError);

	/** The star's direction, a unit vector in the equatorial frame. */
	FVector3d Direction(const FStar& Star);

	/** A direction (the equatorial frame) to the octahedral map's 0..1 coordinates, and back. */
	FVector2d Encode(const FVector3d& Direction);
	FVector3d Decode(const FVector2d& UV);

	/** The map, InSize x InSize texels (x fastest; FColor channels as above); returns how many stars found no cell. */
	int32 Build(const TArray<FStar>& Stars, int32 InSize, TArray<FColor>& OutTexels);
}
