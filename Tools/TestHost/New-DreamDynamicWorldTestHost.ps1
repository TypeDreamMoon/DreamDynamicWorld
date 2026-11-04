<#
.SYNOPSIS
    Creates or checks the DreamDynamicWorld test host: a minimal project on a stock installed engine whose
    Plugins/DreamDynamicWorld is a git worktree of this repository, with DreamShader (and optionally DreamFX)
    beside it to generate the plugin's materials and effects.

.DESCRIPTION
    Safe to run again at any time. In order:

      1. Checks: git is on PATH, -RepoPath is a git repository, -Root is not inside it, -Root holds no other
         project, and the engine the template's .uproject asks for exists.
      2. Worktrees. Plugins/DreamDynamicWorld on -Branch (created at -StartPoint when missing), and
         Plugins/DreamShader (and Plugins/DreamFX with -WithDreamFX) detached at -ToolRef of their own
         repositories. An existing worktree is reported and never removed, reset or switched.
      3. The template. Missing files are written; matching files are left alone (line endings and a byte-order
         mark do not count); a file that differs is reported and kept, unless -Force, which keeps the old one
         as <name>.bak-<timestamp>.
      4. <Root>/.dreamdynamicworld-testhost.json records what the host was made from.
      5. Prints the commands that build the host and run the tests.

    Exit codes: 0 the host matches the template; 1 a check failed; 2 usable, but it does not match the
    template (files kept without -Force, or -WhatIf found work to do).

.EXAMPLE
    pwsh -NoProfile -File Tools\TestHost\New-DreamDynamicWorldTestHost.ps1 -Root I:\Projects\DreamDynamicWorldTestHost -WhatIf
#>
[CmdletBinding()]
param(
    # The host directory. Defaults to $env:DREAMDYNAMICWORLD_TEST_HOST.
    [string]$Root = $env:DREAMDYNAMICWORLD_TEST_HOST,

    # Any working tree of the DreamDynamicWorld repository. Defaults to the one holding this script.
    [string]$RepoPath,

    # The branch the DreamDynamicWorld worktree has, or is created with.
    [string]$Branch = 'dev',

    # Where -Branch starts when it does not exist yet.
    [string]$StartPoint = 'main',

    # A working tree of the DreamShader repository. Defaults to a DreamShader folder next to this repository.
    [string]$DreamShaderRepo,

    # A working tree of the DreamFX repository, used with -WithDreamFX. Defaults to a sibling folder too.
    [string]$DreamFXRepo,

    # Also add Plugins/DreamFX.
    [switch]$WithDreamFX,

    # What the tool worktrees follow, detached (the working project usually has their main checked out).
    [string]$ToolRef = 'main',

    # The engine root. Defaults to the installed engine the template's .uproject associates with.
    [string]$EngineRoot,

    # Replace template files that differ.
    [switch]$Force,

    # Report only.
    [switch]$WhatIf
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$TemplateDirectory = Join-Path $PSScriptRoot 'Template'
$TemplateVersion = 1
$script:Mismatch = $false

function Fail([string]$Message) { Write-Host "FAILED: $Message" -ForegroundColor Red; exit 1 }
function Step([string]$Message) { Write-Host "== $Message" -ForegroundColor Cyan }

function Get-NormalizedText([string]$Path)
{
    $Text = [System.IO.File]::ReadAllText($Path)
    if ($Text.Length -gt 0 -and $Text[0] -eq [char]0xFEFF) { $Text = $Text.Substring(1) }
    return $Text.Replace("`r`n", "`n")
}

function Get-FullPath([string]$Path) { return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/') }

function Test-PathUnder([string]$Path, [string]$Directory)
{
    $P = (Get-FullPath $Path) + '\'
    $D = (Get-FullPath $Directory) + '\'
    return $P.StartsWith($D, [System.StringComparison]::OrdinalIgnoreCase)
}

function Get-RepoTop([string]$Path, [string]$What)
{
    $Top = (& git -C $Path rev-parse --show-toplevel 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $Top) { Fail "$Path is not inside a git repository ($What)." }
    return Get-FullPath $Top
}

# Ensures <Root>/Plugins/<Name> is a worktree of the repository at $Repo. With $Detach the worktree follows
# $Ref detached; otherwise it has branch $Ref, created at $Start when missing.
function Confirm-Worktree([string]$Name, [string]$Repo, [string]$Ref, [string]$Start, [bool]$Detach)
{
    $Path = Join-Path $Root "Plugins\$Name"
    $Common = Get-FullPath (& git -C $Repo rev-parse --path-format=absolute --git-common-dir)

    if (Test-Path $Path)
    {
        $Have = (& git -C $Path rev-parse --path-format=absolute --git-common-dir 2>$null)
        if ($LASTEXITCODE -ne 0 -or -not $Have) { Fail "$Path exists but is not a git checkout." }
        if ((Get-FullPath $Have) -ne $Common) { Fail "$Path is a checkout of another repository ($Have)." }
        $Head = (& git -C $Path rev-parse --abbrev-ref HEAD)
        $Commit = (& git -C $Path rev-parse --short HEAD)
        if (-not $Detach -and $Head -ne $Ref)
        {
            Write-Host "  kept: $Name has '$Head' ($Commit), not '$Ref'. This script never switches a worktree." -ForegroundColor Yellow
            $script:Mismatch = $true
        }
        else { Write-Host "  ok: $Name on $Head ($Commit)" }
        return
    }

    if ($WhatIf) { Write-Host "  would create the worktree $Path ($Ref$(if ($Detach) { ', detached' }))"; $script:Mismatch = $true; return }

    if ($Detach) { & git -C $Repo worktree add --detach $Path $Ref }
    else
    {
        & git -C $Repo show-ref --verify --quiet "refs/heads/$Ref"
        if ($LASTEXITCODE -eq 0) { & git -C $Repo worktree add $Path $Ref }
        else { & git -C $Repo worktree add -b $Ref $Path $Start }
    }
    if ($LASTEXITCODE -ne 0) { Fail "git worktree add failed for $Name." }
    Write-Host "  created: $Path"
}

# ------------------------------------------------------------------------------------------ 1. checks
Step 'Checks'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail 'git is not on PATH.' }
if ([string]::IsNullOrWhiteSpace($Root)) { Fail 'No -Root given and DREAMDYNAMICWORLD_TEST_HOST is not set.' }
$Root = Get-FullPath $Root

if ([string]::IsNullOrWhiteSpace($RepoPath)) { $RepoPath = $PSScriptRoot }
$RepoTop = Get-RepoTop $RepoPath 'DreamDynamicWorld'
# The main checkout, not a worktree, is where the sibling tool repositories are looked for.
$MainTop = Split-Path (Get-FullPath (& git -C $RepoTop rev-parse --path-format=absolute --git-common-dir)) -Parent

if ([string]::IsNullOrWhiteSpace($DreamShaderRepo)) { $DreamShaderRepo = Join-Path (Split-Path $MainTop -Parent) 'DreamShader' }
$DreamShaderTop = Get-RepoTop $DreamShaderRepo 'DreamShader'
if ($WithDreamFX)
{
    if ([string]::IsNullOrWhiteSpace($DreamFXRepo)) { $DreamFXRepo = Join-Path (Split-Path $MainTop -Parent) 'DreamFX' }
    $DreamFXTop = Get-RepoTop $DreamFXRepo 'DreamFX'
}

if ((Test-PathUnder $Root $MainTop) -and -not (Test-PathUnder $RepoTop $Root)) { Fail "-Root ($Root) is inside the repository's working tree ($MainTop)." }

$TemplateProject = Get-ChildItem -Path $TemplateDirectory -Filter '*.uproject' | Select-Object -First 1
if (-not $TemplateProject) { Fail "No .uproject in $TemplateDirectory." }
$ProjectName = [System.IO.Path]::GetFileNameWithoutExtension($TemplateProject.Name)

if (Test-Path $Root)
{
    $Others = @(Get-ChildItem -Path $Root -Filter '*.uproject' -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne $TemplateProject.Name })
    if ($Others.Count -gt 0) { Fail "$Root already holds another project ($($Others[0].Name))." }
}

if ([string]::IsNullOrWhiteSpace($EngineRoot))
{
    $Association = (Get-Content $TemplateProject.FullName -Raw | ConvertFrom-Json).EngineAssociation
    $Key = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association"
    if (Test-Path $Key) { $EngineRoot = (Get-ItemProperty $Key).InstalledDirectory }
    # Engines installed by the Epic launcher are listed in its own manifest rather than the registry.
    $Launcher = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    if ([string]::IsNullOrWhiteSpace($EngineRoot) -and (Test-Path $Launcher))
    {
        $Entry = (Get-Content $Launcher -Raw | ConvertFrom-Json).InstallationList |
            Where-Object { $_.AppName -eq "UE_$Association" } | Select-Object -First 1
        if ($Entry) { $EngineRoot = $Entry.InstallLocation }
    }
    if ([string]::IsNullOrWhiteSpace($EngineRoot)) { Fail "No installed engine registered for '$Association'; pass -EngineRoot." }
}
$EngineRoot = Get-FullPath $EngineRoot
if (-not (Test-Path (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'))) { Fail "$EngineRoot is not an engine root." }

Write-Host "  root         $Root"
Write-Host "  repository   $MainTop"
Write-Host "  DreamShader  $DreamShaderTop"
if ($WithDreamFX) { Write-Host "  DreamFX      $DreamFXTop" }
Write-Host "  engine       $EngineRoot"

# --------------------------------------------------------------------------------------- 2. worktrees
Step 'Worktrees'
Confirm-Worktree 'DreamDynamicWorld' $RepoTop $Branch $StartPoint $false
Confirm-Worktree 'DreamShader' $DreamShaderTop $ToolRef '' $true
if ($WithDreamFX) { Confirm-Worktree 'DreamFX' $DreamFXTop $ToolRef '' $true }

# ---------------------------------------------------------------------------------------- 3. template
Step 'Template'

$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
foreach ($File in Get-ChildItem -Path $TemplateDirectory -Recurse -File)
{
    $Relative = [System.IO.Path]::GetRelativePath($TemplateDirectory, $File.FullName)
    $Target = Join-Path $Root $Relative

    if (-not (Test-Path $Target))
    {
        if ($WhatIf) { Write-Host "  would write $Relative"; $script:Mismatch = $true; continue }
        New-Item -ItemType Directory -Force -Path (Split-Path $Target) | Out-Null
        Copy-Item $File.FullName $Target
        Write-Host "  wrote $Relative"
        continue
    }

    $Want = Get-NormalizedText $File.FullName
    $Have = Get-NormalizedText $Target
    if ($Want -eq $Have) { continue }

    $WantLines = $Want -split "`n"
    $HaveLines = $Have -split "`n"
    $Line = 0
    while ($Line -lt [Math]::Min($WantLines.Count, $HaveLines.Count) -and $WantLines[$Line] -eq $HaveLines[$Line]) { $Line++ }

    if ($Force -and -not $WhatIf)
    {
        Copy-Item $Target "$Target.bak-$Stamp"
        Copy-Item $File.FullName $Target -Force
        Write-Host "  replaced $Relative (old one kept as .bak-$Stamp)"
    }
    else
    {
        Write-Host "  differs: $Relative, first at line $($Line + 1)" -ForegroundColor Yellow
        $script:Mismatch = $true
    }
}

# ------------------------------------------------------------------------------------- 4. the record
$WorktreePath = Join-Path $Root 'Plugins\DreamDynamicWorld'
$RecordPath = Join-Path $Root '.dreamdynamicworld-testhost.json'
if (-not $WhatIf -and (Test-Path $WorktreePath))
{
    $Record = [ordered]@{
        templateVersion = $TemplateVersion
        repository      = $MainTop
        branch          = (& git -C $WorktreePath rev-parse --abbrev-ref HEAD)
        dreamShader     = $DreamShaderTop
        dreamFX         = $(if ($WithDreamFX) { $DreamFXTop } else { $null })
        engine          = $EngineRoot
    }
    $Json = ($Record | ConvertTo-Json) + "`n"
    if (-not (Test-Path $RecordPath) -or (Get-NormalizedText $RecordPath) -ne $Json.Replace("`r`n", "`n"))
    {
        [System.IO.File]::WriteAllText($RecordPath, $Json)
        Write-Host '  wrote .dreamdynamicworld-testhost.json'
    }
}

# ---------------------------------------------------------------------------------- 5. what to do next
Step 'Next'
$Project = Join-Path $Root "$ProjectName.uproject"
Write-Host "  build: & '$EngineRoot\Engine\Build\BatchFiles\Build.bat' ${ProjectName}Editor Win64 Development `"-Project=$Project`" -WaitMutex -NoHotReloadFromIDE -NoEngineChanges"
Write-Host "  test:  pwsh -NoProfile -File '$WorktreePath\Tools\TestHost\Invoke-DreamDynamicWorldTests.ps1' -Project '$Project'"

if ($script:Mismatch) { exit 2 }
exit 0
