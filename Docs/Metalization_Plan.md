# Metalization: plan & demo guide

This document covers three things: what Metalization is, what was wrong with the first attempt, and how to run, play, and extend the current demo in Unreal Engine 5.8.

---

## 1. What we're building

A third-person robot shooter. The defining pillars:

| Pillar | What it means in-engine |
|---|---|
| Responsive third-person gunplay | Over-the-shoulder camera, strafing movement, distinct weapons, impact feedback |
| **Hacking / possession** (Hack Tether) | Take over an enemy robot. It keeps its own body, gun, and health, and the player's body is left behind and vulnerable |
| Robots, not soldiers | Localized damage (shoot the gun arm off), robot-specific movement, electrical stuns |
| Data-driven content | Weapons, upgrades, and enemy archetypes live in Data Assets, not hard-coded actors |
| Mission structure | Objectives, checkpoints, pickups, scripted dialogue, a boss encounter |

Two factions: the **Heroes** (the player's side, blues/greens) and the **Villains** (the enemy army, reds/oranges). All models are graybox placeholders (§3a).

---

## 2. What was wrong with the first attempt

| Problem | Effect | Fix |
|---|---|---|
| Master materials lacked **Used with Skeletal Mesh** | Every robot rendered with the default grey material in game | Pipeline sets the flag. `ue_validate.py` fails the build if any material is missing it |
| Material slots never actually assigned | Robots showed the world-grid material | UE Python iterates struct arrays as copies; the fixed importer rebuilds the slot list |
| Import scripts and intermediate files deleted | Nothing reproducible, no way to fix or re-import | Committed pipeline in `Tools/AssetPipeline/`, re-runnable end to end |
| Only the player + 2 weapons imported | 25 characters and most weapons missing | All 27 characters, 15 weapons and 7 props import automatically |
| Retargeted UE Mannequin anims onto the player | The player robot has no spine and a 28-bone rig, so humanoid retargeting fights the skeleton | Procedural robot animator driven by a per-robot rig profile (§4) |
| Weapon attached by socket "snap" with identity offset | Gun floated / pointed the wrong way | **Bonemerge**: weapons are skinned to the holder's `weapon_bone` and follow the body via leader pose |
| `BP_ThirdPersonGameMode` pointed at a C++ class that no longer existed | Template map broken | Restored to the template character |
| Python setup crashed the editor (`get_default_object(generated_class)` without calling it) | Unfinished setup | Fixed in `restore_template_game_mode()` |

---

## 3. Asset pipeline (`Tools/AssetPipeline/`)

Run everything with the editor **closed**:

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1
# -SkipBlender / -SkipImport / -SkipGameplay to re-run individual stages, -Blend <file> to export another .blend
```

| Stage | Script | Output |
|---|---|---|
| 1 | `blender_export.py` (Blender 5.2, headless) | `Saved/AssetPipeline/`: FBX per character, part, and weapon-on-holder; `manifest.json` |
| 2 | `ue_import_assets.py` (UE commandlet) | `/Game/Metalization/{Materials,Characters,Weapons,Props}` |
| 3 | `ue_setup_gameplay.py` | `/Game/Metalization/Data` (weapons, archetypes), `/Game/Metalization/Maps/MZ_Outpost_Demo` and `MZ_Arena` |
| 4 | `ue_validate.py` | Size, material-slot, and usage-flag checks (`PASSED` / list of problems) |

Before stage 2, the runner deletes the generated `Characters`, `Weapons`, `Props`, `Textures` and `Materials/Instances` folders. Reimporting over an existing mesh keeps its old material slots, and deleting assets from the commandlet leaves the files on disk.

Key decisions:
- **Materials**: graybox materials have no textures and set the master material's `Tint` (and `Color` on the light master) to their flat colour. The master `M_MZ_Base` also supports packed textures for future art (metallic = 1 − R, roughness = 1 − G of the base colour, emission = base × self-illum mask). Every robot material has `HitFlash` and `HackGlow` parameters that gameplay drives.
- **Scale**: Blender metres → UE centimetres. The player is 128 cm and a Brute 278 cm.
- **Weapons** are exported twice: skinned onto each holder's skeleton (held in hand) and as a static prop with its pivot at the grip (pickups).
- **Level geometry** uses the template's prototype grid (`T_GridChecker_A`, green channel as a subtle shade) under the `MI_Env_*` tints.
- The level scripts regenerate the maps in place, so **make manual layout changes on a copy of the map**.

### 3a. Graybox models (`Art/Graybox/Metalization_Graybox.blend`)

The source of truth for every model. Layout the exporter expects:

| Collection | Contents |
|---|---|
| `Characters/Heroes/<Name>`, `Characters/Villains/<Name>` | Armature `hero_<id>_ARM` / `villain_<id>_ARM`. First sub-collection = body, then optional parts (the player's and Shooter's spare hand, backpacks, the Guide's tether...) |
| `Weapons/<Weapon>`, `Grenades/<Grenade>` | Armature `weapon_<id>_ARM` whose root bone is the grip. Names map to IDs in `blender_export.py` (`WEAPON_COLLECTIONS`, `GRENADE_COLLECTIONS`) |

Bodies are one bevelled box per bone, rigidly skinned to it. Gameplay depends on the bone names (`bip_head`, `bip_pelvis`, `bip_hip_L/R`, `bip_upperArm/lowerArm_L/R`, `weapon_bone`, `minigun_R/L`, `ankle_1_L/R`, `hip_L/R`), so keep them when replacing art. Materials are flat colours: Heroes in blues/greens, Villains in reds/oranges, a lighter accent on head/hands/feet, a glowing visor on the front of the player's head, and one colour per weapon family.

To replace a graybox with authored art, model it in `Metalization_Graybox.blend` on the same armature (or edit the boxes), then run `run_pipeline.ps1`.

---

## 4. Gameplay architecture (C++ in `Source/Metalization/Game/`)

| System | Classes | Notes |
|---|---|---|
| Robots | `AMetalBotCharacter` + `UMetalBotArchetype` (Data Asset) | One class for the player, enemies, and NPCs. Archetype sets mesh, parts, rig, stats, hit zones, weapons, AI tuning, hackability, camera |
| Weapons | `UMetalWeaponData` (Data Asset), `AMetalProjectile` | Hitscan / projectile / thrown / melee. Per-level stats and meshes (upgrades). Ammo, clips, reload |
| Damage | `AMetalBotCharacter::TakeDamage` | No friendly fire. Hit zones per bone: head = ×1.5–2. Destroying a Shooter's **gun arm** disarms it and it charges into melee |
| Possession | `AMetalPlayerController` | Explicit control transfer: AI brain destroyed, the player's body powered down in place (still shootable), timer, `[F]` to eject, host self-destructs on release (a tactical bomb) |
| Animation | `UMetalBotAnimInstance` | Native procedural poses: gait from velocity, aim pitch, recoil, hit react, jump, powered-down slump, EMP twitch. Configured per robot via `FMetalBotRig` |
| AI | `AMetalBotAIController` | Patrol → Investigate → Combat (strafe at preferred range, burst fire) → Retreat. Plus Stunned, Disabled-weapon melee, squad alerts. Runtime navmesh |
| Mission | `AMetalMission`, `AMetalTriggerZone`, `AMetalControlConsole`, `AMetalGate`, `AMetalPickup` | Ordered objectives (eliminate tagged / reach zone / activate console / hijack tagged) with dialogue lines. Checkpoints. Villain-only consoles for possession puzzles |
| Flow / UI | `AMetalGameMode`, `AMetalHUD` | Respawn at checkpoint keeping inventory. Dialogue queue, banners, objective, health, ammo, tether timer, hack prompt, hit markers |
| Flying robots | `AMetalStalker`, `UMetalFlightMovementComponent` | Hover-gunship flight, boost, dodge roll, four arm guns that can be shot off one by one. Friendly Stalkers can be piloted with the Hack Tether with no time limit. See `Docs/SpaceBattle_Plan.md` |
| Testing | `AMetalDevAutopilot` (non-shipping) | `-MetalAutopilot=combat|gate|boss|arena|flight -MetalObjective=N -MetalShots=… -MetalCam=… -MetalTeleport=… -MetalQuitAt=…` for scripted smoke tests and screenshots |

Input is Enhanced Input, built in code (`AMetalPlayerController::CreateInputObjects`), for keyboard/mouse and gamepad.

### Why procedural animation for now
The models are bind poses only. The robots' skeletons are all different, and none is humanoid enough for clean Mannequin retargeting. The native animator produces readable, mechanical motion for **every** robot from about 15 numbers per archetype. When we author real animation, assign an Anim Blueprint to the mesh and the procedural instance is simply not used.

---

## 5. The demo: "Outpost - The Gate Job"

Open `/Game/Metalization/Maps/MZ_Outpost_Demo` (the project's default map) and press Play.

| Area | Content |
|---|---|
| Boneyard + Hangar | Every imported robot on display (the giants get their own hall). The Guide briefs you |
| Courtyard | 3 Shooters + a Soldier on a catwalk. Cover, ammo, grenades. **Checkpoint** |
| Scrap heap | Platforming up a crate staircase. Hidden **Shotgun** on a pillar. **Laser upgrade** on the wall top |
| Gate yard | 2 Shooters, then an energy fence that lasers and the Tether pass through. **Hijack a guard → walk him to the console → [E]** opens the gate |
| Arena | **Checkpoint**, Rocket Launcher, 2 Soldiers + a **Brute** boss (destroy it, or hijack it and turn its miniguns on its friends) |
| Extraction | Reach the pad → mission complete (time, kills, replay) |

### Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move / look | WASD / mouse | Left / right stick |
| Fire / aim (zoom) | LMB / RMB | RT / LT |
| Jump | Space | A |
| Switch weapon | 1–6, mouse wheel | Y |
| Reload | R | X |
| EMP grenade | G | RB |
| Hack Tether (hack / disconnect) | F | LB |
| Use console | E | B |
| Replay after mission | Enter | Start |

### Testing without playing

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario combat   # shoot, hijack, fight, disconnect
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario gate     # possession puzzle
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario boss     # hijack the Brute
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario arena    # free roam in MZ_Arena
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario flight   # pilot a Stalker in MZ_FlightTest
```

Each run launches the standalone game, drives the player with `AMetalDevAutopilot`, prints a state trace, and saves screenshots to `Saved/Screenshots/WindowsEditor`. `-MetalObjective=N` starts the mission at a later objective, which is also handy in the editor's *Standalone Game* launch options. The `combat` hijack occasionally misses when the target Shooter walks behind a crate; rerun it once before suspecting a regression.

### Verified by automated runs
- All 27 characters import at correct scale with every material slot assigned (`ue_validate.py`: PASSED).
- Combat: the Laser hits and damages Shooters. AI spots, alerts its squad, strafes and returns fire.
- Hack Tether: hijack a Shooter, fight as it, disconnect (host self-destructs, control returns to the player). Same for the Brute, whose miniguns destroyed a Soldier.
- Gate puzzle: hijack a gate guard through the energy fence, operate the console, gate opens, objective advances.
- Death → "SCRAPPED!" → respawn. Weapon pickup (Shotgun) is added and equipped. Extraction → mission-complete screen.

### Known gaps in this first pass
- **Not exercised by the autopilot yet**: shooting off a Shooter's gun arm (→ melee), EMP grenade stun, the rocket launcher, checkpoint respawn position, the Laser upgrade. The code paths are in place, but they need a human play-through.
- Showcase robots without a hand-tuned rig profile stand in their **bind (T-)pose**. Only the Player, Shooter, Soldier and Brute have tuned stances and gaits.
- **No audio**, and effects are placeholder glow meshes (`AMetalFX`).
- Level geometry is greybox (scaled cubes with the template's prototype grid).
- Balance (damage, AI accuracy, tether duration) is a first pass and lives in the data assets for tuning.

---

## 5b. Free-roam arena: `MZ_Arena`

A recreation of the first progress update video described in `Docs/example_video/`: the player runs and double-jumps around the UE Third Person template arena while enemy robots wander the floor. Open `/Game/Metalization/Maps/MZ_Arena` and press Play.

| Element | How it's built |
|---|---|
| Arena | `ue_setup_arena.py` copies the template's geometry and sun angle from `/Game/ThirdPerson/Lvl_ThirdPerson` into a plain (non World Partition) level. It re-skins the geometry in the video's grey look: light floor, grey walls, dark tops on the raised blocks. It also adds a clear sky with clouds and eight blue physics cubes |
| Robots | 4 groups (east/north/west/south) of Shooters, a Soldier, and neutral Miner/Worker heroes. They patrol at 60% speed with `bDocileUntilAttacked`: they ignore the player until shot, then the squad fights back |
| Game mode | `AMetalArenaGameMode`: no mission, `bActionCamera` and `bMinimalHUD` |
| Action camera | While not shooting, the player turns to face the way they run and the camera orbits freely, centred behind them. Firing or aiming snaps to the crosshair, and the camera blends to over-the-shoulder strafing for 1.5 s after the last shot |
| Movement | The player has `MaxJumps = 2`. The second jump is a forward flip: the whole mesh spins around the capsule centre while the animator tucks the legs. Free running leans into the stride and pumps both arms. Arms fling out in the air, and the knees absorb the landing |
| HUD | Hidden while exploring. It appears when shooting, hurt, or hijacking |

Regenerate only this map with `$env:MZ_LEVELS="arena"` before running `ue_setup_gameplay.py` (or `outpost` for the mission only). Test it with `smoke_test.ps1 -Scenario arena`: the run takes screenshots of running, the flip, turning, landing and shooting.

The Player archetype changes apply to both maps: the double jump, plus speed 600 and stride 200. The stride was 95, which made the legs cycle about 5× a second at full speed, the main source of the "janky" run.

## 5c. Stalker flight sandbox: `MZ_FlightTest`

Phase 1 of the space battle plan (`Docs/SpaceBattle_Plan.md`). Walk up to one of the three parked blue Stalkers, aim at it and press **[F]** to pilot it.
- Fly with WASD and the mouse, climb and dive with Space/Ctrl.
- Boost with Shift, dodge-roll with Q/E, and fire all four arm guns with LMB.
- Press [F] again to return to your body.

The range has towers and an arch to weave through, five hovering red Stalkers to shoot (their arms come off one at a time), and a Shooter squad on the ground. Regenerate it with `$env:MZ_LEVELS="flight"`.

## 6. Roadmap

1. **Feel pass**: sounds (MetaSounds), Niagara muzzle/impact/explosion effects to replace the `AMetalFX` placeholders, camera shake, and tuning the procedural gait per robot in the archetype assets.
2. **Authored animation**: proper rigs (Control Rig, or our own models) and Anim Blueprints. Keep the procedural animator as a fallback.
3. **More hacking verbs**: robots that open specific doors, ride lifts, turret possession, possession-only routes.
4. **Items and vehicles**: model them in the graybox .blend and add them to `blender_export.py`.
5. **AI depth**: move the C++ state machine into StateTree/Behavior Tree assets, add cover selection with EQS, snipers, and commanders who buff squads.
6. **Progression**: SaveGame for checkpoints, campaign state, and weapon upgrades across missions.
7. **Replace the graybox models** with authored art (§3a). The pipeline is model-agnostic. New characters need an FBX, an archetype asset, and a rig profile.
