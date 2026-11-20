<#
.SYNOPSIS
    Measures the GPU time of the sky dome and of the volumetric clouds (realistic and painted) at standard views and
    weathers in a game window, and prints a table.

.DESCRIPTION
    One game session (Invoke-DreamSkyScript.ps1) steps through every mode x weather x view: it sets the weather,
    turns the view, waits for the picture to settle and dumps ProfileGPU -Samples times. The dumps are read back from
    the log; each number is the median over the samples (ms, GPU busy time):

      Frame       the whole frame on the graphics queue (busy + wait)
      Dome        the sky dome's draws (SM_SkySphere, found by r.ShowMaterialDrawEvents), outside the sky capture
      Capture     CaptureConvolveSkyEnvMap: the real-time sky light capture (draws the dome and the clouds again)
      Clouds      VolumetricCloud: the engine's cloud layer for the view (all queues)
      Shadow      VolumetricCloudShadow: its shadow map
      Sky         Dome + Capture + Clouds + Shadow

    Modes: Toon (the dome draws the clouds; DreamSky.Clouds 0), Realistic (M_DreamVolumetricCloud) and Painted
    (M_DreamVolumetricCloud_Painted), both with DreamSky.Clouds 1. Views are Name=pitch[/yaw] (degrees; the level
    camera's yaw when left out). The volumetric render target mode is the engine's default unless -CloudRenderMode.

    The numbers are only as quiet as the GPU: other work on it (another editor, a game, a build that compiles shaders)
    shows up in them. -Log reads an earlier run's log again instead of running.

    Writes <host>/Saved/ClaudeRuns/gpu-<stamp>.csv. Exit codes: 0 measured, 1 the run failed or nothing was read.

.EXAMPLE
    pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyGpu.ps1 -OffScreen
    pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyGpu.ps1 -Modes Painted -Weathers WP_Cloudy,WP_Storm -Views Up=35 -OffScreen
    pwsh -NoProfile -File Tools\TestHost\Measure-DreamSkyGpu.ps1 -Log <host>\Saved\ClaudeRuns\gpu-....log
#>
[CmdletBinding()]
param(
    [string[]]$Modes = @('Toon', 'Realistic', 'Painted'),
    [string[]]$Weathers = @('WP_Clear', 'WP_Cloudy', 'WP_Overcast', 'WP_Storm'),
    [string[]]$Views = @('Horizon=3', 'Up=35'),
    [double]$Hour = 14,
    [int]$Samples = 3,
    [int]$Settle = 40,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [Nullable[int]]$CloudRenderMode,
    # Console commands run once before the measurements (e.g. 'r.ScreenPercentage 100'); none may contain a comma.
    [string[]]$Commands = @(),
    [string]$Log,
    [string]$Project,
    # Frames held after switching to a volumetric material: its shaders keep compiling after 'settle' returns.
    [int]$CompileHold = 600,
    [switch]$OffScreen,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# pwsh -File passes a list argument as one comma-joined string.
foreach ($ListName in 'Modes', 'Weathers', 'Views') { Set-Variable -Name $ListName -Value @((Get-Variable -Name $ListName -ValueOnly) | ForEach-Object { $_ -split ',' } | Where-Object { $_ }) }
$Unknown = @($Modes | Where-Object { $_ -notin 'Toon', 'Realistic', 'Painted' }); if ($Unknown) { throw "Unknown mode: $($Unknown -join ', ')" }
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture

$MaterialOf = @{
    Realistic = '/DreamDynamicWorld/Sky/M_DreamVolumetricCloud.M_DreamVolumetricCloud'
    Painted   = '/DreamDynamicWorld/Sky/M_DreamVolumetricCloud_Painted.M_DreamVolumetricCloud_Painted'
}

if (-not $Log)
{
    $Lines = @(
        '# Measure-DreamSkyGpu.ps1',
        'r.ProfileGPU.ShowUI 0',
        'r.ProfileGPU.TableFormatting 0',
        'r.ProfileGPU.ShowHeader 1',
        'r.ShowMaterialDrawEvents 1',
        "time $($Hour.ToString($Invariant))"
    ) + $Commands
    if ($null -ne $CloudRenderMode) { $Lines += "r.VolumetricRenderTarget.Mode $CloudRenderMode" }
    foreach ($Mode in $Modes)
    {
        if ($Mode -eq 'Toon') { $Lines += 'DreamSky.Clouds 0' }
        else { $Lines += 'DreamSky.Clouds 1', "DreamSky.CloudMaterial $($MaterialOf[$Mode])", 'settle', "wait $CompileHold" }
        foreach ($Weather in $Weathers)
        {
            $Lines += "DreamWeather.Set $Weather 0"
            foreach ($View in $Views)
            {
                $Name, $Angles = $View -split '=', 2
                $Lines += "aim $($Angles -replace '/', ' ')", "settle $Settle", "mark $Mode|$Weather|$Name"
                for ($I = 0; $I -lt $Samples; ++$I) { $Lines += 'ProfileGPU', 'wait 12' }
            }
        }
    }
    $Lines += 'mark end', 'wait 10', 'quit'
    $Params = @{ Name = 'gpu'; Lines = $Lines; ResX = $ResX; ResY = $ResY; Settle = $Settle; TimeoutMinutes = 40 }
    if ($Project) { $Params.Project = $Project }
    if ($OffScreen) { $Params.OffScreen = $true }
    if ($Force) { $Params.Force = $true }
    $Output = @(& (Join-Path $PSScriptRoot 'Invoke-DreamSkyScript.ps1') @Params)
    $Ok = $LASTEXITCODE -eq 0
    $Log = [string]$Output[-1]
    if (-not $Ok -or -not (Test-Path $Log))
    {
        Write-Host "FAILED: the game run did not finish (log $Log)" -ForegroundColor Red
        exit 1
    }
}

# --- Read the dumps back --------------------------------------------------------------------------------------------
# A dump (r.ProfileGPU.TableFormatting 0) is "GPU Profile for Frame N - Graphics pipeline 0 - GPU 0", a header whose
# "Frame Time" line gives the frame, then one line per event: <3 spaces a level><percent>% <ms>ms[ + <ms> Wait]   <name>.
# Other queues follow as "<Queue> pipeline N - GPU 0" and their own lines.
$NodePattern = '^(?<lead> *)(?<pct>\d+\.\d)% +(?<ms>\d+\.\d+)ms(?: \+ (?<wait>\d+\.\d+) Wait)?   (?<name>.*)$'
$Label = $null
$Dumps = New-Object System.Collections.Generic.List[object]
$Current = $null
$Stack = @()
foreach ($Line in [System.IO.File]::ReadLines($Log))
{
    # (Each line may carry the log's own prefix.)
    $Text = $Line -replace '^\[[^\]]*\]\[[^\]]*\]Log\w+: (?:Display: |Warning: )?', ''
    if ($Text -match 'Script: mark (?<label>.+)$') { $Label = $Matches.label.Trim(); $Current = $null; continue }
    # A dump prints the other queues first ("Compute pipeline 0 - GPU 0"), then the graphics queue under its header.
    $Graphics = $Text -match 'GPU Profile for Frame (?<frame>\d+)'
    if ($Graphics -or $Text -match '^\w+ pipeline \d+ - GPU \d+\s*$')
    {
        if (-not ($Graphics -and $Current -and $Current.Pending))
        {
            $Current = [pscustomobject]@{ Label = $Label; Pending = -not $Graphics; Frame = 0.0; Dome = 0.0; Capture = 0.0; Clouds = 0.0; Shadow = 0.0 }
            $Dumps.Add($Current)
        }
        if ($Graphics) { $Current.Pending = $false }
        $Stack = @()
        continue
    }
    if (-not $Current -or -not $Label -or $Label -eq 'end') { continue }
    if ($Text -match '- Frame Time\s*: (?<ms>[\d.]+)ms') { $Current.Frame = [double]::Parse($Matches.ms, $Invariant); continue }
    if ($Text -notmatch $NodePattern) { continue }
    $Pad = [Math]::Max(0, 4 - $Matches.pct.Length)
    $Level = [int][Math]::Round(($Matches.lead.Length - $Pad) / 3.0)
    $Ms = [double]::Parse($Matches.ms, $Invariant)
    $Name = $Matches.name
    $Stack = @($Stack | Select-Object -First $Level) + $Name
    $Path = $Stack -join ' / '
    $InCapture = $Path -match 'CaptureConvolveSkyEnvMap'
    if ($Name -eq 'CaptureConvolveSkyEnvMap') { $Current.Capture += $Ms }
    elseif ($Name -eq 'VolumetricCloud') { $Current.Clouds += $Ms }
    elseif ($Name -eq 'VolumetricCloudShadow') { $Current.Shadow += $Ms }
    elseif ($Name -match 'SM_SkySphere' -and -not $InCapture) { $Current.Dome += $Ms }
}

$Groups = @($Dumps | Where-Object { $_.Label } | Group-Object Label)
if ($Groups.Count -eq 0)
{
    Write-Host "FAILED: no ProfileGPU dumps in $Log" -ForegroundColor Red
    exit 1
}
function Median([double[]]$Values)
{
    $Sorted = @($Values | Sort-Object)
    if ($Sorted.Count -eq 0) { return 0.0 }
    if ($Sorted.Count % 2) { return $Sorted[[int][Math]::Floor($Sorted.Count / 2)] }
    return ($Sorted[$Sorted.Count / 2 - 1] + $Sorted[$Sorted.Count / 2]) / 2.0
}
$Rows = foreach ($Group in $Groups)
{
    $Mode, $Weather, $View = $Group.Name -split '\|'
    $Row = [ordered]@{ Mode = $Mode; Weather = $Weather; View = $View; N = $Group.Count }
    foreach ($Key in 'Frame', 'Dome', 'Capture', 'Clouds', 'Shadow')
    {
        $Row[$Key] = [Math]::Round((Median @($Group.Group | ForEach-Object { $_.$Key })), 3)
    }
    $Row.Sky = [Math]::Round($Row.Dome + $Row.Capture + $Row.Clouds + $Row.Shadow, 3)
    [pscustomobject]$Row
}
$Rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
$Csv = Join-Path (Split-Path $Log) ("gpu-$((Get-Date).ToString('yyyyMMdd-HHmmss')).csv")
$Rows | Export-Csv -Path $Csv -NoTypeInformation -Encoding utf8
Write-Host "ms, median of $($Rows[0].N) ProfileGPU dumps each; csv $Csv; log $Log"
exit 0
