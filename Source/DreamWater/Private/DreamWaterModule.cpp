// Copyright DreamMoon. All Rights Reserved.

#include "DreamWaterSubsystem.h"
#include "DreamWorldParameters.h"
#include "Modules/ModuleManager.h"

namespace DreamWater::Module
{
	const FName Owner(TEXT("DreamWater"));
}

class FDreamWaterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// The view's water (see DreamWater::Names::MPC_Water): no body by default.
		FDreamWorldParameters::RegisterVector(DreamWater::Module::Owner, DreamWater::Names::MPC_Water, FLinearColor(-1.0e6f, 0.0f, 0.0f, 0.0f));
	}

	virtual void ShutdownModule() override
	{
		FDreamWorldParameters::UnregisterOwner(DreamWater::Module::Owner);
	}
};

IMPLEMENT_MODULE(FDreamWaterModule, DreamWater)
