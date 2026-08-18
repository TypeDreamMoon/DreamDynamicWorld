// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamWindEditor : ModuleRules
{
	public DreamWindEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DreamWind",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"Projects",
			"Settings",
			"DeveloperSettings",
			"Landscape",
			"RenderCore",
			"RHI",
		});
	}
}
