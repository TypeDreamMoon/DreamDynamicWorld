// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "DreamSkyTypes.h"
#include "DreamAtmosphereTypes.h"
#include "DreamSkyGrade.h"
#include "DreamSkyStyle.generated.h"

/**
 * The aurora: curtains of light along the magnetic latitude, some 100 to 300 kilometres up, toward the pole (green
 * low down, red and violet higher), folding and drifting. It shows at night where the world lies far enough north or
 * south (the celestial settings' latitude, in the realistic mode), or as strong as the artistic mode is told; a sky
 * modifier can bring it anywhere.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyAurora
{
	GENERATED_BODY()

	/** How bright it draws at its strongest, in the dome's own measure (a dark night's zenith is some 0.006). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", UIMax = "0.3"))
	float Brightness = 0.12f;

	/** Its colour low down, near its lower edge (the green of oxygen some 100 km up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (HideAlphaChannel))
	FLinearColor LowColor = FLinearColor(0.30f, 0.90f, 0.56f, 1.0f);

	/** Its colour at the top of the curtains (the red and violet some 250 km up and more). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (HideAlphaChannel))
	FLinearColor HighColor = FLinearColor(0.62f, 0.30f, 0.58f, 1.0f);

	/** How fast the curtains fold, ripple and drift (1 = as a quiet night's, some tens of seconds a fold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", UIMax = "8"))
	float Speed = 1.0f;

	/** How far toward the pole the arc stands from the view, km (further = lower in the sky). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (UIMin = "-500", UIMax = "1500"))
	float ArcDistance = 250.0f;

	/** How wide the arc is, km. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "5", UIMax = "400"))
	float ArcWidth = 70.0f;

	/** Realistic mode: the latitude (north or south) where it begins to show. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "90"))
	float StartLatitude = 50.0f;

	/** Realistic mode: the latitude from which it shows in full. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "90"))
	float FullLatitude = 66.0f;

	/** Under the toon and painted clouds, its light goes in three clean tones (as the clouds' own); off, it keeps its
	 *  own soft light whatever the clouds' look. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora")
	bool bStylizedTones = false;

	/** The banded tones' three steps (of its brightness), and how soft their edges are (1 = the look's own). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bStylizedTones"))
	float ToneStep1 = 0.14f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bStylizedTones"))
	float ToneStep2 = 0.295f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bStylizedTones"))
	float ToneStep3 = 0.52f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", UIMax = "4", EditCondition = "bStylizedTones"))
	float ToneSoftness = 1.0f;

	/** The banded tones' brightness at their first and second steps (the third reaches the full: 1 minus both). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bStylizedTones"))
	float ToneLevel1 = 0.22f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bStylizedTones"))
	float ToneLevel2 = 0.33f;

	/** The curtains' ribbons: how long along the oval and how wide across their noise is (km), and how thin the ribbons
	 *  are drawn (a power: higher, thinner). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "10", UIMax = "5000"))
	float RibbonLength = 900.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "10", UIMax = "2000"))
	float RibbonWidth = 260.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "1", UIMax = "40"))
	float RibbonSharpness = 13.0f;

	/** The rays along a curtain: how far apart (km), how much they stand out, and how fast the curtain fades upward
	 *  (the rays reach higher). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "1", UIMax = "300"))
	float RaySpacing = 45.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "0", UIMax = "4"))
	float RayContrast = 1.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "0.1", UIMax = "6"))
	float UpwardFade = 1.5f;

	/** Its sharp lower edge and its top (km up), and how bright the band along the lower edge shines. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "50", UIMax = "200"))
	float LowerEdge = 95.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "100", UIMax = "600"))
	float TopAltitude = 320.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "0", UIMax = "3"))
	float EdgeBand = 0.7f;

	/** How wide the oval the curtains stand in is, in ArcWidths. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora|Shape", meta = (ClampMin = "0.5", UIMax = "10"))
	float OvalWidth = 3.5f;

	/** Artistic mode (no latitude): how present it is, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ClampMin = "0", ClampMax = "1"))
	float ArtisticPresence = 0.0f;
};

/**
 * How a sky looks and lights the scene through the day. Every curve is keyed by the sun's elevation in
 * degrees (-90..90), not by the hour, so one style works for any day length, latitude or season.
 *
 * The defaults are a restrained stylized palette. A project makes its own by duplicating the default
 * asset (Setup creates /DreamDynamicWorld/Sky/DS_DreamSkyStylized) and editing the curves.
 */
/**
 * The night sky's own light: the faint airglow of the upper air everywhere, and a city's light pollution, a warm glow
 * climbing from the horizon that the clouds' bases catch (an overcast over a city glows orange from below).
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyNightGlow
{
	GENERATED_BODY()

	/** The airglow added to the whole night sky, in the dome's measure (a dark night's zenith is some 0.006). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Glow", meta = (HideAlphaChannel))
	FLinearColor Airglow = FLinearColor(0.0010f, 0.0016f, 0.0011f, 1.0f);

	/** How strong a city's glow is at the horizon, in the dome's measure (0: none, the wilderness; 0.02 a town; 0.08 a city). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Glow", meta = (ClampMin = "0", UIMax = "0.3"))
	float LightPollution = 0.0f;

	/** Its colour (sodium orange; whiter for modern lights). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Glow", meta = (HideAlphaChannel))
	FLinearColor LightPollutionColor = FLinearColor(1.0f, 0.58f, 0.28f, 1.0f);

	/** How high it climbs into the sky: the sine of the elevation where it has fallen to a third. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Glow", meta = (ClampMin = "0.01", ClampMax = "1"))
	float Climb = 0.18f;

	/** How much of it the clouds' bases catch (and weather haze spreads it further up the sky). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Glow", meta = (ClampMin = "0", UIMax = "4"))
	float CloudUnderLight = 3.0f;
};

/**
 * Eclipses: where the sun and the moon line up (in the realistic celestial mode), or as a sky modifier sets one. A solar
 * eclipse covers the sun's disk with the moon's (drawn at their true places, to the disks' size) and takes the sun's
 * light with it, the sky darkening and the stars coming out near totality, when the corona shows; in a lunar eclipse
 * the earth's umbra crosses the moon, turning it a dark copper, and the moon's light fails.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyEclipse
{
	GENERATED_BODY()

	/** Realistic celestial mode: eclipses where the sun and the moon line up (off: only those a sky modifier sets). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse")
	bool bFromSunAndMoon = true;

	/** How bright the corona is at totality, as a share of the sun disk's brightness at its rim. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "0", UIMax = "0.2"))
	float Corona = 0.05f;

	/** How much of the sun's disk is covered before the sky and the exposure darken (the eye makes up for the sun's
	 * failing light until then); the sun's light itself goes with the covered share all the way. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "0", ClampMax = "0.99"))
	float DarkensFrom = 0.8f;

	/** At totality the sky, its light and the exposure are as the style has them with the sun this far (degrees) under the
	 * horizon: a deep blue twilight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "-18", ClampMax = "0"))
	float TotalityTwilight = -5.0f;

	/** The horizon all round at totality: the far air outside the moon's shadow, still in the sun, glowing orange. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (HideAlphaChannel))
	FLinearColor TotalityRing = FLinearColor(0.60f, 0.34f, 0.12f, 1.0f);

	/** How low the ring keeps to the horizon: the sky gradient's horizon exponent at totality (smaller: a thinner ring). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "0.05", ClampMax = "1"))
	float TotalityRingExponent = 0.12f;

	/** The share of the stars that come out at totality: the brightest only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "0", ClampMax = "1"))
	float TotalityStars = 0.04f;

	/** How much of the moon's light is left at the middle of a total lunar eclipse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "0", ClampMax = "1"))
	float BloodMoonLight = 0.03f;

	/** The moon's light in the umbra (the dark copper of sunlight bent through the earth's air). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (HideAlphaChannel))
	FLinearColor BloodMoonColor = FLinearColor(1.0f, 0.42f, 0.25f, 1.0f);
};

/**
 * The sunlight the air below the clouds scatters toward the view, where the clouds let it through (the sky cloud
 * renderer's own shadow of them): shafts down through the gaps of broken clouds, seen from anywhere -- the sun in view
 * or behind it. Drawn by the sky cloud renderer (ADreamSky::DrawsClouds) over the scene before the post processing.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyLightShafts
{
	GENERATED_BODY()

	/** How bright the shafts are drawn, times the light the haze scatters (a styled sky outshines its sun far more than
	 * the real one does, so the bare scattering would not show against it); 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0", UIMax = "16"))
	float Intensity = 4.0f;

	/** How much of the light the haze the shafts show in scatters at the ground, per km. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0", UIMax = "0.3"))
	float Density = 0.06f;

	/** How high (km) the haze thins to a third of that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0.05", UIMax = "5"))
	float ScaleHeight = 1.5f;

	/** How much more the haze scatters forward (toward the sun) than to the side: 0 = alike every way, toward 1 = only
	 * about the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "-0.9", ClampMax = "0.9"))
	float Anisotropy = 0.4f;

	/** How far along the view the shafts are gathered (km). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0.1", UIMax = "40"))
	float Distance = 16.0f;

	/** How much the weather's haze (fog, dust) thickens it: the density times 1 + this x the haze. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0", UIMax = "6"))
	float WeatherHaze = 2.0f;

	/** The cloud cover from which the shafts show, and by which they are whole (a clear sky casts none: the haze lit
	 * evenly is the fog's and the sky's to draw). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0", ClampMax = "1"))
	float CoverStart = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ClampMin = "0", ClampMax = "1"))
	float CoverFull = 0.35f;
};

/**
 * The Milky Way behind the stars (T_DreamMilkyWay, made from noise by Setup), where it stands on the celestial sphere
 * for the latitude and the sidereal time: the band widening and brightening toward the centre in Sagittarius, its star
 * clouds, its dust lanes and the Great Rift. Only on a dark night: the twilight, moonlight, a city's light, haze and fog
 * all wash it out -- a full moon or a bright city all but hides it, a moonless night in the country shows it best.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyMilkyWay
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way")
	bool bEnabled = true;

	/** How bright it is drawn on the darkest night (in the dome's measure, as the stars' intensity curve). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ClampMin = "0", UIMax = "2"))
	float Intensity = 0.35f;

	/** A power on its light: above 1 the star clouds stand out more against the faint band, below 1 it is flatter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ClampMin = "0.25", UIMax = "3"))
	float Contrast = 1.0f;

	/** How dark its dust lanes and the Great Rift are (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ClampMin = "0", UIMax = "3"))
	float Dust = 1.0f;

	/** Its colour: warm white toward the centre, cooler along the arms (0 = grey, 1 = as made). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ClampMin = "0", UIMax = "2"))
	float Saturation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (HideAlphaChannel))
	FLinearColor Tint = FLinearColor::White;

	/** The sun's elevation (degrees) where it starts to show, and where the night is dark enough for all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (UIMin = "-20", UIMax = "0"))
	float ShowsBelow = -10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (UIMin = "-25", UIMax = "0"))
	float FullBelow = -18.0f;

	/** How much a full moon high up washes it out (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonWash = 0.92f;

	/** The city's light (the dome's LightPollution) that leaves a third of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ClampMin = "0.001", UIMax = "0.1"))
	float CityWash = 0.02f;
};

/** Which stars the dome draws. */
UENUM(BlueprintType)
enum class EDreamSkyStarField : uint8
{
	/** A scatter of stars of the style's density, turning with the sky. */
	Stylized,
	/** The real sky's: the Yale Bright Star Catalogue's some 9100 stars to magnitude 6.5, each where it is for the
	 *  latitude and the sidereal time, as bright as its magnitude and its own colour (T_DreamStarMap, built by Setup). */
	Catalogue,
};

/** The lens flare's look (FDreamSkyLensFlare). */
UENUM(BlueprintType)
enum class EDreamSkyLensFlarePreset : uint8
{
	None,
	/** A soft glow about the sun, a faint ring, ghosts of a few colours across the view. */
	Natural,
	/** A long blue streak across the sun, oval ghosts stretched with it. */
	Anamorphic,
	/** Flat hexagons in a few clean tones and a crisp ring (for the toon and painted looks). */
	Stylized,
};

/**
 * The sun's lens flare, over the scene after the depth of field: as bright as the sun shows on screen over the
 * threshold -- measured from the frame itself, so the clouds, a wall or an eclipse in front of the sun weaken it, and
 * it is gone with the sun out of view.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyLensFlare
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens Flare")
	EDreamSkyLensFlarePreset Preset = EDreamSkyLensFlarePreset::None;

	/** How bright the flare is drawn for the sun's brightness over the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens Flare", meta = (ClampMin = "0", UIMax = "8"))
	float Intensity = 1.0f;

	/** How bright the sun has to show on screen before it flares (1 = white on screen). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens Flare", meta = (ClampMin = "0", UIMax = "10"))
	float Threshold = 1.0f;

	/** Over the sun's own colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens Flare", meta = (HideAlphaChannel))
	FLinearColor Tint = FLinearColor::White;
};

/**
 * Rain drops on the camera's lens, over the finished picture: they gather while the view is out in the rain facing
 * into it (looking up catches more than looking down; a roof overhead or a room keeps them off) and dry off indoors
 * or once the rain stops. A lens effect, so off unless the project turns it on.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyScreenDroplets
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets")
	bool bEnabled = false;

	/** How many drops a wet screen holds (1 = as the rain gives). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets", meta = (ClampMin = "0", UIMax = "2"))
	float Intensity = 1.0f;

	/** How big the drops are drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets", meta = (ClampMin = "0.25", UIMax = "3"))
	float DropSize = 1.0f;

	/** How far a drop bends the picture behind it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets", meta = (ClampMin = "0", UIMax = "3"))
	float Refraction = 1.0f;

	/** How fast (s) the screen gets wet out in the rain, and dries off out of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets", meta = (ClampMin = "0", UIMax = "30"))
	float WetSeconds = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets", meta = (ClampMin = "0", UIMax = "60"))
	float DrySeconds = 12.0f;
};

/**
 * Frost on the camera's lens, over the finished picture: it grows in from the rim (the corners first) while the view
 * is out in the snow in the cold, stays a long while out in the cold once the snow is over, and melts indoors or in the
 * warm -- into drops on the lens, when those are drawn. A lens effect, so off unless the project turns it on.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyScreenFrost
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost")
	bool bEnabled = false;

	/** How thick the frost is drawn (1 = as the cold gives). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0", UIMax = "2"))
	float Intensity = 1.0f;

	/** How big its crystals are drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0.25", UIMax = "3"))
	float CrystalSize = 1.0f;

	/** How much it blurs the picture behind it, and how far its crystals bend it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0", UIMax = "3"))
	float Blur = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0", UIMax = "3"))
	float Refraction = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (HideAlphaChannel))
	FLinearColor Tint = FLinearColor(0.88f, 0.94f, 1.0f);

	/** The air's temperature (degrees C) where frost starts to grow, and where it is as cold as it gets for it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (UIMin = "-30", UIMax = "5"))
	float StartsBelow = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (UIMin = "-40", UIMax = "0"))
	float FullBelow = -8.0f;

	/** How fast (s) it grows out in the snow, melts indoors or in the warm, and fades out in the cold with no snow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0", UIMax = "120"))
	float FreezeSeconds = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0", UIMax = "60"))
	float MeltSeconds = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ClampMin = "0", UIMax = "600"))
	float LingerSeconds = 90.0f;
};

/**
 * The shimmer of hot air: what lies far off near the horizon wavers, rising, on a hot day in the sun -- more the hotter
 * it is, the higher the sun, the clearer and drier the air; the wind stirs it away.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyHeatHaze
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze")
	bool bEnabled = true;

	/** How far it shifts the picture (1 = some 2.5 pixels at 1080p, at its strongest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (ClampMin = "0", UIMax = "4"))
	float Intensity = 1.0f;

	/** The air's temperature (degrees C) where it starts, and where it is as strong as it gets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (UIMin = "0", UIMax = "50"))
	float StartsAbove = 24.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (UIMin = "0", UIMax = "60"))
	float FullAbove = 36.0f;

	/** How far off (m) what wavers starts, and where it wavers fully. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (ClampMin = "0", UIMax = "2000"))
	float NearDistance = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (ClampMin = "1", UIMax = "5000"))
	float FullDistance = 400.0f;

	/** How far (degrees) above and below the horizon it reaches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (ClampMin = "0.1", UIMax = "15"))
	float BandDegrees = 2.5f;

	/** How fast it shimmers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (ClampMin = "0", UIMax = "4"))
	float Speed = 1.0f;
};

/**
 * A rainbow: 42 degrees round the point straight away from the sun (or the moon), with the fainter second bow beyond
 * it, colours turned about, and the darker sky between -- while there is rain in the air (as it falls, and a while
 * after) and the light low enough and not behind the clouds.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyRainbow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow")
	bool bEnabled = true;

	/** How bright it is drawn (1 = as the light gives). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "0", UIMax = "4"))
	float Intensity = 1.0f;

	/** The second bow's brightness, of the first's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "0", UIMax = "1"))
	float SecondBow = 0.43f;

	/** How wide its colours spread (1 = as the sun's disc spreads them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "0.5", UIMax = "3"))
	float Width = 1.0f;

	/** How broken the bows are where the rain is thinner, 0 whole .. 1 in pieces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "0", ClampMax = "1"))
	float Patchiness = 0.4f;

	/** How long (s) the rain stays in the air once it has stopped falling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "0", UIMax = "600"))
	float LingerSeconds = 90.0f;

	/** How far off (m) the rain that shows it starts, and where it shows fully. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "0", UIMax = "1000"))
	float NearDistance = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ClampMin = "1", UIMax = "5000"))
	float FullDistance = 250.0f;
};

/**
 * The ice halos and the corona: under a sheet of high ice cloud (cirrostratus) a 22 degree ring round the sun or the moon
 * and the sun dogs either side of it at its height; in thin cloud round the moon the corona's coloured rings. Drawn on
 * the dome, in the light the high cloud is lit by (the style's CloudLitColor), as the real ones look whatever the look.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyHalos
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos")
	bool bEnabled = true;

	/** How much high cloud (CloudHighCover) the halos begin to show at, and show in full at: a sheet of ice cloud makes them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", ClampMax = "1"))
	float HighCoverFrom = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", ClampMax = "1"))
	float HighCoverFull = 0.8f;

	/** The 22 degree ring's brightness, of the lit high cloud's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", UIMax = "2"))
	float Intensity = 0.35f;

	/** The ring's radius (degrees: its red inner edge) and how wide it fades outward (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "10", UIMax = "30"))
	float Radius = 22.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0.2", UIMax = "6"))
	float Width = 1.6f;

	/** The sun dogs' brightness, of the lit high cloud's (they fade out as the sun climbs past some 50 degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", UIMax = "3"))
	float SunDogs = 0.9f;

	/** How milky the sheet makes the sky round the halos, 0 .. 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", ClampMax = "1"))
	float Milkiness = 0.12f;

	/** The moon's corona in thin cloud: its brightness (of the lit cloud's), how much high cloud it begins and shows
	 *  fully at, and its first red ring's radius (degrees; smaller droplets, larger rings; at least two and a half times
	 *  the drawn moon's radius, MoonDiskSize, or the moon would hide it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", UIMax = "4"))
	float Corona = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", ClampMax = "1"))
	float CoronaCoverFrom = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0", ClampMax = "1"))
	float CoronaCoverFull = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ClampMin = "0.5", UIMax = "8"))
	float CoronaRadius = 2.5f;
};

/**
 * The earth's shadow and the Belt of Venus: after sunset the low sky away from the sun lies in the earth's own shadow,
 * blue-grey up to some twice the sun's depression, with a pink band over it. The physical sky has them of itself; these
 * settings draw them over the style's own gradient, as far as the grade's GradientBlend shows it (none at 0).
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyEarthShadow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Earth Shadow")
	bool bEnabled = true;

	/** How strongly they are drawn over the gradient, 0 .. 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Earth Shadow", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	/** The shadow's colour, times the gradient. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Earth Shadow", meta = (HideAlphaChannel))
	FLinearColor ShadowTint = FLinearColor(0.5f, 0.56f, 0.82f);

	/** The belt's colour over it, times the gradient. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Earth Shadow", meta = (HideAlphaChannel))
	FLinearColor BeltTint = FLinearColor(1.45f, 0.95f, 1.12f);

	/** How high the shadow stands, in the sun's depressions (the physical sky's: about 2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Earth Shadow", meta = (ClampMin = "0.5", UIMax = "4"))
	float Height = 2.0f;
};

/**
 * Noctilucent clouds: thin silver-blue ice some 82 km up, at high latitudes in the weeks round midsummer, lit by the sun
 * long after it has set on the air below; seen in the deep twilight low over where the sun went, in bands and fine
 * billows. Drawn on the dome, in the dome's own measure, the same under every look.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyNoctilucent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds")
	bool bEnabled = true;

	/** How bright they draw at their strongest, in the dome's own measure (a dark night's zenith is some 0.006). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", UIMax = "0.3"))
	float Brightness = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (HideAlphaChannel))
	FLinearColor Color = FLinearColor(0.62f, 0.8f, 1.0f, 1.0f);

	/** Their height, km, and the spacing of their bands and of the billows across them, km. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "60", UIMax = "100"))
	float Altitude = 82.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "5", UIMax = "200"))
	float BandSpacing = 50.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "1", UIMax = "40"))
	float BillowSpacing = 9.0f;

	/** How fast they drift, km a second (some 50 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", UIMax = "0.5"))
	float Drift = 0.05f;

	/** Realistic mode: the latitudes (north or south) they begin and are full at, and fade out by toward the pole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", ClampMax = "90"))
	float StartLatitude = 45.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", ClampMax = "90"))
	float FullLatitude = 52.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", ClampMax = "90"))
	float EndLatitude = 72.0f;

	/** Realistic mode: how many days from midsummer (June 21 north, December 21 south) they are full, and gone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", UIMax = "120"))
	float SeasonFullDays = 35.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "1", UIMax = "180"))
	float SeasonEndDays = 65.0f;

	/** Artistic mode: how present they are (none unless told). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float ArtisticPresence = 0.0f;
};

/** A yearly meteor shower: when it peaks, where its radiant stands among the stars, how strong it is. */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyMeteorShower
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors")
	FName Name;

	/** Its peak, month and day (the same every year, near enough). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "1", ClampMax = "12"))
	int32 PeakMonth = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "1", ClampMax = "31"))
	int32 PeakDay = 12;

	/** How many days either side it falls to some 60% of its peak. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0.1", UIMax = "10"))
	float WidthDays = 2.0f;

	/** The radiant: right ascension and declination, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", ClampMax = "360"))
	float RightAscension = 48.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "-90", ClampMax = "90"))
	float Declination = 58.0f;

	/** Its zenithal hourly rate at the peak: meteors an hour under a dark sky with the radiant overhead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", UIMax = "200"))
	float ZenithalHourlyRate = 100.0f;
};

/**
 * Meteors and satellites in the night sky: sporadic meteors any night, the yearly showers by the date (streaking away
 * from their radiant), and satellites the sun still lights after dusk crossing in a few minutes and fading into the
 * earth's shadow. Drawn on the dome, real-time seconds, the same under every look.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyMeteors
{
	GENERATED_BODY()

	FDreamSkyMeteors();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors")
	bool bEnabled = true;

	/** Sporadic meteors an hour over the whole sky on a dark night. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", UIMax = "60"))
	float SporadicPerHour = 8.0f;

	/** Times every rate (the real ones are sparse for a game: a meteor every few minutes at a strong shower's peak). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", UIMax = "20"))
	float RateScale = 3.0f;

	/** How bright a meteor's head draws, in the dome's own measure, and its colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", UIMax = "5"))
	float Brightness = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (HideAlphaChannel))
	FLinearColor Color = FLinearColor(0.85f, 0.95f, 1.0f, 1.0f);

	/** How long their trails are (1 = some 5 to 25 degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0.2", UIMax = "3"))
	float TrailLength = 1.0f;

	/** The yearly showers (the seven strongest by default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors")
	TArray<FDreamSkyMeteorShower> Showers;

	/** Satellites crossing an hour while the sun still lights them, and how bright (the dome's measure). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", UIMax = "120"))
	float SatellitesPerHour = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ClampMin = "0", UIMax = "2"))
	float SatelliteBrightness = 0.25f;
};

/**
 * The flat clouds: a cheap sheet of cloud the dome draws at the cloud base where the sky cloud renderer does not draw
 * (below Shader Model 5, turned off, or chosen for a low quality: DreamSky.Clouds.Flat), from the weather map's cover
 * and darkness, in the style's cloud colours, with its shadow on the ground.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyFlatClouds
{
	GENERATED_BODY()

	/** Draw them where the renderer does not (off: no clouds there, as before). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flat Clouds")
	bool bEnabled = true;

	/** How much of the sky behind them they hide, 0 .. 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flat Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float Opacity = 0.92f;

	/** 0: shaded smoothly; 2 .. 4: in that many flat tones, as the toon look's clouds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flat Clouds", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Tones = 0;

	/** The clouds' size (1 = some 4 km across the middle octave). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flat Clouds", meta = (ClampMin = "0.1", UIMax = "4"))
	float Scale = 1.0f;

	/** How much of the sun their shadow takes on the ground (0: none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flat Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float ShadowStrength = 0.65f;
};

/**
 * The clouds clearing round the sun and the moon: a low sun's evening clouds thin out round it, its disk and the glow
 * about it showing through, and the moon's likewise. Each ray's cloud is eaten back from its edges and thinned as near
 * the light as the falloff says (the thicker cores last), so the clouds shrink and fade toward it rather than a hole
 * opening; the same for every cloud look, the high cloud and the flat clouds.
 */
USTRUCT(BlueprintType)
struct DREAMSKY_API FDreamSkyCloudClearing
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Clearing")
	bool bEnabled = true;

	/** How far round the light the clouds thin, degrees (to some 37% of the strength at this angle). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Clearing", meta = (ClampMin = "1", UIMax = "60"))
	float Radius = 14.0f;

	/** How it falls off with the angle: 2 a soft bell, higher a flatter middle with a quicker edge, 1 a long tail. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Clearing", meta = (ClampMin = "0.5", UIMax = "8"))
	float Exponent = 2.0f;

	/** How much the clouds thin right by the sun (0 none .. 1 most of them), over the sun's elevation: by default plain
	 *  with a low sun, almost none by noon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Clearing", meta = (XAxisName = "Sun elevation", YAxisName = "Strength"))
	FRuntimeFloatCurve SunStrength;

	/** The same by the moon, over the moon's elevation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Clearing", meta = (XAxisName = "Moon elevation", YAxisName = "Strength"))
	FRuntimeFloatCurve MoonStrength;
};

UCLASS(BlueprintType)
class DREAMSKY_API UDreamSkyStyle : public UDataAsset
{
	GENERATED_BODY()

public:
	UDreamSkyStyle();

	/** The dome's colours, sun/moon/stars and clouds at a moment. */
	FDreamSkyLook EvaluateLook(const FDreamCelestialState& Celestial) const;

	/** How present the aurora is where the world lies (FDreamSkyLook::Aurora), 0..1. */
	float EvaluateAuroraPresence(const FDreamCelestialSettings& Settings) const;

	/** How much of the aurora the sky's darkness shows at a sun elevation (degrees), 0..1. */
	static float AuroraNight(float SunElevation);

	/** How much of the night sky the air lets through, 1 clear .. 0 none, for a haze (FDreamSkyLook::Haze's alpha) and
	 *  the weather's fog and cloud cover (0..1 each): see NightSkyHazeExponent. */
	float NightSkyThrough(float Haze, float Fog, float CloudCover) const;

	/** The strongest meteor shower on a date and how strong it is then (none in the gaps between them). */
	FDreamSkyShowerActivity EvaluateMeteorShower(const FDateTime& Date) const;

	/** How present noctilucent clouds are where and when the world is: by latitude and the days from midsummer in the
	 *  realistic mode, as the style says in the artistic one. */
	float EvaluateNoctilucentPresence(const FDreamCelestialSettings& Settings, const FDateTime& Date) const;

	/** How far the twilight lets them show for the sun this high (degrees): from 4 under, full from 7 to 13, gone by 17
	 *  (the air below dark enough, they still in the sun). */
	static float NoctilucentTwilight(float SunElevation);

	/** How far the halos and the corona show for this much high cloud (CloudHighCover), 0 .. 1 each. */
	FVector2f EvaluateHaloPresence(float HighCover) const;

	/** How far (degrees of azimuth) a sun dog stands from a light this high (degrees) for ice of this refractive index
	 *  (1.31 the yellow), the plate crystals' least deviation; negative when there is none (the light over some 60). */
	static float SunDogAzimuth(float LightElevation, float RefractiveIndex = 1.31f);

	/** How much of the night's own glow and a city's light shows at a sun elevation (degrees), 0..1: from sunset on. */
	static float GlowNight(float SunElevation);

	/** Lights, ambient, fog and exposure at a moment (before any smoothing). */
	FDreamSkyLighting EvaluateLighting(const FDreamCelestialState& Celestial) const;

	/**
	 * What the weather does to both (when Environment.bActive): clouds, a dimmer and greyer sky, more fog.
	 * bCloudsShadowTheSun: the clouds cast their own shadow from the sun (a cloud layer that hides the sun itself, as the
	 * engine's volumetric clouds do), so an overcast sky does not dim the sun as well (OvercastLightDimming still dims the
	 * moon). The sky's own clouds do not: Dream Sky leaves it false.
	 */
	/** bCloudsShadowTheSun: the clouds' own shadow takes the sun off the ground (the sky cloud renderer's light function):
	 * the sun light is left whole and the overcast's dimming goes to Lighting.SunOvercastScale instead. */
	void ApplyWeather(const FDreamWorldEnvironment& Environment, FDreamSkyLighting& Lighting, FDreamSkyLook& Look, bool bCloudsShadowTheSun = false) const;

	/** A colour taken Amount (0..1) of the way to DustColor's hue at its own brightness (what airborne dust does to it). */
	FLinearColor DustTinted(const FLinearColor& Color, float Amount) const;

	/** What being indoors does to the lighting (FDreamViewOcclusion from the view probe). */
	void ApplyInterior(const FDreamViewOcclusion& Occlusion, FDreamSkyLighting& Lighting) const;

	/** A lightning flash on top of the (eased) lighting: instant, never smoothed. Always sets Lighting.FlashGain. */
	void ApplyFlash(const FDreamWorldEnvironment& Environment, FDreamSkyLighting& Lighting) const;

	/** Colour of a lightning flash in the ambient light, the sky and the flash light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning")
	FLinearColor LightningColor = FLinearColor(0.75f, 0.8f, 1.0f, 1.0f);

	/** How bright a full flash makes the ambient light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (ClampMin = "0", UIMax = "4"))
	float LightningAmbient = 0.8f;

	/** Illuminance of the directional light a full flash casts from the strike, lux (0 = no flash light). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (ClampMin = "0", UIMax = "50"))
	float LightningLightIlluminance = 8.0f;

	/** Whether the flash light casts shadows (each flash then redraws the shadow maps it reaches). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning")
	bool bLightningCastsShadows = false;

	/**
	 * How far a flash keeps up with the exposure: 0 = the same light day and night (lost in daylight), 1 = as
	 * bright on screen at noon as at night. A flash is brightened by 2^((EV100 - LightningReferenceEV) * this).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (ClampMin = "0", ClampMax = "1"))
	float LightningExposureCompensation = 0.5f;

	/** The exposure (EV100) at which a flash shows at its plain brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lightning", meta = (UIMin = "-2", UIMax = "6"))
	float LightningReferenceEV = 1.0f;

	// ---- Sky dome ------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor ZenithColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor HorizonColor;

	/** Below the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor GroundColor;

	/** < 1 keeps the horizon colour low, > 1 lets it climb. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dome", meta = (ClampMin = "0.05", UIMax = "4"))
	float HorizonExponent = 0.45f;

	// ---- Atmosphere and grade ------------------------------------------------------------------

	/** The air this sky is made of (DreamAtmosphere): the physical sky, its light through the air and the distance's haze. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	FDreamAtmosphereParameters Atmosphere;

	/**
	 * This sky's table baked ahead for where the GPU cannot build one (mobile, no Shader Model 5): Setup bakes the
	 * default style's, `DreamSky.BakeSky <style>` any style's (a renderer is needed). Without one there the sky is the
	 * gradient. Stands only for the air it was baked from: bake again after changing Atmosphere.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	FDreamAtmosphereBakedSky BakedSky;

	/** The style's look over the physical sky: exposure, saturation, hue, contrast and tints over the day, the weather's part. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grade", meta = (ShowOnlyInnerProperties))
	FDreamSkyGrade Grade;

	// ---- Sun ----------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Radiance"))
	FRuntimeCurveLinearColor SunDiskColor;

	/** The glow around the sun (and along the horizon beside it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor SunHaloColor;

	/** Angular radius of the disk, degrees. The real sun is 0.27; a readable stylized sun is several times that. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0.05", UIMax = "10"))
	float SunDiskSize = 1.6f;

	/** Tightness of the halo: higher = smaller. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "1", UIMax = "64"))
	float SunHaloExponent = 6.0f;

	/**
	 * How large the sun's disk is drawn, times SunDiskSize, over its elevation: larger near the horizon (the moon
	 * illusion, drawn). Only the disk as seen -- its glow, an eclipse's corona and the lens flare with it; the light's
	 * source angle, and so the shadows' softness, stays as it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Disk scale"))
	FRuntimeFloatCurve SunDiskScale;

	/**
	 * Share of the sun disk the sky light sees when it captures the dome. A full disk in the captured light
	 * makes a hot spot every rough surface reflects; the sun's light comes from the directional light anyway.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0", ClampMax = "1"))
	float SunDiskInSkyLight = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor SunLightColor;

	/** Illuminance of the sun's directional light, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (XAxisName = "Sun elevation", YAxisName = "Lux"))
	FRuntimeFloatCurve SunIlluminance;

	// ---- Moon and stars -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor MoonDiskColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor MoonHaloColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0.05", UIMax = "10"))
	float MoonDiskSize = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "1", UIMax = "128"))
	float MoonHaloExponent = 24.0f;

	/** How large the moon's disk is drawn, times MoonDiskSize, over its elevation: larger near the horizon. Only the disk
	 *  as seen -- its glow, its corona and a lunar eclipse's shadow on it with it; the light's source angle stays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Moon elevation", YAxisName = "Disk scale"))
	FRuntimeFloatCurve MoonDiskScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor MoonLightColor;

	/** Illuminance of the moon light at full moon, lux. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (XAxisName = "Sun elevation", YAxisName = "Lux"))
	FRuntimeFloatCurve MoonIlluminance;

	/** How much a thin moon dims the moon light: 0 = never, 1 = by its lit fraction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonPhaseDimming = 0.3f;

	/**
	 * When the moon is below the horizon at night, light the night from where it would read best (opposite the
	 * sun) instead of leaving the scene without a main light. Most stylized games want this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon")
	bool bNightLightAlwaysUp = true;

	/** How soft the moon's terminator is (in the sunlight's cosine across it; small = a crisp edge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0.002", UIMax = "0.5"))
	float MoonTerminatorSoftness = 0.12f;

	/** The earthshine on the moon's dark side at its strongest (near the new moon), of the lit side's brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", UIMax = "0.3"))
	float MoonEarthshine = 0.06f;

	/** How wide the moon light's source grows (degrees) as its light comes from all over the sky instead of from the moon
	 *  -- a closing cover, a dust storm (FDreamSkyLighting::MoonShadow) -- so its shadows blur, and go once none of it
	 *  comes straight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", UIMax = "45", Units = "Degrees"))
	float MoonDiffuseSourceAngle = 20.0f;

	/** How much of the moon's face is NASA's map of it (T_DreamMoonMap, which Setup bakes), 1 .. 0 the procedural maria
	 *  (MoonMaria): the map's near side toward the earth, its north toward the celestial pole. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonFace = 1.0f;

	/** How much darker the procedural maria are than the highlands (where MoonFace is below 1), 0 none .. 1 black (the
	 *  real ones some half as bright). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonMaria = 0.45f;

	/** The moon's colour through a dust storm's haze -- its disk, its glow and its light -- as far as the haze hides the
	 *  night sky (NightSkyThrough): the dust takes the blue first. A fog's droplets leave it white. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (HideAlphaChannel))
	FLinearColor MoonDustTint = FLinearColor(1.0f, 0.6f, 0.35f, 1.0f);

	/** How much of the moonlight's hue (MoonLightColor) the toon clouds' moonlit side takes, 0 .. 1: by moonlight the eye
	 *  sees little colour (low, a grey-blue; 1, the moonlight's blue as it is). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Moon", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonlightCloudSaturation = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (XAxisName = "Sun elevation", YAxisName = "Intensity"))
	FRuntimeFloatCurve StarIntensity;

	/** Which stars: the real sky's, or a stylized scatter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars")
	EDreamSkyStarField StarField = EDreamSkyStarField::Catalogue;

	/** Relative number of stars (the stylized scatter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0", UIMax = "4", EditCondition = "StarField == EDreamSkyStarField::Stylized"))
	float StarDensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Milky Way", meta = (ShowOnlyInnerProperties))
	FDreamSkyMilkyWay MilkyWay;

	/**
	 * How the weather hides the night sky -- the stars, the Milky Way, the aurora, meteors and satellites, noctilucent
	 * clouds, and the moon to a dull disk -- all by one share (NightSkyThrough): the haze (a dust storm's, a fog's) leaves
	 * (1 - haze) to this power of it, so a light haze dims them and a dust storm all but hides them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Sky", meta = (ClampMin = "0.5", UIMax = "6"))
	float NightSkyHazeExponent = 2.0f;

	/** How much of it the weather's fog and its cloud cover each take more, at their thickest (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float NightSkyFogHides = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float NightSkyCloudHides = 0.4f;

	/** How much of the moon (its disk and glow) the night sky's share leaves at none, over what the dome's haze already
	 *  takes: a dull disk in a dust storm, not none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Sky", meta = (ClampMin = "0", ClampMax = "1"))
	float MoonThroughHaze = 0.6f;

	/** How much brighter the catalogue's stars are drawn for each magnitude (1 = as in the sky, 2.512 times; less
	 *  keeps the faint ones in sight next to the bright). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0.1", ClampMax = "1", EditCondition = "StarField == EDreamSkyStarField::Catalogue"))
	float StarContrast = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aurora", meta = (ShowOnlyInnerProperties))
	FDreamSkyAurora Aurora;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Night Glow", meta = (ShowOnlyInnerProperties))
	FDreamSkyNightGlow NightGlow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ShowOnlyInnerProperties))
	FDreamSkyEclipse Eclipse;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Light Shafts", meta = (ShowOnlyInnerProperties))
	FDreamSkyLightShafts LightShafts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lens Flare", meta = (ShowOnlyInnerProperties))
	FDreamSkyLensFlare LensFlare;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Droplets", meta = (ShowOnlyInnerProperties))
	FDreamSkyScreenDroplets ScreenDroplets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen Frost", meta = (ShowOnlyInnerProperties))
	FDreamSkyScreenFrost ScreenFrost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat Haze", meta = (ShowOnlyInnerProperties))
	FDreamSkyHeatHaze HeatHaze;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rainbow", meta = (ShowOnlyInnerProperties))
	FDreamSkyRainbow Rainbow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Halos", meta = (ShowOnlyInnerProperties))
	FDreamSkyHalos Halos;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Earth Shadow", meta = (ShowOnlyInnerProperties))
	FDreamSkyEarthShadow EarthShadow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noctilucent Clouds", meta = (ShowOnlyInnerProperties))
	FDreamSkyNoctilucent Noctilucent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Meteors", meta = (ShowOnlyInnerProperties))
	FDreamSkyMeteors Meteors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flat Clouds", meta = (ShowOnlyInnerProperties))
	FDreamSkyFlatClouds FlatClouds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cloud Clearing", meta = (ShowOnlyInnerProperties))
	FDreamSkyCloudClearing CloudClearing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0", ClampMax = "1"))
	float StarTwinkle = 0.4f;

	/** How fast the stars twinkle (1 = the look's own). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0", UIMax = "4"))
	float StarTwinkleSpeed = 1.0f;

	/** How big the stars are drawn (1 = the look's own; a star is never drawn smaller than about a pixel). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0.1", UIMax = "4"))
	float StarSize = 1.0f;

	/** The stylized scatter's dimmest stars, of its brightest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stars", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "StarField == EDreamSkyStarField::Stylized"))
	float StarDimmest = 0.35f;

	// ---- Clouds -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor CloudLitColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (XAxisName = "Sun elevation", YAxisName = "Colour"))
	FRuntimeCurveLinearColor CloudShadowColor;

	/**
	 * How much the clouds take the colour of the sky around them: 0 = the cloud colours as they are everywhere,
	 * 1 = warm low in a sunset, deep blue overhead, like the sky behind them. The curves still set the level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudSkyTint = 0.6f;

	/** How much the lit side of clouds near the sun catches the sun's halo colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "2"))
	float CloudSunGlow = 0.5f;

	/** Fraction of the sky covered when no weather drives it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudCoverage = 0.45f;

	/**
	 * How much the hour shapes fair-weather cumulus: 0 = the same all day .. 1 = fewer in the morning, the most in the
	 * mid-afternoon, falling apart toward evening, few left in the night. A closing cover is not touched.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudDiurnal = 0.6f;

	/** Size of the cloud shapes; larger = bigger clouds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0.05", UIMax = "8"))
	float CloudScale = 1.0f;

	/** How deep the billows carve into the clouds: 0 = smooth blobs .. 1 = deeply billowed, broken up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "1.5"))
	float CloudErosion = 0.6f;

	/** Fine fraying of the clouds' edges near the view: 0 none .. 0.5 ragged. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "0.5"))
	float CloudDetail = 0.12f;

	/** Toon clouds' tones: 0 = smooth shading, 1 or more = three painted tones (lit, half tone, shade). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "8"))
	int32 CloudBands = 3;

	/**
	 * How far the air lets clouds be seen in clear weather, cm: a cloud this far off near the ground keeps 2% of
	 * its contrast against the sky behind it. The haze thins with height (CloudHazeHeight), so a tower's top stays
	 * clear long after its base has sunk into the horizon; an overcast's air sees about a third as far.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "100000", UIMax = "20000000"))
	float CloudVisibility = 5000000.0f;

	/** How high the hazy lower air reaches, cm: its density falls off exponentially over this height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "10000", UIMax = "500000"))
	float CloudHazeHeight = 120000.0f;

	/** Thin high cloud far above the cumulus (cirrus streaks along the wind): 0 none .. 1 most of the sky. Hidden under a closing overcast. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudHighCover = 0.35f;

	/** Altitude of the high cloud above the ground, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "300000", UIMax = "1500000"))
	float CloudHighAltitude = 900000.0f;

	/** Bright rim where a cloud edge is lit from behind (near the sun). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "2"))
	float CloudRim = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudOpacity = 1.0f;

	/** Drift speed, cloud shapes per real minute at the default day length. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "4"))
	float CloudSpeed = 0.4f;

	/**
	 * How fast the clouds grow and fall apart as they drift: at 1 a cloud builds and dissolves again over some 20
	 * game minutes (small ones sooner, the large ones later), 2 twice as fast, 0 keeps every shape as it is. Follows
	 * the world clock, so a given moment always shows the same clouds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", UIMax = "4"))
	float CloudFormationSpeed = 1.0f;

	/** Drift direction, degrees of yaw (until the wind drives it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	float CloudDirection = 30.0f;

	/** Altitude of the clouds' base (where rising air condenses), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "1000", UIMax = "500000"))
	float CloudLayerHeight = 60000.0f;

	/** How dark storm clouds (weather darkness) draw. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float StormCloudDarkening = 0.6f;

	/** The sky cloud renderer's realistic look (ADreamSky::CloudLook = Realistic). The level's cloud overrides go over it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds|Realistic", meta = (ShowOnlyInnerProperties))
	FDreamSkyRealisticClouds RealisticClouds;

	/** The sky cloud renderer's painted look (ADreamSky::CloudLook = Painted). The level's cloud overrides go over it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds|Painted", meta = (ShowOnlyInnerProperties))
	FDreamSkyPaintedClouds PaintedClouds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds|Toon", meta = (ShowOnlyInnerProperties))
	FDreamSkyToonClouds ToonClouds;

	/**
	 * How much of the sun the toon and painted clouds' shadows on the ground take between broken clouds (1 = all of it,
	 * as under the realistic ones; lower, they lie lighter and keep some of the sun's warmth, as in a painted
	 * background), rising to all of it as the cover closes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1"))
	float StylizedCloudShadow = 1.0f;

	/**
	 * The clouds' lit and shade colours as the style's own curves (CloudLitColor, CloudShadowColor) have them, instead of
	 * from the physical sky's light (DreamAtmosphere): the sun's and the moon's light through the air for the lit tone,
	 * the sky's ambient light for the shade, each times CloudLitScale / CloudShadeScale. (Without the atmosphere's
	 * tables the curves are used anyway.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds")
	bool bCloudColorsAbsolute = false;

	/** The physical colours kept as bright as the style's curves at every moment (only their colour from the light: a
	 *  low sun's red, the sky's blue in the shade), before the scales below. Off: the physical brightness, times them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute"))
	bool bCloudMatchStyleBrightness = true;

	/** The physical light on the toon clouds' lit side, times this, over the sun's elevation (white: as it comes, after
	 *  the steer toward the style's hue below). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Scale"))
	FRuntimeCurveLinearColor CloudLitScale;

	/** The sky's ambient light on the clouds' shade, times this, over the sun's elevation (set at noon to the style's shade). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Scale"))
	FRuntimeCurveLinearColor CloudShadeScale;

	/**
	 * How far the toon clouds' lit and shade colours take the hue of the style's own (CloudLitColor, CloudShadowColor)
	 * instead of the light's, over the sun's elevation, their brightness kept (0 the physical hue, 1 the style's): the
	 * style's blue-lavender shade and softer sunset come back as the sun goes down, whatever hue the light has then.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Style's hue"))
	FRuntimeFloatCurve CloudLitToStyle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Style's hue"))
	FRuntimeFloatCurve CloudShadeToStyle;
	/** The same for the painted look. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Style's hue"))
	FRuntimeFloatCurve PaintedCloudLitToStyle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Style's hue"))
	FRuntimeFloatCurve PaintedCloudShadeToStyle;

	/** The same two scales for the painted look (CloudLitScale and CloudShadeScale are the toon look's). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Scale"))
	FRuntimeCurveLinearColor PaintedCloudLitScale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (EditCondition = "!bCloudColorsAbsolute", XAxisName = "Sun elevation", YAxisName = "Scale"))
	FRuntimeCurveLinearColor PaintedCloudShadeScale;

	/**
	 * How far the realistic and painted clouds' shading toward the light reaches, cm: a cloud's shade toward the sun fades
	 * out by where its shadow would land this far off (from a quarter of it on), so a far tower's shadow does not black
	 * out the clouds beyond it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Clouds", meta = (ClampMin = "500000", UIMax = "20000000"))
	float CloudShadowReach = 6000000.0f;

	// ---- Scene lighting -----------------------------------------------------------------------

	/** Ambient gradient = the dome's zenith / horizon / ground colours times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient", meta = (XAxisName = "Sun elevation", YAxisName = "Scale"))
	FRuntimeFloatCurve AmbientScale;

	/** The least ambient light there is at night, so the darkest scene stays readable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient")
	FLinearColor NightAmbientFloor = FLinearColor(0.02f, 0.026f, 0.05f, 1.0f);

	/** Intensity of the sky light that captures the dome. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient", meta = (XAxisName = "Sun elevation", YAxisName = "Intensity"))
	FRuntimeFloatCurve SkyLightIntensity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (XAxisName = "Sun elevation", YAxisName = "Density"))
	FRuntimeFloatCurve FogDensity;

	/** Fog colour = the horizon colour times this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0", UIMax = "2"))
	float FogBrightness = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0.001", UIMax = "2"))
	float FogHeightFalloff = 0.2f;

	/** (Airborne dust takes it to 1 with the dust: a dust storm hides the far land altogether.) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog", meta = (ClampMin = "0", ClampMax = "1"))
	float FogMaxOpacity = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure")
	EDreamSkyExposureMode ExposureMode = EDreamSkyExposureMode::Manual;

	/** Exposure (EV100) through the day: fixed in Manual mode, the middle of the eye's range in Auto. Lower = brighter picture. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure", meta = (XAxisName = "Sun elevation", YAxisName = "EV100"))
	FRuntimeFloatCurve ExposureEV100;

	/** Auto mode: how far, EV, the eye may adapt either side of the curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure", meta = (ClampMin = "0", UIMax = "8"))
	float ExposureAutoRange = 2.0f;

	/** Auto mode: how fast the eye adapts to brighter light, EV per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure", meta = (ClampMin = "0.02", UIMax = "20"))
	float ExposureSpeedUp = 3.0f;

	/** Auto mode: how fast the eye adapts to darker light, EV per second. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Exposure", meta = (ClampMin = "0.02", UIMax = "20"))
	float ExposureSpeedDown = 1.0f;

	// ---- Weather ------------------------------------------------------------------------------

	/** How much a fully overcast sky dims the sun and the moon. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float OvercastLightDimming = 0.8f;

	/** How grey an overcast sky turns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float OvercastDesaturation = 0.6f;

	/** How much storm clouds darken the sky, the clouds and the ambient light. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float StormDarkening = 0.55f;

	/**
	 * Exposure lift (EV) under a heavy sky, so an overcast day is dim but not black. Storm-dark clouds take it
	 * back (none at full darkness): a storm is meant to look dark.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", UIMax = "3"))
	float OvercastExposureLift = 0.8f;

	/** Fog density multiplier at full weather fog (it grows with the square of the weather's fog). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "1", UIMax = "200"))
	float WeatherFogScale = 100.0f;

	/** How far weather fog turns the fog colour to a neutral grey-white (at full fog). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float FogNeutral = 0.7f;

	/** Exposure lift (EV) in thick fog, and in a dust storm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (UIMin = "-2", UIMax = "2"))
	float FogExposureLift = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (UIMin = "-2", UIMax = "2"))
	float DustExposureLift = 0.0f;

	/**
	 * How much of the dome weather fog hides, in the height fog's colour (the dome itself is past the fog's
	 * cutoff, so without this the sky stays clear above a fogged-in ground). Light fog hazes the horizon; at
	 * 1, full fog leaves only a trace of the sky overhead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float FogSkyHaze = 1.0f;

	/** The same for a dust storm: a full one leaves the clouds and the sun as faint shapes in the dust. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float DustSkyHaze = 1.0f;

	/** The colour of airborne dust: a dust storm tints the fog, the sky and the sunlight toward it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	FLinearColor DustColor = FLinearColor(0.78f, 0.6f, 0.4f, 1.0f);

	/** Fog density multiplier in a full dust storm (it grows with the square of the dust). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "1", UIMax = "200"))
	float DustFogScale = 40.0f;

	/** How much of the sun a full dust storm hides, 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float DustSunDimming = 0.55f;

	// ---- Indoors (the view probe) --------------------------------------------------------------

	/**
	 * Exposure lift (EV) indoors, so a room lit only by its windows is not black (the eye adapts too; small windows
	 * and weak bounced light need some 2.5).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (UIMin = "0", UIMax = "4"))
	float InteriorExposureLift = 2.5f;

	/**
	 * Sky light intensity indoors, as a multiplier: with Lumen the sky light is occluded, so only what the doors and
	 * windows let in brightens (the room's corners stay dark). Lower it toward 1 where the sky light has no occlusion of
	 * its own: there it brightens the whole room alike.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0", UIMax = "4"))
	float InteriorSkyLightScale = 2.5f;

	/**
	 * A weak fill indoors, for projects without Lumen (no light bounces in through the doors and windows there): an
	 * ambient that ignores occlusion, the sky light raised by InteriorFill of its outdoor strength and the shared
	 * parameters' ambient gradient with it, as far as the view is indoors. Off by default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior")
	bool bInteriorFill = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0", UIMax = "2", EditCondition = "bInteriorFill"))
	float InteriorFill = 0.3f;

	/** Sun and moon light indoors, as a multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0", UIMax = "2"))
	float InteriorDirectionalScale = 1.0f;

	/**
	 * How much further from the view the height fog starts indoors, cm: the room's own air is clear (a rain's or a fog
	 * bank's thickened fog would otherwise fill it, and the indoor exposure lift brightens it to a grey haze), the land
	 * outside the doors and windows keeps its fog. Some 15 m clears an ordinary room.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interior", meta = (ClampMin = "0", UIMax = "5000"))
	float InteriorFogStart = 1500.0f;

	// ---- Behaviour ----------------------------------------------------------------------------

	/** Seconds the light takes to follow a change while the clock runs (a toon ramp otherwise flickers across its threshold). Jumps snap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0", UIMax = "5"))
	float LightResponseTime = 0.4f;

	/** The directional lights turn only when the sun or moon moved this many degrees (moving a light invalidates cached shadows). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0", UIMax = "2"))
	float LightRotationThreshold = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	bool bMoonCastsShadows = true;

	/** Day-phase thresholds, degrees of sun elevation: day is full above DayBottom and gone below TwilightTop... */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float DayBottom = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float TwilightTop = -2.0f;

	/** ...night starts below NightTop and is full below NightBottom. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float NightTop = -4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	float NightBottom = -12.0f;
};

namespace DreamSky
{
	/**
	 * How far fair-weather cumulus have grown by this hour (0..24), 0..1: they build through the late morning, peak
	 * in the mid-afternoon and fall apart toward evening; a few hang on through the night.
	 */
	DREAMSKY_API float CumulusGrowth(float Hours);

	/**
	 * How far the clouds have changed their shapes by this moment of the world clock (game seconds) at a formation
	 * speed (UDreamSkyStyle::CloudFormationSpeed), as a phase 0..1 through the noise the shapes are cut from: it rises
	 * steadily with the clock and wraps where the noise repeats, so the clouds change smoothly and never jump.
	 */
	DREAMSKY_API float CloudFormation(double WorldSeconds, float Speed);

	/** How far CloudFormation moves per game hour at a speed of 1 (a cloud grows and dissolves within about an eighth of it). */
	inline constexpr double CloudFormationPerHour = 0.375;
}
