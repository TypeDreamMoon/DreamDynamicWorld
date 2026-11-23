// Copyright DreamMoon. All Rights Reserved.

#include "DreamWeatherFog.h"
#include "DreamWorldParameters.h"
#include "Modules/ModuleManager.h"

namespace DreamWeatherFX::Module
{
	const FName Owner(TEXT("DreamWeatherFX"));
}

class FDreamWeatherFXModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FDreamWorldParameters::RegisterVector(DreamWeatherFX::Module::Owner, DreamWeatherFX::Names::MPC_FogDrift, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}

	virtual void ShutdownModule() override
	{
		FDreamWorldParameters::UnregisterOwner(DreamWeatherFX::Module::Owner);
	}
};

IMPLEMENT_MODULE(FDreamWeatherFXModule, DreamWeatherFX)
