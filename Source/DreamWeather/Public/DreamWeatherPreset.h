// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DreamWeatherTypes.h"
#include "DreamWeatherPreset.generated.h"

class UTexture2D;

/**
 * A kind of weather: channel values (cloud cover, rain, wind...). Channels it leaves out are at their
 * registry default. The plugin ships WP_Clear, WP_Cloudy, WP_Overcast, WP_Rain, WP_Storm, WP_Snow, WP_Fog and WP_Dust
 * under /DreamDynamicWorld/Weather/Presets (the Setup step recreates them).
 */
UCLASS(BlueprintType)
class DREAMWEATHER_API UDreamWeatherPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Shown in forecasts and UIs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (Categories = "Dream.Weather.Channel"))
	TMap<FGameplayTag, float> Channels;

	/**
	 * A sky modifier (Dream Sky) the sky takes on as far as this weather is in effect where the view is: through the
	 * schedule's transitions, forced weather and zones alike (a storm's gloom, a fog's flat light).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky", meta = (AllowedClasses = "/Script/DreamSky.DreamSkyModifier"))
	TObjectPtr<UDataAsset> SkyModifier;

	/** The value of a channel (registry index), its default when the preset does not set it. */
	float GetValue(int32 ChannelIndex) const;

	/** Every channel. */
	UFUNCTION(BlueprintPure, Category = "Weather")
	FDreamWeatherValues GetValues() const;

	UFUNCTION(BlueprintPure, Category = "Weather")
	FText GetDisplayNameOrName() const;
};
