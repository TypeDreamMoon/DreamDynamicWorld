<#
.SYNOPSIS
    Builds the DreamDynamicWorld test host and runs the plugin's automation tests headlessly.

.DESCRIPTION
    Runs at below-normal priority with no window (-nullrhi), so it can run while the machine is in use.
    The report lands in <host>/Saved/DreamDynamicWorldTests/<stamp>/ (index.json + the run's log); a summary
    of passed / failed tests is printed at the end.

    Exit codes: 0 every test passed; 1 the build failed or the editor never reported; 2 a test failed.

.EXAMPLE
    pwsh -NoProfile -File Tools\TestHost\Invoke-DreamDynamicWorldTests.ps1 -Project <host>\DreamDynamicWorldTestHost.uproject
    pwsh -NoProfile -File Tools\TestHost\Invoke-DreamDynamicWorldTests.ps1 -Project <host>\DreamDynamicWorldTestHost.uproject -NoBuild -Filter DreamWorld.Clock
#>
[CmdletBinding()]
param(
    # The host's .uproject. Defaults to the project two folders above this plugin.
    [string]$Project,

    # Automation filter (prefix). The plugin's tests live under DreamWind., DreamWorld., DreamSky. and DreamWeather.
    [string[]]$Filter = @('DreamWind.', 'DreamWorld.', 'DreamSky.', 'DreamWeather.', 'DreamCloud.', 'DreamSurface.', 'DreamAtmosphere.'),

    # The engine root. Defaults to the one recorded in the host's .dreamdynamicworld-testhost.json.
    [string]$EngineRoot,

    # Skip the build.
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

try { [System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal' } catch { }

if ([string]::IsNullOrWhiteSpace($Project))
{
    $HostRoot = Split-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) -Parent
    $Project = Get-ChildItem -Path $HostRoot -Filter '*.uproject' | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Project -or -not (Test-Path $Project)) { Write-Host 'FAILED: no host .uproject; pass -Project.' -ForegroundColor Red; exit 1 }
$Project = [System.IO.Path]::GetFullPath($Project)
$HostRoot = Split-Path $Project -Parent
$ProjectName = [System.IO.Path]::GetFileNameWithoutExtension($Project)

if ([string]::IsNullOrWhiteSpace($EngineRoot))
{
    $Record = Join-Path $HostRoot '.dreamdynamicworld-testhost.json'
    if (Test-Path $Record) { $EngineRoot = (Get-Content $Record -Raw | ConvertFrom-Json).engine }
}
if ([string]::IsNullOrWhiteSpace($EngineRoot)) { Write-Host 'FAILED: no engine; pass -EngineRoot.' -ForegroundColor Red; exit 1 }

# The derived data caches live off the system drive (user environment); a process started from a shell that
# predates that setting would otherwise rebuild them under the default location.
foreach ($Name in 'UE-ZenDataPath', 'UE-LocalDataCachePath')
{
    $Value = [Environment]::GetEnvironmentVariable($Name, 'User')
    if ($Value) { [Environment]::SetEnvironmentVariable($Name, $Value, 'Process') }
}

$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path $HostRoot "Saved\DreamDynamicWorldTests\$Stamp"
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null

if (-not $NoBuild)
{
    Write-Host "== Build (${ProjectName}Editor)" -ForegroundColor Cyan
    $BuildLog = Join-Path $RunDir 'build.log'
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') "${ProjectName}Editor" Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE -NoEngineChanges *> $BuildLog
    if ($LASTEXITCODE -ne 0)
    {
        Select-String -Path $BuildLog -Pattern 'error ' | Select-Object -First 30 | ForEach-Object { Write-Host $_.Line }
        Write-Host "FAILED: build (log $BuildLog)" -ForegroundColor Red
        exit 1
    }
}

Write-Host '== Tests' -ForegroundColor Cyan
$Editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Log = Join-Path $RunDir 'editor.log'
$Tests = ($Filter -join '+')
$Arguments = "`"$Project`" -ExecCmds=`"Automation RunTests $Tests`" -TestExit=`"Automation Test Queue Empty`" -ReportExportPath=`"$RunDir`" -abslog=`"$Log`" -nullrhi -unattended -nopause -nosplash -nosound -NoDreamShaderEditorBridge"
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -PassThru -WindowStyle Hidden
try { $Process.PriorityClass = 'BelowNormal' } catch { }
$Process.WaitForExit()

$Index = Join-Path $RunDir 'index.json'
if (-not (Test-Path $Index)) { Write-Host "FAILED: no report (log $Log)" -ForegroundColor Red; exit 1 }

$Report = Get-Content $Index -Raw -Encoding UTF8 | ConvertFrom-Json
$Passed = @($Report.tests | Where-Object { $_.state -eq 'Success' })
$Failed = @($Report.tests | Where-Object { $_.state -ne 'Success' })
foreach ($Test in $Failed)
{
    Write-Host "  FAIL $($Test.fullTestPath)" -ForegroundColor Red
    foreach ($Entry in @($Test.entries) | Where-Object { $_.event.type -ne 'Info' } | Select-Object -First 6)
    {
        Write-Host "       $($Entry.event.message)"
    }
}
Write-Host "  $($Passed.Count) passed, $($Failed.Count) failed  (report $RunDir)"
if ($Failed.Count -gt 0) { exit 2 }
if ($Passed.Count -eq 0) { Write-Host 'FAILED: no test ran.' -ForegroundColor Red; exit 1 }
exit 0
