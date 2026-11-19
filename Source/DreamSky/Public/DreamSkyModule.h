// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMSKY_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamSky, Log, All);

/**
 * The sky's vectors in the plugin's shared Material Parameter Collection. The sky material reads the look
 * ones; any material may read the lighting ones (a toon shader's main light, ambient and day phase).
 */
namespace DreamSky::Names
{
	// look of the dome
	inline const FName MPC_Zenith(TEXT("DDW_SkyZenith"));         // rgb
	inline const FName MPC_Horizon(TEXT("DDW_SkyHorizon"));       // rgb, a = gradient exponent
	inline const FName MPC_Ground(TEXT("DDW_SkyGround"));         // rgb
	inline const FName MPC_SunHalo(TEXT("DDW_SunHalo"));          // rgb, a = halo exponent
	inline const FName MPC_SunDisk(TEXT("DDW_SunDisk"));          // rgb (radiance), a = angular radius, degrees
	inline const FName MPC_MoonDisk(TEXT("DDW_MoonDisk"));        // rgb, a = angular radius, degrees
	inline const FName MPC_MoonHalo(TEXT("DDW_MoonHalo"));        // rgb, a = halo exponent
	inline const FName MPC_Stars(TEXT("DDW_Stars"));              // intensity, density, twinkle, -
	inline const FName MPC_Cloud0(TEXT("DDW_Cloud0"));            // coverage, -, scale, bands
	inline const FName MPC_Cloud1(TEXT("DDW_Cloud1"));            // drift x, drift y (in 2.2 km x scale), -, -
	inline const FName MPC_Cloud2(TEXT("DDW_Cloud2"));            // cumulus grown by the hour (0..1), erosion, detail near the view, -
	inline const FName MPC_Cloud3(TEXT("DDW_Cloud3"));            // visibility (cm), haze height (cm), high cloud cover, high cloud altitude (cm)
	inline const FName MPC_CloudLit(TEXT("DDW_CloudLit"));        // rgb, a = rim strength
	inline const FName MPC_CloudShadow(TEXT("DDW_CloudShadow"));  // rgb, a = opacity
	inline const FName MPC_CloudLayer(TEXT("DDW_CloudLayer"));    // layer height (cm), darkness at the view, storm darkening, 1 = engine volumetric clouds drawn
	inline const FName MPC_SkyExtra(TEXT("DDW_SkyExtra"));        // cloud sky tint, cloud sun glow, sun disk share in the sky light, flash gain
	inline const FName MPC_SkyHaze(TEXT("DDW_SkyHaze"));          // rgb = height fog colour, a = how much of the dome weather fog hides

	// lighting, for every material
	inline const FName MPC_MainLight(TEXT("DDW_MainLight"));            // direction toward the light, a = illuminance (lux)
	inline const FName MPC_MainLightColor(TEXT("DDW_MainLightColor"));  // rgb, a = 0 sun / 1 moon
	inline const FName MPC_AmbientSky(TEXT("DDW_AmbientSky"));          // rgb (intensity included)
	inline const FName MPC_AmbientEquator(TEXT("DDW_AmbientEquator"));  // rgb
	inline const FName MPC_AmbientGround(TEXT("DDW_AmbientGround"));    // rgb
	inline const FName MPC_DayPhase(TEXT("DDW_DayPhase"));              // day, twilight, night, sun elevation / 90
}

class FDreamSkyModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
