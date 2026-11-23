<#
.SYNOPSIS
    Times each of the plugin's GPU features on the showcase level, one at a time, at fixed views and weathers, and
    writes a Markdown table: the feature, whether it is on by default, its cost (median ms), and how busy the GPU was
    with other work while it was timed.

.DESCRIPTION
    One game session on Lvl_DreamWorldShowcase (Setup -WorldShowcase) at 1920x1080. For each feature it sets the
    feature's weather, hour and view, then either
      toggle  times the frame (or a named GPU event) with the feature off and on, -Samples ProfileGPU dumps each, and
              reports on - off; or
      events  sums the feature's own GPU events in -Samples dumps (a pass that does not run every frame - the surface
              maps update every 0.1 s - is reported per frame it ran, with the share of frames it ran in).
    Each figure is a median. The table goes to <host>/Saved/ClaudeRuns/gpu-features-<stamp>.md and the console.

    The GPU must be quiet: before starting it waits (up to -WaitMinutes) until nvidia-smi reads under -MaxBusy percent;
    if it never does it exits, naming the processes using the GPU (it stops nothing). While the game runs it samples
    every process's GPU use (nvidia-smi pmon) and reports, per feature, how busy the GPU was with work other than this
    game.

    A whole run takes some minutes (-Samples 7 over every feature): run it when the GPU can be spared. A short check:
      Measure-ShowcaseGpu.ps1 -Only LensFlare,Rain -Samples 1

    Features (-Only takes the keys): SurfaceMaps, SurfaceMaterial, Trails, Ripples, Glass, Rain, Snow, Dust, Debris,
    Clouds, LightShafts, Aurora, StarMap, LensFlare, ScreenDroplets, Frost, HeatHaze, Rainbow, Eclipse.

.EXAMPLE
    pwsh -NoProfile -File Tools\Showcase\Measure-ShowcaseGpu.ps1 -Project <host>\DreamDynamicWorldTestHost.uproject
    pwsh -NoProfile -File Tools\Showcase\Measure-ShowcaseGpu.ps1 -Only Eclipse,Rain -Samples 1 -Project <host>\...uproject
    pwsh -NoProfile -File Tools\Showcase\Measure-ShowcaseGpu.ps1 -Log <host>\Saved\ClaudeRuns\gpu-features-....log
#>
[CmdletBinding()]
param(
    [string]$Project,
    # -Scene Showcase: the showcase level's features (above). WindField: the wind's on the wind field test level
    # (Setup -WindFieldTestMap): WindVolume, WindOcclusion, WindOcclusionVolume, WindVolumeAll, WindGrass.
    [ValidateSet('Showcase', 'WindField')]
    [string]$Scene = 'Showcase',
    [string[]]$Only = @(),
    [int]$Samples = 7,
    # Start only once the GPU reads under this many percent busy; give up after WaitMinutes.
    [int]$MaxBusy = 15,
    [double]$WaitMinutes = 20,
    # Frames held after a feature's setup (its shaders may compile, its weather fill the air).
    [int]$Hold = 240,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    # Frames run before the first feature, for the level's shaders to finish compiling.
    [int]$WarmUp = 1800,
    # Take a picture of each feature's view after its setup (GPU_<key>.png), to check what was timed.
    [switch]$Shots,
    # Read an earlier run's log (and its GPU samples next to it) instead of running.
    [string]$Log
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Invariant = [System.Globalization.CultureInfo]::InvariantCulture
$Map = '/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcase'

# --- The features -----------------------------------------------------------------------------------------------------
# Views from the showcase's shot camera (200, -300, 320); the sun stands due south (-X) at 60 degrees at noon in the
# default (artistic) sky, so 'Sun' looks at it.
$Views = @{
    Wide    = @('move 200 -300 320', 'aim -10 18')
    Up      = @('move 200 -300 320', 'aim 10 18')
    Horizon = @('move 200 -300 320', 'aim 0 18')
    Sun     = @('move 200 -300 320', 'aim 60 180')
    NightUp = @('move 200 -300 320', 'aim 45 18')
    Rainbow = @('move 200 -300 320', 'aim 15 60')
    Glass   = @('move 400 820 150', 'aim -2 0')
    Grove   = @('move 200 1300 250', 'aim -8 69')
}
# Weather '' = the schedule's (its history fills the puddles and the snow); a preset is forced at once.
function Feature($Key, $Name, $Default, $Kind, $Hour, $Weather, $View, $Setup = @(), $Off = @(), $On = @(), $Reset = @(), $Event = '', $Under = '')
{
    [pscustomobject]@{ Key = $Key; Name = $Name; Default = $Default; Kind = $Kind; Hour = $Hour; Weather = $Weather; View = $View
        Setup = @($Setup); Off = @($Off); On = @($On); Reset = @($Reset); Event = $Event; Under = $Under }
}
$Features = @(
    (Feature SurfaceMaps 'Surface maps (one full update)' 'yes' events 12.3 '' Wide -Setup @('DreamSurface.IntegrateEveryFrame 1') `
        -Reset @('DreamSurface.IntegrateEveryFrame 0') -Event '^DreamSurfaceMap')
    (Feature SurfaceMaterial 'Surface materials (their draws)' 'per material' events 12.3 '' Wide -Event 'DreamSurface(Terrain|Toon)?Demo|MI_DreamSurface' -Under 'BasePass')
    (Feature Trails 'Trails (prints, stirred water)' 'yes' events 19.6 '' Wide -Setup @('DreamSurface.Walk 300 -200 900 200') -Event '^DreamSurfaceTrails')
    (Feature Ripples 'Ripples and wet spots (base pass)' 'yes, in rain' toggle 12.3 '' Wide -Setup @('ShowFlag.Particles 0') `
        -Off @('DreamWeather.Set WP_Overcast 0') -On @('DreamWeather.Clear 0') -Reset @('ShowFlag.Particles 1') -Event '^BasePass$')
    (Feature Glass 'Rain on window glass (its draws)' 'per material' events 12.3 '' Glass -Event 'M_DreamGlassDemo')
    (Feature Rain 'Rain particles' 'yes, by weather' toggle 12 WP_Rain Wide -Off @('ShowFlag.Particles 0') -On @('ShowFlag.Particles 1'))
    (Feature Snow 'Snow particles' 'yes, by weather' toggle 12 WP_Snow Wide -Off @('ShowFlag.Particles 0') -On @('ShowFlag.Particles 1'))
    (Feature Dust 'Dust particles' 'yes, by weather' toggle 12 WP_Dust Wide -Off @('ShowFlag.Particles 0') -On @('ShowFlag.Particles 1'))
    (Feature Debris 'Wind debris (leaves, autumn)' 'yes, by season and wind' toggle 12 WP_Overcast Grove -Setup @('DreamWorld.Date 2026-10-20') `
        -Off @('ShowFlag.Particles 0') -On @('ShowFlag.Particles 1') -Reset @('DreamWorld.Date 2026-01-20'))
    (Feature Clouds 'Cloud renderer' 'yes' events 10 WP_Cloudy Up -Event '^DreamSky\.Clouds')
    (Feature LightShafts 'Light shafts' 'yes' toggle 12 WP_Cloudy Sun -Off @('DreamSky.LightShafts 0') -On @('DreamSky.LightShafts 1') -Event '^DreamSky\.LightShafts')
    (Feature Aurora 'Aurora' 'by sky modifier' toggle 19.5 WP_Clear NightUp -Off @('DreamSky.Modifier.Clear 0') -On @('DreamSky.Modifier.Add Aurora 0') `
        -Reset @('DreamSky.Modifier.Clear 0'))
    (Feature StarMap 'Star map (catalogue over the scatter)' 'by style' toggle 19.5 WP_Clear NightUp -Off @('DreamSky.StarField 0') -On @('DreamSky.StarField 1') `
        -Reset @('DreamSky.StarField -1'))
    (Feature LensFlare 'Lens flare (natural preset)' 'by style' toggle 12 WP_Clear Sun -Off @('DreamSky.LensFlare.Preset 0') -On @('DreamSky.LensFlare.Preset 1') `
        -Reset @('DreamSky.LensFlare.Preset -1') -Event '^DreamSky\.LensFlare')
    (Feature ScreenDroplets 'Rain drops on the lens' 'by style, in rain' toggle 12 WP_Rain Wide -Off @('DreamSky.ScreenDroplets 0') -On @('DreamSky.ScreenDroplets 1') `
        -Reset @('DreamSky.ScreenDroplets -1') -Event '^DreamSky\.Screen')
    (Feature Frost 'Frost on the lens' 'by style, in the cold' toggle 12 WP_Snow Wide -Off @('DreamSky.ScreenFrost 0') -On @('DreamSky.ScreenFrost 1') `
        -Reset @('DreamSky.ScreenFrost -1') -Event '^DreamSky\.Screen')
    (Feature HeatHaze 'Heat haze' 'by style, on a hot day' toggle 13 WP_Clear Horizon -Off @('DreamSky.HeatHaze 0') -On @('DreamSky.HeatHaze 1') `
        -Reset @('DreamSky.HeatHaze -1'))
    (Feature Rainbow 'Rainbow' 'by style, after a shower' toggle 16 WP_Clear Rainbow -Off @('DreamSky.Rainbow 0') -On @('DreamSky.Rainbow 1') `
        -Reset @('DreamSky.Rainbow -1'))
    (Feature Eclipse 'Solar eclipse' 'by date (realistic sky) or modifier' toggle 12 WP_Clear Sun -Off @('DreamSky.Modifier.Clear 0') `
        -On @('DreamSky.Modifier.Add Eclipse 0') -Reset @('DreamSky.Modifier.Clear 0'))
)
if ($Scene -eq 'WindField')
{
    # The wind on its test level: 40 000 blades of wind grass, a house, a wall, a corner, a burst every 4 s, a steady
    # wind along +x, no weather actor. Grass looks over the grass at the house; Shadow down on the house's lee.
    $Map = '/DreamDynamicWorld/Wind/Maps/Lvl_DreamWindFieldTest'
    $Views = @{
        Grass  = @('move -800 -2200 250', 'aim -8 45')
        Shadow = @('move 600 -1800 900', 'aim -35 60')
    }
    $Features = @(
        (Feature WindVolume 'Wind volume update (near + far, compute)' 'yes' events 11 '' Grass -Event '^DreamWindVolume')
        (Feature WindOcclusion 'Wind shadow march of the baked map (materials + volume, frame)' 'without the runtime map' toggle 11 '' Grass `
            -Setup @('DreamWind.RuntimeTerrain 0') -Off @('DreamWind.Occlusion 0') -On @('DreamWind.Occlusion 1') -Reset @('DreamWind.Occlusion -1', 'DreamWind.RuntimeTerrain -1'))
        (Feature WindOcclusionVolume 'Wind shadow march of the baked map (the volume update''s share)' 'without the runtime map' toggle 11 '' Grass `
            -Setup @('DreamWind.RuntimeTerrain 0') -Off @('DreamWind.Occlusion 0') -On @('DreamWind.Occlusion 1') -Reset @('DreamWind.Occlusion -1', 'DreamWind.RuntimeTerrain -1') -Event '^DreamWindVolume')
        (Feature WindTerrainRuntime 'Runtime terrain map instead of the baked march (everything, frame)' 'yes' toggle 11 '' Grass `
            -Off @('DreamWind.RuntimeTerrain 0') -On @('DreamWind.RuntimeTerrain 1') -Reset @('DreamWind.RuntimeTerrain -1'))
        (Feature WindTerrainPasses 'Runtime terrain map (its capture scatter and derive passes)' 'yes' events 11 '' Grass -Event 'RenderGraphExecute - DreamWind\.Terrain')
        (Feature WindVolumeAll 'Wind volume (update + material reads, frame)' 'yes' toggle 11 '' Grass `
            -Off @('DreamWind.Volume 0') -On @('DreamWind.Volume 1') -Reset @('DreamWind.Volume -1'))
        (Feature WindGrass 'Wind grass, 40 000 blades (all its draws, frame)' 'per level' toggle 11 '' Grass `
            -Off @('ShowFlag.InstancedStaticMeshes 0') -On @('ShowFlag.InstancedStaticMeshes 1') -Reset @('ShowFlag.InstancedStaticMeshes 1'))
    )
}
$MapName = Split-Path $Map -Leaf
if ($Only.Count -gt 0)
{
    $Unknown = @($Only | Where-Object { $Features.Key -notcontains $_ })
    if ($Unknown) { throw "Unknown feature: $($Unknown -join ', ') (known: $($Features.Key -join ', '))" }
    $Features = @($Features | Where-Object { $Only -contains $_.Key })
}

function Median([double[]]$Values)
{
    $Sorted = @($Values | Sort-Object)
    if ($Sorted.Count -eq 0) { return [double]::NaN }
    if ($Sorted.Count % 2) { return $Sorted[[int][Math]::Floor($Sorted.Count / 2)] }
    return ($Sorted[$Sorted.Count / 2 - 1] + $Sorted[$Sorted.Count / 2]) / 2.0
}
function Get-GpuBusy([int]$Reads = 5)
{
    $Values = for ($I = 0; $I -lt $Reads; ++$I)
    {
        [double](nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | Select-Object -First 1)
        Start-Sleep -Milliseconds 600
    }
    return (Median $Values)
}

if (-not $Log)
{
    # --- Wait for a quiet GPU ---------------------------------------------------------------------------------------
    $Deadline = (Get-Date).AddMinutes($WaitMinutes)
    $Busy = Get-GpuBusy
    while ($Busy -ge $MaxBusy)
    {
        if ((Get-Date) -gt $Deadline)
        {
            Write-Host "Not started: the GPU stayed $Busy% busy (at least $MaxBusy%) for $WaitMinutes minutes. Using it now:" -ForegroundColor Yellow
            nvidia-smi pmon -c 1 -s u | Where-Object { $_ -notmatch '^\s*#' -and $_ -match '\s(\d+)\s+\S+\s+(\d+)\s' } | ForEach-Object { "  $_" }
            exit 2
        }
        Write-Host "GPU $Busy% busy; waiting for it to drop under $MaxBusy%..."
        Start-Sleep -Seconds 30
        $Busy = Get-GpuBusy
    }
    $BusyBefore = $Busy

    # --- The script ---------------------------------------------------------------------------------------------------
    $Lines = @('# Measure-ShowcaseGpu.ps1', 'r.ProfileGPU.ShowUI 0', 'r.ProfileGPU.TableFormatting 0', 'r.ProfileGPU.ShowHeader 1',
        'r.ShowMaterialDrawEvents 1', 'ShowFlag.EyeAdaptation 0', 'DreamWorld.Pause 1', 'settle', "wait $WarmUp")
    $Profile = @(); for ($I = 0; $I -lt $Samples; ++$I) { $Profile += 'ProfileGPU', 'wait 12' }
    foreach ($F in $Features)
    {
        $Lines += "mark $($F.Key)|setup"
        $Lines += if ($F.Weather) { "DreamWeather.Set $($F.Weather) 0" } else { 'DreamWeather.Clear 0' }
        $Lines += "time $($F.Hour.ToString($Invariant))"
        $Lines += $Views[$F.View] + $F.Setup + @('settle', "wait $Hold")
        if ($Shots) { $Lines += "shot GPU_$($F.Key)" }
        if ($F.Kind -eq 'toggle')
        {
            $Lines += $F.Off + @('settle 20', 'wait 30', "mark $($F.Key)|off") + $Profile
            $Lines += $F.On + @('settle 20', 'wait 30', "mark $($F.Key)|on") + $Profile
        }
        else
        {
            $Lines += @("mark $($F.Key)|on") + $Profile
        }
        $Lines += $F.Reset
    }
    $Lines += 'mark end', 'wait 10', 'quit'

    # --- Run, sampling every process's GPU use alongside ----------------------------------------------------------------
    $Stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $Name = 'gpu-features'
    $HostRoot = if ($Project) { Split-Path ([System.IO.Path]::GetFullPath($Project)) -Parent } else { Split-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) -Parent }
    $Samples_ = Join-Path $HostRoot "Saved/ClaudeRuns/$Name-$Stamp.gpu.txt"
    New-Item -ItemType Directory -Force (Split-Path $Samples_) | Out-Null
    $Sampler = Start-Job -ArgumentList $Samples_ -ScriptBlock {
        param($Out)
        while ($true)
        {
            $Now = (Get-Date).ToString('o')
            $Rows = nvidia-smi pmon -c 1 -s u | Where-Object { $_ -notmatch '^\s*#' }
            $Ours = @(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -match $using:MapName } | ForEach-Object { $_.ProcessId })
            foreach ($Row in $Rows) { Add-Content -Path $Out -Value "$Now|$($Ours -join ',')|$Row" }
            Start-Sleep -Seconds 2
        }
    }
    try
    {
        $Params = @{ Name = $Name; Lines = $Lines; Map = $Map; ResX = $ResX; ResY = $ResY; FixedFps = 30; OffScreen = $true; Force = $true; TimeoutMinutes = 60 }
        if ($Project) { $Params.Project = $Project }
        $Output = @(& (Join-Path $PSScriptRoot '..\TestHost\Invoke-DreamSkyScript.ps1') @Params)
        $Ok = $LASTEXITCODE -eq 0
    }
    finally
    {
        Stop-Job $Sampler; Remove-Job $Sampler -Force
    }
    $Log = [string]$Output[-1]
    if (-not $Ok -or -not (Test-Path $Log))
    {
        Write-Host "FAILED: the game run did not finish (log $Log)" -ForegroundColor Red
        exit 1
    }
    Set-Content -Path ([System.IO.Path]::ChangeExtension($Log, '.busy.txt')) -Value $BusyBefore
    Move-Item $Samples_ ([System.IO.Path]::ChangeExtension($Log, '.gpu.txt')) -Force -ErrorAction SilentlyContinue
}

# --- Read the dumps back ----------------------------------------------------------------------------------------------
# A dump (r.ProfileGPU.TableFormatting 0): "GPU Profile for Frame N - Graphics pipeline 0 - GPU 0", a header whose
# "Frame Time" line gives the frame, then one line per event: <3 spaces a level><percent>% <ms>ms[ + <ms> Wait]   <name>.
# Other queues come first as "<Queue> pipeline N - GPU 0" with lines of their own; an event is summed from any queue.
$NodePattern = '^(?<lead> *)(?<pct>\d+\.\d)% +(?<ms>\d+\.\d+)ms(?: \+ (?<wait>\d+\.\d+) Wait)?   (?<name>.*)$'
$Label = $null
$Marks = New-Object System.Collections.Generic.List[object]
$Dumps = New-Object System.Collections.Generic.List[object]
$Current = $null
$Stack = @()
foreach ($Line in [System.IO.File]::ReadLines($Log))
{
    $Text = $Line -replace '^\[[^\]]*\]\[[^\]]*\]Log\w+: (?:Display: |Warning: )?', ''
    if ($Text -match 'Script: \[\d+\] mark (?<label>.+)$' -or $Text -match 'Script: mark (?<label>.+)$')
    {
        $Label = $Matches.label.Trim()
        if ($Line -match '^\[(?<t>\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d):(?<ms>\d+)\]')
        {
            $At = [datetime]::ParseExact($Matches.t, 'yyyy.MM.dd-HH.mm.ss', $Invariant, [System.Globalization.DateTimeStyles]::AssumeUniversal -bor [System.Globalization.DateTimeStyles]::AdjustToUniversal)
            $Marks.Add([pscustomobject]@{ Label = $Label; At = $At })
        }
        $Current = $null
        continue
    }
    $Graphics = $Text -match 'GPU Profile for Frame (?<frame>\d+)'
    if ($Graphics -or $Text -match '^\w+ pipeline \d+ - GPU \d+\s*$')
    {
        if (-not ($Graphics -and $Current -and $Current.Pending))
        {
            $Current = [pscustomobject]@{ Label = $Label; Pending = -not $Graphics; Frame = 0.0; Events = New-Object System.Collections.Generic.List[object] }
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
    $Stack = @($Stack | Select-Object -First $Level) + $Matches.name
    $Current.Events.Add([pscustomobject]@{ Stack = $Stack; Ms = [double]::Parse($Matches.ms, $Invariant) })
}

# The events of a dump matching a pattern (and lying under an event matching Under, if given), an event inside one
# already counted left out.
function Get-EventMs($Dump, [string]$Pattern, [string]$Under)
{
    $Sum = 0.0
    $Found = $false
    foreach ($E in $Dump.Events)
    {
        $Name = $E.Stack[-1]
        if ($Name -notmatch $Pattern) { continue }
        $Above = @($E.Stack | Select-Object -SkipLast 1)
        if (@($Above | Where-Object { $_ -match $Pattern }).Count -gt 0) { continue }
        if ($Under -and @($Above | Where-Object { $_ -match $Under }).Count -eq 0) { continue }
        $Sum += $E.Ms
        $Found = $true
    }
    return [pscustomobject]@{ Ms = $Sum; Found = $Found }
}

# Other work on the GPU (nvidia-smi pmon, every 2 s): the sum of the SM use of every process but this game's.
$Busy = @()
$GpuFile = [System.IO.Path]::ChangeExtension($Log, '.gpu.txt')
if (Test-Path $GpuFile)
{
    $Busy = foreach ($Row in Get-Content $GpuFile)
    {
        $At, $Ours, $Rest = $Row -split '\|', 3
        $Cols = -split $Rest
        if ($Cols.Count -lt 5 -or $Cols[3] -notmatch '^\d+$') { continue }
        [pscustomobject]@{ At = ([datetime]::Parse($At, $Invariant)).ToUniversalTime(); Pid = $Cols[1]; Sm = [int]$Cols[3]; Ours = @($Ours -split ',') }
    }
}
$BusyBeforeFile = [System.IO.Path]::ChangeExtension($Log, '.busy.txt')
$BusyBefore = if (Test-Path $BusyBeforeFile) { Get-Content $BusyBeforeFile } else { '?' }
function Get-OthersBusy([string]$Key)
{
    $Mine = @($Marks | Where-Object { $_.Label -like "$Key|*" })
    if ($Mine.Count -eq 0 -or -not $Busy) { return '?' }
    $From = $Mine[0].At
    $Next = @($Marks | Where-Object { $_.At -gt $Mine[-1].At -and $_.Label -notlike "$Key|*" } | Select-Object -First 1)
    $To = if ($Next) { $Next[0].At } else { $Mine[-1].At.AddMinutes(1) }
    $Seconds = @($Busy | Where-Object { $_.At -ge $From -and $_.At -le $To } | Group-Object { $_.At.ToString('o') })
    if ($Seconds.Count -eq 0) { return '?' }
    $PerRead = foreach ($S in $Seconds)
    {
        $Sum = 0
        foreach ($P in $S.Group) { if ($P.Ours -notcontains $P.Pid) { $Sum += $P.Sm } }
        $Sum
    }
    return [Math]::Round((Median @($PerRead | ForEach-Object { [double]$_ })), 0)
}

# --- The table --------------------------------------------------------------------------------------------------------
$Rows = foreach ($F in $Features)
{
    $On = @($Dumps | Where-Object { $_.Label -eq "$($F.Key)|on" -and $_.Frame -gt 0 })
    $Off = @($Dumps | Where-Object { $_.Label -eq "$($F.Key)|off" -and $_.Frame -gt 0 })
    $Note = ''
    if ($On.Count -eq 0)
    {
        $Cost = 'n/a'; $Note = 'no dumps'
    }
    elseif ($F.Kind -eq 'events')
    {
        $Hits = @($On | ForEach-Object { Get-EventMs $_ $F.Event $F.Under } | Where-Object { $_.Found })
        $Cost = if ($Hits.Count) { (Median @($Hits.Ms)).ToString('0.000', $Invariant) } else { '0' }
        if ($Hits.Count -lt $On.Count) { $Note = "ran in $($Hits.Count) of $($On.Count) frames (per frame it ran)" }
    }
    else
    {
        $Metric = if ($F.Event) { $F.Event } else { '' }
        $OnMs = if ($Metric) { @($On | ForEach-Object { (Get-EventMs $_ $Metric '').Ms }) } else { @($On.Frame) }
        $OffMs = if ($Metric) { @($Off | ForEach-Object { (Get-EventMs $_ $Metric '').Ms }) } else { @($Off.Frame) }
        $Cost = ((Median $OnMs) - (Median $OffMs)).ToString('0.000', $Invariant)
        $Note = "on $((Median $OnMs).ToString('0.000', $Invariant)) - off $((Median $OffMs).ToString('0.000', $Invariant)) ms, " + $(if ($Metric) { 'its GPU events' } else { 'whole frame' })
    }
    if ($F.Key -in 'Rain', 'Snow', 'Dust', 'Debris') { $Note = "$Note; the draws only (the GPU simulation runs while hidden, some 0.01 ms)" }
    [pscustomobject]@{ Feature = $F.Name; Default = $F.Default; Cost = $Cost; N = $On.Count; Others = Get-OthersBusy $F.Key; How = "$($F.Kind): $(if ($F.Weather) { $F.Weather } else { 'schedule' }) $($F.Hour)h $($F.View)"; Note = $Note }
}

$Md = New-Object System.Collections.Generic.List[string]
$Md.Add("# Showcase GPU costs ($ResX x $ResY)")
$Md.Add('')
$Md.Add("Log: ``$(Split-Path $Log -Leaf)``. GPU busy before the run: $BusyBefore%. Each cost is a median of $Samples ProfileGPU dumps (ms).")
$Md.Add("'Other GPU use' is the median summed SM use (%) of every process but the game while the feature was timed.")
$Failed = @(Select-String -Path $Log -Pattern 'Failed to compile Material (?<m>\S+)' | ForEach-Object { $_.Matches[0].Groups['m'].Value } | Sort-Object -Unique)
if ($Failed) { $Md.Add(''); $Md.Add("**WARNING**: these materials failed to compile and drew the engine's default instead: $($Failed -join ', ').") }
$Md.Add('')
$Md.Add('| Feature | On by default | Cost (ms) | Dumps | Other GPU use (%) | How | Note |')
$Md.Add('| :-- | :-- | --: | --: | --: | :-- | :-- |')
foreach ($R in $Rows) { $Md.Add("| $($R.Feature) | $($R.Default) | $($R.Cost) | $($R.N) | $($R.Others) | $($R.How) | $($R.Note) |") }
$MdPath = [System.IO.Path]::ChangeExtension($Log, '.md')
Set-Content -Path $MdPath -Value $Md -Encoding utf8
$Md | ForEach-Object { Write-Host $_ }
Write-Host "Table: $MdPath"
exit 0
