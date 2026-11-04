// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWorldTypes.h"

/**
 * Sun, moon and calendar maths. Pure functions of (settings, time): the same inputs give the same sky on
 * every machine, which is what makes saved games, replays and screenshots repeatable.
 *
 * World time is a double: seconds since 0001-01-01 00:00 of the world's calendar (FDateTime's epoch), in the
 * world's local time. Directions are world space, pointing from the ground to the body; north is +X turned
 * by NorthYaw, east is +Y turned the same way.
 */
namespace DreamWorld::Celestial
{
	DREAMDYNAMICWORLD_API double FromDateTime(const FDateTime& Time);
	DREAMDYNAMICWORLD_API FDateTime ToDateTime(double WorldSeconds);

	/** Hours since local midnight, 0..24. */
	DREAMDYNAMICWORLD_API double HoursOfDay(double WorldSeconds);

	/** Whole days since 0001-01-01. */
	DREAMDYNAMICWORLD_API int64 DayIndex(double WorldSeconds);

	/** The direction at Elevation degrees above the horizon and Azimuth degrees clockwise from north. */
	DREAMDYNAMICWORLD_API FVector DirectionFromAngles(double ElevationDeg, double AzimuthDeg, double NorthYawDeg);

	/** Elevation (degrees) of a direction. */
	DREAMDYNAMICWORLD_API double ElevationOf(const FVector& Direction);

	/** Mean phase of the moon at a UTC Julian day: 0 new, 0.5 full. */
	DREAMDYNAMICWORLD_API double MeanMoonPhase(double JulianDayUtc);

	/** Lit fraction of the disk for a phase. */
	DREAMDYNAMICWORLD_API double IlluminationFromPhase(double Phase);

	/** Where the sun and the moon are. */
	DREAMDYNAMICWORLD_API FDreamCelestialState Evaluate(const FDreamCelestialSettings& Settings, double WorldSeconds);

	/**
	 * Day / twilight / night weights from the sun's elevation. Day fades in from TwilightTop to DayBottom
	 * degrees, night fades in from NightTop down to NightBottom; twilight fills the rest.
	 */
	DREAMDYNAMICWORLD_API FDreamDayPhase DayPhase(float SunElevation, float DayBottom = 6.0f, float TwilightTop = -2.0f, float NightTop = -4.0f, float NightBottom = -12.0f);
}
