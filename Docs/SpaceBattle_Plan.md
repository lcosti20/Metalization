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

Built (see Progress). Layout in `Art/Graybox/Metalization_Graybox.blend`: `Ships/<group>/<part>`, one collection per static mesh. A part collection holds:
- its render meshes (joined at export)
- `UCX_*` boxes or convex hulls for collision
- a `PIVOT_*` empty (the mesh origin)
- `SOCKET_<name>` empties: +X is forward, +Z is up, and custom properties are carried through

Collections ending in `_Preview` are display-only. `CapShip_Preview` instances the modular parts on their other sockets so the whole ship can be seen in Blender.

| Mesh | Size | Notes |
|---|---|---|
| `SM_CapShip_Hull` | 600 x 162 x 119 m | Ship forward is +X. Armoured rear block, tapered prow, keel, bridge tower. The 80 x 30 m hangar mouth is on the starboard (+Y) face: a 4 m recess with a glowing frame. 9 collision hulls; the recess is left open. Sockets: `hangar_portal` (forward = out of the ship, `width`/`height` in m), `engines`, `shieldgen_L/R`, `comms`, `turret_01..09` (05 is on the prow, 06-09 hang under the hull) |
| `SM_CapShip_Engines` | 33 x 140 x 48 m | Three nozzles with glowing exhausts. Goes on `engines` |
| `SM_CapShip_ShieldGen` | 25 x 25 x 16 m | Dome on a pedestal. Goes on both `shieldgen_*` sockets |
| `SM_CapShip_Comms` | 16 x 24 x 33 m | Mast and dish. Goes on `comms` |
| `SM_CapShip_TurretBase` / `_TurretGun` | 16 m ring / 28 m gun | The base goes on `turret_##`. The gun goes on the base's `gun` socket, pivoting at its yaw/pitch centre with barrels along +X |
| `SM_Hangar_Shell` | 124 x 104 x 44 m | Interior room: origin is floor level at the middle of the mouth, the room extends along -Y and the mouth faces +Y. The 80 x 30 m mouth sits on a 4 m sill, so robots on foot can't walk out into space. Sockets: `hangar_portal` (same size as the hull's), `corridor_door` (8 x 6 m doorway in the back wall), `pad_1..3` (30 m apart, facing the mouth) |
| `SM_Hangar_Pad` | 16 x 14 m | Parking pad for the `pad_*` sockets |
| `SM_Hangar_BlastDoor` | 11 x 8 m frame | Fits the corridor doorway, facing +X |

Materials are flat colours: `GB_ship_Hull`/`_Accent`/`_Dark`/`_Glow` and `GB_hangar_Floor`/`_Wall`/`_Hazard`. Per-team hull tint comes in Phase 3 (dynamic material on the `_Hull` slot, as the Stalker does). The `launch_dir` socket from the first draft was dropped: the portal socket's forward is the launch direction.

## 3. Stalker: flight, weapons, animation

- `AMetalStalker : AMetalBotCharacter` with `UMetalFlightMovementComponent`, the stock character movement in `MOVE_Flying` plus a tunable **grip** (how fast the velocity swings to a new stick direction). The capsule wraps only the body. The arms take hits through the mesh's physics bodies and never snag on geometry.
- **Flight model**: W/S thrust along the full aim (pitch up and push forward to climb), A/D strafe level, Space/Ctrl climb and dive. The body yaws toward the aim at `TurnRate`. Pitch and bank are visual only, because the capsule stays upright. It banks into strafes and turns and levels out when the controls are released. With no input it brakes to a dead hover. There's no gravity in flight.
- **Parked**: a Stalker placed with `bStartParked` sits on the floor with its arms folded and no brain. Linking to it lifts it off; disconnecting leaves it hovering, ready to be linked again.
- **Boost**: hold to raise the speed cap and acceleration (45 to 100 m/s). It always thrusts forward along the aim, like sprinting. It drains a 3 s meter that refills 0.6 s after release. Arms close up and sweep back while boosting.
- **Dodge**: Q/E is a sideways burst with a barrel roll, costing a quarter of the boost meter.
- All tuning lives in the archetype (`DA_Bot_Stalker` > Flight).
- **Controls** (separate mapping context while flying):

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Aim / steer | Mouse | Right stick |
| Throttle / strafe | W/S, A/D | Left stick |
| Climb / dive | Space / Left Ctrl | A / B |
| Dodge roll left / right | Q / E | D-pad left / right |
| Boost | Left Shift | Left stick click |
| Fire (4 guns in X order) | LMB | RT |
| Zoom | RMB | LT |
| Disconnect | F | LB |

- **Weapon**: `WD_StalkerCannons`, fast projectiles. `MuzzleBones = gun_upper_L, gun_lower_R, gun_upper_R, gun_lower_L`.
- **Hit zones**: one per gun arm (90 durability), using the new `FMetalHitZone::DisablesMuzzleBone`. A broken arm is hidden, its physics bodies are removed, and the firing order skips its gun. Losing all four disables the weapon. Hits on the mesh map to zones by bone name.
- **Team colours**: `UMetalBotArchetype::TeamColors` repaints the `_Body`/`_Accent` slots for the current team (Heroes use the player's blues).
- **Animation** (flight branch of `UMetalBotAnimInstance`, enabled by `FMetalBotRig::FlyerArmUpperL`): arms open into the X (upper pair up, lower pair down), close and sweep back under boost, kick back on each shot, and fold flat while parked. The body bobs while hovering.

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

- **Phase 1: done (2026-10-19).**
  - Code: `AMetalStalker` (`Source/Metalization/Game/MetalStalker.*`), the flight input context in `AMetalPlayerController`, and friendly links (`UMetalBotArchetype::bAllyHackable`; `AMetalBotCharacter::CanBeHackedBy` replaces `CanBeHacked`).
  - Assets and map: `WD_StalkerCannons`, `MZ_FlightTest` (`Tools/AssetPipeline/ue_setup_flight.py`, `MZ_LEVELS=flight`).
  - Test: `smoke_test.ps1 -Scenario flight` links to a parked Stalker, climbs, cruises, boosts, shoots two arms off an enemy Stalker and destroys it, dodges both ways and disconnects. `combat` and `boss` still pass.
  - No AI pilots yet (Phase 6). Unpiloted Stalkers just hover or sit parked.
- **Phase 2: done (2026-10-19).**
  - The graybox ship and hangar kit are in the `.blend` (§2).
  - `blender_export.py` exports `Ships/` parts with UCX collision and writes the socket transforms (UE space) to the manifest. `ue_import_assets.py` imports them to `/Game/Metalization/Ships` and adds the sockets to the meshes. `run_pipeline.ps1` wipes `Ships` before importing.
  - `ue_validate.py` checks each ship mesh for collision and its required sockets, checks the hull length, checks that the hangar mouth is on the +Y side (which catches an axis mix-up), and checks that both ends of the hangar portal are the same size. Result: PASSED.
  - The `flight` and `boss` smoke tests still pass after the full reimport.
- Phase 3: next (`AMetalCapitalShip`, interior instancing, parked Stalkers on the pads).
