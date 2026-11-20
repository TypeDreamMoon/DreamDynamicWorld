// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DreamCloudVolume.generated.h"

class UDreamCloudVolumeComponent;

/**
 * A cloud standing in the level: drag it in, give it a mesh or a few primitives, and it becomes a cloud of that shape
 * (see UDreamCloudVolumeComponent). Move, turn and scale it freely: the cloud follows.
 */
UCLASS(ClassGroup = (DreamDynamicWorld), meta = (DisplayName = "Dream Cloud Volume"))
class DREAMCLOUD_API ADreamCloudVolume : public AActor
{
	GENERATED_BODY()

public:
	ADreamCloudVolume();

	UFUNCTION(BlueprintCallable, Category = "Dream Cloud")
	UDreamCloudVolumeComponent* GetCloud() const { return Cloud; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cloud", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDreamCloudVolumeComponent> Cloud;
};
