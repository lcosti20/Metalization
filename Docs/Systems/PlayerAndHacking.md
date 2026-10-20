# Player control and the Hack Tether

Files: `MetalPlayerController.h/.cpp`, `MetalPlayerCharacter.h/.cpp`

`AMetalPlayerController` turns input into robot commands, runs the Hack Tether, finds consoles to interact with, and opens the pause menu.

## Input

All input objects are created in code by `CreateInputObjects` (there are no input assets). It builds two mapping contexts that share one set of actions:

| Context | Used when | Notable bindings |
|---|---|---|
| `IMC_Metalization` | Walking | WASD/left stick move, mouse/right stick look, Space jump, LMB/RT fire, RMB/LT aim, R reload, G grenade, F tether, E interact, 1-6 weapons, wheel cycles, Esc/P/Start pause |
| `IMC_MetalizationFlight` | Possessing an `AMetalStalker` | Same move/look/fire/aim/tether, Space/Ctrl climb and dive, Shift boost, Q/E dodge-roll |

`UpdateMappingContext` swaps contexts on every possession and widens the camera's pitch limits for flyers. WASD is turned into a 2D axis with Enhanced Input modifiers (swizzle W/S onto Y, negate S and A); look negates Y so pushing up looks up.

Each handler checks `CanAct()` (the possessed robot is alive, awake and not stunned) and forwards to the robot. Walking movement is relative to the camera's yaw; flying forward follows the full aim direction, so pitching up and pushing forward climbs.

## The player's body

The controller remembers two robots:

- `PlayerBody`: the player's own robot. Set in `OnPossess` whenever a possession is *not* part of a tether switch (level start, respawn).
- `Host`: the robot currently hijacked or linked, if any.

`bSwitchingBodies` is raised around the tether's own `Possess` calls so `OnPossess` can tell them apart from a fresh spawn.

## Hack Tether

```mermaid
sequenceDiagram
    participant P as Player controller
    participant B as Player body
    participant T as Target robot
    P->>P: FindHackCandidate (every tick)
    Note over P: F pressed
    P->>T: destroy its AI controller
    P->>B: SetDormant(true)
    P->>T: SetHijacked(true) (enemies only)
    P->>T: Possess
    Note over P,T: timer runs (enemies) or open-ended (allies)
    P->>B: Possess, SetDormant(false)
    alt enemy
        P->>T: SelfDestruct(1.2 s)
    else ally
        P->>T: SetDormant(true) (idle, linkable again)
    end
```

**Finding a target.** `FindHackCandidate` runs every tick while the player is in their own body. It looks for robots that `CanBeHackedBy` the body, within `TetherRange`, inside a 7-degree cone around the view direction, with a clear line from the body's eyes, and picks the most centred one. The HUD shows a cyan crosshair and a "[F] HACK ..." or "[F] PILOT ..." prompt over it.

**Who can be taken.** `AMetalBotCharacter::CanBeHackedBy` has two rules:

- **Enemies** need `bHackable`. This is a *hijack*: timed by the archetype's `PossessionDuration`, the robot joins the Heroes and glows, and when the link ends it self-destructs, damaging whatever is near it.
- **Same-team robots** need `bAllyHackable` and must have no controller (an idle parked Stalker). This is a *link*: no time limit, no self-destruct, no beam. When it ends the robot goes dormant and can be linked again.

**Starting** (`BeginPossession`): the target's AI controller is unpossessed and destroyed, the body is powered down where it stands (it can still be shot), the controller possesses the target, and the control rotation is reset to the target's facing.

**Ending** (`EndPossession`) happens when the player presses F again, the hijack timer runs out (checked in `Tick`), or the host dies (`OnBotDied`). Control returns to the body if it is still alive.

**Player death.** If the player's body dies (even mid-hijack, since the body stays vulnerable), `OnBotDied` ends any link and calls `AMetalGameMode::PlayerDied`.

## Interaction

Every tick the controller finds the nearest unused `AMetalControlConsole` within its `InteractRadius`. The HUD shows its prompt; pressing E calls `TryActivate` with the possessed robot and shows the result message. See [MissionsAndUI.md](MissionsAndUI.md).

## Pause

`OpenPauseMenu` releases held buttons (their release events would be lost while the menu has input), creates `UMetalPauseMenu`, pauses the game and switches to UI-only input with the cursor shown. `ClosePauseMenu` reverses it. While the menu is open it handles its own back keys.
