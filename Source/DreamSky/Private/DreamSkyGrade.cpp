// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyGrade.h"
#include "DreamSkyGradeShim.h"
#include "DreamWorldTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkyGrade)

namespace DreamSkyGradeEval
{
	/** One key: a curve that holds Value over the day (the identity, until a style shapes it). */
	static void Flat(FRuntimeFloatCurve& Curve, float Value)
	{
		FRichCurve& Rich = *Curve.GetRichCurve();
		Rich.Reset();
		Rich.AddKey(0.0f, Value);
	}

	static void Flat(FRuntimeCurveLinearColor& Curve, const FLinearColor& Value)
	{
		const float Values[4] = { Value.R, Value.G, Value.B, Value.A };
		for (int32 c = 0; c < 4; ++c)
		{
			Curve.ColorCurves[c].Reset();
			Curve.ColorCurves[c].AddKey(0.0f, Values[c]);
		}
	}

	static float Eval(const FRuntimeFloatCurve& Curve, float X, float Default)
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->Eval(X) : Default;
	}

	static FLinearColor Eval(const FRuntimeCurveLinearColor& Curve, float X)
	{
		bool bAny = false;
		for (const FRichCurve& Channel : Curve.ColorCurves)
		{
			bAny |= Channel.GetNumKeys() > 0;
		}
		if (!bAny)
		{
			return FLinearColor::White;
		}
		FLinearColor C = Curve.GetLinearColorValue(X);
		C.A = 1.0f;
		return FLinearColor(FMath::Max(C.R, 0.0f), FMath::Max(C.G, 0.0f), FMath::Max(C.B, 0.0f), 1.0f);
	}
}

FDreamSkyGrade::FDreamSkyGrade()
{
	using namespace DreamSkyGradeEval;
	Flat(Exposure, 0.0f);
	Flat(Saturation, 1.0f);
	Flat(HueShift, 0.0f);
	Flat(Contrast, 1.0f);
	Flat(Tint, FLinearColor::White);
	Flat(ZenithTint, FLinearColor::White);
	Flat(HorizonTint, FLinearColor::White);
}

FDreamSkyGradeValues FDreamSkyGrade::Evaluate(float SunElevation, const FDreamWorldEnvironment& Environment) const
{
	using namespace DreamSkyGradeEval;
	const float E = SunElevation;
	FDreamSkyGradeValues V;
	float EV = Eval(Exposure, E, 0.0f);
	V.Saturation = FMath::Max(Eval(Saturation, E, 1.0f), 0.0f);
	V.HueShift = FMath::DegreesToRadians(Eval(HueShift, E, 0.0f));
	V.Contrast = FMath::Clamp(Eval(Contrast, E, 1.0f), 0.1f, 4.0f);
	V.Tint = Eval(Tint, E);
	V.ZenithTint = Eval(ZenithTint, E);
	V.HorizonTint = Eval(HorizonTint, E);
	V.TintSpread = FMath::Max(TintSpread, 0.05f);
	V.GradientBlend = FMath::Clamp(GradientBlend, 0.0f, 1.0f);

	// The afterglow: from the sun on the horizon to 12 degrees under it, the twilight scaled.
	const float Twilight = FMath::SmoothStep(-12.0f, -9.0f, E) * (1.0f - FMath::SmoothStep(-1.0f, 0.5f, E));
	EV += FMath::Log2(FMath::Max(FMath::Lerp(1.0f, FMath::Max(Afterglow, 0.0f), Twilight), 1.0e-3f));

	if (Environment.bActive)
	{
		// The same reading of the sky's cover the style's weather uses: closing from half cover, or rain under any.
		const float Overcast = FMath::Max(FMath::SmoothStep(0.45f, 0.95f, FMath::Clamp(Environment.CloudCoverage, 0.0f, 1.0f)),
			0.6f * FMath::Clamp(Environment.Precipitation, 0.0f, 1.0f));
		V.Saturation *= FMath::Lerp(1.0f, FMath::Clamp(OvercastSaturation, 0.0f, 1.0f), Overcast);
		EV += OvercastExposure * Overcast;
		const float Fog = FMath::Clamp(Environment.Fog, 0.0f, 1.0f);
		V.Saturation *= 1.0f - FMath::Clamp(FogGrey, 0.0f, 1.0f) * Fog;
		const float Dust = FMath::Clamp(Environment.Dust, 0.0f, 1.0f) * FMath::Clamp(DustStrength, 0.0f, 1.0f);
		V.Tint = V.Tint * FMath::Lerp(FLinearColor::White, FLinearColor(DustTint.R, DustTint.G, DustTint.B, 1.0f), Dust);
		V.Tint.A = 1.0f;
	}
	V.ExposureScale = FMath::Pow(2.0f, EV);
	return V;
}

void FDreamSkyGradeValues::Pack(FVector4f Out[4]) const
{
	Out[0] = FVector4f(ExposureScale, Saturation, HueShift, Contrast);
	Out[1] = FVector4f(Tint.R, Tint.G, Tint.B, TintSpread);
	Out[2] = FVector4f(ZenithTint.R, ZenithTint.G, ZenithTint.B, GradientBlend);
	Out[3] = FVector4f(HorizonTint.R, HorizonTint.G, HorizonTint.B, 0.0f);
}

FLinearColor FDreamSkyGradeValues::Apply(const FLinearColor& Color, float ViewZ) const
{
	using namespace DreamWind::Hlsl;
	FVector4f G[4];
	Pack(G);
	const float3 C = DSG_Grade(float3(Color.R, Color.G, Color.B), ViewZ, ToHlsl(G[0]), ToHlsl(G[1]), ToHlsl(G[2]), ToHlsl(G[3]));
	return FLinearColor(C.x, C.y, C.z, Color.A);
}
