// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamWindSetupCommandlet.generated.h"

/**
 * UnrealEditor-Cmd.exe <project> -run=DreamWindSetup
 * Creates (or repairs) the wind Material Parameter Collection and the volume render target at the
 * paths in Project Settings > Dream Wind, and saves them. Exit code 0 on success.
 */
UCLASS()
class DREAMWINDEDITOR_API UDreamWindSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamWindSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
