// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The weather: a state of named channels chosen over time by a selector (a pure function of the world time
 * and seed), eased per channel, overridden by a stack of modifiers and by zones in the world, queried at any
 * point, and drawn into the weather map the sky reads. It drives the wind and hands the sky what it needs
 * through the world clock's environment state.
 */
public class DreamWeather : ModuleRules
{
	public DreamWeather(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"GameplayTags",
			"DreamDynamicWorld",
			"DreamWind",
			"UMG",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"Renderer",
			"Json",
			"JsonUtilities",
		});
	}
}
