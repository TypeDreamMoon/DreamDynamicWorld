<#
.SYNOPSIS
    Runs a DreamWorld.Script file in a game window on the sky test level and waits for it to finish.

.DESCRIPTION
    The script's lines are DreamWorld.Script steps (see that command's help): console commands, 'wait', 'settle',
    'time', 'aim', 'move', 'rise', 'fly', 'shot', 'frames', 'mark', 'quit'. Give a file with -Script, or the lines
    themselves with -Lines (written to <host>/Saved/ClaudeRuns/<Name>.dws). A final 'quit' is added when missing.

    Opens a game window and renders on the GPU, so by default it refuses to start while a process named in
    -BlockingProcesses runs; -Force starts anyway. Images land in <host>/Saved/Screenshots/<platform>/DreamWorld.

    Writes the path of the game's log as its last output line; exit codes: 0 the script ran to its end, 1 not.

.EXAMPLE
    pwsh -NoProfile -File Tools\TestHost\Invoke-DreamSkyScript.ps1 -Name Fly -FixedFps 30 -OffScreen -Lines 'time 15','settle','fly 100 0','frames Fly 60'
#>
[CmdletBinding()]
param(
    [string]$Script,
    [string[]]$Lines,
    [string]$Name = 'Script',
    [string]$Project,
    [string]$Map = '/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest',
    [int]$ResX = 1280,
    [int]$ResY = 720,
    # Frames a 'settle' waits after the shaders are compiled.
    [int]$Settle = 30,
    # Run the game clock at a fixed step of 1/FixedFps s a frame, however long a frame really takes (-UseFixedTimeStep).
    [int]$FixedFps = 0,
    [string]$ExtraArgs = '',
    [string]$EngineRoot,
    [string[]]$BlockingProcesses = @('TGame'),
    [int]$TimeoutMinutes = 20,
    [switch]$OffScreen,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

if ([string]::IsNullOrWhiteSpace($Project))
{
    $HostRoot = Split-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) -Parent
    $Project = Get-ChildItem -Path $HostRoot -Filter '*.uproject' | Select-Object -First 1 -ExpandProperty FullName
}
$Project = [System.IO.Path]::GetFullPath($Project)
$HostRoot = Split-Path $Project -Parent
if ([string]::IsNullOrWhiteSpace($EngineRoot))
{
    $EngineRoot = (Get-Content (Join-Path $HostRoot '.dreamdynamicworld-testhost.json') -Raw | ConvertFrom-Json).engine
}

$Blocking = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $BlockingProcesses -contains $_.ProcessName })
if ($Blocking.Count -gt 0 -and -not $Force)
{
    Write-Host "Not started: $($Blocking[0].ProcessName) is running (the script opens a window and uses the GPU). Pass -Force to start anyway." -ForegroundColor Yellow
    exit 1
}

$Runs = Join-Path $HostRoot 'Saved\ClaudeRuns'
New-Item -ItemType Directory -Force -Path $Runs | Out-Null
if ($Lines)
{
    $Script = Join-Path $Runs "$Name.dws"
    Set-Content -Path $Script -Value $Lines -Encoding utf8
}
if (-not $Script -or -not (Test-Path $Script))
{
    throw "No script: pass -Script <file> or -Lines."
}
$Script = [System.IO.Path]::GetFullPath($Script)
$Body = @(Get-Content $Script)
if (-not ($Body | Where-Object { $_.Trim() -match '^quit\b' }))
{
    $Script = Join-Path $Runs "$Name.run.dws"
    Set-Content -Path $Script -Value (@($Body) + 'quit') -Encoding utf8
}

foreach ($Var in 'UE-ZenDataPath', 'UE-LocalDataCachePath')
{
    $Value = [Environment]::GetEnvironmentVariable($Var, 'User')
    if ($Value) { [Environment]::SetEnvironmentVariable($Var, $Value, 'Process') }
}

$Log = Join-Path $Runs "$Name-$((Get-Date).ToString('yyyyMMdd-HHmmss')).log"
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Arguments = "`"$Project`" $Map -game -windowed -ResX=$ResX -ResY=$ResY -nosound -nosplash -unattended -abslog=`"$Log`" " +
    "-ExecCmds=`"DreamWorld.Script $Script Settle=$Settle`""
if ($FixedFps -gt 0) { $Arguments += " -UseFixedTimeStep -FPS=$FixedFps" }
if ($OffScreen) { $Arguments += ' -WinX=8000 -WinY=100' }
if ($ExtraArgs) { $Arguments += " $ExtraArgs" }
Write-Host "== $Name ($($Body.Count) lines) on $Map"
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru
if (-not $Process.WaitForExit($TimeoutMinutes * 60000))
{
    Stop-Process -Id $Process.Id -Force
    Write-Host "FAILED: still running after $TimeoutMinutes minutes, stopped (log $Log)" -ForegroundColor Red
    $Log
    exit 1
}
$Done = Select-String -Path $Log -Pattern 'Script: done \((.*)\)' | Select-Object -Last 1
if (-not $Done -or $Done.Matches[0].Groups[1].Value -ne 'quit')
{
    Write-Host "FAILED: the script did not run to its end (log $Log)" -ForegroundColor Red
    $Log
    exit 1
}
$Log
exit 0
