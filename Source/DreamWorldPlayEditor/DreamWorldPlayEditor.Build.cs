// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * Editor side of the gameplay-facing parts: the DreamWorldPlaySetup commandlet that builds the weather's sound
 * graphs (MetaSound) and sound classes and the gameplay showcase level.
 */
public class DreamWorldPlayEditor : ModuleRules
{
	public DreamWorldPlayEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"GameplayTags",
			"DreamDynamicWorld",
			"DreamSky",
			"DreamWeather",
			"DreamWeatherAudio",
			"DreamWind",
			"MetasoundEngine",
			"MetasoundFrontend",
			"MetasoundEditor",
		});
	}
}
