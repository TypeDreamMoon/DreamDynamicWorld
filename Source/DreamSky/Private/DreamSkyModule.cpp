// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyModule.h"
#include "DreamWorldParameters.h"

DEFINE_LOG_CATEGORY(LogDreamSky);

void FDreamSkyModule::StartupModule()
{
	using namespace DreamSky::Names;
	const FName Owner(TEXT("DreamSky"));
	const FLinearColor Black(0.0f, 0.0f, 0.0f, 0.0f);
	FDreamWorldParameters::RegisterVector(Owner, MPC_Zenith, FLinearColor(0.2f, 0.4f, 0.75f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Horizon, FLinearColor(0.6f, 0.72f, 0.85f, 0.5f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Ground, FLinearColor(0.2f, 0.22f, 0.25f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SunHalo, FLinearColor(1.0f, 0.9f, 0.7f, 8.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SunDisk, FLinearColor(20.0f, 19.0f, 17.0f, 1.2f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_MoonDisk, FLinearColor(1.0f, 1.0f, 1.0f, 1.5f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_MoonHalo, FLinearColor(0.1f, 0.12f, 0.2f, 16.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Stars, Black);
	FDreamWorldParameters::RegisterVector(Owner, MPC_Cloud0, FLinearColor(0.45f, 0.08f, 1.0f, 3.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Cloud1, FLinearColor(0.0f, 0.0f, 0.25f, 0.15f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_CloudLit, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_CloudShadow, FLinearColor(0.6f, 0.65f, 0.75f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_CloudLayer, FLinearColor(150000.0f, 0.0f, 0.6f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SkyExtra, FLinearColor(0.6f, 0.5f, 0.1f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SkyHaze, FLinearColor(0.5f, 0.5f, 0.5f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_MainLight, FLinearColor(0.0f, 0.0f, 1.0f, 10.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_MainLightColor, FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AmbientSky, FLinearColor(0.3f, 0.4f, 0.6f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AmbientEquator, FLinearColor(0.3f, 0.3f, 0.35f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AmbientGround, FLinearColor(0.1f, 0.1f, 0.1f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_DayPhase, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
}

void FDreamSkyModule::ShutdownModule()
{
	FDreamWorldParameters::UnregisterOwner(TEXT("DreamSky"));
}

IMPLEMENT_MODULE(FDreamSkyModule, DreamSky)
