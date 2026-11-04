# DreamDynamicWorld test host

A minimal project on a **stock installed engine** that exists to build DreamDynamicWorld and run its tests,
away from any project you actually work in.

## Why

- **Work in progress never reaches the working project.** The host's `Plugins/DreamDynamicWorld` is a separate
  git worktree (on `dev` by default) with its own `Binaries/` and `Intermediate/`.
- **"The project builds" is not "the plugin builds".** A working project may build modules as one unity blob;
  the host builds the plugin file by file, so a header that defines a function twice, or an include that only
  a neighbour provided, shows up here. It also runs on the stock engine, not a modified one.
- **The working project's editor stays open.** Building links the plugin's DLLs; a running editor holds them.

## Layout

```
DreamDynamicWorldTestHost/
  DreamDynamicWorldTestHost.uproject       from Template/
  Config/DefaultEngine.ini                 from Template/ -- renderer settings shared with the DreamShader host (warm shader cache)
  Config/DefaultGame.ini                   from Template/
  Source/                                  from Template/ -- targets and an empty game module
  Plugins/DreamDynamicWorld/               worktree of this repository
  Plugins/DreamShader/                     worktree of DreamShader, detached at main -- generates the plugin's materials
  Plugins/DreamFX/                         (with -WithDreamFX) worktree of DreamFX, detached at main
  .dreamdynamicworld-testhost.json         what the host was made from
```

The template lives here, in `Tools/TestHost/Template/`; UnrealBuildTool only looks for rules under a plugin's
`Source/`, so no project picks up the template's `.Build.cs` / `.Target.cs` files.

## Creating it

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamDynamicWorldTestHost.ps1 -Root <host dir> -WhatIf
pwsh -NoProfile -File Tools\TestHost\New-DreamDynamicWorldTestHost.ps1 -Root <host dir>
```

| Parameter | Default | |
| --- | --- | --- |
| `-Root` | `$env:DREAMDYNAMICWORLD_TEST_HOST` | The host directory |
| `-Branch` / `-StartPoint` | `dev` / `main` | The plugin worktree's branch, created at the start point when missing |
| `-DreamShaderRepo` / `-DreamFXRepo` | sibling folders of this repository | Where the tool plugins' repositories are |
| `-WithDreamFX` | off | Also add DreamFX, which regenerates the plugin's effects under `DFX/` (rain, snow, wind leaves) |
| `-ToolRef` | `main` | What the tool worktrees follow, detached |
| `-EngineRoot` | the installed engine the `.uproject` asks for (registry, then the Epic launcher's list) | |
| `-Force` / `-WhatIf` | off | Replace differing template files (old ones kept as `.bak-<stamp>`) / report only |

The script never removes, resets or switches an existing worktree. Exit codes: `0` matches the template, `1` a
check failed, `2` usable but different from the template.

## Building and testing

```powershell
pwsh -NoProfile -File <host>\Plugins\DreamDynamicWorld\Tools\TestHost\Invoke-DreamDynamicWorldTests.ps1
```

Builds the editor target, then runs `DreamWind.`, `DreamWorld.` and `DreamSky.` tests headlessly (`-nullrhi`, no
window, below-normal priority). `-NoBuild` skips the build, `-Filter` picks other prefixes. The report lands in
`<host>/Saved/DreamDynamicWorldTests/<stamp>/` (`index.json`, `editor.log`). Exit codes: `0` all passed, `1` the
build failed or nothing ran, `2` a test failed.

The shared assets (the parameter collection, the default sky style, the sky test level) come from

```powershell
UnrealEditor-Cmd.exe <host>\DreamDynamicWorldTestHost.uproject -run=DreamDynamicWorldSetup -TestMap -unattended -nullrhi
```

and the plugin's materials from DreamShader's commandlet (`-run=DreamShader compile`).
