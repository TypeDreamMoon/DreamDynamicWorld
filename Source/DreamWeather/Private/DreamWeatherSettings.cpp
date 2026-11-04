// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherSettings.h"
#include "DreamWeatherChannels.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeatherSettings)

UDreamWeatherSettings::UDreamWeatherSettings()
{
	BuiltInChannels = FDreamWeatherChannels::MakeBuiltIn();
}

#if WITH_EDITOR
void UDreamWeatherSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FDreamWeatherChannels::Invalidate();
}
#endif
