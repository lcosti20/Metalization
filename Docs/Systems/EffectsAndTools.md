# Effects and dev tools

Files: `MetalFX.h/.cpp`, `MetalDevAutopilot.h/.cpp`, `Tools/AssetPipeline/smoke_test.ps1`

## Effects: `AMetalFX`

All combat effects are a stand-in that uses no effect assets: a short-lived actor with an engine mesh using the `M_FX_Glow` material (unlit additive, parameters `Color` and `Intensity`), plus an optional point light. Every call site goes through the static helpers, so replacing them with Niagara systems later is a change in one file.

| Helper | Shape | Used for |
|---|---|---|
| `SpawnBeam(World, Start, End, Color, Width, Lifetime)` | Engine cylinder stretched between two points; thins to nothing as it fades | Hitscan tracers, the Hack Tether beam |
| `SpawnBurst(World, Location, Color, Radius, Lifetime)` | Engine sphere that grows to `Radius`, with a light reaching four times as far | Muzzle flashes, impacts, explosions |

The effect scales and fades its glow and light over `Lifetime` (the scale follows a square root, so it changes fast at first), then destroys itself. With `Lifetime <= 0` it is persistent: the caller keeps it, moves it with `SetBeamEndpoints`, and destroys it (the tether beam works this way).

`MetalAssets::GetGlowMaterial()` loads `M_FX_Glow` by path and caches it. Because it is only referenced by path from C++, the cooker can't discover it; `Config/DefaultGame.ini` force-cooks `/Game/Metalization/Materials` (and `/Data`) with `DirectoriesToAlwaysCook`. Any new asset loaded only by path from C++ must live in one of those folders, or get its own entry.

## Dev autopilot: `AMetalDevAutopilot`

A non-shipping helper for automated smoke tests and screenshots. `AMetalGameMode::BeginPlay` spawns it when any of these command-line options is present:

| Option | Effect |
|---|---|
| `-MetalAutopilot[=<scenario>]` | Run a scripted scenario (default `combat`) |
| `-MetalTeleport=X,Y,Z,Yaw` | Move the player on start |
| `-MetalCam=X,Y,Z,Pitch,Yaw[;...]` | View from fixed cameras instead of the player, one per screenshot |
| `-MetalShots=2,5.5,9` | Take screenshots at these times |
| `-MetalMenuLevel=<name>` | Level Select entry the `menu` scenario picks |
| `-MetalObjective=N` | Start the mission at objective N (handled by `AMetalMission`) |
| `-MetalQuitAt=12` | Quit at this time |

The clock starts once the player has a pawn and uses unpaused time, so scenarios keep running while the pause menu is open (the actor also ticks while paused).

### Scenarios

A scenario is a list of `{time, EStep}` pairs in `Scenarios()` in the `.cpp`. Steps use the same functions as real input (`ToggleTether`, `StartFire`, `SetBoostHeld`, `PressButton` on the pause menu...), plus a few test shortcuts (teleport to the console, wreck the enemy ship). "Aim" steps keep the control rotation on a target every tick until the next aim step clears it.

| Scenario | Level | Covers |
|---|---|---|
| `combat` | Outpost | Shoot, hijack the nearest enemy, fight with it, disconnect |
| `gate` | Outpost | Hijack a gate guard, operate the console, disconnect |
| `boss` | Outpost | Hijack the Brute and use its miniguns |
| `arena` | Arena | Run, double-jump flip, turn, shoot with the action camera |
| `flight` | Flight Test | Link to a Stalker, climb, cruise, boost, shoot a gun arm off an enemy, dodge, disconnect |
| `hangar` | Space Battle | Spawn aboard, link, fly out through the portal, shoot a shield generator, fly back in |
| `battle` | Space Battle | Fly into the enemy hangar, shoot life support, wreck the ship, check victory |
| `dogfight` | Space Battle | No inputs: AI pilots and turrets fight it out |
| `menu` | any | Pause menu: Level Select, Back, switch level, Continue |
| `ships` | Space Battle | No inputs: exterior screenshots |

To add a step, add an `EStep` value **and** its name at the same position in the `Names` table in `RunStep` (the log uses it), then handle it in the `switch`.

Once a second the autopilot logs the player's pawn, health, position and ammo, every ship's tickets, hull and subsystem health, and every AI robot's state (`[Autopilot]` lines in the log).

### Running a smoke test

Build the editor target, then:

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario combat
```

Screenshots land in `Saved\Screenshots\WindowsEditor` and the log in `Saved\smoke_<scenario>.log`. Add `-Packaged` to run the scenario in the packaged Development build instead.
