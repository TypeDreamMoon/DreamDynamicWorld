// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * Sequencer tracks for the world: a time-of-day track that drives the world clock, and a weather track whose
 * sections force a preset (with transitions from the sections' easing), shape channels with curves, or strike
 * lightning on keys. The tracks keep no state of their own: they drive the clock through its external drive and
 * the weather through its modifier stack, the one place that state lives.
 */
public class DreamWorldSequencer : ModuleRules
{
	public DreamWorldSequencer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"MovieScene",
			"MovieSceneTracks",
			"DreamDynamicWorld",
			"DreamWeather",
		});

		// The tests play a level sequence.
		PrivateDependencyModuleNames.AddRange(new string[] { "LevelSequence" });
	}
}
