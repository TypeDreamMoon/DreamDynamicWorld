// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/*
 * The game target of the DreamDynamicWorld test host. The tests only build the editor target; this one is
 * here so the host is an ordinary, complete project and a cooked-game check can be added later without
 * touching the template.
 */
public class DreamDynamicWorldTestHostTarget : TargetRules
{
	public DreamDynamicWorldTestHostTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("DreamDynamicWorldTestHost");
	}
}
