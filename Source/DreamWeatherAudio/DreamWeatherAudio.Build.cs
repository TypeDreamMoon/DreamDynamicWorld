// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The weather's sound: wind, rain and thunder around the listener, muffled and placed by the view probe, and the
 * ambience of a place (birds by day, insects by night) driven by the hour, the weather, the wind and the season.
 * Every sound sits in a swappable slot (project settings or a sound set asset); the plugin ships the MetaSound
 * graphs that play them and no recordings of its own.
 */
public class DreamWeatherAudio : ModuleRules
{
	public DreamWeatherAudio(ReadOnlyTargetRules Target) : base(Target)
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
			"DreamWeather",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AudioExtensions",
			"DreamWind",
			"DreamWeatherFX",
			"MetasoundEngine",
			"SignalProcessing",
		});
	}
}
