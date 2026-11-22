// Copyright DreamMoon. All Rights Reserved.

#include "DreamSkyModule.h"
#include "DreamWorldParameters.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/CoreRedirects.h"

DEFINE_LOG_CATEGORY(LogDreamSky);

void FDreamSkyModule::StartupModule()
{
	// Dream Sky's settings that were named for the engine's volumetric clouds, kept by the sky cloud renderer, and the
	// choice of what drew the clouds, read once into the renderer's look: a level saved with the old names reads them.
	// (This module starts before the object system, and a plugin's own config is not read for redirects: they go in as
	// soon as a loading phase ends with the object system up, well before any level loads.)
	auto AddRedirects = []()
	{
		TArray<FCoreRedirect> Redirects;
		Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Script/DreamSky.DreamSky.Clouds"), TEXT("/Script/DreamSky.DreamSky.Clouds_DEPRECATED"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Script/DreamSky.DreamSky.VolumetricCloudMaterial"), TEXT("/Script/DreamSky.DreamSky.VolumetricCloudMaterial_DEPRECATED"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Script/DreamSky.DreamSky.VolumetricSunScattering"), TEXT("/Script/DreamSky.DreamSky.CloudSunScattering"));
		Redirects.Emplace(ECoreRedirectFlags::Type_Property, TEXT("/Script/DreamSky.DreamSky.VolumetricShadowTracingDistance"), TEXT("/Script/DreamSky.DreamSky.CloudShadowTracingDistance"));
		FCoreRedirects::AddRedirectList(Redirects, TEXT("DreamSky"));
	};
	if (FCoreRedirects::IsInitialized())
	{
		AddRedirects();
	}
	else
	{
		RedirectsHandle = IPluginManager::Get().OnLoadingPhaseComplete().AddLambda([this, AddRedirects](ELoadingPhase::Type, bool)
		{
			if (FCoreRedirects::IsInitialized() && RedirectsHandle.IsValid())
			{
				AddRedirects();
				IPluginManager::Get().OnLoadingPhaseComplete().Remove(RedirectsHandle);
				RedirectsHandle.Reset();
			}
		});
	}

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
	FDreamWorldParameters::RegisterVector(Owner, MPC_StarPole, FLinearColor(0.0f, 0.0f, 1.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_StarEquinox, FLinearColor(1.0f, 0.0f, 0.0f, 0.8f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Cloud0, FLinearColor(0.45f, 0.08f, 1.0f, 3.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Cloud1, FLinearColor(0.0f, 0.0f, 0.25f, 0.15f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Cloud2, FLinearColor(0.6f, 0.5f, 0.08f, 0.55f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Cloud3, FLinearColor(0.5585f, 0.3f, 400000.0f, 250000.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_CloudLit, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_CloudShadow, FLinearColor(0.6f, 0.65f, 0.75f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_CloudLayer, FLinearColor(150000.0f, 0.0f, 0.6f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SkyExtra, FLinearColor(0.6f, 0.5f, 0.1f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SkyHaze, FLinearColor(0.5f, 0.5f, 0.5f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_SkyModClouds, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Aurora, FLinearColor(0.0f, 1.0f, 1.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AuroraLow, FLinearColor(0.15f, 1.0f, 0.45f, 250.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AuroraHigh, FLinearColor(0.75f, 0.2f, 0.55f, 70.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_NightGlow, FLinearColor(0.0f, 0.0f, 0.0f, 3.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_LightPollution, FLinearColor(0.0f, 0.0f, 0.0f, 0.18f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_Eclipse, FLinearColor(0.0f, 1000.0f, 0.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_LunarEclipse, FLinearColor(0.0f, 1000.0f, 0.0f, 2.7f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_MainLight, FLinearColor(0.0f, 0.0f, 1.0f, 10.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_MainLightColor, FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AmbientSky, FLinearColor(0.3f, 0.4f, 0.6f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AmbientEquator, FLinearColor(0.3f, 0.3f, 0.35f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_AmbientGround, FLinearColor(0.1f, 0.1f, 0.1f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, MPC_DayPhase, FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
}

void FDreamSkyModule::ShutdownModule()
{
	if (RedirectsHandle.IsValid())
	{
		IPluginManager::Get().OnLoadingPhaseComplete().Remove(RedirectsHandle);
		RedirectsHandle.Reset();
	}
	FDreamWorldParameters::UnregisterOwner(TEXT("DreamSky"));
}

IMPLEMENT_MODULE(FDreamSkyModule, DreamSky)
