<#
Launches the demo in standalone game mode with the dev autopilot and collects screenshots.

    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1                 # combat loop
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario gate  # possession puzzle
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario boss  # hijack the boss
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario arena # free roam in MZ_Arena
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario flight # pilot a Stalker in MZ_FlightTest
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario hangar # spawn aboard a capital ship, pilot a parked Stalker
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario battle # board the enemy hangar, shoot life support, win
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario dogfight # 90 s AI-only battle (pilots + turrets)
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario menu   # pause menu: Level Select, Back, switch to Arena, Continue
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario ships  # exterior views of MZ_SpaceBattle

Screenshots land in Saved\Screenshots\WindowsEditor, the log in Saved\smoke_<scenario>.log.
Requires the editor target to be built. See AMetalDevAutopilot for all command-line switches.

-Packaged runs the same scenario in the packaged Development build (Windows\Metalization.exe, made with
BuildCookRun -archivedirectory=<project>) instead of the editor. Its screenshots land in
Windows\Metalization\Saved\Screenshots\Windows; the log is Saved\smoke_<scenario>_packaged.log.
#>
param(
    [ValidateSet("combat", "gate", "boss", "arena", "flight", "hangar", "battle", "dogfight", "menu", "ships")]
    [string]$Scenario = "combat",
    [string]$Editor = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [switch]$Packaged
)
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$setups = @{
    "combat" = @("-MetalTeleport=2300,0,120,0", "-MetalShots=2.5,6.4,8.5,11", "-MetalQuitAt=13")
    "gate"   = @("-MetalObjective=2", "-MetalTeleport=9200,-650,120,0", "-MetalShots=1.3,2.3,4,7", "-MetalQuitAt=9")
    "boss"   = @("-MetalObjective=4", "-MetalTeleport=11400,1000,150,-28", "-MetalShots=1.4,3,5.5,9", "-MetalQuitAt=10")
    "arena"  = @("-MetalShots=0.8,1.7,2.5,2.65,2.8,3.3,4.6,6.9,7.8,9", "-MetalQuitAt=10")
    "flight" = @("-MetalShots=1.4,2.2,3.5,6,9,10.8,11.4,12.5,14", "-MetalQuitAt=15")
    "hangar" = @("-MetalShots=10.6,13.2,15.2,17,20,25,28.5,31.5", "-MetalQuitAt=33")
    "battle" = @("-MetalShots=16.2,17.5,24,27,31.5,33.5", "-MetalQuitAt=35")
    "dogfight" = @("-MetalCam=0,-30000,25000,-28,90;-45000,-110000,30000,-20,45;30000,40000,14000,-12,-110;-60000,0,40000,-35,0",
                   "-MetalShots=20,40,60,80", "-MetalQuitAt=90")
    # Runs in the Outpost, switches to the Arena from Level Select, and runs again there (shots 00-02, then 03-06).
    "menu"   = @("-MetalMenuLevel=Arena", "-MetalShots=1.2,2.0,2.8,5.2", "-MetalQuitAt=6.5")
    "ships"  = @("-MetalCam=-150000,0,60000,-20,0;40000,-140000,25000,-19,124;18000,30000,2000,-2,90;-18000,-30000,2000,-2,-90",
                 "-MetalShots=1.5,2.5,3.5,4.5", "-MetalQuitAt=5.5")
}
$maps = @{ "arena" = "MZ_Arena"; "flight" = "MZ_FlightTest"; "hangar" = "MZ_SpaceBattle"; "battle" = "MZ_SpaceBattle"; "dogfight" = "MZ_SpaceBattle"; "ships" = "MZ_SpaceBattle" }
$map = "/Game/Metalization/Maps/" + $(if ($maps.ContainsKey($Scenario)) { $maps[$Scenario] } else { "MZ_Outpost_Demo" })
$suffix = if ($Packaged) { "_packaged" } else { "" }
$log = "$Project\Saved\smoke_$Scenario$suffix.log".Replace("\", "/")
$shots = if ($Packaged) { "$Project\Windows\Metalization\Saved\Screenshots" } else { "$Project\Saved\Screenshots" }
Remove-Item -Recurse -Force $shots -ErrorAction SilentlyContinue
[string[]]$options = @("-windowed", "-ResX=1600", "-ResY=900", "-nosplash", "-unattended", "-log", "-abslog=$log", "-MetalAutopilot=$Scenario")
$options += $setups[$Scenario]
if ($Packaged) {
    $game = "$Project\Windows\Metalization.exe"
    if (-not (Test-Path $game)) { throw "No packaged build at $game" }
    # The root exe is a launcher that starts Windows\Metalization\Binaries\Win64\Metalization.exe: wait for both.
    # Start-Process joins arguments with spaces and no quoting: quote the ones with spaces (the log path).
    $quoted = (@($map) + $options) | ForEach-Object { if ($_ -match " ") { '"' + $_ + '"' } else { $_ } }
    Start-Process -FilePath $game -ArgumentList $quoted -Wait
    while (Get-Process -Name "Metalization" -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }
} else {
    $uproject = $Project.Replace("\", "/") + "/Metalization.uproject"
    & $Editor (@($uproject, $map, "-game") + $options) | Out-Null
}
Select-String -Path $log -Pattern "\[Autopilot\] (step|t=[0-9.]+ pawn)|Objective|LogMetal: (Warning|Error)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
Get-ChildItem $shots -Recurse -Filter *.png -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }

