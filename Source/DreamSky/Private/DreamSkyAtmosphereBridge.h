// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ADreamSky;
class UDreamSkyStyle;
class UDreamWorldSubsystem;
struct FDreamSkyLighting;
struct FDreamSkyLook;

/**
 * The sky on the physical atmosphere (DreamAtmosphere): the style's air handed to the atmosphere, the physical sky read
 * back into the lighting and the look (the sun's light through the air, the ambient light, the fog's colour, the dome's
 * zenith and horizon for the sky light's capture and the clouds), the style's grade into the shared parameters, the sky
 * table onto the dome. As far as the grade's GradientBlend says, the style's own curves stay (all of them while the
 * atmosphere is not there: no renderer, the tables not built yet).
 */
namespace DreamSky::AtmosphereBridge
{
	/** The shared parameters the dome reads the grade from (DDW_SkyGrade0..3). */
	void RegisterParameters(FName Owner);

	/**
	 * After the style's weather (the lighting and the look as the style's curves have them, weather included): the
	 * physical sky in, as far as the grade says. Writes the grade parameters and binds the sky table to the dome.
	 */
	void Apply(UDreamWorldSubsystem& Clock, ADreamSky& Sky, const UDreamSkyStyle& Style, FDreamSkyLighting& Lighting, FDreamSkyLook& Look);
}
