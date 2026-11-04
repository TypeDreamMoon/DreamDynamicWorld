// Copyright DreamMoon. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DreamWind : ModuleRules
{
	public DreamWind(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The analytic wind model lives in Shaders/Shared/DreamWindShared.h, a single source that is
		// compiled both as HLSL (through the /Plugin/DreamDynamicWorld virtual directory) and as C++ (through
		// DreamWindHlslShim.h). Exposing the directory publicly lets DreamWindNiagara reuse it.
		PublicIncludePaths.Add(Path.Combine(PluginDirectory, "Shaders", "Shared"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"RenderCore",
			"RHI",
			"DreamDynamicWorld",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Renderer",
		});
	}
}
