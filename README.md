# Metalization 🤖

Metalization is a third-person robot shooter set in the distant future, when all the stars have died out and only robots remain. Two robot civilizations, the **Heroes** and the **Villains**, live on orbiting shell worlds (mini-planets with a black hole in each core) and have been at war with each other for millions of years.

You fight as a Hero robot armed with the **Hack Tether**: aim it at an enemy robot and take it over. You keep its body, its guns and its health, while your own body is left powered down where you stood. Fights move from the ground to orbit, where you pilot flying Stalkers between two capital ships.

[![Production Alpha Video](https://i.vimeocdn.com/video/2220664671-353d681842eb638bdfaddd33ed0599f9e1332090267b72ad46de947791a9c820-d?mw=900&mh=508&q=70)](https://vimeo.com/1242418953/9494e93d6d?share=copy&fl=sv&fe=ci)

## Overview

The current build is a playable prototype made of graybox models. Its pillars:

- **Third-person gunplay**: over-the-shoulder camera, strafing, upgradable weapons (Laser, Repeater, Shotgun, Rocket Launcher, EMP grenades).
- **Hacking**: hijack enemies for a limited time (when you let go they reboot and rejoin their side; only damage destroys them), or link to friendly flyers with no time limit. Hijacking an enemy first means rewiring its fake Blueprint in a quick puzzle while the fight goes on; a wrong compile makes it put up a 4-second firewall.
- **Robots, not soldiers**: localized damage (shoot a gun arm off and the robot charges in to melee), EMP stuns, a different body plan for every robot.
- **Space battles**: two capital ships with destructible subsystems, hangars you fly in and out of through portals, AI pilots and turrets, and reinforcement tickets.
- **Data-driven content**: robots, weapons and ship classes are ScriptableObject data assets, not hard-coded objects.

## Levels

| Level | Map | What's there |
|---|---|---|
| **Operation Redline (Production Alpha)** | `MZ_ProductionAlpha` | The Sprint 4 demo: one Blue-vs-Red ground mission that teaches sprinting, jumping, double jumps, anti-grav lifts, shooting and weapon-arm swaps, grenades, sneaking, sniping, hacking and consoles in order, then throws them all into a final battle. See [Docs/ProductionAlpha.md](Docs/ProductionAlpha.md) |
| Title screen | `MZ_Title` (a player build's start scene) | Start Mission / Level Select / Controls / Quit |
| Outpost - The Gate Job | `MZ_Outpost_Demo` | A short mission: firefights, platforming, a gate puzzle that needs a hijacked guard, and a Brute boss |
| Arena | `MZ_Arena` | Free roam with an action camera and a double-jump flip; enemies ignore you until you shoot them |
| Flight Test | `MZ_FlightTest` | Link to a parked Stalker and practise flying and dogfighting |
| Space Battle | `MZ_SpaceBattle` | Spawn aboard your capital ship, take a Stalker out of the hangar, and destroy the enemy ship |
| Robot Dispenser Test | `MZ_DispenserTest` | Two robot dispensers: one rebuilds its guards, one welds a new robot every few seconds |

Switch levels in game from the pause menu (Esc, P or Start > Level Select).

## Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move / look | WASD / mouse | Left / right stick |
| Sprint (hold) | Shift | Left stick click |
| Sneak | Hold Ctrl, or toggle with C | Right stick click |
| Fire / aim (zoom; scope with the Sniper) | LMB / RMB | RT / LT |
| Jump (press again in mid-air to double-jump) | Space | A |
| Swap weapon arm (acted out: twist off, rack, snap on) | 1–6, mouse wheel | Y |
| Reload | R | X |
| EMP grenade | G | RB |
| Hack Tether (hack / disconnect) | F | LB |
| Hack window: drag nodes and wires | Drag with LMB (Alt+click a pin breaks its links) | Left stick cursor, hold A to drag (X breaks links) |
| Hack window: Save & Compile / cancel | Ctrl+S or the button / the [X] or Esc | Y / B |
| Use console | E | B |
| Pause menu | Esc or P | Start |
| Replay after a mission | Enter | Pause menu > Restart Mission |

Anti-grav lifts need no button: walk into the glowing beam.

The hack window: hacking an enemy opens a `BP_<Unit>Controller` graph laid out left to right. Swap the Switch Team outputs (red to Call Shoot, blue to Call Ceasefire), then Save & Compile. Wire pins by dragging, or by clicking one pin and then the other (handy on a gamepad). Any other wiring fails and firewalls the robot for 4 seconds; the [X] cancels with no penalty.

While flying a Stalker:

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Climb / dive | Space / Ctrl | A / B |
| Boost | Shift | Left stick click |
| Dodge-roll | Q / E | D-pad left / right |

## Technical Stack

- Engine: Unity 6 (6000.6.1f1), Universal Render Pipeline, Input System, AI Navigation
- Primary language: C# (all gameplay code is in `Assets/Metalization/Scripts/Runtime/`, namespace `Metalization`)
- Art: graybox models, one Blender 5.2 file per model, exported to FBX by a scripted asset pipeline
- Platform: PC (keyboard/mouse and gamepad)

The game was first built in Unreal Engine 5.8 (C++) and ported to Unity; [Docs/UnityPort.md](Docs/UnityPort.md) maps the
Unreal classes and conventions to the Unity ones and lists what changed.

## Repository Structure

```text
Metalization/
├── Art/Graybox/                    # One .blend per model (Characters/, Weapons/, Grenades/, Ships/, Shared/); see its README
├── Assets/Metalization/
│   ├── Scripts/Runtime/            # Gameplay code (Core, Bots, AI, Player, Missions, Level, UI, Space, Dev)
│   ├── Scripts/Editor/             # Asset pipeline and level builders (Metalization menu)
│   ├── Shaders/                    # URP shaders: glow, particles, energy field, portal
│   ├── Models/, Audio/             # Generated by the Blender export and the sound synthesizer
│   ├── Data/, Materials/, Prefabs/ # Generated: robot/weapon/ship data assets, materials, ship part prefabs
│   ├── Scenes/                     # Generated levels (MZ_*.unity)
│   └── Resources/MetalGameAssets   # Shared runtime asset table
├── Docs/
│   ├── Architecture.md             # Start here for the code: how the systems fit together
│   ├── UnityPort.md                # Unreal -> Unity conversion notes
│   ├── Systems/                    # One page per gameplay system
│   ├── Metalization_Plan.md        # The plan and the demo guide
│   └── SpaceBattle_Plan.md         # Design notes for the space battles
├── Packages/, ProjectSettings/     # Unity project configuration
└── Tools/AssetPipeline/            # Blender export, sound synthesis, pipeline and smoke-test scripts
```

## Getting Started

### Prerequisites

- Unity 6000.6.1f1 (through Unity Hub)
- [Git LFS](https://git-lfs.com/) (`.fbx`, `.blend` and audio files are stored with it)
- Blender 5.2 and Python 3, only if you want to re-run the asset pipeline

### Setup

1. Clone the repository with Git LFS installed:

```bash
git lfs install
git clone https://github.com/lcosti20/Metalization.git
```

2. Open the project folder in Unity Hub.
3. Open `Assets/Metalization/Scenes/MZ_Title.unity` (or any `MZ_*` level) and press Play.

The generated assets and scenes are committed, so you don't need to run the pipeline to play. In the editor,
**Metalization > Build All Assets** regenerates everything from the exported models (materials, data assets, prefabs and
every level), and **Metalization > Build Level** rebuilds one level. The levels are generated by code
(`Assets/Metalization/Scripts/Editor/MetalLevels*.cs`), so make level changes there rather than by hand.

To re-export the models from the Blender files and rebuild everything (Unity closed):

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1
```

With the editor open, add `-SkipUnity` and then use **Metalization > Build All Assets**.

### Automated tests

Scripted smoke tests run a Development player build with a dev autopilot, take screenshots and log what happened:

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Build -Scenario combat
```

`-Build` makes the player first (Unity closed; or use **Metalization > Build Windows Player**). Scenarios: `combat`, `gate`,
`boss`, `arena`, `flight`, `hangar`, `battle`, `dogfight`, `menu`, `ships`, and for the Production Alpha `alpha`, `bridge`,
`lift`, `swap`, `bunker`, `sneak`, `sniper`, `hack`, `title`, `dispenser` and more (see the script). Details are in
[Docs/Systems/EffectsAndTools.md](Docs/Systems/EffectsAndTools.md).

## Development Roadmap

- [x] Sprint 1: Project pitch and vision
- [x] Sprint 2: Preproduction and greybox testing
- [x] Sprint 3: Technical framework
- [x] Sprint 4: Production alpha (playable build done; hand playtest pending, see [Docs/Sprint4_TaskList.md](Docs/Sprint4_TaskList.md))
- [ ] Sprint 5: Production beta and polish
- [ ] Sprint 6: Final release and trailer

## Project Documentation

- Code documentation: [Docs/Architecture.md](Docs/Architecture.md) and the system pages in [Docs/Systems/](Docs/Systems/)
- Plan and demo guide: [Docs/Metalization_Plan.md](Docs/Metalization_Plan.md)
- Production Alpha (mission guide, new mechanics, testing): [Docs/ProductionAlpha.md](Docs/ProductionAlpha.md) and the [Sprint 4 task list](Docs/Sprint4_TaskList.md)
- Space battle design: [Docs/SpaceBattle_Plan.md](Docs/SpaceBattle_Plan.md)

For development logs, sprint notes, and course deliverables, see the project outcome site:

- Larry's GAME 405 Course Outcome Site: https://sites.google.com/view/larrys-game-405-project/home
- Sprint tracker: https://facultypages.scad.edu/~wchiu/405/SprintTracker/index.html#/p/s39e9h2

## Credits

- Larry Costigan — Game Designer & Developer
- Course: GAME 405 - Interactive Design and Game Dev Studio I
- Instructor: Prof. Wan Chiu

## Notes

This repository is a prototype and will keep changing as gameplay systems and content are expanded during development. Every model is a graybox placeholder, and effects and animation are simple stand-ins built in code until authored art replaces them.
