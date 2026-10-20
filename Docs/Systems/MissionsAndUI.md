# Missions, levels, HUD and menus

Files: `MetalGameMode.h/.cpp`, `MetalMission.h/.cpp`, `MetalTriggerZone.h/.cpp`, `MetalControlConsole.h/.cpp`, `MetalGate.h/.cpp`, `MetalPickup.h/.cpp`, `MetalHUD.h/.cpp`, `MetalPauseMenu.h/.cpp`, `MetalLevelSettings.h`

## Game modes

| Class | Used by | Differences |
|---|---|---|
| `AMetalGameMode` | `MZ_Outpost_Demo`, `MZ_FlightTest`, the ship interior map | The base mode |
| `AMetalArenaGameMode` | `MZ_Arena` | `bActionCamera` (free-running camera, see [Robots.md](Robots.md)) and `bMinimalHUD` |
| `AMetalSpaceBattleGameMode` | `MZ_SpaceBattle` | Tickets, spawning aboard ships. See [SpaceBattle.md](SpaceBattle.md) |

Each map's game mode is set in its World Settings by the asset pipeline's level scripts.

`AMetalGameMode` sets the default classes (`AMetalPlayerCharacter`, `AMetalPlayerController`, `AMetalHUD`), and at `BeginPlay` starts the first `AMetalMission` in the level and, in non-shipping builds, the dev autopilot if requested.

### Messages

The game mode holds the on-screen text; the HUD only draws it.

| Kind | Call | Behaviour |
|---|---|---|
| Dialogue | `ShowMessage(Speaker, Text, Duration)` with a speaker | Queued; each line shows for its duration, then the next |
| Notice | `ShowMessage` with an empty speaker | Replaces the current notice ("Objective complete", "ACCESS DENIED") |
| Banner | `ShowBanner(Text, Duration)` | Big centred text ("CHECKPOINT", "SCRAPPED!", "MISSION COMPLETE") |

### Death, checkpoints and respawn

1. The player controller reports the body's death with `PlayerDied`.
2. The game mode saves the body's weapons, equipped slot and grenades, shows "SCRAPPED!", and after 3 s calls `Respawn`.
3. `Respawn` destroys the old body (and any host the player was still in), spawns a fresh body at the last checkpoint (or a player start), and restores the saved weapons (each topped up by half its maximum reserve) and at least two grenades.

`SetCheckpoint` is called by checkpoint trigger zones.

### Completion

`CompleteMission(EndBanner)` freezes the mission timer and shows the end banner with time and kills (kills count Villains killed by a player controller). Enter then calls `RestartMission`, which reloads the level.

## Missions

An `AMetalMission` actor placed in the level holds an ordered list of `FMetalObjective`s, each with a description, a type, a target (tag or actor) and dialogue to play when it starts. Four times a second it checks whether the active objective is complete; if so it shows a notice and activates the next. After the last one it plays the completion dialogue and completes the mission.

| `EMetalObjectiveType` | Complete when | Target |
|---|---|---|
| `EliminateTagged` | No living robot carries the tag | `TargetTag` |
| `ReachZone` | The robot the player currently controls is inside the zone | `TargetActor` (an `AMetalTriggerZone`) |
| `ActivateConsole` | The console has been activated | `TargetActor` (an `AMetalControlConsole`) |
| `HijackTagged` | The player controls a hijacked robot with the tag (any, if no tag) | `TargetTag` |

To add an objective type, add the enum value and a case in `AMetalMission::IsObjectiveComplete`.

In non-shipping builds, `-MetalObjective=N` on the command line starts at objective N.

## Level actors

| Actor | What it does |
|---|---|
| `AMetalTriggerZone` | Box volume with an optional glowing floor disc. With `bIsCheckpoint`, the first time the player's **own body** enters it sets the checkpoint and fully repairs the player. Also the target of `ReachZone` objectives |
| `AMetalControlConsole` | Opens its `LinkedGates` when operated by a robot whose **archetype** belongs to `RequiredFaction` (Villains by default). The player can't use it in their own body; they have to hijack an enemy and walk it over. A hijacked robot fights for the Heroes but keeps its archetype, which is why the check uses the archetype's team |
| `AMetalGate` | A door slab that slides by `OpenOffset` over `OpenTime` once `Open()` is called |
| `AMetalPickup` | Floating, spinning collectible: repair, ammo (fraction of reserve), grenades, or a weapon (granted, or upgraded one level if owned). Only the player's own body collects it, and it stays put if it would have no effect |

Placeholder geometry for these uses the engine's basic shapes with the `M_FX_Glow` material.

## HUD

`AMetalHUD` draws everything with Canvas calls each frame from public getters; it stores no state. Sizes are authored for 1080p and scaled by `UI = max(0.6, height / 1080)`.

- Damage vignette when the possessed robot was just hit.
- Combat UI (hidden while exploring with `bMinimalHUD` until the player aims, fires, is hurt or is hijacking): crosshair (cyan over a hack candidate, red on a confirmed hit), name and health bar, weapon and ammo, grenades, weapon list. For flyers, speed, the boost meter and a lead reticle on the enemy flyer nearest the crosshair.
- Hack Tether timer, or "LINKED" for ally links.
- Console prompt.
- Space battle status (tickets, hull bars, subsystem pips, markers). See [SpaceBattle.md](SpaceBattle.md).
- Objective, dialogue subtitles, notices, banners, and the end-of-mission stats.

`DrawMarker` projects a world point to the screen; off-screen points (optionally) get an arrow on the screen edge, pushed out from the centre until it meets the margin.

## Pause menu and Level Select

`UMetalPauseMenu` is a `UUserWidget` whose tree is built in `BuildTree()` (no widget blueprint): a dimmed backdrop, a title, and a widget switcher with two panels.

- **Main**: Continue, Level Select, Quit.
- **Level Select**: one button per entry in `UMetalLevelSettings::Levels` (the current level is marked), then Back.

Buttons are `UMetalMenuButton`s, which run a stored lambda when pressed, so each level button can carry its own map. Hovering moves keyboard focus, so mouse, keyboard and gamepad share one selection; the menu re-focuses the first button whenever nothing is focused. Escape, P, Start and B go back a panel or close the menu.

`UMetalLevelSettings` is a developer-settings class stored in `Config/DefaultGame.ini` and editable at Project Settings > Game > Metalization Levels. Any map listed there must also be in the packaging settings' `MapsToCook`, or it won't be in packaged builds.
