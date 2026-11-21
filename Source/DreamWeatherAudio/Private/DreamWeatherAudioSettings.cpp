// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherAudioSettings.h"
#include "DreamAmbiencePreset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherAudioSettings)

const FDreamWeatherSoundSlot& FDreamWeatherSounds::Get(EDreamWeatherVoice Voice) const
{
	return const_cast<FDreamWeatherSounds*>(this)->Get(Voice);
}

FDreamWeatherSoundSlot& FDreamWeatherSounds::Get(EDreamWeatherVoice Voice)
{
	switch (Voice)
	{
	case EDreamWeatherVoice::Wind: return Wind;
	case EDreamWeatherVoice::WindGust: return WindGust;
	case EDreamWeatherVoice::RainLight: return RainLight;
	case EDreamWeatherVoice::RainHeavy: return RainHeavy;
	case EDreamWeatherVoice::RainOnRoof: return RainOnRoof;
	case EDreamWeatherVoice::RainOnLeaves: return RainOnLeaves;
	case EDreamWeatherVoice::Snow: return Snow;
	default: return Dust;
	}
}

void FDreamWeatherSounds::FillEmptyFrom(const FDreamWeatherSounds& Other)
{
	for (const EDreamWeatherVoice Voice : TEnumRange<EDreamWeatherVoice>())
	{
		if (Get(Voice).IsEmpty())
		{
			Get(Voice) = Other.Get(Voice);
		}
	}
	if (ThunderNear.IsEmpty())
	{
		ThunderNear = Other.ThunderNear;
	}
	if (ThunderFar.IsEmpty())
	{
		ThunderFar = Other.ThunderFar;
	}
	for (const TPair<FName, FDreamWeatherSoundSlot>& Pair : Other.Ambience)
	{
		const FDreamWeatherSoundSlot* Mine = Ambience.Find(Pair.Key);
		if (!Mine || Mine->IsEmpty())
		{
			Ambience.Add(Pair.Key, Pair.Value);
		}
	}
}

UDreamWeatherAudioSettings::UDreamWeatherAudioSettings()
{
}

FDreamWeatherSounds UDreamWeatherAudioSettings::ResolveSounds() const
{
	FDreamWeatherSounds Out;
	if (const UDreamWeatherSoundSet* Set = SoundSet.LoadSynchronous())
	{
		Out = Set->Sounds;
	}
	Out.FillEmptyFrom(Sounds);
	return Out;
}

FDreamWeatherAudioTuning UDreamWeatherAudioSettings::MakeTuning(const FDreamWeatherSounds& InSounds) const
{
	FDreamWeatherAudioTuning T;
	T.WindLoudSpeed = WindLoudSpeed;
	T.InteriorMinVolume = InteriorMinVolume;
	T.MinCutoffHz = MinCutoffHz;
	T.ThunderNearDistance = ThunderNearDistance;
	T.ThunderMaxDistance = ThunderMaxDistance;
	T.ThunderInteriorMuffle = ThunderInteriorMuffle;
	for (const EDreamWeatherVoice Voice : TEnumRange<EDreamWeatherVoice>())
	{
		T.SlotVolume[(int32)Voice] = InSounds.Get(Voice).Volume;
	}
	T.ThunderNearVolume = InSounds.ThunderNear.Volume;
	T.ThunderFarVolume = InSounds.ThunderFar.IsEmpty() ? InSounds.ThunderNear.Volume : InSounds.ThunderFar.Volume;
	return T;
}

void FDreamAmbienceParams::ForEach(TFunctionRef<void(FName, float)> Visit) const
{
	static const FName TimeOfDayName(TEXT("TimeOfDay")), DayName(TEXT("Day")), TwilightName(TEXT("Twilight")), NightName(TEXT("Night"));
	static const FName SeasonNames[4] = { FName(TEXT("Spring")), FName(TEXT("Summer")), FName(TEXT("Autumn")), FName(TEXT("Winter")) };
	static const FName YearName(TEXT("Year")), CloudName(TEXT("Cloud")), RainName(TEXT("Rain")), SnowName(TEXT("Snow")), FogName(TEXT("Fog"));
	static const FName DustName(TEXT("Dust")), LightningName(TEXT("Lightning")), WindName(TEXT("Wind")), GustName(TEXT("Gust")), InteriorName(TEXT("Interior"));
	static const FName LeftName(TEXT("Left")), RightName(TEXT("Right")), MuffleName(TEXT("Muffle"));
	Visit(TimeOfDayName, TimeOfDay);
	Visit(DayName, Phase.Day);
	Visit(TwilightName, Phase.Twilight);
	Visit(NightName, Phase.Night);
	for (int32 s = 0; s < 4; ++s)
	{
		Visit(SeasonNames[s], Seasons[s]);
	}
	Visit(YearName, Year);
	Visit(CloudName, Cloud);
	Visit(RainName, Rain);
	Visit(SnowName, Snow);
	Visit(FogName, Fog);
	Visit(DustName, Dust);
	Visit(LightningName, Lightning);
	Visit(WindName, Wind);
	Visit(GustName, Gust);
	Visit(InteriorName, Interior);
	Visit(LeftName, Occlusion.Left);
	Visit(RightName, Occlusion.Right);
	Visit(MuffleName, Occlusion.Muffle);
}

void FDreamAmbienceParams::SeasonWeights(const FDateTime& Date, bool bSouthernHemisphere, float OutWeights[4], float& OutYear)
{
	// Year: 0 on 1 March (the start of the meteorological spring) .. 1 a year later.
	const int32 Year = Date.GetMonth() >= 3 ? Date.GetYear() : Date.GetYear() - 1;
	const FDateTime March(Year, 3, 1);
	const double Days = (Date - March).GetTotalDays();
	const double DaysInYear = (FDateTime(Year + 1, 3, 1) - March).GetTotalDays();
	OutYear = float(FMath::Clamp(Days / DaysInYear, 0.0, 1.0));

	// In quarters: each season is a quarter, whole in its middle, blending across the half month either side of a turn.
	double Quarter = double(OutYear) * 4.0 + (bSouthernHemisphere ? 2.0 : 0.0);
	constexpr double HalfBlend = 1.0 / 6.0;
	float Sum = 0.0f;
	for (int32 s = 0; s < 4; ++s)
	{
		double Q = FMath::Fmod(Quarter - double(s) + 8.0, 4.0);
		if (Q > 2.0)
		{
			Q -= 4.0;
		}
		const double Inside = FMath::Min(Q, 1.0 - Q);
		OutWeights[s] = float(FMath::Clamp((Inside + HalfBlend) / (2.0 * HalfBlend), 0.0, 1.0));
		Sum += OutWeights[s];
	}
	for (int32 s = 0; s < 4; ++s)
	{
		OutWeights[s] = Sum > 0.0f ? OutWeights[s] / Sum : (s == 0 ? 1.0f : 0.0f);
	}
}
