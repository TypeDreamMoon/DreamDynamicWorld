// Copyright DreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DREAMWIND_API DECLARE_LOG_CATEGORY_EXTERN(LogDreamWind, Log, All);

namespace DreamWind
{
	/** Names shared by the subsystem (writer) and the asset setup / materials (readers). */
	namespace Names
	{
		/** Vector parameters of the Material Parameter Collection, in DW_UnpackParams / DW_UnpackVolume order. */
		inline const FName MPC_P0(TEXT("DreamWind_P0"));
		inline const FName MPC_P1(TEXT("DreamWind_P1"));
		inline const FName MPC_P2(TEXT("DreamWind_P2"));
		inline const FName MPC_V0(TEXT("DreamWind_V0"));
		inline const FName MPC_V1(TEXT("DreamWind_V1"));

		/** Virtual shader directory mapped to <Plugin>/Shaders. */
		inline const TCHAR* ShaderVirtualDirectory = TEXT("/Plugin/DreamWind");
	}
}

class FDreamWindModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
