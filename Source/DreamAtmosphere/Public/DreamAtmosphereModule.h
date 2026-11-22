// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMATMOSPHERE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamAtmosphere, Log, All);

class FDreamAtmosphereModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
