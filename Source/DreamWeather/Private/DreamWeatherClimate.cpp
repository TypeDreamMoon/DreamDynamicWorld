// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherClimate.h"
#include "DreamWeatherPreset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherClimate)

namespace DreamWeather::Climate
{
	static FDreamClimateSeason S(float High, float Low, float Mm, float Cloud)
	{
		FDreamClimateSeason Out;
		Out.High = High;
		Out.Low = Low;
		Out.PrecipitationMm = Mm;
		Out.Cloudiness = Cloud;
		return Out;
	}

	static FDreamClimate C(FDreamClimateSeason Spring, FDreamClimateSeason Summer, FDreamClimateSeason Autumn, FDreamClimateSeason Winter, bool bDust = false)
	{
		FDreamClimate Out;
		Out.Spring = Spring;
		Out.Summer = Summer;
		Out.Autumn = Autumn;
		Out.Winter = Winter;
		Out.bDust = bDust;
		return Out;
	}

	/**
	 * Representative seasons of each type (rounded from long-term normals of a typical place of the type; spring,
	 * summer, autumn, winter as in the north): high, low (degrees C), precipitation over the season (mm), cloudiness.
	 */
	static const FDreamClimate* Table()
	{
		static const FDreamClimate Types[(int32)EDreamKoppenClimate::Num] =
		{
			/* Af  */ C(S(32, 25, 600, 0.70f), S(31, 25, 450, 0.70f), S(31, 24, 550, 0.70f), S(30, 24, 750, 0.75f)),
			/* Am  */ C(S(33, 24, 400, 0.50f), S(31, 25, 1700, 0.85f), S(31, 23, 500, 0.60f), S(27, 16, 40, 0.30f)),
			/* Aw  */ C(S(35, 26, 250, 0.50f), S(33, 26, 550, 0.80f), S(32, 25, 600, 0.70f), S(32, 22, 60, 0.30f)),
			/* BWh */ C(S(34, 20, 40, 0.15f), S(43, 28, 0, 0.05f), S(36, 21, 5, 0.10f), S(22, 10, 35, 0.25f), true),
			/* BWk */ C(S(22, 8, 15, 0.30f), S(31, 18, 25, 0.25f), S(21, 5, 10, 0.25f), S(3, -9, 8, 0.35f), true),
			/* BSh */ C(S(40, 24, 20, 0.20f), S(36, 26, 400, 0.55f), S(37, 24, 60, 0.25f), S(33, 17, 1, 0.10f), true),
			/* BSk */ C(S(16, 1, 150, 0.45f), S(30, 15, 140, 0.35f), S(18, 2, 75, 0.35f), S(7, -8, 40, 0.45f), true),
			/* Csa */ C(S(19, 9, 180, 0.50f), S(30, 18, 60, 0.20f), S(23, 13, 300, 0.45f), S(13, 4, 240, 0.60f)),
			/* Csb */ C(S(18, 10, 280, 0.55f), S(24, 15, 60, 0.35f), S(21, 13, 350, 0.55f), S(14, 6, 500, 0.65f)),
			/* Cwa */ C(S(26, 19, 450, 0.75f), S(32, 26, 1100, 0.70f), S(28, 21, 300, 0.50f), S(19, 12, 80, 0.50f)),
			/* Cwb */ C(S(26, 10, 60, 0.40f), S(23, 12, 480, 0.80f), S(22, 10, 200, 0.60f), S(21, 6, 25, 0.30f)),
			/* Cfa */ C(S(19, 10, 400, 0.60f), S(29, 22, 480, 0.75f), S(22, 14, 560, 0.60f), S(10, 2, 160, 0.40f)),
			/* Cfb */ C(S(14, 6, 150, 0.70f), S(22, 13, 160, 0.65f), S(15, 8, 200, 0.75f), S(8, 3, 170, 0.80f)),
			/* Cfc */ C(S(6, 0, 200, 0.80f), S(13, 8, 160, 0.80f), S(7, 2, 280, 0.85f), S(3, -2, 260, 0.85f)),
			/* Dsa */ C(S(15, 3, 150, 0.55f), S(31, 16, 15, 0.15f), S(19, 6, 60, 0.35f), S(2, -8, 120, 0.60f)),
			/* Dsb */ C(S(11, -1, 160, 0.55f), S(26, 10, 60, 0.30f), S(15, 1, 80, 0.40f), S(-3, -16, 70, 0.55f)),
			/* Dwa */ C(S(20, 8, 60, 0.40f), S(31, 22, 400, 0.60f), S(19, 7, 90, 0.35f), S(3, -8, 10, 0.25f), true),
			/* Dwb */ C(S(9, 2, 150, 0.55f), S(22, 16, 360, 0.75f), S(13, 5, 220, 0.50f), S(-6, -14, 40, 0.35f)),
			/* Dwc */ C(S(8, -7, 45, 0.45f), S(24, 9, 250, 0.60f), S(6, -8, 60, 0.50f), S(-16, -30, 10, 0.30f)),
			/* Dfa */ C(S(15, 5, 270, 0.60f), S(28, 18, 300, 0.50f), S(17, 7, 230, 0.55f), S(1, -8, 150, 0.65f)),
			/* Dfb */ C(S(10, 1, 130, 0.65f), S(23, 13, 230, 0.60f), S(9, 2, 160, 0.75f), S(-4, -10, 130, 0.85f)),
			/* Dfc */ C(S(5, -8, 40, 0.60f), S(21, 9, 130, 0.60f), S(3, -6, 80, 0.75f), S(-19, -28, 40, 0.70f)),
			/* Dfd */ C(S(2, -14, 30, 0.55f), S(24, 11, 120, 0.55f), S(-1, -12, 50, 0.70f), S(-35, -43, 25, 0.50f)),
			/* ET  */ C(S(-9, -17, 15, 0.70f), S(7, 1, 60, 0.85f), S(-6, -12, 40, 0.85f), S(-21, -28, 15, 0.60f)),
			/* EF  */ C(S(-25, -38, 25, 0.50f), S(-11, -25, 30, 0.55f), S(-25, -38, 30, 0.50f), S(-32, -45, 20, 0.45f)),
		};
		return Types;
	}
}

const FDreamClimateSeason& FDreamClimate::Season(int32 Index) const
{
	switch (Index & 3)
	{
	case 0: return Spring;
	case 1: return Summer;
	case 2: return Autumn;
	default: return Winter;
	}
}

const FDreamClimate& FDreamClimate::Koppen(EDreamKoppenClimate Type)
{
	const int32 Index = FMath::Clamp((int32)Type, 0, (int32)EDreamKoppenClimate::Num - 1);
	return DreamWeather::Climate::Table()[Index];
}

void FDreamClimate::GetTemperature(float& OutMean, float& OutSeasonalAmplitude, float& OutDiurnalAmplitude) const
{
	float Sum = 0.0f, Warmest = -1000.0f, Coldest = 1000.0f, Swing = 0.0f;
	for (int32 s = 0; s < 4; ++s)
	{
		const FDreamClimateSeason& Q = Season(s);
		const float Mean = 0.5f * (Q.High + Q.Low);
		Sum += Mean;
		Warmest = FMath::Max(Warmest, Mean);
		Coldest = FMath::Min(Coldest, Mean);
		Swing += 0.5f * FMath::Max(Q.High - Q.Low, 0.0f);
	}
	OutMean = Sum * 0.25f;
	// A season's mean is three months': the warmest and coldest months stand a little further apart.
	OutSeasonalAmplitude = 0.5f * (Warmest - Coldest) * 1.1f;
	OutDiurnalAmplitude = Swing * 0.25f;
}

void FDreamClimate::GetWeatherOdds(int32 SeasonIndex, float OutOdds[NumKinds]) const
{
	const FDreamClimateSeason& Q = Season(SeasonIndex);
	const float Mean = 0.5f * (Q.High + Q.Low);
	// Share of days with rain or snow: some 300 mm a season is wet most days.
	const float Wet = FMath::Clamp(Q.PrecipitationMm / 300.0f, 0.02f, 0.7f);
	const float Snow = 1.0f - FMath::SmoothStep(-2.0f, 4.0f, Mean);
	const float Cloud = FMath::Clamp(Q.Cloudiness, 0.0f, 1.0f);
	const float Dry = 1.0f - Wet;
	const float Thunder = FMath::SmoothStep(15.0f, 26.0f, Q.High);
	OutOdds[0] = (1.0f - Cloud) * Dry;                                          // clear
	OutOdds[1] = Cloud * Dry * 0.55f;                                           // cloudy
	OutOdds[2] = Cloud * Dry * 0.35f;                                           // overcast
	OutOdds[3] = Wet * (1.0f - Snow) * (1.0f - 0.5f * Thunder);                 // rain
	OutOdds[4] = Wet * (1.0f - Snow) * 0.5f * Thunder;                          // storm
	OutOdds[5] = Wet * Snow;                                                    // snow
	OutOdds[6] = Cloud * Dry * 0.10f * (1.0f - FMath::SmoothStep(20.0f, 30.0f, Q.High));  // fog, not in the heat
	OutOdds[7] = bDust ? Dry * 0.12f : 0.0f;                                    // dust
}

UDreamWeatherSelector_Climate::UDreamWeatherSelector_Climate()
{
	static const TCHAR* Names[FDreamClimate::NumKinds] = { TEXT("WP_Clear"), TEXT("WP_Cloudy"), TEXT("WP_Overcast"), TEXT("WP_Rain"),
		TEXT("WP_Storm"), TEXT("WP_Snow"), TEXT("WP_Fog"), TEXT("WP_Dust") };
	Presets.SetNum(FDreamClimate::NumKinds);
	for (int32 i = 0; i < FDreamClimate::NumKinds; ++i)
	{
		Presets[i] = TSoftObjectPtr<UDreamWeatherPreset>(FSoftObjectPath(FString::Printf(TEXT("/DreamDynamicWorld/Weather/Presets/%s.%s"), Names[i], Names[i])));
	}
}

const FDreamClimate& UDreamWeatherSelector_Climate::GetClimate() const
{
	return CustomClimate ? CustomClimate->Climate : FDreamClimate::Koppen(Climate);
}

bool UDreamWeatherSelector_Climate::GetClimateTemperature(float& OutMean, float& OutSeasonalAmplitude, float& OutDiurnalAmplitude) const
{
	GetClimate().GetTemperature(OutMean, OutSeasonalAmplitude, OutDiurnalAmplitude);
	return true;
}

float UDreamWeatherSelector_Climate::WeightOf(int32 EntryIndex, int64 SlotIndex) const
{
	if (!Presets.IsValidIndex(EntryIndex) || Presets[EntryIndex].IsNull() || EntryIndex >= FDreamClimate::NumKinds)
	{
		return 0.0f;
	}
	const double SlotStart = double(SlotIndex) * FMath::Max(0.5, double(SlotHours)) * 3600.0;
	float Odds[FDreamClimate::NumKinds];
	GetClimate().GetWeatherOdds(SeasonAt(SlotStart, bSouthernHemisphere), Odds);
	return Odds[EntryIndex];
}
