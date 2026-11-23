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

## Looking at the sky

All of these open a game window on the sky test level (`-OffScreen` puts it off the desktop; it still renders) and
refuse to start while a game runs unless `-Force`. Images land in `<host>/Saved/Screenshots/<platform>/DreamWorld/`,
logs in `<host>/Saved/ClaudeRuns/`.

| Script | |
| --- | --- |
| `Take-DreamSkyShots.ps1` | one shot per hour (`DreamWorld.ShotTour`), and a contact sheet |
| `Invoke-DreamSkyScript.ps1` | runs a `DreamWorld.Script` file (`-Script`) or lines (`-Lines`); `-FixedFps 30` steps the game clock 1/30 s a frame however slow the frames are |
| `Measure-DreamSkyGpu.ps1` | GPU time of the dome, the sky capture and the sky cloud renderer's clouds, per look x weather x view (`ProfileGPU`, median of `-Samples`); a table and a csv |
| `Measure-DreamSkyStability.ps1` | flies the view at `-Speeds` (m/s) while saving every frame, then counts the frames where the sky jumps (`measure_frame_change.py`) |

`DreamWorld.Script <file>` runs one line at a time: a console command, or `wait <frames>`, `settle [frames]` (until
no shader compiles, then that many frames), `time <hours>`, `aim <pitch> [yaw]`, `move <x> <y> [z]`, `rise <dz>`,
`home`, `fly <m/s> [heading] [climb m/s]`, `shot <name>`, `frames <name> <count>`, `mark <label>`, `quit`.

`time` stops the clock (it sets the hour paused), and `settle` and `wait` only let frames go by: after a `time` no game
hour passes, whatever `DreamWorld.DayLength` says. For weather to build up over hours (snow lying, rime, puddles, the
surfaces' memory), let the clock run: `DreamWorld.Pause 0`, `wait <frames>`, `DreamWorld.Pause 1` (as
`Tools/Showcase/tour_cold.dws` does).

```powershell
pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyGpu.ps1 -OffScreen
pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyGpu.ps1 -Modes Painted -Weathers WP_Cloudy,WP_Storm -Views Up=35 -OffScreen
pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyStability.ps1 -Modes Toon -Speeds 0,150,600 -OffScreen
```

The GPU numbers are only as quiet as the GPU: another editor or game on it shows up in them.
