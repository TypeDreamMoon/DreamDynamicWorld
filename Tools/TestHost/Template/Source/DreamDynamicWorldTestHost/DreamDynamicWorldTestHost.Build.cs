// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The test host's only module, and it does nothing. A project needs a primary game module to be a code
 * project, and a code project is what gives the host its own editor target -- which is what builds the
 * plugins in Plugins/. It depends on no more than a module has to, so a dependency the plugin forgot to
 * declare cannot be satisfied silently from here.
 */
public class DreamDynamicWorldTestHost : ModuleRules
{
	public DreamDynamicWorldTestHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
