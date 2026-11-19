// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherChannels.h"
#include "DreamWeatherHlslShim.h"
#include "DreamWeatherModule.h"
#include "DreamWeatherSettings.h"

namespace DreamWeather::Registry
{
	static TArray<FDreamWeatherChannelDef>& Cache()
	{
		static TArray<FDreamWeatherChannelDef> Channels;
		return Channels;
	}

	static FDreamWeatherChannelDef Make(const FGameplayTag& Tag, float Default, float Min, float Max, float TransitionMinutes, float LeadMinutes)
	{
		FDreamWeatherChannelDef Def;
		Def.Channel = Tag;
		Def.Default = Default;
		Def.Min = Min;
		Def.Max = Max;
		Def.TransitionMinutes = TransitionMinutes;
		Def.LeadMinutes = LeadMinutes;
		return Def;
	}
}

TArray<FDreamWeatherChannelDef> FDreamWeatherChannels::MakeBuiltIn()
{
	using namespace DreamWeather::Tags;
	using DreamWeather::Registry::Make;
	// Clouds and wind gather ahead of a change, rain and lightning follow it, the ground lags behind.
	TArray<FDreamWeatherChannelDef> Out;
	Out.Add(Make(Channel_CloudCoverage, 0.3f, 0.0f, 1.0f, 40.0f, 15.0f));
	Out.Add(Make(Channel_CloudDarkness, 0.0f, 0.0f, 1.0f, 30.0f, 5.0f));
	Out.Add(Make(Channel_Precipitation, 0.0f, 0.0f, 1.0f, 15.0f, -10.0f));
	Out.Add(Make(Channel_SnowRatio, 0.0f, 0.0f, 1.0f, 10.0f, 0.0f));
	Out.Add(Make(Channel_Fog, 0.0f, 0.0f, 1.0f, 30.0f, 0.0f));
	Out.Add(Make(Channel_Dust, 0.0f, 0.0f, 1.0f, 20.0f, 0.0f));
	Out.Add(Make(Channel_Wind, 0.3f, 0.0f, 1.0f, 15.0f, 10.0f));
	Out.Add(Make(Channel_Lightning, 0.0f, 0.0f, 1.0f, 10.0f, -15.0f));
	Out.Add(Make(Channel_Temperature, 0.0f, -40.0f, 40.0f, 60.0f, 0.0f));
	Out.Add(Make(Channel_Wetness, 0.0f, 0.0f, 1.0f, 20.0f, -10.0f));
	Out.Add(Make(Channel_SnowCover, 0.0f, 0.0f, 1.0f, 60.0f, -20.0f));
	check(Out.Num() == NumBuiltIn);
	return Out;
}

const TArray<FDreamWeatherChannelDef>& FDreamWeatherChannels::Get()
{
	TArray<FDreamWeatherChannelDef>& Channels = DreamWeather::Registry::Cache();
	if (Channels.Num() > 0)
	{
		return Channels;
	}
	const UDreamWeatherSettings* Settings = UDreamWeatherSettings::Get();
	Channels = MakeBuiltIn();
	// The built-ins' timings and defaults from the settings, when they still describe the same channels.
	if (Settings && Settings->BuiltInChannels.Num() == NumBuiltIn)
	{
		for (int32 i = 0; i < NumBuiltIn; ++i)
		{
			if (Settings->BuiltInChannels[i].Channel == Channels[i].Channel)
			{
				Channels[i] = Settings->BuiltInChannels[i];
			}
		}
	}
	if (Settings)
	{
		for (const FDreamWeatherChannelDef& Custom : Settings->CustomChannels)
		{
			const bool bTaken = Channels.ContainsByPredicate([&Custom](const FDreamWeatherChannelDef& D) { return D.Channel == Custom.Channel; });
			if (Custom.Channel.IsValid() && !bTaken)
			{
				Channels.Add(Custom);
			}
			else if (bTaken)
			{
				UE_LOG(LogDreamWeather, Warning, TEXT("Weather channel %s is registered twice; the second one is ignored."), *Custom.Channel.ToString());
			}
		}
	}
	return Channels;
}

int32 FDreamWeatherChannels::IndexOf(const FGameplayTag& Channel)
{
	const TArray<FDreamWeatherChannelDef>& Channels = Get();
	for (int32 i = 0; i < Channels.Num(); ++i)
	{
		if (Channels[i].Channel == Channel)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FDreamWeatherValues FDreamWeatherChannels::Defaults()
{
	FDreamWeatherValues Out;
	for (const FDreamWeatherChannelDef& Def : Get())
	{
		Out.Values.Add(Def.Default);
	}
	return Out;
}

void FDreamWeatherChannels::Invalidate()
{
	DreamWeather::Registry::Cache().Reset();
}

void FDreamWeatherChannels::ApplyCloudGates(FDreamWeatherValues& InOut)
{
	const UDreamWeatherSettings* S = UDreamWeatherSettings::Get();
	if (!S || InOut.Values.Num() < NumBuiltIn)
	{
		return;
	}
	using namespace DreamWind::Hlsl;
	const float Cover = InOut.Values[CloudCoverage];
	const float Dark = InOut.Values[CloudDarkness];
	// The same gate the weather map applies on the GPU (DreamWeatherShared.h).
	InOut.Values[Precipitation] *= DWX_CloudAllows(Cover, float(S->PrecipitationCloudCover.X), float(S->PrecipitationCloudCover.Y));
	InOut.Values[Lightning] *= DWX_CloudAllows(Dark, float(S->LightningCloudDarkness.X), float(S->LightningCloudDarkness.Y))
		* DWX_CloudAllows(Cover, float(S->LightningCloudCover.X), float(S->LightningCloudCover.Y));
}

float FDreamWeatherChannels::Combine(EDreamWeatherBlend Blend, float Current, float Value, float W)
{
	switch (Blend)
	{
	case EDreamWeatherBlend::Add: return Current + Value * W;
	case EDreamWeatherBlend::Multiply: return Current * FMath::Lerp(1.0f, Value, W);
	case EDreamWeatherBlend::Max: return FMath::Lerp(Current, FMath::Max(Current, Value), W);
	case EDreamWeatherBlend::Min: return FMath::Lerp(Current, FMath::Min(Current, Value), W);
	default: return FMath::Lerp(Current, Value, W);
	}
}
