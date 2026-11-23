// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyStyle.h"
#include "DreamWorldCelestial.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkyStyle)

namespace DreamSky::Style
{
	struct FColorKey
	{
		float Elevation;
		FLinearColor Color;
	};

	struct FFloatKey
	{
		float Elevation;
		float Value;
	};

	static void SetKeys(FRuntimeCurveLinearColor& Curve, std::initializer_list<FColorKey> Keys)
	{
		for (FRichCurve& Channel : Curve.ColorCurves)
		{
			Channel.Reset();
		}
		for (const FColorKey& K : Keys)
		{
			const float Values[4] = { K.Color.R, K.Color.G, K.Color.B, K.Color.A };
			for (int32 c = 0; c < 4; ++c)
			{
				const FKeyHandle Handle = Curve.ColorCurves[c].AddKey(K.Elevation, Values[c]);
				Curve.ColorCurves[c].SetKeyInterpMode(Handle, RCIM_Linear);
			}
		}
	}

	static void SetKeys(FRuntimeFloatCurve& Curve, std::initializer_list<FFloatKey> Keys)
	{
		FRichCurve& Rich = *Curve.GetRichCurve();
		Rich.Reset();
		for (const FFloatKey& K : Keys)
		{
			const FKeyHandle Handle = Rich.AddKey(K.Elevation, K.Value);
			Rich.SetKeyInterpMode(Handle, RCIM_Linear);
		}
	}

	static FLinearColor Eval(const FRuntimeCurveLinearColor& Curve, float X)
	{
		FLinearColor C = Curve.GetLinearColorValue(X);
		C.A = 1.0f;
		return C;
	}

	static float Eval(const FRuntimeFloatCurve& Curve, float X, float Default = 0.0f)
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->Eval(X) : Default;
	}

	static FLinearColor C(float R, float G, float B) { return FLinearColor(R, G, B, 1.0f); }
}

UDreamSkyStyle::UDreamSkyStyle()
{
	using namespace DreamSky::Style;

	SetKeys(ZenithColor, {
		{ -20.0f, C(0.006f, 0.009f, 0.025f) }, { -10.0f, C(0.010f, 0.015f, 0.040f) }, { -6.0f, C(0.020f, 0.028f, 0.070f) },
		{ -3.0f, C(0.045f, 0.055f, 0.130f) }, { 0.0f, C(0.100f, 0.130f, 0.260f) }, { 3.0f, C(0.150f, 0.220f, 0.420f) },
		{ 8.0f, C(0.180f, 0.320f, 0.600f) }, { 15.0f, C(0.200f, 0.380f, 0.700f) }, { 60.0f, C(0.180f, 0.380f, 0.740f) } });
	SetKeys(HorizonColor, {
		{ -20.0f, C(0.008f, 0.012f, 0.030f) }, { -10.0f, C(0.020f, 0.025f, 0.055f) }, { -6.0f, C(0.060f, 0.050f, 0.100f) },
		{ -3.0f, C(0.220f, 0.120f, 0.160f) }, { 0.0f, C(0.700f, 0.360f, 0.220f) }, { 3.0f, C(0.850f, 0.520f, 0.320f) },
		{ 8.0f, C(0.780f, 0.660f, 0.550f) }, { 15.0f, C(0.620f, 0.720f, 0.840f) }, { 60.0f, C(0.560f, 0.700f, 0.860f) } });
	SetKeys(GroundColor, {
		{ -12.0f, C(0.010f, 0.012f, 0.020f) }, { -3.0f, C(0.060f, 0.045f, 0.060f) }, { 3.0f, C(0.200f, 0.150f, 0.140f) },
		{ 15.0f, C(0.300f, 0.320f, 0.340f) } });

	SetKeys(SunDiskColor, {
		{ -2.0f, C(0.0f, 0.0f, 0.0f) }, { 0.0f, C(8.0f, 4.0f, 1.5f) }, { 5.0f, C(14.0f, 10.0f, 6.0f) }, { 20.0f, C(20.0f, 19.0f, 16.0f) } });
	SetKeys(SunHaloColor, {
		{ -6.0f, C(0.0f, 0.0f, 0.0f) }, { -2.0f, C(0.50f, 0.18f, 0.08f) }, { 2.0f, C(1.40f, 0.65f, 0.25f) },
		{ 8.0f, C(1.20f, 0.85f, 0.50f) }, { 20.0f, C(0.90f, 0.85f, 0.70f) }, { 60.0f, C(0.70f, 0.70f, 0.65f) } });
	SetKeys(SunLightColor, {
		{ -2.0f, C(1.0f, 0.45f, 0.20f) }, { 3.0f, C(1.0f, 0.62f, 0.38f) }, { 10.0f, C(1.0f, 0.85f, 0.70f) },
		{ 25.0f, C(1.0f, 0.95f, 0.88f) }, { 60.0f, C(1.0f, 0.98f, 0.95f) } });
	SetKeys(SunIlluminance, {
		{ -2.0f, 0.0f }, { 0.0f, 0.8f }, { 4.0f, 3.0f }, { 10.0f, 6.0f }, { 25.0f, 9.0f }, { 60.0f, 10.0f } });

	SetKeys(MoonDiskColor, { { -6.0f, C(1.20f, 1.25f, 1.35f) }, { 6.0f, C(0.55f, 0.58f, 0.64f) } });
	SetKeys(MoonHaloColor, { { -10.0f, C(0.05f, 0.07f, 0.12f) }, { -2.0f, C(0.0f, 0.0f, 0.0f) } });
	SetKeys(MoonLightColor, { { -90.0f, C(0.55f, 0.65f, 1.0f) }, { 0.0f, C(0.60f, 0.65f, 0.95f) } });
	SetKeys(MoonIlluminance, { { -12.0f, 1.2f }, { -6.0f, 0.5f }, { -2.0f, 0.0f } });
	SetKeys(StarIntensity, { { -12.0f, 1.0f }, { -6.0f, 0.3f }, { -2.0f, 0.0f } });

	// Sunlit cloud tops are about as bright as a white surface in the sun; their shaded sides take the sky's blue.
	SetKeys(CloudLitColor, {
		{ -12.0f, C(0.060f, 0.070f, 0.110f) }, { -4.0f, C(0.320f, 0.230f, 0.280f) }, { 0.0f, C(1.500f, 0.800f, 0.500f) },
		{ 5.0f, C(1.800f, 1.400f, 1.080f) }, { 15.0f, C(2.000f, 1.950f, 1.880f) }, { 60.0f, C(2.200f, 2.200f, 2.150f) } });
	// Painted cloud shade is a pale sky-tinted blue-grey by day (the shade about 0.85 of the lit side's brightness
	// on screen), a lavender blue toward sunset (about half): the complement of the warm light, never a neutral grey.
	SetKeys(CloudShadowColor, {
		{ -12.0f, C(0.030f, 0.036f, 0.065f) }, { -4.0f, C(0.090f, 0.075f, 0.140f) }, { 0.0f, C(0.300f, 0.230f, 0.420f) },
		{ 5.0f, C(0.560f, 0.520f, 0.780f) }, { 10.0f, C(0.950f, 0.960f, 1.250f) }, { 15.0f, C(1.300f, 1.420f, 1.660f) },
		{ 60.0f, C(1.400f, 1.520f, 1.780f) } });

	// The physical light on the clouds (the style's brightness matched, bCloudMatchStyleBrightness), its hue steered toward
	// the style's own colours as the sun goes down -- whatever hue the light has then (a warm dusk's sky, the night's blue
	// floor): the shade to the style's blue-lavender (the complement of the warm light) from 20 degrees down, all of the way
	// by 5 and through the night; the lit side part of the way back to the style's softer peach at sunset (the toon look
	// 0.45 of it, the painted 0.65), still warming with the light, and to its moonlit grey-blue at night. Noon as it is.
	SetKeys(CloudLitToStyle, { { -12.0f, 0.6f }, { -4.0f, 0.5f }, { 0.0f, 0.45f }, { 5.0f, 0.4f }, { 12.0f, 0.25f }, { 20.0f, 0.0f }, { 60.0f, 0.0f } });
	SetKeys(CloudShadeToStyle, { { -12.0f, 1.0f }, { 5.0f, 1.0f }, { 10.0f, 0.85f }, { 15.0f, 0.5f }, { 20.0f, 0.0f }, { 60.0f, 0.0f } });
	SetKeys(PaintedCloudLitToStyle, { { -12.0f, 0.75f }, { -4.0f, 0.7f }, { 0.0f, 0.65f }, { 5.0f, 0.6f }, { 12.0f, 0.35f }, { 20.0f, 0.0f }, { 60.0f, 0.0f } });
	SetKeys(PaintedCloudShadeToStyle, { { -12.0f, 1.0f }, { 5.0f, 1.0f }, { 10.0f, 0.85f }, { 15.0f, 0.5f }, { 20.0f, 0.0f }, { 60.0f, 0.0f } });
	SetKeys(CloudLitScale, { { -12.0f, C(1.0f, 1.0f, 1.0f) }, { 60.0f, C(1.0f, 1.0f, 1.0f) } });
	SetKeys(CloudShadeScale, { { -12.0f, C(1.0f, 1.0f, 1.0f) }, { 60.0f, C(1.0f, 1.0f, 1.0f) } });
	SetKeys(PaintedCloudLitScale, { { -12.0f, C(1.0f, 1.0f, 1.0f) }, { 60.0f, C(1.0f, 1.0f, 1.0f) } });
	SetKeys(PaintedCloudShadeScale, { { -12.0f, C(1.0f, 1.0f, 1.0f) }, { 60.0f, C(1.0f, 1.0f, 1.0f) } });
	SetKeys(AmbientScale, { { -12.0f, 1.0f }, { 60.0f, 1.0f } });
	SetKeys(SkyLightIntensity, { { -12.0f, 1.0f }, { 0.0f, 0.9f }, { 60.0f, 1.0f } });
	SetKeys(FogDensity, { { -10.0f, 0.020f }, { 0.0f, 0.025f }, { 5.0f, 0.020f }, { 60.0f, 0.015f } });
	// A white surface in full sun sits near 0.6 and the zenith near 0.17 (linear) at noon; night stays readable.
	SetKeys(ExposureEV100, { { -10.0f, 0.6f }, { -4.0f, 0.7f }, { 0.0f, 0.85f }, { 3.0f, 1.25f }, { 10.0f, 1.8f }, { 60.0f, 2.1f } });
}

FDreamSkyLook UDreamSkyStyle::EvaluateLook(const FDreamCelestialState& Celestial) const
{
	using namespace DreamSky::Style;
	const float E = Celestial.SunElevation;
	FDreamSkyLook Look;
	Look.Zenith = Eval(ZenithColor, E);
	Look.Horizon = Eval(HorizonColor, E);
	Look.Ground = Eval(GroundColor, E);
	Look.SunHalo = Eval(SunHaloColor, E);
	Look.SunDisk = Eval(SunDiskColor, E);
	Look.MoonDisk = Eval(MoonDiskColor, E);
	Look.MoonHalo = Eval(MoonHaloColor, E) * FMath::Lerp(1.0f, Celestial.MoonIllumination, MoonPhaseDimming);
	Look.CloudLit = Eval(CloudLitColor, E);
	Look.CloudShadow = Eval(CloudShadowColor, E);
	Look.StarIntensity = FMath::Max(0.0f, Eval(StarIntensity, E));
	Look.CloudCoverage = CloudCoverage;
	Look.CloudDarkness = 0.0f;
	Look.HighCover = FMath::Clamp(CloudHighCover, 0.0f, 1.0f);
	Look.Meteors = 1.0f;
	Look.LightPollution = FMath::Max(NightGlow.LightPollution, 0.0f);
	Look.LightShafts = FMath::Max(LightShafts.Intensity, 0.0f);
	Look.HorizonExponent = HorizonExponent;
	Look.LensFlare = FMath::Max(LensFlare.Intensity, 0.0f);
	Look.MilkyWay = MilkyWay.bEnabled ? FMath::Max(MilkyWay.Intensity, 0.0f) : 0.0f;
	ToonClouds.Bake(Look.Toon);
	Look.Painted = PaintedClouds;
	return Look;
}

float UDreamSkyStyle::EvaluateAuroraPresence(const FDreamCelestialSettings& Settings) const
{
	if (Settings.Mode != EDreamCelestialMode::Realistic)
	{
		return FMath::Clamp(Aurora.ArtisticPresence, 0.0f, 1.0f);
	}
	const float Latitude = FMath::Abs(float(Settings.Latitude));
	const float From = FMath::Min(Aurora.StartLatitude, Aurora.FullLatitude);
	const float To = FMath::Max(Aurora.StartLatitude, Aurora.FullLatitude);
	return To > From ? FMath::SmoothStep(From, To, Latitude) : (Latitude >= To ? 1.0f : 0.0f);
}

FDreamSkyMeteors::FDreamSkyMeteors()
{
	// The seven strongest yearly showers (the International Meteor Organization's working list).
	auto Add = [this](const TCHAR* Name, int32 Month, int32 Day, float Width, float Ra, float Dec, float Zhr)
	{
		FDreamSkyMeteorShower& S = Showers.AddDefaulted_GetRef();
		S.Name = FName(Name);
		S.PeakMonth = Month;
		S.PeakDay = Day;
		S.WidthDays = Width;
		S.RightAscension = Ra;
		S.Declination = Dec;
		S.ZenithalHourlyRate = Zhr;
	};
	Add(TEXT("Quadrantids"), 1, 4, 0.6f, 230.0f, 49.0f, 110.0f);
	Add(TEXT("Lyrids"), 4, 22, 1.5f, 272.0f, 33.0f, 18.0f);
	Add(TEXT("EtaAquariids"), 5, 6, 4.0f, 338.0f, -1.0f, 50.0f);
	Add(TEXT("Perseids"), 8, 12, 2.5f, 48.0f, 58.0f, 100.0f);
	Add(TEXT("Orionids"), 10, 21, 3.0f, 95.0f, 16.0f, 20.0f);
	Add(TEXT("Leonids"), 11, 17, 1.0f, 152.0f, 22.0f, 15.0f);
	Add(TEXT("Geminids"), 12, 14, 1.5f, 112.0f, 33.0f, 150.0f);
}

FDreamSkyShowerActivity UDreamSkyStyle::EvaluateMeteorShower(const FDateTime& Date) const
{
	FDreamSkyShowerActivity Best;
	const double Day = double(Date.GetDayOfYear()) + Date.GetTimeOfDay().GetTotalHours() / 24.0;
	for (const FDreamSkyMeteorShower& S : Meteors.Showers)
	{
		const int32 Month = FMath::Clamp(S.PeakMonth, 1, 12);
		const int32 PeakDay = FMath::Clamp(S.PeakDay, 1, FDateTime::DaysInMonth(Date.GetYear(), Month));
		double Days = FMath::Abs(Day - double(FDateTime(Date.GetYear(), Month, PeakDay).GetDayOfYear()));
		Days = FMath::Min(Days, 365.25 - Days);
		const float Rate = S.ZenithalHourlyRate * float(FMath::Exp(-0.5 * FMath::Square(Days / FMath::Max(double(S.WidthDays), 0.1))));
		if (Rate > Best.ZenithalHourlyRate && Rate > 1.0f)
		{
			Best.Name = S.Name;
			Best.ZenithalHourlyRate = Rate;
			Best.RightAscension = S.RightAscension;
			Best.Declination = S.Declination;
		}
	}
	return Best;
}

float UDreamSkyStyle::EvaluateNoctilucentPresence(const FDreamCelestialSettings& Settings, const FDateTime& Date) const
{
	if (!Noctilucent.bEnabled)
	{
		return 0.0f;
	}
	if (Settings.Mode != EDreamCelestialMode::Realistic)
	{
		return FMath::Clamp(Noctilucent.ArtisticPresence, 0.0f, 1.0f);
	}
	const FDreamSkyNoctilucent& N = Noctilucent;
	const float Latitude = FMath::Abs(float(Settings.Latitude));
	const float Band = FMath::SmoothStep(N.StartLatitude, FMath::Max(N.FullLatitude, N.StartLatitude + 0.01f), Latitude)
		* (1.0f - FMath::SmoothStep(FMath::Max(N.EndLatitude - 5.0f, N.FullLatitude), FMath::Max(N.EndLatitude, N.FullLatitude + 0.01f), Latitude));
	// (Days from the hemisphere's midsummer, round the year.)
	const float Midsummer = Settings.Latitude >= 0.0 ? 172.0f : 355.0f;
	float Days = FMath::Abs(float(Date.GetDayOfYear()) - Midsummer);
	Days = FMath::Min(Days, 365.0f - Days);
	const float Season = 1.0f - FMath::SmoothStep(N.SeasonFullDays, FMath::Max(N.SeasonEndDays, N.SeasonFullDays + 1.0f), Days);
	return Band * Season;
}

float UDreamSkyStyle::NoctilucentTwilight(float SunElevation)
{
	return FMath::SmoothStep(-17.0f, -13.0f, SunElevation) * (1.0f - FMath::SmoothStep(-7.0f, -4.0f, SunElevation));
}

float UDreamSkyStyle::GlowNight(float SunElevation)
{
	// (A city's lights come on as the sun sets and stand out by the end of the civil twilight.)
	return 1.0f - FMath::SmoothStep(-6.0f, 2.0f, SunElevation);
}

FVector2f UDreamSkyStyle::EvaluateHaloPresence(float HighCover) const
{
	if (!Halos.bEnabled)
	{
		return FVector2f::ZeroVector;
	}
	auto Over = [HighCover](float From, float Full) { return FMath::SmoothStep(From, FMath::Max(Full, From + 1.0e-3f), HighCover); };
	return FVector2f(Over(Halos.HighCoverFrom, Halos.HighCoverFull), Over(Halos.CoronaCoverFrom, Halos.CoronaCoverFull));
}

float UDreamSkyStyle::SunDogAzimuth(float LightElevation, float RefractiveIndex)
{
	// (Plate crystals floating flat turn a light this high as a 60 degree prism of the effective index
	// sqrt(n^2 - sin^2 h) / cos h: the least deviation 2 asin(n' / 2) - 60, in azimuth.)
	const float H = FMath::DegreesToRadians(FMath::Clamp(LightElevation, -10.0f, 89.0f));
	const float Effective = FMath::Sqrt(FMath::Max(RefractiveIndex * RefractiveIndex - FMath::Square(FMath::Sin(H)), 0.0f)) / FMath::Cos(H);
	if (Effective * 0.5f >= 1.0f)
	{
		return -1.0f;
	}
	return FMath::RadiansToDegrees(2.0f * FMath::Asin(Effective * 0.5f)) - 60.0f;
}

float UDreamSkyStyle::AuroraNight(float SunElevation)
{
	// (It shows once the sky is dark enough: from the end of the civil twilight, in full by the nautical's end.)
	return 1.0f - FMath::SmoothStep(-14.0f, -6.0f, SunElevation);
}

void UDreamSkyStyle::ApplyFlash(const FDreamWorldEnvironment& Env, FDreamSkyLighting& L) const
{
	// A flash that reads at night is lost in daylight: brighten it with the exposure (part of the way).
	L.FlashGain = FMath::Pow(2.0f, FMath::Max(0.0f, L.ExposureEV100 - LightningReferenceEV) * FMath::Clamp(LightningExposureCompensation, 0.0f, 1.0f));
	L.LightningIlluminance = 0.0f;
	L.LightningColor = FLinearColor(LightningColor.R, LightningColor.G, LightningColor.B, 1.0f);
	const float F = Env.bActive ? FMath::Clamp(Env.LightningFlash, 0.0f, 1.0f) : 0.0f;
	if (F <= 0.0f)
	{
		return;
	}
	const float Strength = F * L.FlashGain;
	const FLinearColor Flash = L.LightningColor * (Strength * LightningAmbient);
	L.AmbientSky += Flash;
	L.AmbientEquator += Flash * 0.7f;
	L.AmbientGround += Flash * 0.3f;
	L.SkyLightIntensity *= 1.0f + 2.0f * F;
	L.LightningIlluminance = Strength * LightningLightIlluminance;
	L.LightningDirection = Env.LightningDirection.IsNearlyZero() ? FVector::UpVector : Env.LightningDirection.GetSafeNormal();
}

void UDreamSkyStyle::ApplyInterior(const FDreamViewOcclusion& Occlusion, FDreamSkyLighting& L) const
{
	const float In = Occlusion.bValid ? FMath::Clamp(Occlusion.Interior, 0.0f, 1.0f) : 0.0f;
	if (In <= 0.0f)
	{
		return;
	}
	L.ExposureEV100 -= InteriorExposureLift * In;
	const float OutdoorSkyLight = L.SkyLightIntensity;
	L.SkyLightIntensity *= FMath::Lerp(1.0f, InteriorSkyLightScale, In);
	if (bInteriorFill && InteriorFill > 0.0f)
	{
		const float Fill = InteriorFill * In;
		L.SkyLightIntensity += Fill * OutdoorSkyLight;
		L.AmbientSky *= 1.0f + Fill;
		L.AmbientEquator *= 1.0f + Fill;
		L.AmbientGround *= 1.0f + Fill;
	}
	const float Directional = FMath::Lerp(1.0f, InteriorDirectionalScale, In);
	L.SunIlluminance *= Directional;
	L.MoonIlluminance *= Directional;
	L.MainLightIlluminance *= Directional;
}

void UDreamSkyStyle::ApplyWeather(const FDreamWorldEnvironment& Env, FDreamSkyLighting& L, FDreamSkyLook& Look, bool bCloudsShadowTheSun) const
{
	if (!Env.bActive)
	{
		return;
	}
	const float Coverage = FMath::Clamp(Env.CloudCoverage, 0.0f, 1.0f);
	const float Dark = FMath::Clamp(Env.CloudDarkness, 0.0f, 1.0f);
	// The sun hides once clouds close up; rain thickens them further.
	const float Overcast = FMath::Max(FMath::SmoothStep(0.45f, 0.95f, Coverage), 0.6f * FMath::Clamp(Env.Precipitation, 0.0f, 1.0f));
	const float Darken = 1.0f - StormDarkening * Dark;
	const float Desaturate = OvercastDesaturation * Overcast;

	auto Grey = [Desaturate, Darken](const FLinearColor& C)
	{
		const float Luma = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
		FLinearColor Out = FMath::Lerp(C, FLinearColor(Luma, Luma, Luma, 1.0f) * 1.05f, Desaturate) * Darken;
		Out.A = 1.0f;
		return Out;
	};

	Look.CloudCoverage = Coverage;
	Look.CloudDarkness = Dark;
	Look.Zenith = Grey(Look.Zenith);
	Look.Horizon = Grey(Look.Horizon);
	Look.Ground = Grey(Look.Ground);
	Look.SunHalo = Look.SunHalo * (1.0f - Overcast);
	Look.SunDisk = Look.SunDisk * (1.0f - Overcast);
	Look.MoonHalo = Look.MoonHalo * (1.0f - Overcast);
	Look.StarIntensity *= 1.0f - Overcast;
	// Clouds only turn grey here; how dark they are is drawn per direction from the weather map (StormCloudDarkening).
	auto GreyOnly = [Desaturate](const FLinearColor& C)
	{
		const float Luma = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
		FLinearColor Out = FMath::Lerp(C, FLinearColor(Luma, Luma, Luma, 1.0f) * 1.05f, Desaturate);
		Out.A = 1.0f;
		return Out;
	};
	Look.CloudLit = GreyOnly(Look.CloudLit);
	Look.CloudShadow = GreyOnly(Look.CloudShadow);

	// Clouds that cast their own shadow (the engine's volumetric clouds) already hide the sun where they are, and are
	// lit by it: dimming it for the cover as well would darken an overcast day twice, the clouds' own tops with it.
	const float LightScale = 1.0f - OvercastLightDimming * Overcast;
	const float SunScale = bCloudsShadowTheSun ? 1.0f : LightScale;
	L.SunIlluminance *= SunScale;
	L.MoonIlluminance *= LightScale;
	L.MainLightIlluminance *= L.bMainLightIsMoon ? LightScale : SunScale;
	// (What the clouds' shadow does not reach still takes the overcast's dimming.)
	L.SunOvercastScale *= bCloudsShadowTheSun ? LightScale : 1.0f;
	L.AmbientSky = Grey(L.AmbientSky);
	L.AmbientEquator = Grey(L.AmbientEquator);
	L.AmbientGround = Grey(L.AmbientGround);
	L.FogColor = Grey(L.FogColor);
	// Squared: a little weather fog (rain's) only thickens the air a little; a fog bank swallows the scene.
	const float Fog = FMath::Clamp(Env.Fog, 0.0f, 1.0f);
	// A fog bank is a neutral grey-white, whatever tint the clear air's haze had.
	const float FogLuma = 0.2126f * L.FogColor.R + 0.7152f * L.FogColor.G + 0.0722f * L.FogColor.B;
	L.FogColor = FMath::Lerp(L.FogColor, FLinearColor(FogLuma, FogLuma, FogLuma, 1.0f) * 1.1f, FogNeutral * Fog);
	L.FogColor.A = 1.0f;
	L.FogDensity *= FMath::Lerp(1.0f, WeatherFogScale, Fog * Fog) * (1.0f + 0.5f * FMath::Clamp(Env.Precipitation, 0.0f, 1.0f));

	// Dust: the air turns a sandy ochre and thickens; the sky and the sunlight take on its colour, the sun dims.
	const float Dust = FMath::Clamp(Env.Dust, 0.0f, 1.0f);
	if (Dust > 0.0f)
	{
		auto Luma = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
		const FLinearColor Sand = DustColor / FMath::Max(Luma(DustColor), 1.0e-3f);   // the dust's hue at unit brightness
		auto Tint = [&](const FLinearColor& C, float Amount)
		{
			FLinearColor Out = FMath::Lerp(C, Sand * Luma(C), Amount);
			Out.A = C.A;
			return Out;
		};
		L.FogColor = Tint(L.FogColor, Dust);
		L.FogDensity *= FMath::Lerp(1.0f, DustFogScale, Dust * Dust);
		Look.Zenith = Tint(Look.Zenith, 0.5f * Dust);
		Look.Horizon = Tint(Look.Horizon, 0.8f * Dust);
		Look.CloudLit = Tint(Look.CloudLit, 0.5f * Dust);
		Look.CloudShadow = Tint(Look.CloudShadow, 0.5f * Dust);
		L.SunColor = Tint(L.SunColor, 0.5f * Dust);
		L.MainLightColor = L.bMainLightIsMoon ? L.MainLightColor : L.SunColor;
		L.AmbientSky = Tint(L.AmbientSky, 0.6f * Dust);
		L.AmbientEquator = Tint(L.AmbientEquator, 0.6f * Dust);
		const float SunLeft = 1.0f - DustSunDimming * Dust;
		L.SunIlluminance *= SunLeft;
		L.MainLightIlluminance *= L.bMainLightIsMoon ? 1.0f : SunLeft;
		Look.SunDisk = Look.SunDisk * SunLeft;
	}

	// The dome is past the height fog's cutoff: it is hazed in the fog's own colour, so the fogged ground meets it.
	const float Haze = FMath::Clamp(FogSkyHaze * Fog + DustSkyHaze * Dust, 0.0f, 1.0f);
	Look.Haze = FLinearColor(L.FogColor.R, L.FogColor.G, L.FogColor.B, Haze);
	// The lift keeps a grey day from going murky; a storm's darkness is meant to show, so it fades out with it.
	L.ExposureEV100 -= OvercastExposureLift * Overcast * (1.0f - Dark);
	L.ExposureEV100 -= FogExposureLift * FMath::Clamp(Env.Fog, 0.0f, 1.0f) + DustExposureLift * FMath::Clamp(Env.Dust, 0.0f, 1.0f);
}

FDreamSkyLighting UDreamSkyStyle::EvaluateLighting(const FDreamCelestialState& Celestial) const
{
	using namespace DreamSky::Style;
	const float E = Celestial.SunElevation;
	FDreamSkyLighting L;
	L.SunElevation = E;
	L.DayPhase = DreamWorld::Celestial::DayPhase(E, DayBottom, TwilightTop, NightTop, NightBottom);

	L.SunLightDirection = Celestial.SunDirection;
	L.SunColor = Eval(SunLightColor, E);
	L.SunIlluminance = FMath::Max(0.0f, Eval(SunIlluminance, E));

	// The moon lights the night from where it is; below the horizon, either nothing does or (stylized) a
	// stand-in light opposite the sun. Crossing over is a fade, never a jump.
	const float PhaseScale = FMath::Lerp(1.0f, Celestial.MoonIllumination, MoonPhaseDimming);
	const float NightLux = FMath::Max(0.0f, Eval(MoonIlluminance, E)) * PhaseScale;
	const float MoonUp = FMath::SmoothStep(-1.0f, 6.0f, Celestial.MoonElevation);
	L.MoonColor = Eval(MoonLightColor, E);
	if (bNightLightAlwaysUp)
	{
		// Blend the direction from the stand-in (opposite the sun) to the real moon as it rises.
		const FVector StandIn = -Celestial.SunDirection;
		L.MoonLightDirection = FMath::Lerp(StandIn, Celestial.MoonDirection, double(MoonUp)).GetSafeNormal();
		if (L.MoonLightDirection.IsNearlyZero())
		{
			L.MoonLightDirection = FVector::UpVector;
		}
		L.MoonIlluminance = NightLux;
	}
	else
	{
		L.MoonLightDirection = Celestial.MoonDirection;
		L.MoonIlluminance = NightLux * MoonUp;
	}

	L.bMainLightIsMoon = L.MoonIlluminance > L.SunIlluminance;
	L.MainLightDirection = L.bMainLightIsMoon ? L.MoonLightDirection : L.SunLightDirection;
	L.MainLightColor = L.bMainLightIsMoon ? L.MoonColor : L.SunColor;
	L.MainLightIlluminance = FMath::Max(L.SunIlluminance, L.MoonIlluminance);

	const float Ambient = FMath::Max(0.0f, Eval(AmbientScale, E, 1.0f));
	const FLinearColor Floor(NightAmbientFloor.R, NightAmbientFloor.G, NightAmbientFloor.B, 1.0f);
	auto Max3 = [](const FLinearColor& A, const FLinearColor& B) { return FLinearColor(FMath::Max(A.R, B.R), FMath::Max(A.G, B.G), FMath::Max(A.B, B.B), 1.0f); };
	L.AmbientSky = Max3(Eval(ZenithColor, E) * Ambient, Floor);
	L.AmbientEquator = Max3(Eval(HorizonColor, E) * Ambient, Floor);
	L.AmbientGround = Max3(Eval(GroundColor, E) * Ambient, Floor * 0.5f);
	L.SkyLightIntensity = FMath::Max(0.0f, Eval(SkyLightIntensity, E, 1.0f));

	L.FogColor = Eval(HorizonColor, E) * FogBrightness;
	L.FogColor.A = 1.0f;
	L.FogDensity = FMath::Max(0.0f, Eval(FogDensity, E));
	L.ExposureEV100 = Eval(ExposureEV100, E);
	return L;
}

float DreamSky::CumulusGrowth(float Hours)
{
	static const FVector2f Keys[] = { { 0.0f, 0.25f }, { 7.0f, 0.25f }, { 11.0f, 0.75f }, { 14.0f, 1.0f }, { 17.0f, 1.0f }, { 21.0f, 0.45f }, { 24.0f, 0.25f } };
	const float H = FMath::Fmod(FMath::Fmod(Hours, 24.0f) + 24.0f, 24.0f);
	for (int32 Index = 1; Index < UE_ARRAY_COUNT(Keys); ++Index)
	{
		if (H <= Keys[Index].X)
		{
			const float T = (H - Keys[Index - 1].X) / (Keys[Index].X - Keys[Index - 1].X);
			return FMath::Lerp(Keys[Index - 1].Y, Keys[Index].Y, FMath::SmoothStep(0.0f, 1.0f, T));
		}
	}
	return Keys[0].Y;
}

float DreamSky::CloudFormation(double WorldSeconds, float Speed)
{
	// (In double until it is wrapped: the clock runs for days of game seconds.)
	const double Phase = WorldSeconds / 3600.0 * CloudFormationPerHour * double(FMath::Max(Speed, 0.0f));
	return float(Phase - FMath::FloorToDouble(Phase));
}

FDreamSkyToonClouds::FDreamSkyToonClouds()
{
	// (The look's own: the half tone from 0.24 to 0.28 of the light, the lit tone from 0.56 to 0.6; an underside from its
	// deepest shade to the shade over 0.05..0.35 of its glow, to the half tone over 0.45..0.8.)
	Bands = { { 0.26f, 0.04f, 0.5f }, { 0.58f, 0.04f, 1.0f } };
	UndersideBands = { { 0.2f, 0.3f, 0.5f }, { 0.625f, 0.35f, 1.0f } };
	FRichCurve* Curve = ToneCurve.GetRichCurve();
	Curve->AddKey(0.0f, 0.0f);
	Curve->AddKey(0.26f, 0.5f);
	Curve->AddKey(0.58f, 1.0f);
}

float FDreamSkyToonClouds::EvaluateBands(const TArray<FDreamSkyToneBand>& InBands, float X)
{
	TArray<FDreamSkyToneBand, TInlineAllocator<8>> Sorted(InBands);
	Sorted.Sort([](const FDreamSkyToneBand& A, const FDreamSkyToneBand& B) { return A.At < B.At; });
	float Tone = 0.0f, Before = 0.0f;
	for (const FDreamSkyToneBand& Band : Sorted)
	{
		const float Half = 0.5f * FMath::Max(Band.Softness, 0.0f);
		const float Step = Half > 0.0f ? FMath::SmoothStep(Band.At - Half, Band.At + Half, X) : (X >= Band.At ? 1.0f : 0.0f);
		Tone += (Band.Tone - Before) * Step;
		Before = Band.Tone;
	}
	return FMath::Clamp(Tone, 0.0f, 1.0f);
}

void FDreamSkyToonClouds::Bake(FDreamSkyToonBaked& Out) const
{
	Out.Ramp.SetNumUninitialized(FDreamSkyToonBaked::RampSize);
	const FRichCurve* Curve = ToneCurve.GetRichCurveConst();
	const bool bCurve = bUseToneCurve && Curve && Curve->GetNumKeys() > 0;
	for (int32 Index = 0; Index < FDreamSkyToonBaked::RampSize; ++Index)
	{
		const float X = float(Index) / float(FDreamSkyToonBaked::RampSize - 1);
		const float Tone = bCurve ? FMath::Clamp(Curve->Eval(X), 0.0f, 1.0f) : EvaluateBands(Bands, X);
		Out.Ramp[Index] = FVector2f(Tone, EvaluateBands(UndersideBands, X));
	}
	Out.Half = FVector4f(HalfToneTint.R, HalfToneTint.G, HalfToneTint.B, FMath::Clamp(HalfToneMix, 0.0f, 1.0f));
	Out.Deep = FVector4f(UndersideDeepTint.R, UndersideDeepTint.G, UndersideDeepTint.B, KeyHeightPivot);
	Out.Key = FVector4f(KeyFacing, KeyUp, KeyHeight, FMath::Clamp(StormLitDarkening, 0.0f, 1.0f));
	Out.Key2 = FVector4f(KeyLow, FMath::Max(KeyRange, 0.01f), FMath::Max(RimPower, 0.5f), FMath::Max(RimStrength, 0.0f));
	Out.RimFocus = FMath::Max(RimFocus, 0.5f);
	const FDreamSkyToonAdvanced& A = Advanced;
	Out.Advanced[0] = FVector4f(FMath::Max(A.CastDepth, 1.0f) * 100.0f, A.CastFrom, FMath::Max(A.CastTo, A.CastFrom + 0.001f), FMath::Max(A.ThinColumn, 1.0f) * 100.0f);
	Out.Advanced[1] = FVector4f(A.AmbientBase, A.AmbientUp, A.AmbientThin, A.AmbientHeight);
	Out.Advanced[2] = FVector4f(A.UndersideSelfShade, A.WarmFrom, FMath::Max(A.WarmTo, A.WarmFrom + 0.001f), A.WarmTowardSun);
	Out.Advanced[3] = FVector4f(A.UndersideGaps, A.UndersideFlanks, A.UndersideTowardSun, A.UndersideInStorm);
	Out.Advanced[4] = FVector4f(A.DeckLitFrom, FMath::Max(A.DeckLitTo, A.DeckLitFrom + 0.001f), A.DeckTopFrom, FMath::Max(A.DeckTopTo, A.DeckTopFrom + 0.001f));
	Out.Advanced[5] = FVector4f(A.RimHeightFrom, FMath::Max(A.RimHeightTo, A.RimHeightFrom + 0.001f), A.StormTopDarkening, FMath::Max(A.CrackScale, 0.1f));
	Out.Advanced[6] = FVector4f(FMath::Max(A.VeilPower, 0.1f), FMath::Max(A.VeilStormPower, 0.0f), FMath::Max(A.CrackTear, 0.0f), 0.0f);
}

void FDreamSkyToonBaked::BlendToward(const FDreamSkyToonBaked& To, float Alpha)
{
	if (!To.IsValid() || Alpha <= 0.0f)
	{
		return;
	}
	if (!IsValid())
	{
		*this = To;
		return;
	}
	for (int32 Index = 0; Index < RampSize; ++Index)
	{
		Ramp[Index] = FMath::Lerp(Ramp[Index], To.Ramp[Index], Alpha);
	}
	Half = FMath::Lerp(Half, To.Half, Alpha);
	Deep = FMath::Lerp(Deep, To.Deep, Alpha);
	Key = FMath::Lerp(Key, To.Key, Alpha);
	Key2 = FMath::Lerp(Key2, To.Key2, Alpha);
	RimFocus = FMath::Lerp(RimFocus, To.RimFocus, Alpha);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Advanced); ++Index)
	{
		Advanced[Index] = FMath::Lerp(Advanced[Index], To.Advanced[Index], Alpha);
	}
}

FDreamSkyPaintedClouds FDreamSkyPaintedClouds::Lerp(const FDreamSkyPaintedClouds& A, const FDreamSkyPaintedClouds& B, float Alpha)
{
	FDreamSkyPaintedClouds Out = A;
	Out.Extinction = FMath::Lerp(A.Extinction, B.Extinction, Alpha);
	Out.PaintGain = FMath::Lerp(A.PaintGain, B.PaintGain, Alpha);
	Out.EdgeSoftness = FMath::Lerp(A.EdgeSoftness, B.EdgeSoftness, Alpha);
	Out.HalfToneAt = FMath::Lerp(A.HalfToneAt, B.HalfToneAt, Alpha);
	Out.LitToneAt = FMath::Lerp(A.LitToneAt, B.LitToneAt, Alpha);
	Out.ToneSoftness = FMath::Lerp(A.ToneSoftness, B.ToneSoftness, Alpha);
	Out.SilverLining = FMath::Lerp(A.SilverLining, B.SilverLining, Alpha);
	Out.BillowShade = FMath::Lerp(A.BillowShade, B.BillowShade, Alpha);
	Out.HalfToneMix = FMath::Lerp(A.HalfToneMix, B.HalfToneMix, Alpha);
	Out.KeyFalloff = FMath::Lerp(A.KeyFalloff, B.KeyFalloff, Alpha);
	Out.RimFalloff = FMath::Lerp(A.RimFalloff, B.RimFalloff, Alpha);
	Out.RimPower = FMath::Lerp(A.RimPower, B.RimPower, Alpha);
	Out.ShadeAtBase = FMath::Lerp(A.ShadeAtBase, B.ShadeAtBase, Alpha);
	Out.BaseFloor = FMath::Lerp(A.BaseFloor, B.BaseFloor, Alpha);
	Out.WarmFade = FMath::Lerp(A.WarmFade, B.WarmFade, Alpha);
	Out.WarmTowardSun = FMath::Lerp(A.WarmTowardSun, B.WarmTowardSun, Alpha);
	Out.WarmMax = FMath::Lerp(A.WarmMax, B.WarmMax, Alpha);
	Out.SkyInShade = FMath::Lerp(A.SkyInShade, B.SkyInShade, Alpha);
	Out.KeyAtBase = FMath::Lerp(A.KeyAtBase, B.KeyAtBase, Alpha);
	Out.KeyLow = FMath::Lerp(A.KeyLow, B.KeyLow, Alpha);
	Out.KeyHigh = FMath::Lerp(A.KeyHigh, B.KeyHigh, Alpha);
	Out.DeckThinColumn = FMath::Lerp(A.DeckThinColumn, B.DeckThinColumn, Alpha);
	Out.DeckStormThinning = FMath::Lerp(A.DeckStormThinning, B.DeckStormThinning, Alpha);
	Out.DeckFrom = FMath::Lerp(A.DeckFrom, B.DeckFrom, Alpha);
	Out.DeckTo = FMath::Lerp(A.DeckTo, B.DeckTo, Alpha);
	Out.StormLitDarkening = FMath::Lerp(A.StormLitDarkening, B.StormLitDarkening, Alpha);
	Out.StormTopDarkening = FMath::Lerp(A.StormTopDarkening, B.StormTopDarkening, Alpha);
	Out.FloorAtTop = FMath::Lerp(A.FloorAtTop, B.FloorAtTop, Alpha);
	Out.FloorInStorm = FMath::Lerp(A.FloorInStorm, B.FloorInStorm, Alpha);
	Out.LitTint = FMath::Lerp(A.LitTint, B.LitTint, Alpha);
	Out.ShadeTint = FMath::Lerp(A.ShadeTint, B.ShadeTint, Alpha);
	Out.HalfToneTint = FMath::Lerp(A.HalfToneTint, B.HalfToneTint, Alpha);
	return Out;
}
