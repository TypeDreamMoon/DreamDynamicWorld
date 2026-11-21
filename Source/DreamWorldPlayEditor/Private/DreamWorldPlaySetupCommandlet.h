// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamWorldPlaySetupCommandlet.generated.h"

/**
 * UnrealEditor-Cmd.exe <project> -run=DreamWorldPlaySetup [-ShowcaseMap] [-Force] [-ForceAudio]
 *
 * Creates (or repairs) the assets of the gameplay-facing parts: under /DreamDynamicWorld/Audio the weather's voice
 * graph (MS_DreamWeatherVoice), the forest ambience graph and its preset (MS_DreamAmbienceForest, DAP_DreamForest)
 * and the sound classes SC_DreamWeather and SC_DreamOutdoor (-ForceAudio builds the graphs and the preset again).
 * -ShowcaseMap also writes the gameplay showcase level (/DreamDynamicWorld/Play/Maps/Lvl_DreamWorldPlay; -Force
 * rewrites it when it exists). Run DreamDynamicWorldSetup first (the presets and the sky style). Exit code 0 on success.
 */
UCLASS()
class UDreamWorldPlaySetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamWorldPlaySetupCommandlet();
	virtual int32 Main(const FString& Params) override;
};
