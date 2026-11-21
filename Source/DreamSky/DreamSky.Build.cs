// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The sky: a lighting director that turns the sun, the moon and a style into lights, sky light, fog and
 * exposure, a stylized sky dome (gradient, sun and moon disks, stars, high cloud) drawn by a DreamShader material
 * that reads the shared parameter collection, and the sky cloud renderer that draws the cumulus into the view.
 */
public class DreamSky : ModuleRules
{
	public DreamSky(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DreamDynamicWorld",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI",
		});

		if (Target.bBuildEditor)
		{
			// (The editor's notice when the clouds cannot be drawn.)
			PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		}
	}
}
