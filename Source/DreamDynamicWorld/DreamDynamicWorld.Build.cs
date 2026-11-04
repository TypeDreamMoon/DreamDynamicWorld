// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The core every DreamDynamicWorld subsystem builds on: the world clock and the sun and moon it moves,
 * the one Material Parameter Collection all of them write, a frame scheduler, snapshots for save games,
 * and the shader directory mapping of the plugin.
 */
public class DreamDynamicWorld : ModuleRules
{
	public DreamDynamicWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"RenderCore",
		});
	}
}
