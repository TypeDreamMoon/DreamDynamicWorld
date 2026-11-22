// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyConditions.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamSkyConditions)

namespace DreamSky::Conditions
{
	float Smooth(float Edge0, float Edge1, float X)
	{
		if (Edge1 <= Edge0)
		{
			return X >= Edge0 ? 1.0f : 0.0f;
		}
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
		return T * T * (3.0f - 2.0f * T);
	}

	float ValueOf(EDreamSkyWeatherValue Value, const FDreamWorldEnvironment& E)
	{
		switch (Value)
		{
		case EDreamSkyWeatherValue::CloudCoverage: return E.CloudCoverage;
		case EDreamSkyWeatherValue::CloudDarkness: return E.CloudDarkness;
		case EDreamSkyWeatherValue::Precipitation: return E.Precipitation;
		case EDreamSkyWeatherValue::Rain: return E.Precipitation * (1.0f - E.SnowRatio);
		case EDreamSkyWeatherValue::Snow: return E.Precipitation * E.SnowRatio;
		case EDreamSkyWeatherValue::Fog: return E.Fog;
		case EDreamSkyWeatherValue::Dust: return E.Dust;
		case EDreamSkyWeatherValue::Wind: return E.Wind;
		case EDreamSkyWeatherValue::Lightning: return E.Lightning;
		case EDreamSkyWeatherValue::Temperature: return E.Temperature;
		default: return 0.0f;
		}
	}

	float Follow(float Weight, float Target, float Seconds, float DeltaTime, bool bSnap)
	{
		if (bSnap || Seconds <= 0.0f)
		{
			return Target;
		}
		return FMath::Lerp(Weight, Target, 1.0f - FMath::Exp(-FMath::Max(DeltaTime, 0.0f) / Seconds));
	}
}

float FDreamSkyWeatherRange::Evaluate(const FDreamWorldEnvironment& Environment) const
{
	using namespace DreamSky::Conditions;
	const float V = ValueOf(Value, Environment);
	const float S = FMath::Max(Soft, 0.0f);
	return Smooth(Min, Min + S, V) * (1.0f - (S > 0.0f ? Smooth(Max, Max + S, V) : (V > Max ? 1.0f : 0.0f)));
}

float FDreamSkyCondition::Evaluate(float Hours, const FDreamWorldEnvironment& Environment, const FDreamViewOcclusion& View) const
{
	using namespace DreamSky::Conditions;
	float Weight = 1.0f;
	if (bHours)
	{
		// The window from StartHour round to EndHour (a whole day when they meet), faded in and out inside it.
		float Length = FMath::Fmod(EndHour - StartHour + 48.0f, 24.0f);
		Length = Length <= 0.0f ? 24.0f : Length;
		const float Into = FMath::Fmod(Hours - StartHour + 48.0f, 24.0f);
		if (Into >= Length)
		{
			return 0.0f;
		}
		const float Fade = FMath::Min(FMath::Max(FadeHours, 0.0f), 0.5f * Length);
		if (Fade > 0.0f && Length < 24.0f)
		{
			Weight *= Smooth(0.0f, Fade, Into) * Smooth(0.0f, Fade, Length - Into);
		}
	}
	for (const FDreamSkyWeatherRange& Range : Weather)
	{
		Weight *= Range.Evaluate(Environment);
	}
	const float Interior = View.bValid ? FMath::Clamp(View.Interior, 0.0f, 1.0f) : 0.0f;
	if (Place == EDreamSkyPlace::Outdoors)
	{
		Weight *= 1.0f - Interior;
	}
	else if (Place == EDreamSkyPlace::Indoors)
	{
		Weight *= Interior;
	}
	return FMath::Clamp(Weight, 0.0f, 1.0f);
}
