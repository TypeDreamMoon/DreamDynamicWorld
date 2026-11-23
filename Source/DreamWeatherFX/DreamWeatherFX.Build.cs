// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The weather's effects around the view: rain and snow drawn along the fall paths the weather traces. Kept out
 * of DreamWeather, which loads early (it owns a global shader) and must not pull Niagara in with it.
 */
public class DreamWeatherFX : ModuleRules
{
	public DreamWeatherFX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"DreamDynamicWorld",
			"DreamWeather",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DreamWind",
			"DreamSky",
			"Niagara",
			"NiagaraCore",
			"GameplayTags",
		});
	}
}
