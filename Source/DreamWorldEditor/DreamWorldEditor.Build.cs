// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * Editor side of the world clock and the sky: the time-of-day slider in the level editor toolbar, and the
 * DreamDynamicWorldSetup commandlet that creates the plugin's shared assets and its sky test level.
 */
public class DreamWorldEditor : ModuleRules
{
	public DreamWorldEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DreamDynamicWorld",
			"DreamSky",
			"DreamWeather",
			"GameplayTags",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Projects",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"AssetRegistry",
			"DreamWind",
			"DreamWindEditor",
		});
	}
}
