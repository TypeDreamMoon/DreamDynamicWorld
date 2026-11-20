<#
.SYNOPSIS
    Flies the camera across the sky test level at a few speeds, saves every frame, and reports whether the sky jumps
    from one frame to the next.

.DESCRIPTION
    One game session (Invoke-DreamSkyScript.ps1, the game clock at a fixed 1/FixedFps s a frame) runs, for each mode
    and speed: back to the start, aim, settle, then 'fly' at the speed while saving -Frames consecutive frames
    (<Prefix>_<Mode>_<speed>_fNNNN.png). measure_frame_change.py then compares consecutive frames over the top part of
    the picture (-Top) and counts the jumps: steps that stand out from the steps around them.

    Modes as in Measure-DreamSkyGpu.ps1 (Toon, Realistic, Painted). Speeds in m/s; 0 measures the drift of the clouds
    alone. -Heading is the flight's direction and -Pitch / -Yaw the view's (degrees); the default looks toward the
    test level's storm zone while flying across it, so the weather map's edges move through the picture.

    Prints one line per sequence and writes a plot and a strip of the worst step next to the frames
    (<sequence>_plot.png, <sequence>_strip.png). Exit codes: 0 no jumps, 3 jumps found, 1 the run failed.

.EXAMPLE
    pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyStability.ps1 -OffScreen
    pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyStability.ps1 -Modes Toon -Speeds 0,300 -Frames 90 -OffScreen
#>
[CmdletBinding()]
param(
    [string[]]$Modes = @('Toon', 'Realistic', 'Painted'),
    [string[]]$Speeds = @(0, 30, 150, 600),
    [int]$Frames = 60,
    [int]$FixedFps = 30,
    [double]$Hour = 15,
    [string]$Weather = 'WP_Cloudy',
    [double]$Pitch = 12,
    [double]$Yaw = -140,
    [double]$Heading = -50,
    # Where every flight starts, "x,y,z" (cm); the level camera's place when left out.
    [string]$Start,
    [double]$Top = 0.55,
    [string]$Prefix = 'Fly',
    [int]$ResX = 960,
    [int]$ResY = 540,
    [Nullable[int]]$CloudRenderMode,
    [string[]]$Commands = @(),
    # No lightning (StrikesPerMinute 0): a flash brightens every frame it lands in and reads as a jump.
    [switch]$NoLightning,
    [string]$Project,
    # Only measure frames an earlier run saved (with the same -Prefix, -Modes and -Speeds).
    [switch]$NoRun,
    # Frames held after switching to a volumetric material: its shaders keep compiling after 'settle' returns.
    [int]$CompileHold = 600,
    [switch]$OffScreen,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# pwsh -File passes a list argument as one comma-joined string.
$Modes = @($Modes | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Speeds = @($Speeds | ForEach-Object { "$_" -split ',' } | Where-Object { $_ } | ForEach-Object { [double]::Parse($_, [System.Globalization.CultureInfo]::InvariantCulture) })
$Unknown = @($Modes | Where-Object { $_ -notin 'Toon', 'Realistic', 'Painted' }); if ($Unknown) { throw "Unknown mode: $($Unknown -join ', ')" }
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture
$MaterialOf = @{
    Realistic = '/DreamDynamicWorld/Sky/M_DreamVolumetricCloud.M_DreamVolumetricCloud'
    Painted   = '/DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted'
}
function Num([double]$Value) { $Value.ToString($Invariant) }
function SequenceName([string]$Mode, [double]$Speed) { "${Prefix}_${Mode}_$(Num $Speed)" }

if ([string]::IsNullOrWhiteSpace($Project))
{
    $HostRoot = Split-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) -Parent
    $Project = Get-ChildItem -Path $HostRoot -Filter '*.uproject' | Select-Object -First 1 -ExpandProperty FullName
}
$HostRoot = Split-Path ([System.IO.Path]::GetFullPath($Project)) -Parent
$ShotDir = Join-Path $HostRoot 'Saved\Screenshots\WindowsEditor\DreamWorld'

if (-not $NoRun)
{
    foreach ($Mode in $Modes)
    {
        foreach ($Speed in $Speeds)
        {
            Get-ChildItem -Path $ShotDir -Filter "$(SequenceName $Mode $Speed)_f*.png" -ErrorAction SilentlyContinue | Remove-Item
        }
    }
    $Lines = @('# Measure-DreamSkyStability.ps1', "time $(Num $Hour)", "DreamWeather.Set $Weather 0") + $Commands
    if ($null -ne $CloudRenderMode) { $Lines += "r.VolumetricRenderTarget.Mode $CloudRenderMode" }
    $Lines += 'mark start'
    foreach ($Mode in $Modes)
    {
        if ($Mode -eq 'Toon') { $Lines += 'DreamSky.Clouds 0' }
        else { $Lines += 'DreamSky.Clouds 1', "DreamSky.CloudMaterial $($MaterialOf[$Mode])", 'settle', "wait $CompileHold" }
        foreach ($Speed in $Speeds)
        {
            if ($Start) { $Lines += "move $(($Start -split ',' | ForEach-Object { $_.Trim() }) -join ' ')" } else { $Lines += 'home' }
            $Lines += "aim $(Num $Pitch) $(Num $Yaw)", 'settle',
                "mark flight $(SequenceName $Mode $Speed)", "fly $(Num $Speed) $(Num $Heading)",
                "frames $(SequenceName $Mode $Speed) $Frames", 'fly 0'
        }
    }
    $Params = @{ Name = $Prefix; Lines = $Lines; ResX = $ResX; ResY = $ResY; FixedFps = $FixedFps; TimeoutMinutes = 40 }
    if ($NoLightning) { $Params.ExtraArgs = '-ini:Game:[/Script/DreamWeather.DreamWeatherSettings]:StrikesPerMinute=0' }
    if ($OffScreen) { $Params.OffScreen = $true }
    if ($Force) { $Params.Force = $true }
    $Params.Project = $Project
    $Output = @(& (Join-Path $PSScriptRoot 'Invoke-DreamSkyScript.ps1') @Params)
    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "FAILED: the game run did not finish (log $($Output[-1]))" -ForegroundColor Red
        exit 1
    }
    Write-Host "log $($Output[-1])"
}

$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { Write-Host 'FAILED: needs Python with Pillow and NumPy' -ForegroundColor Red; exit 1 }
$Measure = Join-Path $PSScriptRoot 'measure_frame_change.py'
$Worst = 0
foreach ($Mode in $Modes)
{
    foreach ($Speed in $Speeds)
    {
        $Name = SequenceName $Mode $Speed
        $Shots = @(Get-ChildItem -Path $ShotDir -Filter "${Name}_f*.png" -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object FullName)
        if ($Shots.Count -lt 3) { Write-Host "${Name}: no frames"; $Worst = [Math]::Max($Worst, 1); continue }
        $Args = @($Measure, '--top', (Num $Top), '--label', $Name,
            '--plot', (Join-Path $ShotDir "${Name}_plot.png"), '--strip', (Join-Path $ShotDir "${Name}_strip.png"),
            '--json', (Join-Path $ShotDir "${Name}.json")) + $Shots
        $null | & $Python.Source @Args
        if ($LASTEXITCODE -eq 3) { $Worst = [Math]::Max($Worst, 3) }
        elseif ($LASTEXITCODE -ne 0) { $Worst = [Math]::Max($Worst, 1) }
    }
}
exit $Worst
