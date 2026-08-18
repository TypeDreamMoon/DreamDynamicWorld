// Copyright DreamMoon. All Rights Reserved.

#include "DreamWindModule.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogDreamWind);

FDreamWindEditorHooks::FBakeTerrainMap FDreamWindEditorHooks::BakeTerrainMap;

void FDreamWindModule::StartupModule()
{
	// /Plugin/DreamWind/Shared/DreamWindShared.h and /Plugin/DreamWind/Private/*.usf
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamWind"));
	if (Plugin.IsValid())
	{
		const FString ShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		if (!AllShaderSourceDirectoryMappings().Contains(DreamWind::Names::ShaderVirtualDirectory))
		{
			AddShaderSourceDirectoryMapping(DreamWind::Names::ShaderVirtualDirectory, ShaderDir);
			// Shaders/Shared holds .h files compiled by both C++ and HLSL; the shader system only
			// accepts that extension inside a registered shared directory (Niagara does the same).
			AddShaderSourceSharedVirtualDirectory(FString(DreamWind::Names::ShaderVirtualDirectory) + TEXT("/Shared/"));
		}
	}
	else
	{
		UE_LOG(LogDreamWind, Error, TEXT("DreamWind plugin descriptor not found; shader directory mapping skipped."));
	}
}

void FDreamWindModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FDreamWindModule, DreamWind)
