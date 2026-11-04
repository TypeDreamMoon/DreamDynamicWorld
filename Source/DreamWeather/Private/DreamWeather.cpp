// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeather.h"
#include "DreamWeatherSelector.h"
#include "DreamWeatherSubsystem.h"

#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWeather)

ADreamWeather::ADreamWeather()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Selector = CreateDefaultSubobject<UDreamWeatherSelector_Random>(TEXT("Selector"));
}

void ADreamWeather::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (UDreamWeatherSubsystem* Weather = GetWorld() && !IsTemplate() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->RegisterWeatherActor(this);
	}
}

void ADreamWeather::PostUnregisterAllComponents()
{
	if (UDreamWeatherSubsystem* Weather = GetWorld() ? GetWorld()->GetSubsystem<UDreamWeatherSubsystem>() : nullptr)
	{
		Weather->UnregisterWeatherActor(this);
	}
	Super::PostUnregisterAllComponents();
}
