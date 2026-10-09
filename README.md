# Metalization 🤖

Metalization is an RTS/war-sim game set in the distant future when all the stars have died out and only robots remain. Two robot civilizations live on orbiting shell worlds—mini-planets with a black hole in each core—that have been at war with each other for millions of years.

## Overview

In a distant future where the stars have long since died out, humanity is gone and only robots remain. Two rival robot civilizations inhabit orbiting shell worlds, immense mini-planets with black holes at their cores, and they have been locked in a brutal war for millions of years.

You command one of these civilizations and must gather energy, build armies, conquer planets, and destroy your enemies across both land and space. The game blends resource management, unit production, planetary conquest, and large-scale fleet combat into a single continuous strategic experience.

The gameplay focuses on:

- gathering energy from vents scattered across each planet
- producing new units such as soldiers, vehicles, jets, and rockets
- conquering enemy colonies to gain control of entire planets
- building a space fleet to fight in orbit and invade other worlds
- smoothly transitioning between terrestrial and space view by scrolling

## Core Features

- Energy resource management: collect power from energy vents across each planet
- Unit production: build soldiers, vehicles, jets, and rockets
- Planetary conquest: capture colonies to control key regions of the world
- Space warfare: command fleets that battle in orbit and support invasions
- Seamless perspective shifts: move fluidly between ground-level and space-level views
- Strategic empire-building: expand your influence across multiple planetary fronts

## Technical Stack

- Engine: Unreal Engine
- Primary language: C++
- Focus areas: RTS systems, unit production, planetary control, space combat, and camera/world transitions
- Platform: PC

## Repository Structure

```text
Metalization/
├── Config/                     # Unreal project configuration files
├── Content/                    # Game assets, blueprints, maps, materials, and audio
│   ├── Audio/                  # Sound effects and dialogue audio
│   ├── Blueprints/             # Core gameplay logic and systems
│   ├── Maps/                   # Level files and planet environments
│   ├── Materials/              # Materials, shaders, and VFX setup
│   └── Meshes/                 # 3D assets for units, props, and planets
├── Source/                     # C++ source code for the project module
│   └── Metalization/           # Main gameplay implementation
├── Metalization.uproject       # Unreal Engine project file
├── .gitignore                 # Git ignore rules for Unreal projects
├── .gitattributes             # Git attributes configuration
├── .vsconfig                  # Visual Studio configuration for UE projects
├── README.md                  # Project documentation
└── LICENSE                    # Repository licensing details
```

## Getting Started

### Prerequisites

- Unreal Engine 5.x installed through the Epic Games Launcher
- Visual Studio 2022 with the C++ Game Development workload

### Setup

1. Clone the repository:

```bash
git clone https://github.com/lcosti20/Metalization.git
```

2. Open `Metalization.uproject` in Unreal Engine.
3. If prompted, generate project files and allow Unreal Engine to configure the project.
4. Open the generated solution in Visual Studio and build the project in Development Editor mode.
5. Launch the project from Unreal Editor and open the main map to begin testing.

## Controls

- Pan across the planet: move the cursor to a viewport edge or use W/A/S/D
- Zoom: mouse wheel
- Rotate the view: hold the middle mouse button and drag
- Select: left-click an actor tagged `Selectable`
- Select multiple actors: left-drag a selection box; hold Shift to add to the current selection
- Issue a ground-targeted move order: right-click while actors are selected

The player is a collisionless camera pawn. Camera panning follows the planet's spherical surface when a nearby actor tagged `Planet` is present. The player controller exposes the current selection and broadcasts ground-targeted move orders for unit Blueprints to handle. Unit navigation, combat, production, and their interfaces still need game-specific unit/UI implementations.

## Development Roadmap

- [x] Sprint 1: Project pitch and vision
- [x] Sprint 2: Preproduction and greybox testing
- [ ] Sprint 3: Technical framework
- [ ] Sprint 4: Production alpha
- [ ] Sprint 5: Production beta and polish
- [ ] Sprint 6: Final release and trailer

## Project Documentation

For development logs, sprint notes, and course deliverables, see the project outcome site:

- Larry's GAME 405 Course Outcome Site: https://sites.google.com/view/larrys-game-405-project/home
- Sprint tracker: https://facultypages.scad.edu/~wchiu/405/SprintTracker/index.html#/p/s39e9h2

## Credits

- Larry Costigan — Game Designer & Developer
- Course: GAME 405 - Interactive Design and Game Dev Studio I
- Instructor: Prof. Wan Chiu

## Notes

This repository is a prototype and may evolve as gameplay systems and content are expanded during development. The focus is on proving the core concept: a gravity-driven, multi-vehicle action game set across distant shell worlds and orbital conflict.
