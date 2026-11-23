// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindModule.h"
#include "DreamWorldParameters.h"

DEFINE_LOG_CATEGORY(LogDreamWind);

FDreamWindEditorHooks::FBakeTerrainMap FDreamWindEditorHooks::BakeTerrainMap;

void FDreamWindModule::StartupModule()
{
	// The shader directory (/Plugin/DreamDynamicWorld, with Shared/ for DreamWindShared.h) is mapped by the
	// DreamDynamicWorld core module, which loads first.

	// The wind's vectors in the plugin's shared Material Parameter Collection, in DW_UnpackParams /
	// DW_UnpackVolume / DW_UnpackTerrain order.
	using namespace DreamWind::Names;
	const FName Owner(TEXT("DreamWind"));
	FDreamWorldParameters::RegisterVector(Owner, MPC_P0, FLinearColor(1.0f, 0.0f, 0.0f, 0.0f));
	for (const FName& Name : { MPC_P1, MPC_P2, MPC_V0, MPC_V1, MPC_V2, MPC_V3, MPC_T0, MPC_T1, MPC_T2, MPC_L1Near, MPC_L1Far })
	{
		FDreamWorldParameters::RegisterVector(Owner, Name, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	}
	// (shadow strength, soft band cm, steer scale, speed-up strength)
	FDreamWorldParameters::RegisterVector(Owner, MPC_L1Params, FLinearColor(0.9f, 100.0f, 1.0f, 1.0f));
}

void FDreamWindModule::ShutdownModule()
{
	FDreamWorldParameters::UnregisterOwner(TEXT("DreamWind"));
}

IMPLEMENT_MODULE(FDreamWindModule, DreamWind)
