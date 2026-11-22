// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMATMOSPHERE_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamAtmosphere, Log, All);

/** The atmosphere's vectors in the plugin's shared Material Parameter Collection. */
namespace DreamAtmosphere::Names
{
	inline const FName MPC_AtmosphereSun(TEXT("DDW_AtmosphereSun"));    // the sun outside the air: illuminance x colour (rgb), 1 while the sky table is built (w)
	inline const FName MPC_AtmosphereMoon(TEXT("DDW_AtmosphereMoon"));  // the moon outside the air, its phase in: illuminance x colour (rgb), -
}

class FDreamAtmosphereModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
