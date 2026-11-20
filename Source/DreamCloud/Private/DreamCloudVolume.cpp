// Copyright DreamMoon. All Rights Reserved.

#include "DreamCloudVolume.h"
#include "DreamCloudVolumeComponent.h"

ADreamCloudVolume::ADreamCloudVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Cloud = CreateDefaultSubobject<UDreamCloudVolumeComponent>(TEXT("Cloud"));
	RootComponent = Cloud;
}
