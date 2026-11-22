// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The air's scattering: the sky's colour, the light's colour through the air and the haze over the distance, from one
 * physical model (Rayleigh, Mie, ozone, multiple scattering) in Shaders/Shared/DreamAtmosphereShared.h, the same for the
 * GPU's lookup tables and the CPU's values. The sky module grades it into the style's look.
 */
public class DreamAtmosphere : ModuleRules
{
	public DreamAtmosphere(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"DreamDynamicWorld",
			"DreamWind",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI",
		});
	}
}
