# Flight

Files: `MetalStalker.h/.cpp`, `FMetalFlightTuning` in `MetalTypes.h`

`AMetalStalker` is the flying robot of the space battles: a body with four gun arms in an X. It handles like a hover gunship rather than a plane. It can stop and hover, strafe on every axis, banks into turns, and levels out when the controls are released. It is still an `AMetalBotCharacter`, so it has the same weapons, hit zones, damage and possession as every other robot.

## Movement

Flight is a regular `UCharacterMovementComponent` in `MOVE_Flying`, replaced with `UMetalFlightMovementComponent` through the `FObjectInitializer`. The archetype's `FMetalFlightTuning` sets max speed, acceleration, braking and grip.

**Grip instead of fluid friction.** In flying mode Unreal calls `CalcVelocity` with `bFluid = true` and the physics volume's (very low) fluid friction, so the craft would drift on its momentum, and raising the friction would also add drag that caps the top speed. `UMetalFlightMovementComponent::CalcVelocity` passes `FlightGrip` as the friction and `bFluid = false` instead. Grip then controls how quickly the velocity swings round to the stick direction (tight corners, little drift), while letting go of the stick brakes at a steady `Braking` rate (`bUseSeparateBrakingFriction`, `BrakingDecelerationFlying`).

**Staying in flying mode.** Every possession calls `Restart()`, which resets the movement mode to `DefaultLandMovementMode`. `PossessedBy` and `BeginPlay` therefore set `DefaultLandMovementMode = MOVE_Flying`.

**Controls.** Forward follows the full aim direction (pitch up and push forward to climb), strafing stays level, and `AddVerticalInput` climbs or dives along world up.

## Boost and dodge

- **Boost** (`SetBoostHeld`) is the flight version of sprinting. While held and the meter has charge, it drives the craft forward along its aim and raises the speed and acceleration caps to the boost values. The meter drains over `BoostDuration` and refills over `BoostRechargeTime` after a 0.6 s delay.
- **Dodge** (`Dodge(-1 or +1)`) spends `DodgeCost` of the meter for a sideways velocity burst and a 0.45 s barrel roll. The burst is above the speed limit, so the movement component brakes it back down by itself.

## Rotation and tilt

A character's capsule must stay upright, so only **yaw** is real rotation: the body turns towards the controller's aim at `TurnRate` (`bUseControllerDesiredRotation` with a yaw-only rotation rate).

**Pitch** and **bank** are visual only. `TickTilt` computes them each frame and stores them in `MeshTilt`, which the base class's `TickFlip` applies to the mesh around the capsule centre:

- Pitch follows the aim pitch (clamped to 80 degrees), rate-limited like the yaw.
- Bank combines sideways speed and yaw rate, each contributing up to 60% of `MaxBank`, and eases back to level.
- A dodge adds a full 360-degree roll.

`GetAimForward` uses the visual pitch, so shots go where the nose points.

## Being hit

The capsule only wraps the body; the arms reach far outside it. The mesh is set to query-only collision that blocks `Visibility` and `WorldDynamic`, so hitscan traces and projectile sweeps hit the arms' physics bodies. The hit result then names the bone, and each arm is a hit zone that loses its gun (`DisablesMuzzleBone`) when shot off. See [Robots.md](Robots.md#hit-zones).

Shots leave from the tip of each gun arm (`GetMuzzleLocation` pushes the gun bone outward along the body-to-gun direction by the weapon's `MuzzleOffset`).

## Parked, linked and piloted

| State | How it gets there | Behaviour |
|---|---|---|
| Parked | `bStartParked` and no controller at `BeginPlay` (hangar pads) | Rests on the floor, dormant, arms folded |
| Linked by the player | Hack Tether on a friendly parked Stalker (`bAllyHackable`) | Lifts off the pad with an upward kick on possession; open-ended |
| Released | Player disconnects | Dormant, but unlike walkers it doesn't stop dead: it coasts to a hover (`SetDormant` override), ready to be linked again |
| AI piloted | `AMetalHangar::LaunchPilot` | Spawned flying with an `AMetalFlyerAIController`. See [AI.md](AI.md#pilots-ametalflyeraicontroller) |

A Stalker has no default AI controller (`AIControllerClass = nullptr`): parked ones sit idle until someone links in.

The camera boom lags more than a walker's so boosting reads as fast; portals switch the lag off for a couple of frames when a Stalker teleports through them.
