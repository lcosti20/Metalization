# Procedural animation

Files: `MetalBotAnimInstance.h/.cpp`, `FMetalBotRig` in `MetalTypes.h`

## Why it's procedural

The graybox robots are exported as bind poses only, with no animation clips, and their skeletons differ too much to retarget humanoid animation: the Player has no spine, Shooters have reverse-jointed legs, the Brute's guns are its forearms, Stalkers have four gun arms. Instead, one native anim instance builds every pose from the bind pose plus a handful of rotations driven by gameplay state. Each archetype describes its own rig in `FMetalBotRig`: bone names, a combat-ready stance, gait numbers and aim settings.

When real animation exists for a robot, give its mesh an Anim Blueprint instead and this class simply isn't used for it.

## Structure

`AMetalBotCharacter` assigns `UMetalBotAnimInstance` to every robot mesh. Unreal evaluates animation on worker threads, so the work is split in two:

| Part | Thread | Job |
|---|---|---|
| `UMetalBotAnimInstance::NativeUpdateAnimation` | Game | Reads the robot (velocity, aim pitch, stance, stun, boost...), eases each value into a 0-1 blend weight, advances the gait phase, and copies everything into a `FMetalBotAnimState` snapshot on the proxy |
| `FMetalBotAnimProxy::Evaluate` | Worker | Reads only the snapshot and writes the pose |

One-shot reactions are triggered from gameplay code and decay on their own: `PlayRecoil` (each shot), `PlayHitReact` (each hit), `PlayLand` (touchdown).

The gait phase advances by **distance travelled**, not time (`GroundSpeed * dt / StrideLength`), so feet don't slide at any speed.

## Building the pose

`Evaluate` starts from the reference pose and accumulates a character-space rotation per bone in a map (`Add(bone, delta)` multiplies deltas together), plus a translation for the pelvis. Layers are added in this order, each scaled by its blend weight:

1. **Ready stance**: the rig's `*Ready` rotations (e.g. lowering T-posed arms), faded out when slumped.
2. **Walk cycle**: thighs swing along the movement direction, knees bend on the forward swing, pelvis bobs and rolls.
3. **Idle**: slight breathing bob and a slow head turn when standing still.
4. **Free running** (action camera, not in combat): lean into the stride, arms pump.
5. **In the air**: one leg tucked, arms out for balance.
6. **Double-jump flip**: curl into a ball (`FlipTuck`).
7. **Landing**: knees absorb the impact.
8. **Aim**: the weapon arm (forearm, or upper arm with `bAimFromShoulder`; both arms with `bAimWithBothArms`) pitches with the aim and recoil; the head follows a little.
9. **Hit react**: the pelvis snaps away from the hit direction.
10. **Slump / stun**: head down, knees bent, pelvis dropped; stun adds a fast twitch.
11. **Flight** (rigs with `FlyerArmUpperL` set): the four arms open into an X, close and sweep back under boost, kick back on each shot, and fold flat while parked. The body bobs as it hovers.

### Applying the rotations

The deltas are authored in **character space** (X forward, Y right, Z up), but bones live in the mesh's component space, which is rotated relative to the capsule (`MeshToCharacter`, -90 degrees yaw). The final loop walks the bones parent-first:

1. Compute the bone's component-space transform from its local transform and its (already posed) parent.
2. If it has a delta, convert it to component space by conjugation (`ToComponent * Delta * MeshToCharacter`) and pre-multiply it onto the bone's rotation.
3. Add the pelvis offset to the pelvis.
4. Convert back to a parent-relative transform, which is what the compact pose stores.

Because each delta is applied in component space after the parent has been posed, a rotation like "swing the thigh forward 20 degrees" means the same thing on every robot, regardless of how its bones are oriented in the bind pose.

## Tuning a new robot

Fill in the archetype's `Rig`:

- Bone names. Leave any that don't exist as their defaults; missing bones are skipped.
- `*Ready` rotations until the idle pose looks combat-ready.
- `StrideLength` (cm per two steps) so feet don't slide; `LegSwingDegrees`, `KneeBendDegrees` (negative for reverse knees), `BobHeight`, `ArmSwingDegrees` (0 keeps fixed gun arms still).
- `AimPitchWeight`, `bAimFromShoulder`, `bAimWithBothArms` to match how it holds its gun.
- For flyers, the four arm bones and the spread / sweep / fold angles.
