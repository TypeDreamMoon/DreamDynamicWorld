// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamDynamicWorldSetupCommandlet.generated.h"

/**
 * UnrealEditor-Cmd.exe <project> -run=DreamDynamicWorldSetup [-TestMap] [-CloudTestMap] [-Force] [-ForceStyle] [-ForcePresets] [-ForceCloudNoise]
 *
 * Creates (or repairs) the plugin's shared assets: the Material Parameter Collection with every subsystem's
 * parameters, the wind's render targets and presets, the default sky style, the weather presets, the
 * weather map and the clouds' 3D noise (generated again when its generator changed, or with -ForceCloudNoise).
 * -TestMap also writes the sky test level (/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest), -CloudTestMap the cloud
 * volume test level (/DreamDynamicWorld/Cloud/Maps/Lvl_DreamCloudVolumeTest, with its merged mesh); -Force
 * rewrites them when they exist. -ForceStyle writes the class defaults over an existing sky style, -ForcePresets
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
