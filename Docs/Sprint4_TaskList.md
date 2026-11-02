# Sprint 4: Production Alpha task list

**Sprint goal:** a playable, single-mission prototype (Operation Redline) that shows what the final game will
be like. Blue vs Red ground combat only. Every core mechanic is introduced in order from simplest to most
complex, and four new mechanics are added.

**Deliverables (A4):** Final Gameplay Loop · UI · Sound FX · Test Candidate 01 · Functional Prototype · Testing Round 1

## Final gameplay loop / functional prototype

- [x] Mission design: 13 beats from camp to extraction, simplest mechanic first (`Docs/ProductionAlpha.md`)
- [x] Level script `ue_setup_alpha.py` building `MZ_ProductionAlpha`: valley, trench, bridge, cliff, post, bunker, yard, perch, gate, command yard
- [x] Mission objectives with radio dialogue and control tips for every beat
- [x] Per-mission starting loadout (Laser only, no grenades); weapons handed out as they're introduced
- [x] Checkpoints at the bridge ledge and the patrol yard
- [x] Blue allies: camp, forward post, and a reinforcement hangar that opens with the gate
- [x] Faction colours for robots that fight on both sides (Blue Heroes, Red Villains)
- [x] Boss fight (Red commander Brute) and extraction
- [x] **Sprint** (Shift / L3): sprint speed, FOV kick, longer stride and lean, cancelled by aiming, firing or moving backward
- [x] **Sneak** (Ctrl / C / R3): crouch with a shorter capsule, crouch pose, quiet footsteps
- [x] AI hearing (`ReportNoise`) and suspicion meter (`BuildSuspicion`); a hijacked Red is a disguise
- [x] **Sniper rifle**: graybox model in the .blend; `WD_Sniper` with scope camera, zoom, slow look and hip spread
- [x] Red Marksman archetype with a laser sight
- [x] **Anti-gravity lift** actor (`AMetalGravLift`) with rider control, exit push and a regained air jump
- [x] **Weapon-arm swap animation**: twist off, rack on the back, draw, snap on; visible back rack
- [x] Trench artillery barrage (`AMetalBarrage`) as the reason to sprint
- [x] Grenades lob onto the crosshair (ballistic arc, fuse waits for landing)
- [x] Player hitscan traced along the camera ray (fixes shots blocked at ledges)

## UI

- [x] Title screen map and menu (Start Mission, Level Select, Controls, Quit)
- [x] Pause menu: Restart Mission, Controls page, Title Screen
- [x] HUD: control-tip card, objective marker with distance, stance indicator
- [x] HUD: sniper scope overlay (vignette, mil-dots, range)
- [x] HUD: suspicion "?" meters and "!" alerts over enemies
- [x] HUD: "SWAPPING ARM..." state and swap target in the weapon list
- [x] Results panel: time, kills, deaths, accuracy

## Sound FX

- [x] `generate_sfx.py`: 47 original synthesized sounds (weapons, footsteps, lift, swap, stealth, UI, radio, ambience)
- [x] Pipeline stage that generates and imports them (`/Game/Metalization/Audio`, loops flagged)
- [x] `DA_Sounds` library + `MetalAudio` play-by-name helpers, shared distance attenuation
- [x] Hooks: fire/reload/impact per weapon, hits, explosions, footsteps, jump/land, lift, swap beats, tether, console, gate, pickups, checkpoint, objective, mission complete, radio blips, menu
- [x] Ambient wind and distant-battle loops in the mission

## Test Candidate 01 / Testing round 1

- [x] Smoke-test scenarios for every new beat: `alpha`, `bridge`, `lift`, `swap`, `bunker`, `sneak`, `sniper`, `hack`, `title`
- [x] Regression runs of the existing `combat`, `gate`, `boss` scenarios
- [x] Packaged Development build (new maps and audio added to the cook list)
- [ ] Hand playtest of the full mission, start to finish (team / class)
- [ ] Balance pass from playtest feedback (damage, suspicion times, barrage, lift speed)
- [ ] Outcome portal: screenshots, short capture of each beat, this task list

## Issues found and fixed during the sprint

- Unity build grouping made file-local constants collide when new files were added; the module now builds without unity.
- The audio attenuation object was garbage-collected on level travel (crash in `Attenuation.cpp`); now rooted.
- Shots fired from the perch hit its own edge because the muzzle poked into it; player hitscan now traces from the camera.
- Thrown grenades could never land 10 m away (fixed-speed throw); they now lob onto the aim point.
- The forward-post squads started fighting before the player arrived; they're now spaced beyond each other's sight.
- The gate guard patrolled behind the solid gate door, so the tether lost sight of him; he now waits behind the fence.
