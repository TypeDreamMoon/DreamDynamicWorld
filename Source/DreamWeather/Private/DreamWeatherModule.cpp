// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherModule.h"
#include "DreamWorldParameters.h"

DEFINE_LOG_CATEGORY(LogDreamWeather);

namespace DreamWeather::Tags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_CloudCoverage, "Dream.Weather.Channel.CloudCoverage", "Fraction of the sky covered by cloud, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_CloudDarkness, "Dream.Weather.Channel.CloudDarkness", "How dark and heavy the clouds are, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Precipitation, "Dream.Weather.Channel.Precipitation", "Rain or snow intensity, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_SnowRatio, "Dream.Weather.Channel.SnowRatio", "What falls: 0 rain .. 1 snow.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Fog, "Dream.Weather.Channel.Fog", "Fog, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Dust, "Dream.Weather.Channel.Dust", "Dust or sand in the air, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Wind, "Dream.Weather.Channel.Wind", "Wind strength, 0 calm .. 1 storm.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Lightning, "Dream.Weather.Channel.Lightning", "Lightning frequency, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Temperature, "Dream.Weather.Channel.Temperature", "Temperature offset the weather brings, degrees Celsius.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Wetness, "Dream.Weather.Channel.Wetness", "How wet surfaces become, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_SnowCover, "Dream.Weather.Channel.SnowCover", "How much snow settles, 0..1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_FreezingRain, "Dream.Weather.Channel.FreezingRain", "The share of the rain that is supercooled, 0..1: it stays rain below freezing (it is not turned to snow) and freezes where it lands on frozen ground.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Hail, "Dream.Weather.Channel.Hail", "The share of the precipitation that falls as hail, 0..1 (at any temperature: a summer storm hails).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Channel_Sleet, "Dream.Weather.Channel.Sleet", "The share of the precipitation that falls as sleet, 0..1: wet snow and ice pellets, between the rain and the snow.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Clear, "Dream.Weather.State.Clear", "Little or no cloud.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Cloudy, "Dream.Weather.State.Cloudy", "Broken cloud.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Overcast, "Dream.Weather.State.Overcast", "The sky is covered.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Rain, "Dream.Weather.State.Rain", "It is raining.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Rain_Heavy, "Dream.Weather.State.Rain.Heavy", "It is raining hard.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Snow, "Dream.Weather.State.Snow", "It is snowing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Snow_Heavy, "Dream.Weather.State.Snow.Heavy", "It is snowing hard.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Hail, "Dream.Weather.State.Hail", "It is hailing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Sleet, "Dream.Weather.State.Sleet", "Sleet is falling.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Storm, "Dream.Weather.State.Storm", "A thunderstorm.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Fog, "Dream.Weather.State.Fog", "Fog.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dust, "Dream.Weather.State.Dust", "Dust or sand in the air.");
}

void FDreamWeatherModule::StartupModule()
{
	using namespace DreamWeather::Names;
	const FName Owner(TEXT("DreamWeather"));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Weather0, FLinearColor(0.3f, 0.0f, 0.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Weather1, FLinearColor(0.0f, 0.0f, 0.3f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_WeatherMap, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Flash, FLinearColor(0.0f, 0.0f, 1.0f, 0.0f));
}

void FDreamWeatherModule::ShutdownModule()
{
	FDreamWorldParameters::UnregisterOwner(TEXT("DreamWeather"));
}

IMPLEMENT_MODULE(FDreamWeatherModule, DreamWeather)
