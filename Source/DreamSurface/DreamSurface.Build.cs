// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * What the weather leaves on the ground: surfaces that get wet, fill their hollows, gather snow and dust as the weather
 * runs, and dry, drain, melt and clear after it. The state is the integral of the weather's own timeline, so any moment
 * gives the same answer wherever and however often it is asked. Its formula is Shaders/Shared/DreamSurfaceShared.h,
 * the same for a gameplay query on the CPU and the camera-following surface map on the GPU.
 */
public class DreamSurface : ModuleRules
{
	public DreamSurface(ReadOnlyTargetRules Target) : base(Target)
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
			"DreamWeather",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTags",
			"ImageCore",
			"RenderCore",
			"RHI",
			"Renderer",
		});
	}
}
