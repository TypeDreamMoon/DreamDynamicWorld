// Copyright DreamMoon. All Rights Reserved.

#include "DreamDynamicWorldModule.h"
#include "DreamWorldConfig.h"
#include "DreamWorldParameters.h"
#include "DreamWorldSubsystem.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogDreamWorld);

void FDreamDynamicWorldModule::StartupModule()
{
	// /Plugin/DreamDynamicWorld/Private/*.usf and /Plugin/DreamDynamicWorld/Shared/*.h. This module loads
	// first (PostConfigInit, listed first in the descriptor), so every other module of the plugin finds the
	// mapping in place.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DreamWorld::Names::PluginName);
	if (Plugin.IsValid())
	{
		if (!AllShaderSourceDirectoryMappings().Contains(DreamWorld::Names::ShaderVirtualDirectory))
		{
			AddShaderSourceDirectoryMapping(DreamWorld::Names::ShaderVirtualDirectory, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
			// Shaders/Shared holds .h files compiled by both C++ and HLSL; the shader system accepts that
			// extension only inside a registered shared directory.
			AddShaderSourceSharedVirtualDirectory(FString(DreamWorld::Names::ShaderVirtualDirectory) + TEXT("/Shared/"));
		}
	}
	else
	{
		UE_LOG(LogDreamWorld, Error, TEXT("%s plugin descriptor not found; shader directory mapping skipped."), DreamWorld::Names::PluginName);
	}

	const FName Owner(TEXT("DreamDynamicWorld"));
	FDreamWorldParameters::RegisterVector(Owner, DreamWorld::Names::MPC_Time, FLinearColor(0.5f, 0.5f, 0.0f, 0.0f));
	FDreamWorldParameters::RegisterVector(Owner, DreamWorld::Names::MPC_SunDir, FLinearColor(0.0f, 0.0f, 1.0f, 1.0f));
	FDreamWorldParameters::RegisterVector(Owner, DreamWorld::Names::MPC_MoonDir, FLinearColor(0.0f, 0.0f, -1.0f, 0.5f));
	FDreamWorldParameters::RegisterVector(Owner, DreamWorld::Names::MPC_Interior, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));

	// A config keeps the time of day (to the half minute).
	UDreamWorldConfig::FWorldStateProvider Time;
	Time.Capture = [](UWorld& World)
	{
		const UDreamWorldSubsystem* Clock = World.GetSubsystem<UDreamWorldSubsystem>();
		return Clock ? FString::Printf(TEXT("%.3f"), Clock->GetTimeOfDay()) : FString();
	};
	Time.Apply = [](UWorld& World, const FString& Value)
	{
		if (UDreamWorldSubsystem* Clock = World.GetSubsystem<UDreamWorldSubsystem>())
		{
			Clock->SetTimeOfDay(FCString::Atof(*Value));
		}
	};
	UDreamWorldConfig::RegisterWorldState(TEXT("TimeOfDay"), MoveTemp(Time));
}

void FDreamDynamicWorldModule::ShutdownModule()
{
	FDreamWorldParameters::UnregisterOwner(TEXT("DreamDynamicWorld"));
	UDreamWorldConfig::UnregisterWorldState(TEXT("TimeOfDay"));
}

IMPLEMENT_MODULE(FDreamDynamicWorldModule, DreamDynamicWorld)
