// Copyright DreamMoon. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

/*
 * The editor target of the DreamDynamicWorld test host -- the one that is built and launched.
 *
 * The shared build environment is the point: the installed engine's modules are linked as they are, and
 * only this project's module and its project plugins (each with Binaries/Intermediate in its own worktree)
 * are compiled. It is the default already; it is written out so nobody "fixes" it to Unique, which would
 * try to compile the whole engine again.
 */
public class DreamDynamicWorldTestHostEditorTarget : TargetRules
{
	public DreamDynamicWorldTestHostEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		BuildEnvironment = TargetBuildEnvironment.Shared;
		ExtraModuleNames.Add("DreamDynamicWorldTestHost");
	}
}
