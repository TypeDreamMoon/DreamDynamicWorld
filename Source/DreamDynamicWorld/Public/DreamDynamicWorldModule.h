// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMDYNAMICWORLD_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamWorld, Log, All);

namespace DreamWorld::Names
{
	/** The plugin every subsystem ships in. */
	inline const TCHAR* PluginName = TEXT("DreamDynamicWorld");

	/** Virtual shader directory mapped to <Plugin>/Shaders. Shaders/Shared is also registered as a shared directory. */
	inline const TCHAR* ShaderVirtualDirectory = TEXT("/Plugin/DreamDynamicWorld");

	/** Vector parameters of the shared collection written by the world clock. */
	inline const FName MPC_Time(TEXT("DDW_Time"));
	inline const FName MPC_SunDir(TEXT("DDW_SunDir"));
	inline const FName MPC_MoonDir(TEXT("DDW_MoonDir"));
	/** Where the view is (FDreamViewOcclusion): indoors 0..1, roof overhead 0..1, -, valid. */
	inline const FName MPC_Interior(TEXT("DDW_Interior"));
}

class FDreamDynamicWorldModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
