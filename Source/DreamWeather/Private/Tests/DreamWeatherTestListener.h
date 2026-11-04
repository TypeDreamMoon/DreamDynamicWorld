// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DreamWeatherTypes.h"
#include "UObject/Object.h"
#include "DreamWeatherTestListener.generated.h"

class UDreamWeatherPreset;
class UDreamWeatherSubsystem;

/** Counts the weather's events for the automation tests (dynamic delegates need a UFUNCTION to bind). */
UCLASS(Transient)
class UDreamWeatherTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UDreamWeatherSubsystem* Weather);

	int32 Changes = 0;
	int32 Starts = 0;
	int32 Stops = 0;
	int32 TagChanges = 0;
	TObjectPtr<UDreamWeatherPreset> LastCurrent;

	UFUNCTION()
	void HandleChanged(UDreamWeatherPreset* Previous, UDreamWeatherPreset* Current) { ++Changes; LastCurrent = Current; }

	UFUNCTION()
	void HandleStarted() { ++Starts; }

	UFUNCTION()
	void HandleStopped() { ++Stops; }

	UFUNCTION()
	void HandleTags(const FGameplayTagContainer& Tags) { ++TagChanges; }

	int32 Strikes = 0;
	int32 RodHits = 0;

	UFUNCTION()
	void HandleStrike(const FDreamLightningStrike& Strike) { ++Strikes; }

	UFUNCTION()
	void HandleRodHit(const FDreamLightningStrike& Strike) { ++RodHits; }
};
