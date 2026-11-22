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
	inline const FName MPC_Stars(TEXT("DDW_Stars"));              // intensity, density, twinkle, the share of them (dimmest first) hidden
	inline const FName MPC_StarPole(TEXT("DDW_StarPole"));        // xyz the north celestial pole in the world, w 1 = the catalogue's stars (T_DreamStarMap), 0 = the stylized ones
	inline const FName MPC_MilkyWay(TEXT("DDW_MilkyWay"));        // rgb its tint times how bright it shows (0 = not drawn), a contrast (a power)
	inline const FName MPC_MilkyWayDust(TEXT("DDW_MilkyWayDust")); // x how dark its dust lanes, y saturation, -, -
	inline const FName MPC_StarEquinox(TEXT("DDW_StarEquinox"));  // xyz the March equinox (0 h, 0 degrees) in the world, w how much brighter a magnitude is (1 = 2.512x)
	inline const FName MPC_Cloud0(TEXT("DDW_Cloud0"));            // coverage, -, scale, bands
	inline const FName MPC_Cloud1(TEXT("DDW_Cloud1"));            // drift x, drift y (in 2.2 km x scale), formation phase 0..1 (DreamSky::CloudFormation), -
	inline const FName MPC_Cloud2(TEXT("DDW_Cloud2"));            // cumulus grown by the hour (0..1), erosion, detail near the view, -
	inline const FName MPC_Cloud3(TEXT("DDW_Cloud3"));            // visibility (cm), haze height (cm), high cloud cover, high cloud altitude (cm)
	inline const FName MPC_CloudLit(TEXT("DDW_CloudLit"));        // rgb, a = rim strength
	inline const FName MPC_CloudShadow(TEXT("DDW_CloudShadow"));  // rgb, a = opacity
	inline const FName MPC_CloudLayer(TEXT("DDW_CloudLayer"));    // layer height (cm), darkness at the view, storm darkening, 1 while the sky cloud renderer draws the clouds, 0 while none are drawn
	inline const FName MPC_SkyExtra(TEXT("DDW_SkyExtra"));        // cloud sky tint, cloud sun glow, sun disk share in the sky light, flash gain
	inline const FName MPC_SkyHaze(TEXT("DDW_SkyHaze"));
	// The sky modifiers' change to the weather map's cloud cover and darkness: x = how much of the cover is replaced, y =
	// what replaces it, z, w the same for the darkness (all 0: none).
	inline const FName MPC_SkyModClouds(TEXT("DDW_SkyModClouds"));
	// The aurora: strength (night and presence in; negative: in the stylized looks' banded tones), speed, toward the magnetic pole (x, y); its low and high colours,
	// with how far toward the pole the arc stands (km) and how wide it is (km) in their a.
	inline const FName MPC_Aurora(TEXT("DDW_Aurora"));
	inline const FName MPC_AuroraLow(TEXT("DDW_AuroraLow"));
	inline const FName MPC_AuroraHigh(TEXT("DDW_AuroraHigh"));
	// The night's glow: airglow rgb (the night in), how much of the city's light the clouds' bases catch; a city's
	// light at the horizon rgb (the night and the haze in), how high it climbs (sine of the elevation).
	inline const FName MPC_NightGlow(TEXT("DDW_NightGlow"));
	inline const FName MPC_LightPollution(TEXT("DDW_LightPollution"));
	// The eclipses: how much of the sun the moon covers, the moon's centre on the sun (sun radii, x, y), the corona; how
	// much of the moon the umbra covers, its centre on the moon (moon radii), its radius (moon radii).
	inline const FName MPC_Eclipse(TEXT("DDW_Eclipse"));
	inline const FName MPC_LunarEclipse(TEXT("DDW_LunarEclipse"));          // rgb = height fog colour, a = how much of the dome weather fog hides

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

private:
	/** Waiting for the object system to add the sky's redirects (StartupModule). */
	FDelegateHandle RedirectsHandle;
};
