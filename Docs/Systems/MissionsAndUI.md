# Missions, levels, HUD and menus

> **Unity port.** This page was written for the Unreal Engine version; class names have been updated to the Unity ones (see [UnityPort.md](../UnityPort.md) for the full mapping). Distances quoted here are the Unreal values in centimetres (divide by 100 for Unity metres), and Unreal terms such as `BeginPlay`/`Tick`, components, `/Game/...` paths and `.h/.cpp` files correspond to `Start`/`Update`, `MonoBehaviour`s, `Assets/Metalization/...` and the `.cs` files listed in [Architecture.md](../Architecture.md).

Files: `MetalGameMode.h/.cpp`, `MetalDialogue.h/.cpp`, `MetalMission.h/.cpp`, `MetalTriggerZone.h/.cpp`, `MetalControlConsole.h/.cpp`, `MetalGate.h/.cpp`, `MetalPickup.h/.cpp`, `MetalHUD.h/.cpp`, `MetalPauseMenu.h/.cpp`, `MetalLevelSettings.h`

## Game modes

| Class | Used by | Differences |
|---|---|---|
| `MetalGameMode` | `MZ_Outpost_Demo`, `MZ_FlightTest`, the ship interior map | The base mode |
| `MetalGameMode (action camera, minimal HUD)` | `MZ_Arena` | `bActionCamera` (free-running camera, see [Robots.md](Robots.md)) and `bMinimalHUD` |
| `MetalSpaceBattleGameMode` | `MZ_SpaceBattle` | Tickets, spawning aboard ships. See [SpaceBattle.md](SpaceBattle.md) |

Each map's game mode is set in its World Settings by the asset pipeline's level scripts.

`MetalGameMode` sets the default classes (`MetalBot (player archetype)`, `MetalPlayerController`, `MetalHUD`), and at `BeginPlay` starts the first `MetalMission` in the level and, in non-shipping builds, the dev autopilot if requested.

### Messages

The game mode holds the on-screen text; the HUD only draws it.

| Kind | Call | Behaviour |
|---|---|---|
| Dialogue | `ShowMessage(Speaker, Text, Duration)` with a speaker | Forwarded to `MetalDialogue` (see [Dialogue](#dialogue)) |
| Notice | `ShowMessage` with an empty speaker | Replaces the current notice ("Objective complete", "ACCESS DENIED") |
| Banner | `ShowBanner(Text, Duration)` | Big centred text ("CHECKPOINT", "SCRAPPED!", "MISSION COMPLETE") |

### Death, checkpoints and respawn

Checkpoint trigger zones call `SetCheckpoint`. On mission levels (`bResetLevelOnDeath`, the default) it also has
`MetalCheckpoints` (`MetalCheckpoint.h`, a game-instance subsystem, so it survives a level load) take a
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
   - resumes the mission at the snapshot's objective (`MetalMission::RestoreObjective`: no banner or start
     dialogue)
   With no checkpoint reached yet, the retry just reloads the level from the start. Every NPC, projectile and
   timer starts fresh either way. Any other load (Restart, Level Select, the title screen) clears the snapshot.
3. Without it (the arena and the space battle), or once the mission is complete, `Respawn` destroys the old body
   (and any host the player was still in), spawns a fresh body at the last checkpoint (or a player start) in the
   running level, and restores the weapons the player died with (each topped up by half its maximum reserve) and
   at least two grenades.

Only actors placed in the map are tracked, with one exception: robots built by an `MetalRobotDispenser` that are
finished and standing are saved too (`DispensedBots`: dispenser, slot, archetype, transform, health). After the
other restores, each dispenser puts its own back fully built (`RestoreBuiltRobot`): into its Respawn slot, so it
doesn't weld a second copy, or counted against `MaxAlive`. A robot still on the pad isn't saved; the dispenser
just builds it again. Any other actor spawned during play that should survive a checkpoint would need its own
entry in the snapshot.

### Completion

`CompleteMission(EndBanner)` freezes the mission timer and shows the end banner with time and kills (kills count Villains killed by a player controller). Enter then calls `RestartMission`, which reloads the level.

## Dialogue

`MetalDialogue` (a world subsystem, so every map has one) plays radio dialogue during gameplay. There are no cutscenes or choices, and lines can't be skipped: the game keeps running and the lines play automatically.

Each `MetalDialogueLine` has a `Speaker`, the `Line` and a `Duration` (seconds before the next line shows, at least 0.5). `PlayDialogue(Lines)` (also Blueprint-callable) adds lines to the end of the queue, so dialogue from overlapping events plays in order instead of cutting in. Dialogue pauses with the game.

| Trigger | Field |
|---|---|
| Mission objective starts | `MetalObjective::StartDialogue` |
| Mission completes | `MetalMission::CompletionDialogue` |
| Player enters a zone (own body or hijacked), once | `MetalTriggerZone::Dialogue` (set `bShowMarker` false for an invisible trigger) |
| Anything else | Call `PlayDialogue` on the subsystem from C++ or a level Blueprint |

The HUD draws the current line in a box at the bottom centre of the screen (speaker name above the word-wrapped text). The box slides up when dialogue starts and stays put between back-to-back lines. While a line plays, a radio icon with pulsing signal waves floats above the head of the robot the player controls, showing an incoming radio call. It isn't shown when the speaker's name matches the player's own robot (`Player`).

## Missions

An `MetalMission` actor placed in the level holds an ordered list of `MetalObjective`s, each with a description, a type, a target (tag or actor) and dialogue to play when it starts. Four times a second it checks whether the active objective is complete; if so it shows a notice and activates the next. After the last one it plays the completion dialogue and completes the mission.

| `MetalObjectiveType` | Complete when | Target |
|---|---|---|
| `EliminateTagged` | No living robot carries the tag | `TargetTag` |
| `ReachZone` | The robot the player currently controls is inside the zone | `TargetActor` (an `MetalTriggerZone`) |
| `ActivateConsole` | The console has been activated | `TargetActor` (an `MetalControlConsole`) |
| `HijackTagged` | The player controls a hijacked robot with the tag (any, if no tag) | `TargetTag` |

To add an objective type, add the enum value and a case in `MetalMission::IsObjectiveComplete`.

**Objective groups.** An objective with `bWithNext` is active at the same time as the next one; a run of them
is a group. The player can complete a group's objectives in any order, and the mission only moves on once all
of them are done. Inside a group, `Requires` lists the `Id`s of objectives that must be done first: until then
the objective is locked (listed greyed out, not checked, and its `StartDialogue` waits until it unlocks). The
HUD lists the whole group with `[x]` for done ones, shows the hint card and marker of every active one, and
each completion plays the objective's `CompleteDialogue`. `bCheckpointOnComplete` saves a checkpoint where the
player's own body stands when the objective is done (and repairs it). The checkpoint snapshot stores the group's
first index and which of its objectives were done, so a retry resumes with the same ones ticked off.

Production Alpha's command yard is one group: destroy the Brute, shut down the dispensers (`Id`
`Dispensers`, checkpoint), clear the yard (`Requires` `Dispensers`). Extraction follows the group.

In non-shipping builds, `-MetalObjective=N` on the command line starts at objective N.

## Level actors

| Actor | What it does |
|---|---|
| `MetalTriggerZone` | Box volume with an optional glowing floor disc. With `bIsCheckpoint`, the first time the player's **own body** enters it sets the checkpoint and fully repairs the player. Its `Dialogue` plays the first time the player enters in any body. Also the target of `ReachZone` objectives |
| `MetalControlConsole` | Opens its `LinkedGates` and shuts down its `LinkedDispensers` when operated by a robot whose **archetype** belongs to `RequiredFaction` (Villains by default; `Neutral` lets anyone, the player included, use it). With Villains the player can't use it in their own body; they have to hijack an enemy and walk it over. A hijacked robot fights for the Heroes but keeps its archetype, which is why the check uses the archetype's team. `LockedColor`, `PromptText`, `GrantedText` and `DoneText` set its screen colour and messages |
| `MetalRobotDispenser` | A Villain machine (`SM_RobotDispenser`) that welds robots together on its pad in `AssemblyTime` (2 s). The parts appear one at a time (legs and torso, each arm, head, then the gun) under sparks, with drill and hammer sounds, and the robot only powers on (`MetalBot::SetAssembling`) once it is finished. **Respawn** mode rebuilds each of its `AssignedUnits` `RespawnDelay` seconds after it is destroyed and sends the copy back to the original's post. `RecordAssignedUnits` (run by the level scripts) stores what to rebuild, so units that a checkpoint restore removes are rebuilt too. **Continuous** mode picks from the weighted `SpawnChoices` every `SpawnInterval` seconds while fewer than `MaxAlive` of its robots stand. `ActivationGate` keeps it idle until that gate opens, and a standing robot on the pad holds up the next build. A console's `Disable` is permanent; checkpoints restore it through the console, and bring back the finished robots it had built |
| `MetalGate` | A door slab that slides by `OpenOffset` over `OpenTime` once `Open()` is called |
| `MetalPickup` | Floating, spinning collectible: repair, ammo (fraction of reserve), grenades, or a weapon (granted, or upgraded one level if owned). Only the player's own body collects it, and it stays put if it would have no effect |

Placeholder geometry for these uses the engine's basic shapes with the `M_FX_Glow` material.

## HUD

`MetalHUD` draws everything with Canvas calls each frame from public getters; it stores no state. Sizes are authored for 1080p and scaled by `UI = max(0.6, height / 1080)`.

- Damage vignette when the possessed robot was just hit.
- Combat UI (hidden while exploring with `bMinimalHUD` until the player aims, fires, is hurt or is hijacking): crosshair (cyan over a hack candidate, red on a confirmed hit), name and health bar, weapon and ammo, grenades, weapon list. For flyers, speed, the boost meter and a lead reticle on the enemy flyer nearest the crosshair.
- Hack Tether timer, or "LINKED" for ally links.
- Console prompt.
- Space battle status (tickets, hull bars, subsystem pips, markers). See [SpaceBattle.md](SpaceBattle.md).
- Objective, the dialogue box and radio icon, notices, banners, and the end-of-mission stats.

`DrawMarker` projects a world point to the screen; off-screen points (optionally) get an arrow on the screen edge, pushed out from the centre until it meets the margin.

## Pause menu and Level Select

`MetalPauseMenu` is a `UUserWidget` whose tree is built in `BuildTree()` (no widget blueprint): a dimmed backdrop, a title, and a widget switcher with two panels.

- **Main**: Continue, Level Select, Quit.
- **Level Select**: one button per entry in `MetalLevelSettings::Levels` (the current level is marked), then Back.

Buttons are `MetalPauseMenu button`s, which run a stored lambda when pressed, so each level button can carry its own map. Hovering moves keyboard focus, so mouse, keyboard and gamepad share one selection; the menu re-focuses the first button whenever nothing is focused. Escape, P, Start and B go back a panel or close the menu.

`MetalLevelSettings` is a developer-settings class stored in `Config/DefaultGame.ini` and editable at Project Settings > Game > Metalization Levels. Any map listed there must also be in the packaging settings' `MapsToCook`, or it won't be in packaged builds.

## Production Alpha additions

- **Objectives.**
  - `MetalObjective::Hint` is a control tip shown in a card under the objective.
  - `bShowMarker` points the HUD marker (with distance) at the zone or console, or at the nearest robot still
    to deal with (`MetalMission::GetObjectiveMarker`).
- **Mission loadout.** `MetalMission::PlayerStartingWeapons` and `PlayerStartingGrenades` replace the
  archetype loadout on the first tick, so a tutorial can hand weapons out one at a time.
- **Results.** `MetalGameMode` counts `Deaths`, and the controller counts hitscan shots and hits
  (`GetAccuracy`). `MetalHUD::DrawResults` shows time, kills, deaths and accuracy.
- **Title screen.** `MZ_Title` uses `MetalTitleScreen` (no pawn) and `MetalTitleScreen`. The
  controller orbits the camera actor tagged `TitleCamera` and shows `MetalPauseMenu` with `bTitleMode` set.
  `MetalGameMode::ReturnToTitle` and the pause menu's **Title Screen** button go back to it.
- **Pause menu.**
  - In-game: Continue, Restart Mission, Level Select, Controls, Sound, Credits, Title Screen, Quit.
  - On the title screen: Start Mission (the first Level Select entry), Level Select, Controls, Sound, Credits, Quit.
  - **Sound**: Music / Background, Sound Effects and Beep-boops (robot voices) volume sliders (see `Docs/Systems/Audio.md`). Left/right
    moves the focused slider; up/down leaves it.
  - **Credits**: the licensed music and other credits. Keep it in sync with `CREDITS.txt`.
- **Level actors.**
  - `MetalGravLift`: a vertical lift beam, with its origin at the floor pad.
  - `MetalBarrage`: shells land behind the player while they're inside its box.
