<#
.SYNOPSIS
    Shot tour of the showcase levels (Setup -WorldShowcase): every weather of the schedule, snow and dust by day and a
    storm by night, and the dusk sequence, off screen at 1920x1080.

.DESCRIPTION
    Runs tour_schedule.dws and tour_forced.dws on Lvl_DreamWorldShowcase and tour_dusk.dws on Lvl_DreamWorldShowcaseDusk
    through Tools/TestHost/Invoke-DreamSkyScript.ps1. The shots land in <host>/Saved/Screenshots/<platform>/DreamWorld
    as SHOW_<weather>_<view>.png and SHOW_Dusk_<nn>.png; Make-ShowcaseSheet.py tiles them into an overview.

.EXAMPLE
    pwsh -NoProfile -File Tools\Showcase\Invoke-ShowcaseTour.ps1 -Project <host>\DreamDynamicWorldTestHost.uproject
#>
[CmdletBinding()]
param(
    [string]$Project,
    [string[]]$Parts = @('schedule', 'forced', 'dusk')
)
$Runner = Join-Path $PSScriptRoot '..\TestHost\Invoke-DreamSkyScript.ps1'
foreach ($Part in $Parts)
{
    $Map = if ($Part -eq 'dusk') { '/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcaseDusk' } else { '/DreamDynamicWorld/Showcase/Maps/Lvl_DreamWorldShowcase' }
    $Splat = @{ Script = (Join-Path $PSScriptRoot "tour_$Part.dws"); Name = "showcase_$Part"; Map = $Map; FixedFps = 30; ResX = 1920; ResY = 1080; OffScreen = $true; Force = $true }
    if ($Project) { $Splat.Project = $Project }
    & $Runner @Splat
}
