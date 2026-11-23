// Copyright DreamMoon. All Rights Reserved.

#include "DreamSurfaceModule.h"
#include "DreamWorldParameters.h"

DEFINE_LOG_CATEGORY(LogDreamSurface);

namespace DreamSurface::Module
{
	const FName Owner(TEXT("DreamSurface"));
}

void FDreamSurfaceModule::StartupModule()
{
	using namespace DreamSurface::Names;
	const FLinearColor Off(0.0f, 0.0f, 0.0f, 0.0f);
	FDreamWorldParameters::RegisterVector(DreamSurface::Module::Owner, MPC_SurfaceNear, Off);
	FDreamWorldParameters::RegisterVector(DreamSurface::Module::Owner, MPC_SurfaceFar, Off);
	FDreamWorldParameters::RegisterVector(DreamSurface::Module::Owner, MPC_SurfaceView, Off);
	FDreamWorldParameters::RegisterVector(DreamSurface::Module::Owner, MPC_SurfaceViewIce, Off);
	FDreamWorldParameters::RegisterVector(DreamSurface::Module::Owner, MPC_SurfaceParams, FLinearColor(30.0f, 30.0f, 50.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(DreamSurface::Module::Owner, MPC_SurfaceTrails, Off);
}

void FDreamSurfaceModule::ShutdownModule()
{
	FDreamWorldParameters::UnregisterOwner(DreamSurface::Module::Owner);
}

IMPLEMENT_MODULE(FDreamSurfaceModule, DreamSurface)
