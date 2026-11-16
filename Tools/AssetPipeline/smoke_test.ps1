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
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario sound  # pause menu: Sound sliders (music, then beep-boops, down and up), Credits
    powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario ships  # exterior views of MZ_SpaceBattle
  Production Alpha (MZ_ProductionAlpha / MZ_Title):
    -Scenario alpha   sprint down the trench under the barrage, jump the first barricade
    -Scenario swap    pick up the Repeater and swap weapon arms (side camera)
    -Scenario lift    ride the anti-grav lift up the cliff
    -Scenario sneak   crouch through the crawl pipe past the yard patrols (logs suspicion)
    -Scenario sniper  scope in from the perch and shoot a marksman
    -Scenario hack    hack the gate guard through the shield fence (solving the hack puzzle) and open the gate
    -Scenario field   shoot the gate guard through the shield fence: shots ripple it, the guard holds fire; then hack through it
    -Scenario hackfail  hack puzzle outcomes: wrong compile -> 4 s firewall (tether refused), [X] cancel, then a solve
    -Scenario title   the title screen
    -Scenario bridge  double jump across the collapsed bridge onto the higher ledge
    -Scenario bunker  lob EMP grenades into the bunker and shoot the Reds it flushes out
    -Scenario checkpoint  scrap a guard, enter the yard checkpoint, scrap another, die: the reload keeps the first gone, brings the second back
    -Scenario jetpack double jump in the Outpost hangar, filmed from behind (jetpack flames, back rack)
    -Scenario pickups  by the gate: grab the Rocket Launcher, fire + reload (reload bar), take grenades and ammo
    -Scenario lowhealth  drop to 20% health in the Outpost hangar: red flash + double beep each second, then heal
  Robot dispensers (MZ_DispenserTest):
    -Scenario dispenser  scrap an assigned guard and watch it be rebuilt; open the gate to start the timed
                         dispenser; shut both down from the console
    -Scenario yard       Production Alpha patrol yard: scrap a patrol, watch the dispenser rebuild it, shut it down
    -Scenario command    Production Alpha: open the gate, the command-yard dispensers start; shut them down on the catwalk
  -Scenario endgame   Production Alpha final area out of order: yard before Brute, die after the dispenser checkpoint (their robots come back), finish the Brute
  -Scenario yardcheckpoint  Production Alpha patrol yard: a rebuilt patrol saved at the checkpoint comes back once, not twice

Screenshots land in Saved\Screenshots\WindowsEditor, the log in Saved\smoke_<scenario>.log.
Requires the editor target to be built. See AMetalDevAutopilot for all command-line switches.

-Packaged runs the same scenario in the packaged Development build (Windows\Metalization.exe, made with
BuildCookRun -archivedirectory=<project>) instead of the editor. Its screenshots land in
Windows\Metalization\Saved\Screenshots\Windows; the log is Saved\smoke_<scenario>_packaged.log.
#>
param(
    [ValidateSet("combat", "gate", "boss", "arena", "flight", "hangar", "battle", "dogfight", "menu", "ships",
                 "sound", "alpha", "swap", "lift", "sneak", "sniper", "hack", "hackfail", "field", "title", "bridge", "bunker", "jetpack", "checkpoint", "lowhealth", "pickups", "dispenser", "yard", "command", "endgame", "yardcheckpoint")]
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
    "sound"  = @("-MetalShots=1.7,2.5,4.2", "-MetalQuitAt=5.6")
    "alpha"  = @("-MetalObjective=1", "-MetalTeleport=1300,0,120,0", "-MetalShots=1.5,3.5,5.5,8", "-MetalQuitAt=10")
    "swap"   = @("-MetalObjective=5", "-MetalTeleport=12650,350,1330,0", "-MetalCam=12790,470,1350,-8,-140",
                 "-MetalShots=1.5,3.25,3.45,3.65,3.85,4.1,5.4,9", "-MetalQuitAt=10")
    "lift"   = @("-MetalObjective=4", "-MetalTeleport=10850,0,370,0", "-MetalShots=1.2,2.2,3.0,4.5", "-MetalQuitAt=6")
    "sneak"  = @("-MetalObjective=7", "-MetalTeleport=19000,-1225,1330,0", "-MetalShots=2,6,10,14", "-MetalQuitAt=16")
    "sniper" = @("-MetalObjective=9", "-MetalTeleport=23380,1000,2240,0", "-MetalShots=0.8,2.6,3.2,5.2", "-MetalQuitAt=7")
    "hack"   = @("-MetalObjective=10", "-MetalTeleport=30650,650,120,0", "-MetalShots=1.3,1.9,2.3,4,7,9", "-MetalQuitAt=11")
    "field" = @("-MetalObjective=10", "-MetalTeleport=30650,650,120,0", "-MetalShots=1.6,2.2,4.5,6.0,8.5", "-MetalQuitAt=9.5")
    "hackfail" = @("-MetalObjective=10", "-MetalTeleport=30650,650,120,0", "-MetalShots=1.9,2.5,3.0,7.2,8.3,9.5", "-MetalQuitAt=12")
    "title"  = @("-MetalShots=1.5,4", "-MetalQuitAt=5")
    "jetpack" = @("-MetalTeleport=-300,0,120,0", "-MetalCam=-620,-260,190,-4,38", "-MetalShots=0.8,1.36,1.45,1.6,3.38", "-MetalQuitAt=5")
    "lowhealth" = @("-MetalTeleport=-300,0,120,0", "-MetalShots=0.53,0.9,1.53,2.53,4.2", "-MetalQuitAt=5")
    "pickups" = @("-MetalObjective=10", "-MetalTeleport=30300,-700,120,0", "-MetalShots=0.3,1.6,4.45,4.9,5.3,6.8,7.6", "-MetalQuitAt=8.5")
    "dispenser" = @("-MetalCam=1900,-850,230,-6,-20;1900,-850,230,-6,-20;1900,-850,230,-6,-20;1900,-850,230,-6,-20;1900,850,230,-6,20;1900,850,230,-6,20;1900,850,230,-6,20;600,0,700,-25,0",
                    "-MetalShots=1.0,2.6,3.2,3.9,6.3,6.9,7.6,10.2", "-MetalQuitAt=14")
    "yard" = @("-MetalObjective=7", "-MetalTeleport=20650,1350,1330,-90", "-MetalCam=20700,250,1650,-12,50;20700,250,1650,-12,50;20700,250,1650,-12,50;21100,1900,1700,-30,-60",
               "-MetalShots=1.0,2.9,3.6,6.6", "-MetalQuitAt=8")
    # No -MetalCam: the tether aims from the view, so a fixed camera would make the hack miss.
    "command" = @("-MetalObjective=10", "-MetalTeleport=30650,650,120,0", "-MetalShots=5.5,8.5,13.2,14.5", "-MetalQuitAt=16")
    "yardcheckpoint" = @("-MetalObjective=7", "-MetalTeleport=18600,-1225,1330,0", "-MetalQuitAt=19")
    "endgame" = @("-MetalObjective=10", "-MetalTeleport=30650,650,120,0", "-MetalShots=8.7,12.4", "-MetalQuitAt=27")
    "bridge" = @("-MetalObjective=3", "-MetalTeleport=9100,0,120,0", "-MetalShots=1.0,1.4,2.0,3.0", "-MetalQuitAt=4")
    "checkpoint" = @("-MetalObjective=7", "-MetalTeleport=18600,-1225,1330,0", "-MetalShots=1.2,2.2,3.0", "-MetalQuitAt=9")
    "bunker" = @("-MetalObjective=6", "-MetalTeleport=16550,500,1330,0", "-MetalShots=1.2,2.6,4.5,8,11", "-MetalQuitAt=13")
    "ships"  = @("-MetalCam=-150000,0,60000,-20,0;40000,-140000,25000,-19,124;18000,30000,2000,-2,90;-18000,-30000,2000,-2,-90",
                 "-MetalShots=1.5,2.5,3.5,4.5", "-MetalQuitAt=5.5")
}
$maps = @{ "alpha" = "MZ_ProductionAlpha"; "sound" = "MZ_ProductionAlpha"; "swap" = "MZ_ProductionAlpha"; "lift" = "MZ_ProductionAlpha"; "sneak" = "MZ_ProductionAlpha";
           "sniper" = "MZ_ProductionAlpha"; "hack" = "MZ_ProductionAlpha"; "hackfail" = "MZ_ProductionAlpha"; "field" = "MZ_ProductionAlpha"; "bridge" = "MZ_ProductionAlpha"; "bunker" = "MZ_ProductionAlpha"; "checkpoint" = "MZ_ProductionAlpha"; "yard" = "MZ_ProductionAlpha"; "command" = "MZ_ProductionAlpha"; "endgame" = "MZ_ProductionAlpha"; "yardcheckpoint" = "MZ_ProductionAlpha"; "pickups" = "MZ_ProductionAlpha"; "title" = "MZ_Title"; "dispenser" = "MZ_DispenserTest"; "arena" = "MZ_Arena"; "flight" = "MZ_FlightTest"; "hangar" = "MZ_SpaceBattle"; "battle" = "MZ_SpaceBattle"; "dogfight" = "MZ_SpaceBattle"; "ships" = "MZ_SpaceBattle" }
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
Select-String -Path $log -Pattern "\[Autopilot\] (step|t=[0-9.]+ pawn)|\[Hack\]|\[Checkpoint\]|\[Dispenser\]|Objective|LogMetal: (Warning|Error)" |
    ForEach-Object { $_.Line -replace '^\[[^\]]*\]\[[^\]]*\]', '' }
Get-ChildItem $shots -Recurse -Filter *.png -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }

