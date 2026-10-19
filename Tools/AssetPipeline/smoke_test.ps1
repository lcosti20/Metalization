<#
Launches the demo in standalone game mode with the dev autopilot and collects screenshots.

    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1                 # combat loop
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario gate  # possession puzzle
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario boss  # hijack the boss
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario arena # free roam in MZ_Arena
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario flight # pilot a Stalker in MZ_FlightTest

Screenshots land in Saved\Screenshots\WindowsEditor, the log in Saved\smoke_<scenario>.log.
Requires the editor target to be built. See AMetalDevAutopilot for all command-line switches.
#>
param(
    [ValidateSet("combat", "gate", "boss", "arena", "flight")]
    [string]$Scenario = "combat",
    [string]$Editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
)
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$setups = @{
    "combat" = @("-MetalTeleport=2300,0,120,0", "-MetalShots=2.5,6.4,8.5,11", "-MetalQuitAt=13")
    "gate"   = @("-MetalObjective=2", "-MetalTeleport=9200,-650,120,0", "-MetalShots=1.3,2.3,4,7", "-MetalQuitAt=9")
    "boss"   = @("-MetalObjective=4", "-MetalTeleport=11400,1000,150,-28", "-MetalShots=1.4,3,5.5,9", "-MetalQuitAt=10")
    "arena"  = @("-MetalShots=0.8,1.7,2.5,2.65,2.8,3.3,4.6,6.9,7.8,9", "-MetalQuitAt=10")
    "flight" = @("-MetalShots=1.4,2.2,3.5,6,9,10.8,11.4,12.5,14", "-MetalQuitAt=15")
}
$maps = @{ "arena" = "MZ_Arena"; "flight" = "MZ_FlightTest" }
$map = "/Game/Metalization/Maps/" + $(if ($maps.ContainsKey($Scenario)) { $maps[$Scenario] } else { "MZ_Outpost_Demo" })
$log = "$Project\Saved\smoke_$Scenario.log".Replace("\", "/")
Remove-Item -Recurse -Force "$Project\Saved\Screenshots" -ErrorAction SilentlyContinue
$uproject = $Project.Replace("\", "/") + "/Metalization.uproject"
[string[]]$arguments = @($uproject, $map, "-game", "-windowed", "-ResX=1600", "-ResY=900",
    "-nosplash", "-unattended", "-log", "-abslog=$log", "-MetalAutopilot=$Scenario")
$arguments += $setups[$Scenario]
& $Editor @arguments | Out-Null
Select-String -Path $log -Pattern "\[Autopilot\] (step|t=[0-9.]+ pawn)|Objective|LogMetal: (Warning|Error)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
Get-ChildItem "$Project\Saved\Screenshots" -Recurse -Filter *.png | ForEach-Object { $_.FullName }

