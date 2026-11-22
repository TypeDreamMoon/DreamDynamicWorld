// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyAtmosphereBridge.h"
#include "DreamAtmosphereSettings.h"
#include "DreamAtmosphereSubsystem.h"
#include "DreamAtmosphereTables.h"
#include "DreamSky.h"
#include "DreamSkyStyle.h"
#include "DreamSkyTypes.h"
#include "DreamWorldParameters.h"
#include "DreamWorldSubsystem.h"

#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Materials/MaterialInstanceDynamic.h"

static TAutoConsoleVariable<int32> CVarDreamSkyPhysicalSky(
	TEXT("r.DreamSky.PhysicalSky"),
	1,
	TEXT("Draw and light the sky from the physical atmosphere (1, as the style's grade says) or from the style's curves alone (0)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarDreamSkyGradientBlendOverride(
	TEXT("r.DreamSky.GradientBlendOverride"),
	-1.0f,
	TEXT("Overrides the style's grade GradientBlend (0 the physical sky .. 1 the style's gradient); below 0, the style's own."),
	ECVF_Default);

namespace DreamSkyAtmosphereBridgeImpl
{
	const FName SkyGrade[4] = { TEXT("DDW_SkyGrade0"), TEXT("DDW_SkyGrade1"), TEXT("DDW_SkyGrade2"), TEXT("DDW_SkyGrade3") };
	const FName SkyTableParameter(TEXT("SkyTable"));

	float Luma(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	float Eval(const FRuntimeFloatCurve& Curve, float X, float Default)
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->Eval(X) : Default;
	}

	FLinearColor Opaque(const FLinearColor& C)
	{
		return FLinearColor(FMath::Max(C.R, 0.0f), FMath::Max(C.G, 0.0f), FMath::Max(C.B, 0.0f), 1.0f);
	}

	FLinearColor Max3(const FLinearColor& A, const FLinearColor& B)
	{
		return FLinearColor(FMath::Max(A.R, B.R), FMath::Max(A.G, B.G), FMath::Max(A.B, B.B), 1.0f);
	}

	/** Toward 1 at its brightest channel (a light's colour, its strength apart). */
	FLinearColor Normalized(const FLinearColor& C)
	{
		const float Peak = FMath::Max3(C.R, C.G, C.B);
		return Peak > 1.0e-6f ? Opaque(C / Peak) : FLinearColor::White;
	}

	/** Keeps the alpha of the style's colour (some carry a parameter there). */
	FLinearColor Blend(const FLinearColor& Physical, const FLinearColor& Style, float GradientBlend)
	{
		FLinearColor C = FMath::Lerp(Physical, Style, GradientBlend);
		C.A = Style.A;
		return C;
	}

	/**
	 * The weather for the aerial perspective. The clouds hide the sun from the air as from the ground (the style's
	 * overcast dimming, the dust's), whether or not they cast their own shadow on the ground: the air under them is lit by
	 * the deck instead, a grey as bright as the clear sky's horizon, darker under a storm. The grade's weather goes over
	 * it as over the sky, so the far land greys and dims with the horizon behind it. Clear weather leaves the air alone.
	 */
	FDreamAtmosphereWeather AerialWeather(const UDreamSkyStyle& Style, const FDreamWorldEnvironment& Environment)
	{
		FDreamAtmosphereWeather W;
		if (!Environment.bActive)
		{
			return W;
		}
		const float Cover = FDreamSkyGrade::Overcast(Environment);
		const float Dust = FMath::Clamp(Environment.Dust, 0.0f, 1.0f);
		W.DirectLight = (1.0f - FMath::Clamp(Style.OvercastLightDimming, 0.0f, 1.0f) * Cover) * (1.0f - FMath::Clamp(Style.DustSunDimming, 0.0f, 1.0f) * Dust);
		W.DiffuseLight = (1.0f - W.DirectLight) * (1.0f - FMath::Clamp(Style.StormDarkening, 0.0f, 1.0f) * FMath::Clamp(Environment.CloudDarkness, 0.0f, 1.0f));
		float EV = 0.0f;
		Style.Grade.EvaluateWeather(Environment, W.Saturation, EV, W.Tint);
		W.Tint = W.Tint * FMath::Pow(2.0f, EV);
		W.Tint.A = 1.0f;
		return W;
	}

	void BindDome(ADreamSky& Sky, const UDreamAtmosphereSubsystem* Atmosphere)
	{
		UTexture* Table = Atmosphere ? Atmosphere->GetSkyTexture() : nullptr;
		UStaticMeshComponent* Dome = Sky.SkyDome;
		if (!Table || !Dome)
		{
			return;
		}
		UMaterialInterface* Material = Dome->GetMaterial(0);
		UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Material);
		if (!Instance && Material)
		{
			Instance = Dome->CreateAndSetMaterialInstanceDynamic(0);
		}
		UTexture* Current = nullptr;
		if (Instance && (!Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(SkyTableParameter), Current) || Current != Table))
		{
			Instance->SetTextureParameterValue(SkyTableParameter, Table);
		}
	}
}

void DreamSky::AtmosphereBridge::RegisterParameters(FName Owner)
{
	using namespace DreamSkyAtmosphereBridgeImpl;
	// The identity grade, all of it the gradient (as the dome draws until the atmosphere is there).
	FDreamWorldParameters::RegisterVector(Owner, SkyGrade[0], FLinearColor(1.0f, 1.0f, 0.0f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, SkyGrade[1], FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
	FDreamWorldParameters::RegisterVector(Owner, SkyGrade[2], FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, SkyGrade[3], FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
}

void DreamSky::AtmosphereBridge::Apply(UDreamWorldSubsystem& Clock, ADreamSky& Sky, const UDreamSkyStyle& Style, FDreamSkyLighting& L, FDreamSkyLook& Look)
{
	using namespace DreamSkyAtmosphereBridgeImpl;
	UDreamAtmosphereSubsystem* Atmosphere = UDreamAtmosphereSubsystem::Get(Sky.GetWorld());
	if (Atmosphere && !Atmosphere->GetParameters().Equals(Style.Atmosphere))
	{
		Atmosphere->SetParameters(Style.Atmosphere);
	}
	if (Atmosphere && !UDreamAtmosphereSubsystem::CanBuildTables())
	{
		// (Mobile, no Shader Model 5: the style's sky baked ahead.)
		Atmosphere->SetBakedSky(Style.BakedSky);
	}
	const FDreamCelestialState& Celestial = Clock.GetCelestial();
	const FDreamWorldEnvironment& Environment = Clock.GetEnvironment();
	const float E = Celestial.SunElevation;
	FDreamSkyGradeValues Grade = Style.Grade.Evaluate(E, Environment);
	if (CVarDreamSkyGradientBlendOverride.GetValueOnGameThread() >= 0.0f)
	{
		Grade.GradientBlend = FMath::Clamp(CVarDreamSkyGradientBlendOverride.GetValueOnGameThread(), 0.0f, 1.0f);
	}
	const bool bPhysical = CVarDreamSkyPhysicalSky.GetValueOnGameThread() != 0 && Atmosphere && Atmosphere->IsSkyReady() && Grade.GradientBlend < 1.0f;
	if (Atmosphere)
	{
		Atmosphere->SetWeather(AerialWeather(Style, Environment));
	}

	if (bPhysical)
	{
		const FLinearColor SunTop = Atmosphere->GetSunLight();
		const FLinearColor MoonTop = Atmosphere->GetMoonSkyLight();
		// The zenith, and the horizon all round (a little over it, where the dome's horizon colour sits), lit by both.
		const FLinearColor Zenith = Opaque(Atmosphere->SkyZenith(Celestial.SunDirection) * SunTop + Atmosphere->SkyZenith(Celestial.MoonDirection) * MoonTop);
		const FLinearColor Horizon = Opaque(Atmosphere->SkyHorizon(Celestial.SunDirection) * SunTop + Atmosphere->SkyHorizon(Celestial.MoonDirection) * MoonTop);
		const FLinearColor GroundAlbedo(Style.Atmosphere.GroundAlbedo.R, Style.Atmosphere.GroundAlbedo.G, Style.Atmosphere.GroundAlbedo.B, 1.0f);
		const FLinearColor Ground = Opaque(Horizon * GroundAlbedo);

		// As bright as the style's own sky (its curves, before the weather), the colours and their spread the air's: the
		// style's exposure and its light and ambient stay as they were tuned, from noon to the night.
		const FDreamSkyLook Curves = Style.EvaluateLook(Celestial);
		const float Want = FMath::Max(0.5f * (Luma(Curves.Zenith) + Luma(Curves.Horizon)), 1.0e-9f);
		const float Have = 0.5f * (Luma(Zenith) + Luma(Horizon));
		if (Style.Grade.bMatchStyleBrightness)
		{
			Grade.ExposureScale *= FMath::Clamp(Want / FMath::Max(Have, 1.0e-12f), 1.0f / 1.0e6f, 1.0e6f);
		}
		// When the air has next to no light of its own (the sun far under the horizon and the moon down), its colour is
		// noise: the style's night takes over.
		const float Night = 1.0f - FMath::SmoothStep(1.0e-6f, 1.0e-5f, Have / Want);
		Grade.GradientBlend = FMath::Max(Grade.GradientBlend, Night);
		const float B = Grade.GradientBlend;
		const FLinearColor ZenithGraded = Opaque(Grade.Apply(Zenith, 1.0f));
		const FLinearColor HorizonGraded = Opaque(Grade.Apply(Horizon, 0.05f));
		const FLinearColor GroundGraded = Opaque(Grade.Apply(Ground, 0.0f));

		// The dome's zenith and horizon (the sky light's capture watches them; the clouds' sky reads them).
		Look.Zenith = Blend(ZenithGraded, Look.Zenith, B);
		Look.Horizon = Blend(HorizonGraded, Look.Horizon, B);
		Look.Ground = Blend(GroundGraded, Look.Ground, B);

		// The ambient light, from the sky as the style's from its curves.
		const float Ambient = FMath::Max(Eval(Style.AmbientScale, E, 1.0f), 0.0f);
		const FLinearColor Floor(Style.NightAmbientFloor.R, Style.NightAmbientFloor.G, Style.NightAmbientFloor.B, 1.0f);
		L.AmbientSky = Blend(Max3(ZenithGraded * Ambient, Floor), L.AmbientSky, B);
		L.AmbientEquator = Blend(Max3(HorizonGraded * Ambient, Floor), L.AmbientEquator, B);
		L.AmbientGround = Blend(Max3(GroundGraded * Ambient, Floor * 0.5f), L.AmbientGround, B);

		// The fog takes the horizon's colour; its density keeps only the weather's part when the aerial perspective carries
		// the air itself (the style's clear-weather haze would otherwise fog the distance twice).
		L.FogColor = Blend(HorizonGraded * Style.FogBrightness, L.FogColor, B);
		Look.Haze = FLinearColor(L.FogColor.R, L.FogColor.G, L.FogColor.B, Look.Haze.A);
		if (UDreamAtmosphereSettings::Get()->bAerialPerspective)
		{
			const float Clear = FMath::Max(Eval(Style.FogDensity, E, 0.0f), 0.0f);
			L.FogDensity = FMath::Lerp(FMath::Max(L.FogDensity - Clear, 0.0f), L.FogDensity, B);
		}

		// The sun's light through the air: its strength (the weather's dimming kept), its colour (the style's light
		// saturation, the grade's tint, the dust's among it).
		const float SunZ = float(Celestial.SunDirection.GetSafeNormal().Z);
		const FLinearColor Through = Atmosphere->LightTransmittance(SunZ);
		const float CurveSun = FMath::Max(Eval(Style.SunIlluminance, E, 0.0f), 0.0f);
		const float Weather = CurveSun > 0.01f ? L.SunIlluminance / CurveSun : 1.0f;
		const float PhysicalSun = Luma(SunTop) * Luma(Through) * Weather;
		FLinearColor SunColor = Normalized(Through * SunTop);
		const float SunLuma = Luma(SunColor);
		SunColor = FMath::Lerp(FLinearColor(SunLuma, SunLuma, SunLuma, 1.0f), SunColor, FMath::Clamp(Style.Grade.LightSaturation, 0.0f, 2.0f));
		SunColor = Normalized(SunColor * Grade.Tint);
		L.SunColor = Blend(SunColor, L.SunColor, B);
		L.SunIlluminance = FMath::Lerp(PhysicalSun, L.SunIlluminance, B);
		// The main light, as the style decides it: the brighter of the sun and the moon.
		L.bMainLightIsMoon = L.MoonIlluminance > L.SunIlluminance;
		L.MainLightDirection = L.bMainLightIsMoon ? L.MoonLightDirection : L.SunLightDirection;
		L.MainLightColor = L.bMainLightIsMoon ? L.MoonColor : L.SunColor;
		L.MainLightIlluminance = FMath::Max(L.SunIlluminance, L.MoonIlluminance);
	}
	else
	{
		Grade.GradientBlend = 1.0f;
	}

	FVector4f Packed[4];
	Grade.Pack(Packed);
	for (int32 i = 0; i < 4; ++i)
	{
		Clock.SetVectorParameter(SkyGrade[i], FLinearColor(Packed[i].X, Packed[i].Y, Packed[i].Z, Packed[i].W));
	}
	BindDome(Sky, Atmosphere);
}
