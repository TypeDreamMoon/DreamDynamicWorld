// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMSURFACE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamSurface, Log, All);

class FDreamSurfaceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
