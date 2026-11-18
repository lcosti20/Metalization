# Effects and dev tools

Files: `MetalFX.h/.cpp`, `MetalDevAutopilot.h/.cpp`, `Tools/AssetPipeline/smoke_test.ps1`, `Tools/AssetPipeline/ue_setup_fx.py`, `Source/MetalizationEditor/MetalFXAuthoring.h/.cpp`

## Effects: `AMetalFX`

Combat effects are Niagara systems in `/Game/Metalization/FX`. `AMetalFX` is a short-lived actor holding one `UNiagaraComponent` and an optional point light; every call site goes through its static helpers.

| Helper | System | Look | Used for |
|---|---|---|---|
| `SpawnMuzzleFlash(World, Location, Direction, Color, Size)` | `NS_MuzzleFlash` | Hot star, flame petals and sparks thrown along `Direction` | Every gun that fires (robots, Brute rockets, capital ship turrets) |
| `SpawnImpact(World, Location, Normal, Color, Size)` | `NS_Impact` | Flash, sparks bouncing off along `Normal` (they fall with gravity), dust puff | Hitscan hits, projectiles that hit without exploding |
| `SpawnExplosion(World, Location, Color, Radius, Lifetime)` | `NS_Explosion` | White-hot core, fireball puffs, flying sparks, rising smoke | Grenades, rockets, barrage shells, robot deaths, arms and weapons shot off, ship subsystems and wrecks |
| `SpawnBurst(World, Location, Color, Radius, Lifetime)` | `NS_Burst` | Swelling glow with drifting motes | EMP pulses, pickups, consoles, hijacks, reload sparks, dispensers, jetpack puffs |
| `SpawnBeam(World, Start, End, Color, Width, Lifetime)` | `NS_Tracer` (`Lifetime > 0`) or `NS_Beam` (`<= 0`) | Glowing core and halo meshes with crackling sparks; a tracer thins and fades in 0.1 s | Hitscan tracers, the Hack Tether, laser sights, dispenser welding arcs |

How it fits together:

- **Tint:** each system's particles take their colour from the `User.Color` parameter, which the helper sets.
- **Size:** each system is authored at a reference size (10 cm for flashes and impacts, 100 cm radius for explosions and bursts). The helper scales the Niagara component to the requested size, and the systems' Apply Owner Scale module scales sprite sizes to match. Lightweight emitters always simulate in local space, so positions and speeds scale too. Gravity is a world-space acceleration, so sparks fall down whichever way the effect faces.
- **Beams:** a beam is a 100 cm cylinder along local Z. `SetBeamEndpoints` places it between the points and stretches it (X/Y = width / 100, Z = length / 100).
- **Light:** the point light sits on the unscaled root, so its radius doesn't change with the effect's scale. It fades over `Lifetime` (squared, so it drops fast).
- **Clean-up:** the actor destroys itself once the light is gone and the Niagara component has finished. Persistent beams loop forever until their owner destroys them.

`MetalAssets::GetEffect(Name)` loads a system by path once and roots it, so it isn't reloaded between shots. `Config/DefaultGame.ini` force-cooks `/Game/Metalization/FX`, because nothing references these systems except by path. `MetalAssets::GetGlowMaterial()` still serves `M_FX_Glow` to glowing props (consoles, lifts, pickups, projectiles, trigger zones, jet flames). Any new asset loaded only by path from C++ must live in a force-cooked folder or get its own `DirectoriesToAlwaysCook` entry.

### How the systems are built

UE Python can't edit Niagara emitters, so the systems are generated in C++ by the editor-only `MetalizationEditor` module. `ue_setup_fx.py` runs as part of the gameplay stage, or on its own:

```
UnrealEditor-Cmd.exe Metalization.uproject -run=pythonscript -script=<project>/Tools/AssetPipeline/ue_setup_fx.py -unattended -nullrhi
```

1. It creates the materials in `/Game/Metalization/Materials`:
   - `M_FX_Sprite`: an additive soft dot.
   - `MI_FX_SpriteHot`: a brighter, whiter instance of `M_FX_Sprite`.
   - `M_FX_Smoke`: translucent puffs.
   - `M_FX_Fire`: translucent glowing puffs. Fireballs use this rather than an additive material because additive glow vanishes against bright floors.
   - `M_FX_BeamCore`: additive, brightest where the cylinder faces the camera.
2. It then calls `UMetalFXAuthoring::BuildEffectSystems`. For each system, this:
   - clones the engine's `DirectionalBurstLightweight` template (a lightweight, or *stateless*, emitter),
   - duplicates its emitter once per layer,
   - sets each layer's module properties from text in the same format `ExportText` writes,
   - recomputes the distributions' runtime values (Min/Max, curve lookup tables) from their editor channels,
   - binds Initialize Particle's colour to `User.Color`,
   - and compiles and saves the system.

Tune an effect by editing its block in `BuildEffectSystems`, rebuilding the editor target and running `ue_setup_fx.py` again. Hand edits made in the Niagara editor are overwritten on the next run. `UMetalFXAuthoring::DescribeNiagaraSystem(path)` dumps any system's layers, modules and renderers as text, which helps when picking property names and value formats.

Traps:

- The stateless classes live in Niagara's `Internal` folder. `MetalizationEditor.Build.cs` adds the `Internal` folders of Niagara and NiagaraShader to its include path.
- `FNiagaraDistributionBase` isn't exported, so its struct is found by name (`/Script/Niagara.NiagaraDistributionBase`). Its `UpdateValuesFromDistribution` is called through the vtable.
- A new mesh renderer already holds one empty mesh entry, so set `Meshes` to a single entry instead of appending to it.

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
| `fx` | Outpost | Every combat effect set off in a row in front of the camera (three times), plus a persistent beam |
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

### Production Alpha scenarios

These run on `MZ_ProductionAlpha` (and `MZ_Title`). Most jump ahead with `-MetalObjective=N` and teleport to
the beat:

- `alpha`: sprint the trench.
- `bridge`: double jump.
- `lift`: ride the cliff lift.
- `swap`: Repeater pickup and three swaps, filmed from a fixed side camera.
- `bunker`: clears the earlier squad, then lobs grenades.
- `sneak`: crawl pipe.
- `sniper`: scope from the perch.
- `hack`: guard, console, gate and reinforcements.
- `title`: the title screen.
- `jetpack` (Outpost): double jumps in the hangar to show the jetpack firing.

The autopilot also has steps for sprint, sneak, aim and scope, next weapon, grenades and aiming at
marksmen. Its log adds sprint, sneak, swap and scope state for the player and suspicion for every robot.
