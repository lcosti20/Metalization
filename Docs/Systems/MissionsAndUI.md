# Missions, levels, HUD and menus

Files: `MetalGameMode.h/.cpp`, `MetalDialogue.h/.cpp`, `MetalMission.h/.cpp`, `MetalTriggerZone.h/.cpp`, `MetalControlConsole.h/.cpp`, `MetalGate.h/.cpp`, `MetalPickup.h/.cpp`, `MetalHUD.h/.cpp`, `MetalPauseMenu.h/.cpp`, `MetalLevelSettings.h`

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
| Dialogue | `ShowMessage(Speaker, Text, Duration)` with a speaker | Forwarded to `UMetalDialogueSubsystem` (see [Dialogue](#dialogue)) |
| Notice | `ShowMessage` with an empty speaker | Replaces the current notice ("Objective complete", "ACCESS DENIED") |
| Banner | `ShowBanner(Text, Duration)` | Big centred text ("CHECKPOINT", "SCRAPPED!", "MISSION COMPLETE") |

### Death, checkpoints and respawn

Checkpoint trigger zones call `SetCheckpoint`. On mission levels (`bResetLevelOnDeath`, the default) it also has
`UMetalCheckpointSubsystem` (`MetalCheckpoint.h`, a game-instance subsystem, so it survives a level load) take a
snapshot of the level:

- every placed actor that still exists (living robots, pickups not yet taken, ...), keyed by its path in the map
- each living robot's transform, health and dormancy
- activated consoles, used checkpoint zones, zones whose dialogue has played
- the mission's objective index, the kill count and the player's weapons and grenades

When the player's own body dies:

1. The player controller reports the death with `PlayerDied`. The game mode shows "SCRAPPED!" and waits 3 s.
2. With `bResetLevelOnDeath`, `ReloadFromCheckpoint` marks a retry (deaths and mission time carry over) and reloads
   the map. The new game mode takes the snapshot back in `InitGame` and:
   - spawns the player at the checkpoint (`RestartPlayer`)
   - before any `BeginPlay`: destroys placed actors that were gone at the checkpoint, moves the robots back and
     marks used zones so they don't fire again when the player spawns inside them
   - after every `BeginPlay`: restores robot health, opens activated consoles' gates instantly and gives back the
     checkpoint loadout
   - resumes the mission at the snapshot's objective (`AMetalMission::RestoreObjective`: no banner or start
     dialogue)
   With no checkpoint reached yet, the retry just reloads the level from the start. Every NPC, projectile and
   timer starts fresh either way. Any other load (Restart, Level Select, the title screen) clears the snapshot.
3. Without it (the arena and the space battle), or once the mission is complete, `Respawn` destroys the old body
   (and any host the player was still in), spawns a fresh body at the last checkpoint (or a player start) in the
   running level, and restores the weapons the player died with (each topped up by half its maximum reserve) and
   at least two grenades.

Only actors placed in the map are tracked. An actor spawned during play that should survive a checkpoint would
need its own entry in the snapshot.

### Completion

`CompleteMission(EndBanner)` freezes the mission timer and shows the end banner with time and kills (kills count Villains killed by a player controller). Enter then calls `RestartMission`, which reloads the level.

## Dialogue

`UMetalDialogueSubsystem` (a world subsystem, so every map has one) plays radio dialogue during gameplay. There are no cutscenes or choices, and lines can't be skipped: the game keeps running and the lines play automatically.

Each `FMetalDialogueLine` has a `Speaker`, the `Line` and a `Duration` (seconds before the next line shows, at least 0.5). `PlayDialogue(Lines)` (also Blueprint-callable) adds lines to the end of the queue, so dialogue from overlapping events plays in order instead of cutting in. Dialogue pauses with the game.

| Trigger | Field |
|---|---|
| Mission objective starts | `FMetalObjective::StartDialogue` |
| Mission completes | `AMetalMission::CompletionDialogue` |
| Player enters a zone (own body or hijacked), once | `AMetalTriggerZone::Dialogue` (set `bShowMarker` false for an invisible trigger) |
| Anything else | Call `PlayDialogue` on the subsystem from C++ or a level Blueprint |

The HUD draws the current line in a box at the bottom centre of the screen (speaker name above the word-wrapped text). The box slides up when dialogue starts and stays put between back-to-back lines. While a line plays, a radio icon with pulsing signal waves floats above the head of the robot the player controls, showing an incoming radio call. It isn't shown when the speaker's name matches the player's own robot (`Player`).

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
| `AMetalTriggerZone` | Box volume with an optional glowing floor disc. With `bIsCheckpoint`, the first time the player's **own body** enters it sets the checkpoint and fully repairs the player. Its `Dialogue` plays the first time the player enters in any body. Also the target of `ReachZone` objectives |
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
- Objective, the dialogue box and radio icon, notices, banners, and the end-of-mission stats.

`DrawMarker` projects a world point to the screen; off-screen points (optionally) get an arrow on the screen edge, pushed out from the centre until it meets the margin.

## Pause menu and Level Select

`UMetalPauseMenu` is a `UUserWidget` whose tree is built in `BuildTree()` (no widget blueprint): a dimmed backdrop, a title, and a widget switcher with two panels.

- **Main**: Continue, Level Select, Quit.
- **Level Select**: one button per entry in `UMetalLevelSettings::Levels` (the current level is marked), then Back.

Buttons are `UMetalMenuButton`s, which run a stored lambda when pressed, so each level button can carry its own map. Hovering moves keyboard focus, so mouse, keyboard and gamepad share one selection; the menu re-focuses the first button whenever nothing is focused. Escape, P, Start and B go back a panel or close the menu.

`UMetalLevelSettings` is a developer-settings class stored in `Config/DefaultGame.ini` and editable at Project Settings > Game > Metalization Levels. Any map listed there must also be in the packaging settings' `MapsToCook`, or it won't be in packaged builds.

## Production Alpha additions

- **Objectives.**
  - `FMetalObjective::Hint` is a control tip shown in a card under the objective.
  - `bShowMarker` points the HUD marker (with distance) at the zone or console, or at the nearest robot still
    to deal with (`AMetalMission::GetObjectiveMarker`).
- **Mission loadout.** `AMetalMission::PlayerStartingWeapons` and `PlayerStartingGrenades` replace the
  archetype loadout on the first tick, so a tutorial can hand weapons out one at a time.
- **Results.** `AMetalGameMode` counts `Deaths`, and the controller counts hitscan shots and hits
  (`GetAccuracy`). `AMetalHUD::DrawResults` shows time, kills, deaths and accuracy.
- **Title screen.** `MZ_Title` uses `AMetalTitleGameMode` (no pawn) and `AMetalTitlePlayerController`. The
  controller orbits the camera actor tagged `TitleCamera` and shows `UMetalPauseMenu` with `bTitleMode` set.
  `AMetalGameMode::ReturnToTitle` and the pause menu's **Title Screen** button go back to it.
- **Pause menu.**
  - In-game: Continue, Restart Mission, Level Select, Controls, Title Screen, Quit.
  - On the title screen: Start Mission (the first Level Select entry), Level Select, Controls, Quit.
- **Level actors.**
  - `AMetalGravLift`: a vertical lift beam, with its origin at the floor pad.
  - `AMetalBarrage`: shells land behind the player while they're inside its box.
