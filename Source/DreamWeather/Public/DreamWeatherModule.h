// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

DREAMWEATHER_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamWeather, Log, All);

/** The weather's vectors in the plugin's shared Material Parameter Collection. */
namespace DreamWeather::Names
{
	inline const FName MPC_Weather0(TEXT("DDW_Weather0"));     // cloud coverage, cloud darkness, precipitation, snow ratio (at the view)
	inline const FName MPC_Weather1(TEXT("DDW_Weather1"));     // fog, dust, wind, lightning (at the view)
	inline const FName MPC_WeatherMap(TEXT("DDW_WeatherMap")); // minus the drawn map's centre x, y (cm: add a world position), 1 / extent (1/cm), 1 when the map is live
	inline const FName MPC_Flash(TEXT("DDW_Flash"));           // toward the lightning flash xyz, flash 0..1
}

/** The built-in weather channels. A project adds its own in Project Settings > Dream Weather. */
namespace DreamWeather::Tags
{
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_CloudCoverage);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_CloudDarkness);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Precipitation);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_SnowRatio);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Fog);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Dust);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Wind);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Lightning);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Temperature);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Wetness);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_SnowCover);

	// What the weather is like at a point, for gameplay conditions (GAS, StateTree, behaviour trees).
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Clear);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Cloudy);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Overcast);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Rain);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Rain_Heavy);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Snow);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Snow_Heavy);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Storm);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Fog);
	DREAMWEATHER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dust);
}

class FDreamWeatherModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
