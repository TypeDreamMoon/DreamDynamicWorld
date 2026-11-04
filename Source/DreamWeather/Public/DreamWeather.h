// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamWeather.generated.h"

class UDreamWeatherPreset;
class UDreamWeatherSelector;

/**
 * The weather of a level: drop one in to turn the weather on (without it the sky keeps to its own style).
 * Its Selector decides the weather over time from the world clock and seed; zones (ADreamWeatherZone) and
 * modifiers (UDreamWeatherSubsystem::PushModifier) bend it.
 */
UCLASS(ClassGroup = "Dream", hidecategories = (Collision, Physics, Input, Replication, HLOD, Cooking, Networking, Rendering), meta = (DisplayName = "Dream Weather"))
class DREAMWEATHER_API ADreamWeather : public AActor
{
	GENERATED_BODY()

public:
	ADreamWeather();

	/** How the weather changes over time: Fixed, Sequence, Random or Seasonal. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UDreamWeatherSelector> Selector;

	/** Editor only: show this weather in the editor viewport instead of what the selector says. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TObjectPtr<UDreamWeatherPreset> EditorPreview;

	//~ AActor
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
};
