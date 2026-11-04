// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamWorldTestListener.generated.h"

class UDreamWorldSubsystem;

/** Counts the world clock's events for the automation tests (dynamic delegates need a UFUNCTION to bind). */
UCLASS(Transient)
class UDreamWorldTestListener : public UObject
{
	GENERATED_BODY()

public:
	void Bind(UDreamWorldSubsystem* Clock);

	int32 Hours = 0;
	int32 LastHour = -1;
	int32 Dates = 0;
	int32 Sunrises = 0;
	int32 Sunsets = 0;
	int32 Jumps = 0;

	UFUNCTION()
	void HandleHour(int32 Hour) { ++Hours; LastHour = Hour; }

	UFUNCTION()
	void HandleDate(FDateTime Date) { ++Dates; }

	UFUNCTION()
	void HandleSunrise() { ++Sunrises; }

	UFUNCTION()
	void HandleSunset() { ++Sunsets; }

	UFUNCTION()
	void HandleJump() { ++Jumps; }
};
