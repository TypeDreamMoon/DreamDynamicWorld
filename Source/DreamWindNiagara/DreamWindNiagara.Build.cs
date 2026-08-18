// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;

public class DreamWindNiagara : ModuleRules
{
	public DreamWindNiagara(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DreamWind",
			"Niagara",
			"NiagaraCore",
			"NiagaraShader",
			"VectorVM",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
		});
	}
}
