// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * Water the weather fills: water bodies whose level rises with the rain and falls back after, turning muddy and
 * clearing again, with the level's offset, its bands and events, what the view's water is (the shared collection) and
 * the light patterns under the surface. No dependency on the engine's Water plugin: its bodies are driven from the
 * offset by the project (see the README).
 */
public class DreamWater : ModuleRules
{
	public DreamWater(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DreamDynamicWorld",
			"DreamWeather",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
			"JsonUtilities",
		});
	}
}
