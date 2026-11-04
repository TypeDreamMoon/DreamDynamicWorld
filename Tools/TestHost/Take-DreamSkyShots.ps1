<#
.SYNOPSIS
    Photographs the sky test level at a list of hours in a game window (DreamWorld.ShotTour), for judging a day
    cycle by eye.

.DESCRIPTION
    Opens a game window and renders on the GPU, so by default it refuses to start while a process named in
    -BlockingProcesses runs (a game the machine's user may be playing); -Force starts anyway.

    The shots land in <host>/Saved/Screenshots/<platform>/DreamWorld/<Prefix>_HHMM.png. With Python and Pillow
    available, a contact sheet <Prefix>_sheet.png (every shot, labelled) is written next to them.

    Exit codes: 0 shots taken; 1 nothing taken (blocked, failed to start, no shots).

.EXAMPLE
    pwsh -NoProfile -File Tools\TestHost\Take-DreamSkyShots.ps1
    pwsh -NoProfile -File Tools\TestHost\Take-DreamSkyShots.ps1 -Hours 5,5.5,6,6.5,7 -Prefix Dawn
    pwsh -NoProfile -File Tools\TestHost\Take-DreamSkyShots.ps1 -Hours 9,17.5 -Prefix Rain -Commands 'DreamWeather.Set WP_Rain 0'
    pwsh -NoProfile -File Tools\TestHost\Take-DreamSkyShots.ps1 -Hours 12 -Prefix Up -Pitch 40
#>
[CmdletBinding()]
param(
    [string]$Project,
    [double[]]$Hours = @(0, 3, 5, 5.5, 6, 6.5, 8, 12, 16, 17.5, 18, 18.5, 19, 21),
    [string]$Prefix = 'Day',
    [string]$Map = '/DreamDynamicWorld/Sky/Maps/Lvl_DreamSkyTest',
    [int]$ResX = 1280,
    [int]$ResY = 720,
    [int]$Settle = 40,
    [string[]]$BlockingProcesses = @('TGame'),
    # Console commands run before the tour (e.g. 'DreamWeather.Set WP_Rain 0'); none may contain a comma.
    [string[]]$Commands = @(),
    [string]$EngineRoot,
    # Turn the view before the tour (degrees); the level's camera otherwise.
    [Nullable[double]]$Pitch,
    [Nullable[double]]$Yaw,
    # Open the window off screen (it still renders), so it covers nothing while someone else uses the desktop.
    [switch]$OffScreen,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

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
    Write-Host "Not started: $($Blocking[0].ProcessName) is running (the shots open a window and use the GPU). Pass -Force to start anyway." -ForegroundColor Yellow
    exit 1
}

foreach ($Name in 'UE-ZenDataPath', 'UE-LocalDataCachePath')
{
    $Value = [Environment]::GetEnvironmentVariable($Name, 'User')
    if ($Value) { [Environment]::SetEnvironmentVariable($Name, $Value, 'Process') }
}

# '/' between the hours: -ExecCmds splits its commands at commas.
$HourList = ($Hours | ForEach-Object { $_.ToString([System.Globalization.CultureInfo]::InvariantCulture) }) -join '/'
$Stamp = Get-Date
$Log = Join-Path $HostRoot "Saved\ClaudeRuns\shots-$($Stamp.ToString('yyyyMMdd-HHmmss')).log"
New-Item -ItemType Directory -Force -Path (Split-Path $Log) | Out-Null
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$Tour = "DreamWorld.ShotTour Hours=$HourList Prefix=$Prefix Settle=$Settle"
if ($null -ne $Pitch) { $Tour += " Pitch=$(([double]$Pitch).ToString([System.Globalization.CultureInfo]::InvariantCulture))" }
if ($null -ne $Yaw) { $Tour += " Yaw=$(([double]$Yaw).ToString([System.Globalization.CultureInfo]::InvariantCulture))" }
$Exec = @($Commands) + @("$Tour Quit")
$Arguments = "`"$Project`" $Map -game -windowed -ResX=$ResX -ResY=$ResY -nosound -nosplash -unattended -abslog=`"$Log`" " +
    "-ExecCmds=`"$($Exec -join ',')`""
if ($OffScreen) { $Arguments += ' -WinX=8000 -WinY=100' }
Write-Host "== Shots ($($Hours.Count)) of $Map"
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru
try { $Process.PriorityClass = 'BelowNormal' } catch { }
if (-not $Process.WaitForExit(600000))
{
    Stop-Process -Id $Process.Id -Force
    Write-Host "FAILED: still running after 10 minutes, stopped (log $Log)" -ForegroundColor Red
    exit 1
}

$Shots = @(Get-ChildItem -Path (Join-Path $HostRoot 'Saved\Screenshots') -Recurse -Filter "${Prefix}_*.png" -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $Stamp -and $_.Name -notlike '*_sheet.png' } | Sort-Object Name)
if ($Shots.Count -eq 0)
{
    Write-Host "FAILED: no shots (log $Log)" -ForegroundColor Red
    exit 1
}
Select-String -Path $Log -Pattern 'ShotTour: done' | ForEach-Object { Write-Host "  $($_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '')" }
$Shots | ForEach-Object { Write-Host "  $($_.FullName)" }

$Sheet = Join-Path $Shots[0].DirectoryName "${Prefix}_sheet.png"
$Python = Get-Command python -ErrorAction SilentlyContinue
if ($Python)
{
    $Script = Join-Path $PSScriptRoot 'make_contact_sheet.py'
    & $Python.Source $Script $Sheet @($Shots | ForEach-Object { $_.FullName })
    if ($LASTEXITCODE -eq 0) { Write-Host "  sheet: $Sheet" }
}
exit 0
