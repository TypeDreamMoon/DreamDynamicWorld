// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherPreset.h"
#include "DreamWeatherChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherPreset)

float UDreamWeatherPreset::GetValue(int32 ChannelIndex) const
{
	const TArray<FDreamWeatherChannelDef>& Defs = FDreamWeatherChannels::Get();
	if (!Defs.IsValidIndex(ChannelIndex))
	{
		return 0.0f;
	}
	const FDreamWeatherChannelDef& Def = Defs[ChannelIndex];
	const float* Value = Channels.Find(Def.Channel);
	return Value ? FMath::Clamp(*Value, Def.Min, Def.Max) : Def.Default;
}

FDreamWeatherValues UDreamWeatherPreset::GetValues() const
{
	FDreamWeatherValues Out;
	const int32 Num = FDreamWeatherChannels::Num();
	Out.Values.SetNumUninitialized(Num);
	for (int32 i = 0; i < Num; ++i)
	{
		Out.Values[i] = GetValue(i);
	}
	return Out;
}

FText UDreamWeatherPreset::GetDisplayNameOrName() const
{
	return DisplayName.IsEmpty() ? FText::FromString(GetName()) : DisplayName;
}
