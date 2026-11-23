// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyModifier.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkyModifier)

namespace DreamSky::Modifier
{
	float Luminance(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	/** C with its saturation scaled (0 = its grey), alpha kept. */
	FLinearColor Saturate(const FLinearColor& C, float Saturation)
	{
		const float Grey = Luminance(C);
		const FLinearColor Out = FMath::Lerp(FLinearColor(Grey, Grey, Grey, C.A), C, Saturation);
		return FLinearColor(FMath::Max(Out.R, 0.0f), FMath::Max(Out.G, 0.0f), FMath::Max(Out.B, 0.0f), C.A);
	}

	/** C times Tint (rgb), alpha kept. */
	FLinearColor Tinted(const FLinearColor& C, const FLinearColor& Tint)
	{
		return FLinearColor(C.R * Tint.R, C.G * Tint.G, C.B * Tint.B, C.A);
	}
}

void FDreamSkyModifierSettings::Apply(FDreamSkyLighting& L, FDreamSkyLook& K, float Alpha) const
{
	using namespace DreamSky::Modifier;
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (Alpha <= 0.0f)
	{
		return;
	}
	// A scale goes the share of the way along its logarithm (half of a doubling is the square root of two), so stacked
	// modifiers and their fades combine evenly whichever way they go.
	auto Scale = [Alpha](float Value, float By) { return Value * FMath::Pow(FMath::Max(By, 1.0e-4f), Alpha); };
	auto TintBy = [Alpha](const FLinearColor& C, const FLinearColor& Tint) { return FMath::Lerp(C, Tinted(C, Tint), Alpha); };
	auto Toward = [Alpha](float Value, float Target) { return FMath::Lerp(Value, Target, Alpha); };

	if (bOverride_SunIntensity) { L.SunIlluminance = Scale(L.SunIlluminance, SunIntensity); }
	if (bOverride_SunTint) { L.SunColor = TintBy(L.SunColor, SunTint); }
	if (bOverride_MoonIntensity) { L.MoonIlluminance = Scale(L.MoonIlluminance, MoonIntensity); }
	if (bOverride_MoonTint) { L.MoonColor = TintBy(L.MoonColor, MoonTint); }
	if (bOverride_SunIntensity || bOverride_SunTint || bOverride_MoonIntensity || bOverride_MoonTint)
	{
		L.MainLightColor = L.bMainLightIsMoon ? L.MoonColor : L.SunColor;
		L.MainLightIlluminance = L.bMainLightIsMoon ? L.MoonIlluminance : L.SunIlluminance;
	}
	if (bOverride_SunDiskBrightness)
	{
		const float By = FMath::Pow(FMath::Max(SunDiskBrightness, 1.0e-4f), Alpha);
		K.SunDisk = Tinted(K.SunDisk, FLinearColor(By, By, By));
		K.SunHalo = Tinted(K.SunHalo, FLinearColor(By, By, By));
	}
	if (bOverride_MoonDiskBrightness)
	{
		const float By = FMath::Pow(FMath::Max(MoonDiskBrightness, 1.0e-4f), Alpha);
		K.MoonDisk = Tinted(K.MoonDisk, FLinearColor(By, By, By));
		K.MoonHalo = Tinted(K.MoonHalo, FLinearColor(By, By, By));
	}

	if (bOverride_SkyLightIntensity) { L.SkyLightIntensity = Scale(L.SkyLightIntensity, SkyLightIntensity); }
	if (bOverride_AmbientTint)
	{
		L.AmbientSky = TintBy(L.AmbientSky, AmbientTint);
		L.AmbientEquator = TintBy(L.AmbientEquator, AmbientTint);
		L.AmbientGround = TintBy(L.AmbientGround, AmbientTint);
	}
	// A brighter picture is a lower exposure value.
	if (bOverride_ExposureCompensation) { L.ExposureEV100 -= ExposureCompensation * Alpha; }

	if (bOverride_FogDensity) { L.FogDensity = Scale(L.FogDensity, FogDensity); }
	if (bOverride_FogColor)
	{
		const float Have = Luminance(L.FogColor);
		const float Want = FMath::Max(Luminance(FogColor), 1.0e-4f);
		const FLinearColor Target = FogColor * (Have / Want);
		L.FogColor = FMath::Lerp(L.FogColor, FLinearColor(Target.R, Target.G, Target.B, L.FogColor.A), Alpha);
	}
	if (bOverride_Haze) { K.Haze.A = Toward(K.Haze.A, FMath::Clamp(Haze, 0.0f, 1.0f)); }

	if (bOverride_SkyTint)
	{
		K.Zenith = TintBy(K.Zenith, SkyTint);
		K.Horizon = TintBy(K.Horizon, SkyTint);
		K.Ground = TintBy(K.Ground, SkyTint);
		K.SunHalo = TintBy(K.SunHalo, SkyTint);
	}
	if (bOverride_SkySaturation)
	{
		const float S = FMath::Lerp(1.0f, SkySaturation, Alpha);
		K.Zenith = Saturate(K.Zenith, S);
		K.Horizon = Saturate(K.Horizon, S);
		K.Ground = Saturate(K.Ground, S);
		K.SunHalo = Saturate(K.SunHalo, S);
		K.CloudLit = Saturate(K.CloudLit, S);
		K.CloudShadow = Saturate(K.CloudShadow, S);
	}
	if (bOverride_StarIntensity) { K.StarIntensity = Scale(K.StarIntensity, StarIntensity); }
	if (bOverride_Aurora) { K.Aurora = Toward(K.Aurora, FMath::Clamp(Aurora, 0.0f, 1.0f)); }
	if (bOverride_Meteors) { K.Meteors = Scale(K.Meteors, FMath::Max(Meteors, 0.0f)); }
	if (bOverride_Noctilucent) { K.Noctilucent = Toward(K.Noctilucent, FMath::Clamp(Noctilucent, 0.0f, 1.0f)); }
	if (bOverride_LightPollution) { K.LightPollution = Toward(K.LightPollution, FMath::Max(LightPollution, 0.0f)); }
	if (bOverride_Airglow) { K.AirglowScale = Scale(K.AirglowScale, Airglow); }
	if (bOverride_LightShafts) { K.LightShafts = Scale(K.LightShafts, LightShafts); }
	if (bOverride_LensFlare) { K.LensFlare = Scale(K.LensFlare, LensFlare); }
	if (bOverride_MilkyWay) { K.MilkyWay = Scale(K.MilkyWay, MilkyWay); }
	if (bOverride_PaintedClouds && Alpha > 0.0f)
	{
		K.Painted = FDreamSkyPaintedClouds::Lerp(K.Painted, PaintedClouds, Alpha);
	}
	if (bOverride_ToonClouds)
	{
		FDreamSkyToonBaked Theirs;
		ToonClouds.Bake(Theirs);
		K.Toon.BlendToward(Theirs, Alpha);
	}
	if (bOverride_SolarEclipse) { K.SolarEclipse = Toward(K.SolarEclipse, FMath::Clamp(SolarEclipse, 0.0f, 1.0f)); }
	if (bOverride_LunarEclipse) { K.LunarEclipse = Toward(K.LunarEclipse, FMath::Clamp(LunarEclipse, 0.0f, 1.0f)); }

	// (And the same on whatever the weather map holds: the stack's change to it, kept and added.)
	if (bOverride_CloudCoverage)
	{
		const float Target = FMath::Clamp(CloudCoverage, 0.0f, 1.0f);
		K.CloudCoverage = Toward(K.CloudCoverage, Target);
		K.CloudCoverageKept *= 1.0f - Alpha;
		K.CloudCoverageAdded = Toward(K.CloudCoverageAdded, Target);
	}
	if (bOverride_HighCloudCover) { K.HighCover = Toward(K.HighCover, FMath::Clamp(HighCloudCover, 0.0f, 1.0f)); }
	if (bOverride_CloudDarkness)
	{
		const float Target = FMath::Clamp(CloudDarkness, 0.0f, 1.0f);
		K.CloudDarkness = Toward(K.CloudDarkness, Target);
		K.CloudDarknessKept *= 1.0f - Alpha;
		K.CloudDarknessAdded = Toward(K.CloudDarknessAdded, Target);
	}
	if (bOverride_CloudLitTint) { K.CloudLit = TintBy(K.CloudLit, CloudLitTint); }
	if (bOverride_CloudShadowTint) { K.CloudShadow = TintBy(K.CloudShadow, CloudShadowTint); }
}

float UDreamSkyModifier::Shape(float Progress) const
{
	Progress = FMath::Clamp(Progress, 0.0f, 1.0f);
	return FMath::Clamp(UKismetMathLibrary::Ease(0.0f, 1.0f, Progress, Easing, EasingExponent), 0.0f, 1.0f) * FMath::Clamp(MaxAlpha, 0.0f, 1.0f);
}
