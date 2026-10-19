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

Built in Phase 3 (`Source/Metalization/Game/MetalCapitalShip.*`, `MetalHangar.*`):
- **`UMetalCapitalShipData`** (`DA_Ship_Capital`): the part meshes, hull and subsystem health, `ShieldedHullDamageScale` (0.25), team paint for the `_Hull`/`_Accent` slots, the `Interior` level, and the hangar robot archetype plus its respawn delay.
- **`AMetalCapitalShip`** (Blueprintable): `ShipData`, `Team`, `InteriorLocation`, `DrawDistance`.
  - In the editor it builds the whole ship from the data: the hull, then engines, shields and comms on their sockets, and one turret per `turret_NN`.
  - Hits on a subsystem damage that subsystem. Anything else hits the hull, scaled down while either shield generator stands.
  - Friendly fire is ignored. Hit parts flash, destroyed subsystems turn black, and a destroyed ship breaks up in a chain of explosions.
  - Extension points (BlueprintNativeEvents): `OnSubsystemDestroyed`, `OnShipDestroyed`, `OnInteriorReady` and `GetSpawnPoints`. There are also static delegates `OnAnyInteriorReady` and `OnAnyShipDestroyed`. (I dropped `OnHullBreached`; nothing needed it yet.)
- **`UMetalShipSubsystemComponent`**: a static mesh with `Type` (ShieldGenerator / Engines / Comms / LifeSupport) and health. Ticket penalties come with the battle mode.
- **`AMetalHangar`**: a placed actor (not a component) in the interior level.
  - It owns the shell mesh and an instanced pad per `pad_N` socket.
  - The ship hands it its team and robot. It parks a robot on each pad in team colours and replaces destroyed ones after `RespawnDelay`. A robot that's still out flying keeps its pad.
  - Opened on its own, the interior map runs with the hangar's own settings.
- **Turrets** are scenery for now (instanced meshes). `AMetalShipTurret` with AI arrives in Phase 6.
- **`AMetalSpaceBattleGameMode`** holds joining players back until every ship's interior has loaded, then spawns them at their team ship's spawn points. It never reuses the start spot cached at login, because that was picked before the interiors existed. There's a 10 s fallback.
- A new ship class = a new data asset, an optional Blueprint subclass, and an interior map with an `AMetalHangar`.

## 5. Interior sublevel

- `MZ_CapitalShip_Interior` (`ue_setup_space.py`): the hangar, then a 20 m corridor through its back door, then a 24 x 24 m spawn room with four player starts lined up with the corridor. The life-support room and its sabotage console come with the battle mode.
- Each ship calls `ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr` at its `InteriorLocation`, so one interior map serves both teams. In `MZ_SpaceBattle`, the interiors are at (0, 0, +20 km) and (+10 km, 0, +20 km).
  - **They're above the battle, not below as first planned.** UE's default KillZ is -10.5 km, so anything placed lower dies on arrival.
- **Hidden by draw distance:** interior pieces stop drawing beyond 5 km, and ship parts and space scenery beyond 15 km. Neither side shows up as a speck in the other's sky, and the two interiors can't see each other.
- **Lighting:** the interior has its own point lights. Both maps use the same fixed exposure (EV100 3.5, unbound post-process volume), so crossing between them won't flash.

## 6. Hangar portal

Built in Phase 4 (`Source/Metalization/Game/MetalPortal.*`).
- **Pair:** when its interior is ready, each `AMetalCapitalShip` spawns two `AMetalPortal`s and links them.
  - One is at the hull's `hangar_portal` socket, attached to the hull so it moves with the ship.
  - The other is at the interior hangar's mouth, in the interior level.
  - Each end's +X points out of its hangar, and the ends map one to one in those frames. Standing 30 m inside the hangar shows the view from 30 m inside the hull recess.
  - `MapToLinked()` does the mapping, and the ship exposes the pair as `GetOuterPortal` / `GetInnerPortal`.
- **Window:** a `USceneCaptureComponent2D` is moved every frame (after the camera updates) to where the player's camera would be at the far end, with the same FOV.
  - It renders linear HDR (`SCS_SceneColorHDRNoAlpha`) into an `RTF_RGBA16f` target at half the viewport size.
  - The surface (`M_Portal`) samples that target in screen space, so it lines up pixel for pixel. The main view then applies exposure and tonemapping once: `Brightness` = 1.0 matched by screenshot.
  - It only renders while the camera is on that end's side, within 2.5 km, and the surface was drawn in the last 0.25 s.
- **Instead of a clip plane**, the capture hides the actors that would block it: both portal surfaces, and, looking out of the hangar, the ship's own hull (that camera sits inside it). An oblique clip plane would need `r.AllowGlobalClipPlane` and a project-wide shader rebuild.
- **Transit:** each end tracks the pawns and projectiles near it and its side of the plane. On the frame one crosses inside the opening:
  - A pawn is teleported to the mapped spot at the other end, with its velocity and control rotation turned to match. Only yaw applies to its body, so it stays upright.
  - Its camera lag is switched off for two frames, so the camera doesn't sweep across 20 km.
  - It's marked as arrived so it doesn't bounce straight back.
  - Projectiles fizzle at the threshold.
- **AI navigation links** come with the AI pilots (Phase 6).

## 7. Battle mode

Built in Phase 5 (`MetalSpaceBattleGameMode.*`, `MetalHUD.cpp`):
- **Tickets:** 150 per team, all tunable on the game mode. A team loses:
  - 1 per combatant death: a robot on foot, or a Stalker with an AI pilot aboard. A player who loses their Stalker wakes up in their body, and empty parked Stalkers are just hardware.
  - 20 per shield generator, engines or comms destroyed.
  - 30 for life support, then 1 every 4 s while it stays down.
- **Win/lose:** a team is beaten when its tickets hit zero or its ship is destroyed (which takes its remaining tickets). The stats screen then shows VICTORY or DEFEAT (`CompleteMission` takes a banner now), and there are no respawns after that.
- **Life support is in the hangar, not a sabotage console.** Disconnecting always takes the player back to their own body, so nobody can walk aboard the enemy ship. Instead each `AMetalHangar` has a glowing life-support core against its back wall. It's registered with its ship as a subsystem (`RegisterSubsystem`, damage goes through `ApplySubsystemDamage`), so you board the enemy by flying a Stalker in through their hangar mouth.
- **Subsystem effects:**
  - Shields: hull damage drops to 25% while either generator stands.
  - Engines: the hangar's replacement Stalkers take twice as long (`OnSubsystemDestroyed`, an example of the extension point).
  - Comms: only the ticket penalty for now.
  - A notice announces each kill or loss.
- **HUD:**
  - Across the top: both teams' tickets, hull bars (blue while shielded) and one pip per subsystem.
  - In space: a HANGAR marker for your hull mouth, pinned to the screen edge with an arrow when off screen, and an ENEMY HANGAR marker.
  - Within 2.5 km: blinking markers on the enemy's live subsystems, including LIFE SUPPORT once you're in their hangar.
  - Speed, boost and the PILOT prompt came in Phase 1.
- **Lead reticle:** deferred to Phase 6, when there are moving enemy pilots to lead.
### AI (built in Phase 6)
- **`AMetalFlyerAIController`** (`MetalFlyerAIController.*`): each pilot is a Fighter or a Raider (`PilotRole`). Each falls back on the other job when its own has no target.
  - **Fighters** defend their own ship from enemy flyers within 800 m of it, then hunt within 3 km, and turn on whoever shoots them.
  - **Raiders** attack the enemy ship in order: shield generators, then engines and comms, then life support (flying in through the enemy hangar mouth), then the hull.
  - **Attack pattern:** hover and circle. The pilot holds an orbit point on its own side of the target at a 25-35° elevation, swaying sideways and vertically.
    - Dogfight standoff is 40 m (`PreferredRange`), subsystem standoff 220 m (`StrafeRange`).
    - It fires bursts (archetype burst and pause) when its guns are within 8° of the lead point, the target is inside 450 m (`FireRange`), and nothing else is in the line of fire.
  - **Movement:** steering, not a navmesh. It flies at the point, sweeps 1.2 s ahead to slide off hulls (biased upward), boosts on long legs, and dodge-rolls when hit (every 2.5-4.5 s at most).
  - **Portals as navigation links:** a target in another "space" (open space, or a ship's interior within 600 m of its `InteriorLocation`) means flying through a portal first. The pilot lines up 60 m in front of the mouth, then flies straight through. From an interior it leaves by the inner portal; from space it enters by the target ship's outer portal.
- **AI squadrons:** `AMetalCapitalShip::AIPilots` (Heroes 2, Villains 3, because the player is the Heroes' third). The hangar launches them hovering above the pads, staggered 1.5 s apart, and alternates Fighter and Raider. Destroyed pilots relaunch after the hangar's respawn delay. Parked pad Stalkers stay free for players: an ally Stalker can only be linked while nobody is flying it.
- **Turrets:** `AMetalCapitalShip` now aims and fires its turrets (`TurretWeapon` = `WD_TurretFlak`, 1.2 km range, 70°/s, 2.5° extra spread).
  - Each gun picks the nearest piloted enemy Stalker above its horizon with a clear line of sight, swivels its gun instance to the lead point, and fires when within 6°.
  - Shots carry the ship as owner, so neither the ship nor its team's robots take damage from them.
- **Lead reticle:** while piloting, a ring marks where to shoot the enemy flyer nearest the crosshair, with a faint line back to the target.

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
- **Phase 3: done (2026-10-19).**
  - Built: `AMetalCapitalShip`, `UMetalCapitalShipData` (`DA_Ship_Capital`), `UMetalShipSubsystemComponent`, `AMetalHangar`, `AMetalSpaceBattleGameMode`.
  - Maps: `MZ_CapitalShip_Interior`, plus `MZ_SpaceBattle` (two ships broadside to broadside 1.6 km apart, hangar mouths facing each other, with an asteroid field) from `ue_setup_space.py` (`MZ_LEVELS=space`).
  - `smoke_test.ps1 -Scenario hangar`:
    - Both interiors stream in, and each hangar parks 3 Stalkers in its team's colours.
    - The player spawns in the Hero spawn room, runs down the corridor and pilots the centre Stalker, which climbs to the ceiling.
    - Until the portals exist, a dev-only autopilot step then teleports the Stalker outside the Villain ship. It shoots a shield generator (2500 → 1828) while the shielded hull takes nothing.
  - `-Scenario ships` takes exterior shots of the fleet. `flight`, `combat` and `arena` still pass.
  - The autopilot now starts its clock when the player has a body.
- **Phase 4: done (2026-10-19).**
  - Built: `AMetalPortal`, plus `M_Portal` (from `ue_setup_space.py`). The ship data gained `PortalMaterial` and `HangarMouthSize`.
  - `smoke_test.ps1 -Scenario hangar` now runs the full loop:
    - Spawn aboard, pilot a parked Stalker.
    - Fly out through the hangar mouth: it arrives in space at the hull mouth still doing 45 m/s.
    - Boost toward the enemy and shoot its shield generators (2500 → 2464 / 2404).
    - Turn round, fly back in through the hull mouth into the hangar, disconnect.
  - Screenshots show both windows: space and the enemy ship through the hangar mouth, and the lit hangar with its parked Stalkers through the hull mouth.
  - `flight` and `combat` still pass.
  - The Villain side (its hull end is turned 180° from its interior end) was confirmed by hand in the editor.
- **Phase 5: done (2026-10-19).**
  - Built: tickets, penalties, the life-support bleed, win/lose, the in-hangar life-support core, and the battle HUD and markers.
  - New `smoke_test.ps1 -Scenario battle`:
    - Fly out through our portal. A dev shortcut then jumps the Stalker across the 1.5 km gap to 250 m off the enemy hull mouth.
    - Fly in through the Villain portal (the one turned 180°) and shoot their life support (3000 → 2208).
    - A dev step then wrecks the rest of their ship: tickets 150 → 0 and the VICTORY screen.
  - `hangar`, `combat` and `flight` still pass.
- **Phase 6: done (2026-10-19).**
  - Built: `AMetalFlyerAIController`, the hangar AI squadrons, ship turrets plus `WD_TurretFlak`, and the lead reticle.
  - Stalker AI aim error went from 3° to 1.5°, and its preferred range from 25 m to 40 m.
  - New `smoke_test.ps1 -Scenario dogfight` (90 s, AI only, fixed cameras):
    - Both squadrons launch and fly out through both ships' portals (7 crossings).
    - Fighters dogfight and pilots die: Heroes lost 3 tickets.
    - Raiders circle-strafe the enemy shield generators: Villain shield 2500 → 2128, Hero's 2500 → 2356. Flak is visible around the ships.
  - `hangar`, `battle`, `flight` and `combat` still pass with the AI squadrons in the air.
  - **Balance is a first pass.** In 90 s the 3-pilot Villains had the upper hand, and no subsystem fell, so a full battle would take a long time.
- Phase 7: next (polish and shipping).
