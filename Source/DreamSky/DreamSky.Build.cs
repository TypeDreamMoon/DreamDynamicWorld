// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The sky: a lighting director that turns the sun, the moon and a style into lights, sky light, fog and
 * exposure, and a stylized sky dome (gradient, sun and moon disks, stars, toon clouds) drawn by a
 * DreamShader material that reads the shared parameter collection.
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
			"RenderCore",
			"Renderer",
			"RHI",
		});
	}
}
