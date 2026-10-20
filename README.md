# Metalization 🤖

Metalization is a third-person robot shooter set in the distant future, when all the stars have died out and only robots remain. Two robot civilizations, the **Heroes** and the **Villains**, live on orbiting shell worlds (mini-planets with a black hole in each core) and have been at war with each other for millions of years.

You fight as a Hero robot armed with the **Hack Tether**: aim it at an enemy robot and take it over. You keep its body, its guns and its health, while your own body is left powered down where you stood. Fights move from the ground to orbit, where you pilot flying Stalkers between two capital ships.

## Overview

The current build is a playable prototype made of graybox models. Its pillars:

- **Third-person gunplay**: over-the-shoulder camera, strafing, upgradable weapons (Laser, Repeater, Shotgun, Rocket Launcher, EMP grenades).
- **Hacking**: hijack enemies for a limited time (they self-destruct when you let go), or link to friendly flyers with no time limit.
- **Robots, not soldiers**: localized damage (shoot a gun arm off and the robot charges in to melee), EMP stuns, a different body plan for every robot.
- **Space battles**: two capital ships with destructible subsystems, hangars you fly in and out of through portals, AI pilots and turrets, and reinforcement tickets.
- **Data-driven content**: robots, weapons and ship classes are Data Assets, not hard-coded actors.

## Levels

| Level | Map | What's there |
|---|---|---|
| Outpost - The Gate Job | `MZ_Outpost_Demo` (default) | A short mission: firefights, platforming, a gate puzzle that needs a hijacked guard, and a Brute boss |
| Arena | `MZ_Arena` | Free roam with an action camera and a double-jump flip; enemies ignore you until you shoot them |
| Flight Test | `MZ_FlightTest` | Link to a parked Stalker and practise flying and dogfighting |
| Space Battle | `MZ_SpaceBattle` | Spawn aboard your capital ship, take a Stalker out of the hangar, and destroy the enemy ship |

Switch levels in game from the pause menu (Esc, P or Start > Level Select).

## Controls

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Move / look | WASD / mouse | Left / right stick |
| Fire / aim (zoom) | LMB / RMB | RT / LT |
| Jump (press again in mid-air to double-jump) | Space | A |
| Switch weapon | 1–6, mouse wheel | Y |
| Reload | R | X |
| EMP grenade | G | RB |
| Hack Tether (hack / disconnect) | F | LB |
| Use console | E | B |
| Pause menu | Esc or P (Esc also ends Play-In-Editor, so use P there) | Start |
| Replay after a mission | Enter | Pause menu > Level Select |

While flying a Stalker:

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Climb / dive | Space / Ctrl | A / B |
| Boost | Shift | Left stick click |
| Dodge-roll | Q / E | D-pad left / right |

## Technical Stack

- Engine: Unreal Engine 5.8
- Primary language: C++ (all gameplay code is in `Source/Metalization/Game/`)
- Art: graybox models from a single Blender 5.2 file, imported by a scripted asset pipeline
- Platform: PC (keyboard/mouse and gamepad)

## Repository Structure

```text
Metalization/
├── Art/Graybox/                # Metalization_Graybox.blend, the source of every model
├── Config/                     # Unreal project configuration (maps to cook, Level Select list)
├── Content/
│   ├── Metalization/           # Generated game assets: characters, weapons, props, ships, data assets, maps
│   └── ...                     # Unreal template content (ThirdPerson, LevelPrototyping, Variant_*)
├── Docs/
│   ├── Architecture.md         # Start here for the code: how the systems fit together
│   ├── Systems/                # One page per gameplay system
│   ├── Metalization_Plan.md    # The plan, the asset pipeline and the demo guide
│   └── SpaceBattle_Plan.md     # Design notes for the space battles
├── Source/Metalization/
│   ├── Game/                   # Metalization gameplay code (all classes prefixed Metal)
│   └── Variant_*/              # Unreal template code, unused by the game
├── Tools/AssetPipeline/        # Blender export, UE import, level setup, validation and smoke tests
└── Metalization.uproject       # Unreal Engine project file
```

## Getting Started

### Prerequisites

- Unreal Engine 5.8 installed through the Epic Games Launcher
- Visual Studio 2022 with the C++ Game Development workload
- [Git LFS](https://git-lfs.com/) (all `.uasset`, `.umap` and `.blend` files are stored with it)
- Blender 5.2, only if you want to re-run the asset pipeline

### Setup

1. Clone the repository with Git LFS installed:

```bash
git lfs install
git clone https://github.com/lcosti20/Metalization.git
```

2. Right-click `Metalization.uproject` and choose **Generate Visual Studio project files**.
3. Open `Metalization.sln` and build **Development Editor | Win64**.
4. Open `Metalization.uproject`. The editor starts on `MZ_Outpost_Demo`; press Play.

The generated assets are already committed, so you don't need to run the pipeline to play. To rebuild them from the Blender file (editor closed):

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\run_pipeline.ps1
```

The level scripts regenerate the maps in place, so make level changes in the scripts rather than by hand. See [Docs/Metalization_Plan.md](Docs/Metalization_Plan.md) for the pipeline stages and options.

### Automated tests

Scripted smoke tests drive the game with a dev autopilot, take screenshots and log what happened:

```powershell
powershell -ExecutionPolicy Bypass -File Tools\AssetPipeline\smoke_test.ps1 -Scenario combat
```

Scenarios: `combat`, `gate`, `boss`, `arena`, `flight`, `hangar`, `battle`, `dogfight`, `menu`, `ships`. Add `-Packaged` to run one in a packaged build. Details are in [Docs/Systems/EffectsAndTools.md](Docs/Systems/EffectsAndTools.md).

## Development Roadmap

- [x] Sprint 1: Project pitch and vision
- [x] Sprint 2: Preproduction and greybox testing
- [ ] Sprint 3: Technical framework
- [ ] Sprint 4: Production alpha
- [ ] Sprint 5: Production beta and polish
- [ ] Sprint 6: Final release and trailer

## Project Documentation

- Code documentation: [Docs/Architecture.md](Docs/Architecture.md) and the system pages in [Docs/Systems/](Docs/Systems/)
- Plan and demo guide: [Docs/Metalization_Plan.md](Docs/Metalization_Plan.md)
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
