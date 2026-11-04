// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamDynamicWorldSetupCommandlet.generated.h"

/**
 * UnrealEditor-Cmd.exe <project> -run=DreamDynamicWorldSetup [-TestMap] [-Force] [-ForceStyle] [-ForcePresets]
 *
 * Creates (or repairs) the plugin's shared assets: the Material Parameter Collection with every subsystem's
 * parameters, the wind's render targets and presets, the default sky style, the weather presets and the
 * weather map. -TestMap also writes the sky test level (/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest); -Force
 * rewrites it when it exists. -ForceStyle writes the class defaults over an existing sky style, -ForcePresets
 * rewrites the shipped weather presets from their table here (both for after the defaults changed in C++).
 * Exit code 0 on success.
 */
UCLASS()
class UDreamDynamicWorldSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamDynamicWorldSetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
