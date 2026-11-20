// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * Local cloud volumes: an actor that turns a mesh or a few primitives into a cloud standing in the level, baked to a
 * signed distance field and drawn by a translucent marching material. The marching and lighting it shares with the
 * sky's clouds live in Shaders/Private/DreamCloudMarch.ush and DreamCloudLight.ush.
 */
public class DreamCloud : ModuleRules
{
	public DreamCloud(ReadOnlyTargetRules Target) : base(Target)
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
			"DreamDynamicWorld",
			"MeshDescription",
			"StaticMeshDescription",
			"RenderCore",
			"RHI",
			"Renderer",
		});
	}
}
