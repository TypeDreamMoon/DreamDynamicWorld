// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudCarverComponent.h"
#include "DreamCloudSubsystem.h"

#include "Engine/World.h"

void UDreamCloudCarverComponent::OnRegister()
{
	Super::OnRegister();
	LastCarved.Reset();
	if (UDreamCloudSubsystem* Clouds = GetWorld() ? GetWorld()->GetSubsystem<UDreamCloudSubsystem>() : nullptr)
	{
		Clouds->RegisterCarver(this);
	}
}

void UDreamCloudCarverComponent::OnUnregister()
{
	if (UDreamCloudSubsystem* Clouds = GetWorld() ? GetWorld()->GetSubsystem<UDreamCloudSubsystem>() : nullptr)
	{
		Clouds->UnregisterCarver(this);
	}
	Super::OnUnregister();
}
