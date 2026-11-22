// Copyright DreamMoon. All Rights Reserved.

#include "DreamAtmosphereModule.h"
#include "DreamWorldParameters.h"

DEFINE_LOG_CATEGORY(LogDreamAtmosphere);

namespace DreamAtmosphere::Module
{
	const FName Owner(TEXT("DreamAtmosphere"));
}

void FDreamAtmosphereModule::StartupModule()
{
	using namespace DreamAtmosphere::Names;
	FDreamWorldParameters::RegisterVector(DreamAtmosphere::Module::Owner, MPC_AtmosphereSun, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(DreamAtmosphere::Module::Owner, MPC_AtmosphereMoon, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
}

void FDreamAtmosphereModule::ShutdownModule()
{
	FDreamWorldParameters::UnregisterOwner(DreamAtmosphere::Module::Owner);
}

IMPLEMENT_MODULE(FDreamAtmosphereModule, DreamAtmosphere)
