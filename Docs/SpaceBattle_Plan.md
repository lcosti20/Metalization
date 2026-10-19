# Space battles: plan

Two-team capital-ship battles in the style of the classic large-scale space modes: dogfight over and around two capital ships, knock out their subsystems, board the enemy hangar. Single-player with AI on both sides.

Instead of entering vehicles, the player **hacks Stalkers** (flying X-armed robots) parked in their ship's hangar with the Hack Tether.

---

## 0. Decisions

| Question | Decision |
|---|---|
| Stalker flight | A hovering gunship, not a plane. It can stop dead, strafe on all axes, and auto-levels when the controls are released. **Boost** is the flight version of sprinting |
| Networking | Single-player only |
| Hacking friendly robots | No time limit, no self-destruct. The link lasts until the player disconnects (or the Stalker is destroyed); either way control returns to the player's body. Hacking enemy robots keeps the existing timed, self-destructing link |
| Capital ship size | About 600 m long |
| Team colours | Friendly Stalkers take the player's team colour (Heroes blue, Villains red) |
| Naming | No original-game names in code, assets or docs (see the project's naming rule) |

## 1. Existing pieces we build on

| Piece | Use |
|---|---|
| `AMetalBotCharacter` + `UMetalBotArchetype` | `AMetalStalker` subclasses it, so damage, hit zones, teams, weapons, materials, HUD and hacking come for free |
| Hack Tether (`AMetalPlayerController`) | The way into a Stalker. The player's body is left powered down in the hangar and can be shot |
| `MuzzleBones` / `MuzzleCycle` | Alternating fire from the four X-arm guns |
| `SK_Stalker`, `DA_Bot_Stalker` | Rig bones `body`, `arm_upper_0/1_L/R`, `arm_lower_0/1_L/R`, `gun_upper_L/R`, `gun_lower_L/R`. About 11.2 m wide |
| Asset pipeline + `ue_validate.py` | Capital ship meshes and space maps are generated the same way as everything else |
| `AMetalDevAutopilot` + `smoke_test.ps1` | New `flight` and `space` scenarios |

## 2. Graybox capital ship (Blender)

- New `Ships/` collection in `Art/Graybox/Metalization_Graybox.blend`, exported as static meshes (like props).
- One hull of about 600 m, tinted per team with material instances. Separate meshes so parts can break: `SM_CapShip_Hull`, `_Engines`, `_ShieldGen` (two domes), `_Comms`, `_TurretBase`, `_TurretGun`.
- Hangar mouth in the hull side; its frame is an empty `portal_frame` (about 80 x 30 m) written to `manifest.json`.
- Sockets from empties: `turret_##`, `subsystem_*`, `hangar_portal`, `launch_dir`.
- Interior kit: `SM_Hangar_Shell` (its open side matches `portal_frame`), parking pads, blast-door frame. Corridors and the spawn room are box geometry from the level script.
- Pipeline: `SHIP_COLLECTIONS` in `blender_export.py`, `/Game/Metalization/Ships` in the importer, collision and portal-size checks in the validator.

## 3. Stalker: flight, weapons, animation

- `AMetalStalker : AMetalBotCharacter` with `UMetalFlightMovementComponent` (custom `PhysFlying`). Capsule around the body only; the arms are hit through hit zones, so they don't snag on geometry.
- **Flight model**: velocity eases toward the input direction in camera space (forward/strafe/vertical), the body turns toward the camera aim at a limited rate, banks into turns, and rolls back to level when there is no roll input. No gravity in space; with a `bGravityHover` volume/setting (hangar) it holds a hover height above the floor.
- **Boost**: hold to multiply max speed and acceleration, drains a boost meter that refills when released; arms sweep back while boosting.
- **Controls** (separate mapping context while flying):

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Aim / steer | Mouse | Right stick |
| Throttle / strafe | W/S, A/D | Left stick |
| Climb / descend | Space / Left Ctrl | A / B |
| Roll | Q / E | D-pad left / right |
| Boost | Left Shift | Left stick click |
| Fire (4 guns in X order) | LMB | RT |
| Zoom | RMB | LT |
| Disconnect | F | LB |

- **Weapon**: `WD_StalkerCannons`, fast projectiles. `MuzzleBones = gun_upper_L, gun_lower_R, gun_upper_R, gun_lower_L`.
- **Hit zones**: one per gun arm, with durability. A broken arm stops its gun (the barrel cycle skips it) instead of disarming the whole craft. All four gone means no guns.
- **Animation** (flyer branch of `UMetalBotAnimInstance`): bank into turns, arms spread when hovering/firing and swept back when boosting, per-gun recoil, hover bob, arms droop when parked.

## 4. Capital ship class

- `AMetalCapitalShip` (C++, Blueprintable): team, hull health, `UMetalCapitalShipData`, `TSoftObjectPtr<UWorld> InteriorLevel`, `InteriorOffset`. Virtual hooks: `OnSubsystemDestroyed`, `OnHullBreached`, `OnShipDestroyed`, `GetSpawnPoints`.
- `UMetalShipSubsystemComponent`: ShieldGen / Engines / Comms / LifeSupport, health, ticket penalty. Shield up = reduced hull damage.
- `AMetalShipTurret`: AI flak/laser turrets on sockets, destructible.
- `UMetalHangarComponent`: parks dormant Stalkers on pads in the interior instance, respawns a pad's Stalker N seconds after it's destroyed, exposes launch/landing zones.
- A new ship class = a new data asset, an optional Blueprint subclass, and an interior map.

## 5. Interior sublevel

- `MZ_CapitalShip_Interior`: spawn room, corridor, one hangar, a life-support room with a console boarders can sabotage.
- Each ship calls `ULevelStreamingDynamic::LoadLevelInstance` with its own offset (for example Z = -1,000,000 cm and -1,200,000 cm), so one template serves both teams and the interiors never see each other.
- Self-contained lighting, locked exposure.

## 6. Hangar portal

- `AMetalPortal` pairs (interior hangar mouth <-> exterior `hangar_portal` socket).
- Visual: `USceneCaptureComponent2D` placed at the player camera's transform mapped through the pair, oblique clip plane at the linked portal, matching FOV. Only captures when on screen and within about 300 m, at half resolution.
- Transit: a trigger on the portal plane teleports pawns crossing it forward, keeping velocity and control rotation; camera lag snaps. Projectiles die at the portal in the first pass.
- AI pilots treat each pair as a navigation link.

## 7. Battle mode

- `AMetalSpaceBattleGameMode`: two teams with reinforcement tickets, lost per death and per destroyed subsystem. Win by destroying the enemy ship or draining its tickets.
- The player spawns on foot in their ship's spawn room, walks to the hangar, hacks a Stalker.
- HUD: ship and subsystem health, tickets, speed/boost, lead reticle, hack prompt on parked Stalkers, arrow to the home hangar.
- `AMetalFlyerAIController`: dogfight (pursue, lead, burst, boost-dodge when hit), attack run on subsystems, defend, return to hangar (stretch). AI pilots spawn already in Stalkers. Turret AI leads the nearest hostile flyer.

## 8. Phases

1. **Stalker flight sandbox**: `AMetalStalker`, flight movement, input, boost, weapon, hit zones, animation, friendly unlimited hack. `MZ_FlightTest` map. Smoke test `flight`: hack a parked Stalker, fly, boost, shoot targets.
2. **Graybox capital ship**: model, exporter, import, validator.
3. **Capital ship class + interior instancing**: hangar pads with parked Stalkers.
4. **Portals**: capture visual, then two-way transit.
5. **Battle map + game mode**: two ships, subsystems, turrets, tickets, HUD.
6. **AI pilots and turrets**.
7. **Polish + shipping**: cook the interior map (path-only, like `DA_Bot_Player`), docs, `MZ_LEVELS=space`.

## 9. Risks

| Risk | Mitigation |
|---|---|
| Portal render cost, exposure mismatch | Visibility/distance gating, locked exposure, half-res target |
| 11 m X-shaped craft indoors | Large hangar (pads 25 m apart, 80 m mouth), body-only capsule |
| Possession code assumes walking robots | Flyer branches in the controller; disconnect always returns to the body |
| AI crossing portals | Explicit portal links |

## Progress

- Phase 1: in progress.
