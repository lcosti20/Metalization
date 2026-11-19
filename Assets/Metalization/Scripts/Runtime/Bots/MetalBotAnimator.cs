// Procedural animation for the Metalization robots.
//
// The graybox robots ship as bind poses only (no animation clips), and their skeletons differ wildly (the
// Player has no spine, Shooters have reverse knees, Brutes carry guns as forearms). Instead of retargeting
// humanoid clips, this component builds the pose from the bind pose plus a handful of character-space
// rotations driven by gameplay state, using the archetype's MetalBotRig. Authored animation can replace it
// later by giving the model an Animator instead.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    [DefaultExecutionOrder(100)]
    public class MetalBotAnimator : MonoBehaviour
    {
        MetalBot bot;
        Transform[] boneOrder = new Transform[0];
        Vector3[] bindPos;
        Quaternion[] bindRot;
        Vector3[] bindScale;
        readonly Dictionary<Transform, int> indexOf = new Dictionary<Transform, int>();
        readonly HashSet<Transform> hidden = new HashSet<Transform>();

        // Smoothed state (see Evaluate for what each does).
        float gaitPhase, moveAlpha, inAirAlpha, slumpAlpha, stunAlpha, recoil, hitReact, aimPitch, time;
        float combatAlpha = 1f, landAlpha, lean, boostAlpha, parkedAlpha;
        Vector3 moveDirection = Vector3.forward;
        Vector3 hitDirection = Vector3.forward;
        float hipHeight = -1f;

        // Per-frame working sets.
        readonly Dictionary<Transform, Quaternion> rotations = new Dictionary<Transform, Quaternion>();
        readonly Dictionary<Transform, float> scales = new Dictionary<Transform, float>();

        /// <summary>Captures the bind pose of the robot's skeleton (call once the body model exists).</summary>
        public void Init(MetalBot owner)
        {
            bot = owner;
            var list = new List<Transform>();
            if (bot.BodyInstance != null)
            {
                foreach (Transform t in bot.BodyInstance.GetComponentsInChildren<Transform>(true))
                {
                    if (t == bot.BodyInstance.transform || t.GetComponent<Renderer>() != null) continue;
                    list.Add(t); // depth-first pre-order: parents always come before their children
                }
            }
            boneOrder = list.ToArray();
            bindPos = new Vector3[boneOrder.Length];
            bindRot = new Quaternion[boneOrder.Length];
            bindScale = new Vector3[boneOrder.Length];
            indexOf.Clear();
            for (int i = 0; i < boneOrder.Length; i++)
            {
                bindPos[i] = boneOrder[i].localPosition;
                bindRot[i] = boneOrder[i].localRotation;
                bindScale[i] = boneOrder[i].localScale;
                indexOf[boneOrder[i]] = i;
            }
        }

        /// <summary>Kicks the body back from a hit travelling in worldDirection.</summary>
        public void PlayHitReact(Vector3 worldDirection)
        {
            Vector3 local = MetalMath.Flat(transform.InverseTransformDirection(worldDirection));
            hitDirection = local.sqrMagnitude > 1e-6f ? local.normalized : Vector3.forward;
            hitReact = 1f;
        }

        /// <summary>Weapon kick of one shot.</summary>
        public void PlayRecoil() => recoil = 1f;

        /// <summary>Knee-bend absorb on touchdown, 0..1; a stronger landing overrides a weaker one still playing.</summary>
        public void PlayLand(float strength) => landAlpha = Mathf.Max(landAlpha, strength);

        /// <summary>Shrinks a bone (and everything below it) to nothing: a shot-off arm.</summary>
        public void HideBone(string boneName) => SetBoneHidden(boneName, true);

        /// <summary>Hides or shows a bone and its children (a dispenser welds robots together part by part).</summary>
        public void SetBoneHidden(string boneName, bool hide)
        {
            Transform t = bot != null ? bot.GetBone(boneName) : null;
            if (t == null) return;
            if (hide) hidden.Add(t);
            else hidden.Remove(t);
        }

        /// <summary>True if the bone itself was hidden (not just one of its parents).</summary>
        public bool IsBoneHidden(Transform bone) => hidden.Contains(bone);

        static Quaternion SwingToward(Vector3 direction, float degrees)
        {
            Vector3 axis = Vector3.Cross(Vector3.down, direction);
            return axis.sqrMagnitude < 1e-8f ? Quaternion.identity : Quaternion.AngleAxis(degrees, axis.normalized);
        }

        static Quaternion PitchUp(float degrees) => Quaternion.AngleAxis(degrees, Vector3.left);
        static Quaternion Yaw(float degrees) => Quaternion.AngleAxis(degrees, Vector3.up);
        static Quaternion Roll(float degrees) => Quaternion.AngleAxis(degrees, Vector3.forward);

        void Add(Transform bone, Quaternion delta)
        {
            if (bone == null) return;
            rotations[bone] = rotations.TryGetValue(bone, out Quaternion existing) ? delta * existing : delta;
        }

        void LateUpdate()
        {
            if (bot == null || bot.archetype == null || bot.MeshRoot == null || boneOrder.Length == 0) return;
            float dt = Time.deltaTime;
            UpdateState(dt, out State s);
            Evaluate(s);
        }

        struct State
        {
            public float sprintAlpha, crouchAlpha, flipTuck;
            public float swapReach, swapStow, swapTwist;
            public float reloadLower, reloadFetch, reloadEject, reloadReach, reloadSlap, reloadRack, drawLower;
        }

        void UpdateState(float dt, out State s)
        {
            s = default;
            time += dt;
            MetalBotRig rig = bot.Rig;
            float scale = Mathf.Max(0.1f, bot.MeshScale);
            Vector3 localVelocity = transform.InverseTransformDirection(bot.Velocity);
            float groundSpeed = new Vector2(localVelocity.x, localVelocity.z).magnitude;
            // Flyers have no legs to walk or fall with; their arms get the flight poses instead.
            MetalStalker flyer = bot as MetalStalker;
            bool isFlyer = rig.IsFlyer;
            bool falling = !isFlyer && bot.Movement != null && bot.Movement.IsFalling;
            bool canMove = bot.IsAlive && !bot.IsSlumped && !isFlyer;

            if (groundSpeed > 0.1f) moveDirection = new Vector3(localVelocity.x, 0f, localVelocity.z).normalized;
            float targetMove = canMove && !falling ? Mathf.Clamp01(groundSpeed / 2f) : 0f;
            moveAlpha = MetalMath.InterpTo(moveAlpha, targetMove, dt, 10f);
            inAirAlpha = MetalMath.InterpTo(inAirAlpha, falling && canMove ? 1f : 0f, dt, 8f);
            // A body left crouched while the player hacks stays a tidy console instead of sagging.
            bool console = bot.IsAlive && bot.IsSneaking;
            slumpAlpha = MetalMath.InterpTo(slumpAlpha, bot.IsSlumped && !isFlyer && !console ? 1f : 0f, dt, 5f);
            boostAlpha = MetalMath.InterpTo(boostAlpha, flyer != null && flyer.IsBoosting ? 1f : 0f, dt, 5f);
            parkedAlpha = MetalMath.InterpTo(parkedAlpha, flyer != null && (flyer.IsParked || flyer.IsDormant) ? 1f : 0f, dt, 3f);
            stunAlpha = bot.IsStunned ? 1f : MetalMath.InterpTo(stunAlpha, 0f, dt, 6f);
            recoil = MetalMath.InterpTo(recoil, 0f, dt, 14f);
            hitReact = MetalMath.InterpTo(hitReact, 0f, dt, 7f);
            aimPitch = MetalMath.InterpTo(aimPitch, bot.GetAimPitch(), dt, 18f);
            combatAlpha = MetalMath.InterpTo(combatAlpha, bot.IsInCombatStance ? 1f : 0f, dt, 8f);
            landAlpha = MetalMath.InterpTo(landAlpha, 0f, dt, 7f);
            // Running freely the robot leans into its stride, more so when sprinting.
            s.sprintAlpha = bot.SprintAlpha;
            lean = MetalMath.InterpTo(lean, (9f * (1f - combatAlpha) + 9f * s.sprintAlpha) * moveAlpha, dt, 6f);

            // Advance the gait by distance rather than time, so the feet don't slide at any speed.
            float stride = Mathf.Max(0.1f, rig.strideLength * scale * (1f + 0.4f * s.sprintAlpha));
            gaitPhase = Mathf.Repeat(gaitPhase + groundSpeed * dt / stride, 1f);

            s.crouchAlpha = bot.CrouchAlpha;
            s.flipTuck = bot.FlipTuck;
            if (hipHeight < 0f && bot.CanSneak)
            {
                // Feet are at the model origin, so the thigh's bind height is how far the body can sink.
                hipHeight = bot.TryGetBindMatrix(rig.thighL, out Matrix4x4 thigh) ? Mathf.Max(0f, thigh.GetColumn(3).y) : 0f;
            }

            // Weapon swap beats: reach and twist the gun off (0-0.3), rack it and pull the new one (0.3-0.6),
            // bring it round and twist it on (0.6-0.9).
            float swap = bot.SwapProgress;
            if (swap >= 0f)
            {
                float In(float a, float b) => MetalMath.SmoothStep(a, b, swap);
                s.swapReach = In(0f, 0.18f) * (1f - In(0.30f, 0.40f)) + In(0.60f, 0.74f) * (1f - In(0.86f, 1f));
                s.swapStow = In(0.32f, 0.47f) * (1f - In(0.53f, 0.68f));
                s.swapTwist = In(0.14f, 0.28f) * (1f - In(0.30f, 0.36f)) + In(0.70f, 0.80f) * (1f - In(0.80f, 0.90f));
            }

            // Reload beats, in seconds so they stay on the sound's clicks whatever the weapon's reload time.
            if (bot.GetReloadTimeline(out float elapsed, out float snd))
            {
                float InS(float a, float b) => MetalMath.SmoothStep(a, b, snd);
                float Pulse(float start, float peak, float end) => InS(start, peak) * (1f - InS(peak, end));
                s.reloadLower = MetalMath.SmoothStep(0f, 0.18f, elapsed) * (1f - InS(0.9f, 1.12f));
                s.reloadFetch = MetalMath.SmoothStep(0.05f, 0.3f, elapsed) * (1f - InS(0.12f, 0.34f));
                s.reloadEject = Pulse(0f, 0.06f, 0.28f);
                s.reloadReach = InS(0.22f, 0.44f) * (1f - InS(0.9f, 1.06f));
                s.reloadSlap = Pulse(0.43f, 0.49f, 0.64f);
                s.reloadRack = Pulse(0.68f, 0.73f, 0.8f) + Pulse(0.82f, 0.87f, 0.97f);
            }
            float draw = bot.DrawProgress;
            s.drawLower = draw >= 0f ? MetalMath.SmoothStep(0f, 0.45f, draw) * (1f - MetalMath.SmoothStep(0.55f, 1f, draw)) : 0f;
        }

        void Evaluate(State s)
        {
            MetalBotRig rig = bot.Rig;
            rotations.Clear();
            scales.Clear();
            Vector3 pelvisOffset = Vector3.zero;

            Transform pelvis = bot.GetBone(rig.pelvis);
            Transform head = bot.GetBone(rig.head);
            Transform thighL = bot.GetBone(rig.thighL), thighR = bot.GetBone(rig.thighR);
            Transform kneeL = bot.GetBone(rig.kneeL), kneeR = bot.GetBone(rig.kneeR);
            Transform upperArmL = bot.GetBone(rig.upperArmL), upperArmR = bot.GetBone(rig.upperArmR);
            Transform forearmL = bot.GetBone(rig.forearmL), forearmR = bot.GetBone(rig.forearmR);

            // Crouched, the robot is a console gliding on its base: no steps, bob or lean, just the crouch pose.
            float standing = 1f - s.crouchAlpha;
            float cycle = gaitPhase * Mathf.PI * 2f;
            float sin = Mathf.Sin(cycle), cos = Mathf.Cos(cycle);
            float upright = 1f - slumpAlpha;
            float relaxed = (1f - combatAlpha) * upright;
            // Robots with built-in or fixed gun arms (armSwing 0) keep them still.
            float armsRelaxed = rig.armSwingDegrees > 0f ? relaxed : 0f;

            // Combat-ready stance on top of the bind pose.
            Add(upperArmL, Quaternion.Slerp(Quaternion.identity, rig.upperArmLReady, upright));
            Add(upperArmR, Quaternion.Slerp(Quaternion.identity, rig.upperArmRReady, upright));
            Add(forearmL, Quaternion.Slerp(Quaternion.identity, rig.forearmLReady, upright));
            Add(forearmR, Quaternion.Slerp(Quaternion.identity, rig.forearmRReady, upright));

            // The off hand is busy during a weapon swap or reload. Robots that shoot with both arms have no off hand.
            bool offHand = !rig.aimWithBothArms && !rig.IsFlyer;
            float offHandReload = offHand ? Mathf.Max(s.reloadFetch, s.reloadReach) : 0f;
            float leftArmFree = 1f - Mathf.Max(Mathf.Max(s.swapReach, s.swapStow), offHandReload);

            // Walk cycle: legs swing along the movement direction, knees fold while the leg travels forward.
            float step = moveAlpha * standing;
            float swing = rig.legSwingDegrees * step * (1f + 0.35f * s.sprintAlpha);
            Add(thighL, SwingToward(moveDirection, swing * sin));
            Add(thighR, SwingToward(moveDirection, -swing * sin));
            Add(kneeL, SwingToward(moveDirection, -rig.kneeBendDegrees * step * Mathf.Max(0f, cos)));
            Add(kneeR, SwingToward(moveDirection, -rig.kneeBendDegrees * step * Mathf.Max(0f, -cos)));
            pelvisOffset.y += rig.bobHeight * step * (Mathf.Abs(sin) - 0.5f);
            Add(pelvis, Roll(3f * step * sin));

            // Idle "breathing" so the robots never freeze completely.
            float idle = (1f - moveAlpha) * upright * standing;
            pelvisOffset.y += idle * 0.006f * Mathf.Sin(time * 2.2f);
            Add(head, Yaw(idle * 12f * Mathf.Sin(time * 0.45f)));

            // Free running: lean into the stride, arms pump against the legs with the elbows bent.
            Add(pelvis, PitchUp(-lean * standing));
            Add(thighL, SwingToward(Vector3.forward, lean * standing));
            Add(thighR, SwingToward(Vector3.forward, lean * standing));
            float pump = rig.armSwingDegrees * 1.6f * moveAlpha * armsRelaxed * (1f + 0.6f * s.sprintAlpha);
            Add(upperArmL, SwingToward(Vector3.forward, -pump * sin * leftArmFree));
            Add(upperArmR, SwingToward(Vector3.forward, (rig.armSwingDegrees * moveAlpha * armsRelaxed + pump) * sin));
            Add(forearmL, SwingToward(Vector3.forward, 45f * moveAlpha * armsRelaxed * leftArmFree));
            Add(forearmR, SwingToward(Vector3.forward, 45f * moveAlpha * armsRelaxed));

            // Jumping / falling: tuck one leg, extend the other; arms fling out for balance when not shooting.
            Add(thighL, SwingToward(Vector3.forward, 28f * inAirAlpha));
            Add(thighR, SwingToward(Vector3.forward, -12f * inAirAlpha));
            Add(kneeL, SwingToward(Vector3.forward, -rig.kneeBendDegrees * 1.4f * inAirAlpha));
            Add(kneeR, SwingToward(Vector3.forward, -rig.kneeBendDegrees * 0.6f * inAirAlpha));
            float fling = 50f * inAirAlpha * armsRelaxed * (1f - s.flipTuck);
            Add(upperArmL, SwingToward(new Vector3(-1f, 0f, 0.2f).normalized, fling * leftArmFree));
            Add(upperArmR, SwingToward(new Vector3(1f, 0f, 0.2f).normalized, fling));

            // Double-jump flip: curl into a ball.
            if (s.flipTuck > 1e-4f)
            {
                Add(thighL, SwingToward(Vector3.forward, 55f * s.flipTuck));
                Add(thighR, SwingToward(Vector3.forward, 70f * s.flipTuck));
                Add(kneeL, SwingToward(Vector3.forward, -Mathf.Abs(rig.kneeBendDegrees) * 1.6f * s.flipTuck));
                Add(kneeR, SwingToward(Vector3.forward, -Mathf.Abs(rig.kneeBendDegrees) * 2f * s.flipTuck));
                Add(upperArmL, SwingToward(Vector3.forward, 35f * s.flipTuck));
                Add(upperArmR, SwingToward(Vector3.forward, 35f * s.flipTuck));
                Add(head, PitchUp(-25f * s.flipTuck));
            }

            // Landing: absorb the impact through the knees.
            if (landAlpha > 1e-4f)
            {
                Add(thighL, SwingToward(Vector3.forward, 22f * landAlpha));
                Add(thighR, SwingToward(Vector3.forward, 22f * landAlpha));
                Add(kneeL, SwingToward(Vector3.forward, -rig.kneeBendDegrees * 1.2f * landAlpha));
                Add(kneeR, SwingToward(Vector3.forward, -rig.kneeBendDegrees * 1.2f * landAlpha));
                pelvisOffset.y -= 0.09f * landAlpha;
            }

            // Sneaking: the console disguise. Legs fold up and arms fold against the sides, both pull into the body
            // while it sinks to sit on the ground, upright and boxy with the head tilted down like a screen.
            if (s.crouchAlpha > 1e-4f)
            {
                float c = s.crouchAlpha;
                float kneeSign = rig.kneeBendDegrees >= 0f ? 1f : -1f;
                Add(thighL, SwingToward(Vector3.forward, 80f * kneeSign * c));
                Add(thighR, SwingToward(Vector3.forward, 80f * kneeSign * c));
                Add(kneeL, SwingToward(Vector3.forward, -150f * kneeSign * c));
                Add(kneeR, SwingToward(Vector3.forward, -150f * kneeSign * c));
                Add(upperArmL, SwingToward(Vector3.right, 25f * c));
                Add(upperArmR, SwingToward(Vector3.left, 25f * c));
                Add(forearmL, SwingToward(Vector3.forward, 90f * c));
                Add(forearmR, SwingToward(Vector3.forward, 90f * c));
                // The limbs retract once they've started folding, so the fold reads before they vanish.
                float tuck = 1f - 0.97f * MetalMath.SmoothStep(0.15f, 0.85f, c);
                foreach (Transform limb in new[] { thighL, thighR, upperArmL, upperArmR })
                {
                    if (limb != null) scales[limb] = tuck;
                }
                Add(head, PitchUp(-8f * c));
                Add(pelvis, SwingToward(moveDirection, -5f * moveAlpha * c));
                // The capsule shrinks around its bottom but the model stays put, so sink by about the hip height.
                float drop = hipHeight > 0f ? hipHeight * 0.8f : rig.crouchDrop;
                pelvisOffset.y -= drop * c;
            }

            // Weapon swap, off hand: reach across to the gun arm, twist, then swing up over the shoulder to the rack.
            if (s.swapReach > 1e-4f || s.swapStow > 1e-4f)
            {
                float r = s.swapReach * upright;
                float w = s.swapStow * upright;
                Add(upperArmL, Yaw(55f * r) * SwingToward(Vector3.right, 22f * r) * SwingToward(Vector3.forward, 70f * r));
                Add(forearmL, Yaw(80f * r));
                Add(forearmR, Yaw(-30f * r) * PitchUp(10f * r));
                Add(upperArmL, SwingToward(new Vector3(0.3f, 0f, 1f), 160f * w));
                Add(forearmL, SwingToward(Vector3.forward, 70f * w));
                Add(pelvis, Yaw(-14f * w));
                Add(head, Yaw(-25f * w) * PitchUp(-8f * r));
            }
            if (s.swapTwist > 1e-4f)
            {
                Add(forearmR, Roll(75f * s.swapTwist));
                Add(forearmL, Roll(-60f * s.swapTwist));
            }

            // Aim: the weapon arm follows the camera (or AI) pitch, except while lowered to reload or draw.
            float lowered = Mathf.Max(s.reloadLower, s.drawLower);
            float aim = (aimPitch * rig.aimPitchWeight * combatAlpha * (1f - 0.75f * lowered) + 9f * recoil) * upright;
            Transform aimR = rig.aimFromShoulder ? upperArmR : forearmR;
            Transform aimL = rig.aimFromShoulder ? upperArmL : forearmL;

            // Reload: the gun arm drops and rolls its cell port up towards the off hand; the spent cell kicks the gun,
            // the slap and the two racks jolt it, and it lifts back to aim at the end.
            if (!rig.IsFlyer && (s.reloadLower > 1e-4f || s.drawLower > 1e-4f))
            {
                float l = s.reloadLower * upright;
                float jolt = (8f * s.reloadSlap + 6f * s.reloadRack) * upright;
                float d = s.drawLower * upright;
                float gunDrop = -35f * l - 10f * s.reloadEject * upright + jolt - 65f * d;
                float gunRoll = 40f * l + 20f * s.reloadEject * upright + 30f * d;
                Add(aimR, PitchUp(gunDrop));
                Add(forearmR, Roll(gunRoll));
                if (rig.aimWithBothArms)
                {
                    // Twin guns: both drop and roll outwards (mirrored), and the slap knocks them together.
                    Add(aimL, PitchUp(gunDrop));
                    Add(forearmL, Roll(-gunRoll));
                    Add(forearmR, Yaw(14f * s.reloadSlap * upright));
                    Add(forearmL, Yaw(-14f * s.reloadSlap * upright));
                }
                Add(head, PitchUp(-14f * l - 10f * d) * Yaw(10f * l * (offHand ? 1f : 0f)));
                Add(pelvis, Yaw(6f * l) * PitchUp(-2f * s.reloadSlap * upright));
                pelvisOffset.y -= 0.015f * s.reloadSlap * upright;
            }
            if (offHand && (s.reloadFetch > 1e-4f || s.reloadReach > 1e-4f))
            {
                // Off hand: down and back to the hip for a fresh cell, then across to the lowered gun.
                float f = s.reloadFetch * upright;
                Add(upperArmL, SwingToward(new Vector3(-0.4f, 0f, -0.6f).normalized, 30f * f));
                Add(forearmL, SwingToward(Vector3.forward, 50f * f));
                float r = s.reloadReach * upright;
                float push = 12f * s.reloadSlap * upright;
                float pull = -22f * s.reloadRack * upright;
                Add(upperArmL, Yaw(45f * r) * SwingToward(Vector3.right, 20f * r) * SwingToward(Vector3.forward, 50f * r + (push + pull) * r));
                Add(forearmL, Yaw(70f * r) * SwingToward(Vector3.forward, (25f + pull) * r));
            }
            Add(aimR, PitchUp(aim));
            if (rig.aimWithBothArms) Add(aimL, PitchUp(aim));
            else Add(upperArmL, SwingToward(Vector3.forward, -rig.armSwingDegrees * moveAlpha * sin * leftArmFree));
            Add(head, PitchUp(aimPitch * 0.35f * combatAlpha * upright));
            pelvisOffset.z -= 0.015f * recoil;

            // Hit reaction: the torso snaps away from the impact.
            if (hitReact > 1e-4f)
            {
                Vector3 away = -hitDirection;
                Vector3 axis = Vector3.Cross(Vector3.up, away);
                if (axis.sqrMagnitude > 1e-6f) Add(pelvis, Quaternion.AngleAxis(10f * hitReact, axis.normalized));
            }

            // Powered down or stunned: sag, head down, arms limp.
            if (slumpAlpha > 1e-4f || stunAlpha > 1e-4f)
            {
                float twitch = stunAlpha * 6f * Mathf.Sin(time * 47f);
                float sag = Mathf.Max(slumpAlpha, stunAlpha * 0.6f);
                Add(head, PitchUp(-35f * sag + twitch));
                Add(thighL, SwingToward(Vector3.forward, 25f * sag));
                Add(thighR, SwingToward(Vector3.forward, 25f * sag));
                Add(kneeL, SwingToward(Vector3.forward, -45f * sag));
                Add(kneeR, SwingToward(Vector3.forward, -45f * sag));
                Add(pelvis, PitchUp(-12f * sag) * Yaw(twitch));
                pelvisOffset.y -= 0.06f * sag;
            }

            // Flying: the four gun arms open into an X, close up and sweep back under boost, kick back on each shot,
            // and fold back flat while parked. The body bobs as it hovers.
            if (rig.IsFlyer)
            {
                float active = 1f - parkedAlpha;
                float spread = rig.xSpreadDegrees * Mathf.Lerp(0.75f, 1f, combatAlpha) * (1f - 0.6f * boostAlpha) * active;
                float flex = 3f * Mathf.Sin(time * 1.7f) * active;
                float back = rig.boostSweepDegrees * boostAlpha + rig.parkedFoldDegrees * parkedAlpha + 4f * recoil;
                (string bone, float side, float lift)[] arms =
                {
                    (rig.flyerArmUpperL, -1f, 1f), (rig.flyerArmUpperR, 1f, 1f),
                    (rig.flyerArmLowerL, -1f, -1f), (rig.flyerArmLowerR, 1f, -1f),
                };
                foreach (var arm in arms)
                {
                    Add(bot.GetBone(arm.bone), Yaw(arm.side * back) * Roll(arm.side * arm.lift * (spread + flex)));
                }
                pelvisOffset.y += 0.08f * active * Mathf.Sin(time * 2.1f);
            }

            Apply(pelvis, pelvisOffset);
        }

        /// <summary>
        /// Resets the skeleton to the bind pose and applies the character-space deltas down the hierarchy (parents
        /// first). Each delta is conjugated from character space into world space and pre-multiplied onto the bone.
        /// </summary>
        void Apply(Transform pelvis, Vector3 pelvisOffset)
        {
            Transform meshRoot = bot.MeshRoot;
            Quaternion charRot = meshRoot.rotation * Quaternion.Inverse(bot.MeshBaseRotation);
            Quaternion charInv = Quaternion.Inverse(charRot);
            for (int i = 0; i < boneOrder.Length; i++)
            {
                Transform bone = boneOrder[i];
                bone.localPosition = bindPos[i];
                bone.localRotation = bindRot[i];
                Vector3 sc = bindScale[i];
                if (scales.TryGetValue(bone, out float shrink)) sc *= shrink;
                if (hidden.Contains(bone)) sc = Vector3.zero;
                bone.localScale = sc;
                if (rotations.TryGetValue(bone, out Quaternion delta))
                {
                    bone.rotation = (charRot * delta * charInv) * bone.rotation;
                }
                if (bone == pelvis)
                {
                    bone.position += meshRoot.TransformVector(Quaternion.Inverse(bot.MeshBaseRotation) * pelvisOffset);
                }
            }
        }
    }
}
