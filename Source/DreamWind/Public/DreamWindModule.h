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
		inline const FName MPC_V2(TEXT("DreamWind_V2"));
		inline const FName MPC_V3(TEXT("DreamWind_V3"));
		inline const FName MPC_T0(TEXT("DreamWind_T0"));
		inline const FName MPC_T1(TEXT("DreamWind_T1"));
		inline const FName MPC_T2(TEXT("DreamWind_T2"));

		/** The plugin this module ships in (wind is one of its subsystems). */
		inline const TCHAR* PluginName = TEXT("DreamDynamicWorld");

		/** Virtual shader directory mapped to <Plugin>/Shaders. */
		inline const TCHAR* ShaderVirtualDirectory = TEXT("/Plugin/DreamDynamicWorld");
	}
}

class ADreamWindGlobal;

/** Hooks the editor module fills in so runtime objects can offer editor-only actions. */
struct DREAMWIND_API FDreamWindEditorHooks
{
	DECLARE_DELEGATE_OneParam(FBakeTerrainMap, ADreamWindGlobal* /*Actor*/);
	/** Bound by DreamWindEditor: bake (or re-bake) the terrain map of a level actor. */
	static FBakeTerrainMap BakeTerrainMap;
};

class FDreamWindModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
