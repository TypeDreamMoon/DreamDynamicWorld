// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindPassThroughComponent.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DreamWindPassThroughComponent)

const FName UDreamWindPassThroughComponent::PassThroughTag(TEXT("DreamWind.PassThrough"));

UDreamWindPassThroughComponent::UDreamWindPassThroughComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = false;
}

bool UDreamWindPassThroughComponent::IsPassThrough(const AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}
	if (Actor->ActorHasTag(PassThroughTag))
	{
		return true;
	}
	return Actor->FindComponentByClass<UDreamWindPassThroughComponent>() != nullptr;
}
