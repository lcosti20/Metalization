// Shared types for the Metalization gameplay code: teams, the procedural rig description, flight tuning,
// team paint and hit zones.
//
// Units: metres, m/s and degrees. Unity axes: +Z forward, +X right, +Y up. Values ported from the Unreal
// version (centimetres, X forward / Y right / Z up) go through the UE helpers below.
using System;
using UnityEngine;

namespace Metalization
{
    /// <summary>Which side of the war a robot fights for. Possessed robots temporarily switch to the player's team.</summary>
    public enum MetalTeam
    {
        /// <summary>The player's side.</summary>
        Heroes,
        /// <summary>The enemy side.</summary>
        Villains,
        /// <summary>Hostile to nobody and ignored by AI targeting.</summary>
        Neutral,
    }

    /// <summary>
    /// Bone names and tuning for the procedural animator. The graybox rigs differ per robot (the Player has no
    /// spine, Shooters have digitigrade legs, Brutes have built-in guns), so every archetype describes its own rig.
    /// All rotations are in character space: +Z forward, +X right, +Y up.
    /// </summary>
    [Serializable]
    public class MetalBotRig
    {
        [Header("Bones")]
        public string pelvis = "bip_pelvis";
        public string head = "bip_head";
        public string thighL = "bip_hip_L";
        public string thighR = "bip_hip_R";
        public string kneeL = "bip_knee_L";
        public string kneeR = "bip_knee_R";
        public string upperArmL = "bip_upperArm_L";
        public string upperArmR = "bip_upperArm_R";
        public string forearmL = "bip_lowerArm_L";
        public string forearmR = "bip_lowerArm_R";
        [Tooltip("Bone that weapons are bonemerged onto, and where shots originate.")]
        public string weaponBone = "weapon_bone";
        [Tooltip("Off hand that holds the gun during an acted-out weapon swap (falls back to forearmL).")]
        public string handL = "bip_hand_L";
        [Tooltip("Bone the back weapon rack hangs from (empty = pelvis).")]
        public string backBone = "";
        [Tooltip("Where stowed weapons sit on the back, model space of the bind pose, metres.")]
        public Vector3 rackOffset = new Vector3(0f, 0.70f, -0.24f);
        [Tooltip("Centre between the two jetpack thrusters, model space of the bind pose, metres.")]
        public Vector3 jetpackOffset = new Vector3(0f, 0.84f, -0.20f);
        [Tooltip("Sideways distance of each thruster from jetpackOffset, metres.")]
        public float jetpackSpread = 0.14f;

        [Header("Stance (character-space offsets turning the bind pose into a ready stance)")]
        public Quaternion upperArmLReady = Quaternion.identity;
        public Quaternion upperArmRReady = Quaternion.identity;
        public Quaternion forearmLReady = Quaternion.identity;
        public Quaternion forearmRReady = Quaternion.identity;

        [Header("Gait")]
        [Tooltip("How far the pelvis sinks when fully crouched, metres (bind-pose units).")]
        public float crouchDrop = 0.20f;
        [Tooltip("Distance covered by one full gait cycle (two steps), metres.")]
        public float strideLength = 1.2f;
        public float legSwingDegrees = 28f;
        [Tooltip("Positive bends the lower leg backwards (human knee); negative for reverse-jointed legs.")]
        public float kneeBendDegrees = 35f;
        [Tooltip("Up-and-down pelvis bob per step, metres.")]
        public float bobHeight = 0.03f;
        public float armSwingDegrees = 14f;

        [Header("Aim")]
        public float aimPitchWeight = 1f;
        [Tooltip("Pitch the off-hand arm with the aim as well (two-armed gunners like the Brute).")]
        public bool aimWithBothArms;
        [Tooltip("Rotate the upper arms (shoulder) for aiming instead of the forearms (elbow).")]
        public bool aimFromShoulder;

        [Header("Flight (Stalkers): setting flyerArmUpperL switches on the flight poses")]
        public string flyerArmUpperL = "";
        public string flyerArmUpperR = "";
        public string flyerArmLowerL = "";
        public string flyerArmLowerR = "";
        public float xSpreadDegrees = 28f;
        public float boostSweepDegrees = 40f;
        public float parkedFoldDegrees = 55f;

        public bool IsFlyer => !string.IsNullOrEmpty(flyerArmUpperL);
    }

    /// <summary>Flight handling for robots that fly instead of walk (Stalkers). Speeds in m/s.</summary>
    [Serializable]
    public class MetalFlightTuning
    {
        public float maxSpeed = 45f;
        [Tooltip("Cruise acceleration, m/s per second.")]
        public float acceleration = 60f;
        [Tooltip("Deceleration with no input: the craft coasts to a hover.")]
        public float braking = 40f;
        [Tooltip("How quickly the velocity swings round to a new stick direction. Higher = less drift.")]
        public float grip = 3f;
        [Tooltip("Yaw speed of the body following the camera, degrees per second.")]
        public float turnRate = 140f;
        public float maxBank = 35f;
        public float boostSpeed = 100f;
        public float boostAcceleration = 140f;
        [Tooltip("Seconds of continuous boost from a full meter.")]
        public float boostDuration = 3f;
        [Tooltip("Seconds to refill an empty meter once the boost is released.")]
        public float boostRechargeTime = 5f;
        public float dodgeSpeed = 45f;
        [Tooltip("Boost meter fraction one dodge costs.")]
        public float dodgeCost = 0.25f;
    }

    /// <summary>Per-team paint for robots that fight on both sides: applied to the Body / Accent material slots.</summary>
    [Serializable]
    public class MetalTeamColors
    {
        public MetalTeam team;
        public Color body = Color.gray;
        public Color accent = Color.white;
    }

    /// <summary>
    /// A region of a robot that reacts differently to damage (weak points, destructible weapon arms). A hit counts
    /// against the zone whose bone passes closest to the shot, within radius.
    /// </summary>
    [Serializable]
    public class MetalHitZone
    {
        public string zoneName = "";
        public string bone = "";
        [Tooltip("Radius around the bone (metres, before mesh scale) that counts as a hit on this zone.")]
        public float radius = 0.2f;
        public float damageMultiplier = 1f;
        [Tooltip("Damage this zone can absorb before it breaks. 0 = unbreakable.")]
        public float durability;
        [Tooltip("When the zone breaks the robot loses its gun and switches to melee.")]
        public bool disablesWeaponWhenBroken;
        [Tooltip("When the zone breaks, this built-in gun (one of the archetype's muzzle bones) stops firing.")]
        public string disablesMuzzleBone = "";
    }

    /// <summary>
    /// Converts values authored for the Unreal version (centimetres; X forward, Y right, Z up) into Unity space
    /// (metres; Z forward, X right, Y up). Both are left-handed, so the mapping is a pure axis permutation and
    /// rotation angles keep their sign.
    /// </summary>
    public static class UE
    {
        public const float CmToM = 0.01f;

        /// <summary>World or character-space position in cm → Unity metres.</summary>
        public static Vector3 Vec(float x, float y, float z) => new Vector3(y, z, x) * CmToM;

        /// <summary>Direction (or any unitless vector) → Unity axes.</summary>
        public static Vector3 Dir(float x, float y, float z) => new Vector3(y, z, x);

        /// <summary>
        /// Body-mesh space of an imported Blender character in Unreal (+Y forward, +Z up, -X right), cm →
        /// Unity model space (Blender models import facing +Z), metres.
        /// </summary>
        public static Vector3 MeshVec(float x, float y, float z) => new Vector3(-x, z, y) * CmToM;

        /// <summary>An Unreal FRotator (pitch, yaw, roll in degrees) → Unity rotation, same physical rotation.</summary>
        public static Quaternion Rot(float pitch, float yaw, float roll)
        {
            float hp = pitch % 360f * Mathf.Deg2Rad * 0.5f;
            float hy = yaw % 360f * Mathf.Deg2Rad * 0.5f;
            float hr = roll % 360f * Mathf.Deg2Rad * 0.5f;
            float sp = Mathf.Sin(hp), cp = Mathf.Cos(hp);
            float sy = Mathf.Sin(hy), cy = Mathf.Cos(hy);
            float sr = Mathf.Sin(hr), cr = Mathf.Cos(hr);
            // FRotator::Quaternion() in Unreal space...
            float x = cr * sp * sy - sr * cp * cy;
            float y = -cr * sp * cy - sr * cp * sy;
            float z = cr * cp * sy - sr * sp * cy;
            float w = cr * cp * cy + sr * sp * sy;
            // ...with its axis permuted into Unity space.
            return new Quaternion(y, z, x, w);
        }

        /// <summary>Unreal FRotator::Vector() for a pitch/yaw pair, in Unity space.</summary>
        public static Vector3 RotVector(float pitch, float yaw)
        {
            float p = pitch * Mathf.Deg2Rad, yw = yaw * Mathf.Deg2Rad;
            return Dir(Mathf.Cos(p) * Mathf.Cos(yw), Mathf.Cos(p) * Mathf.Sin(yw), Mathf.Sin(p));
        }
    }

    /// <summary>Small maths helpers matching the Unreal ones the original code relied on.</summary>
    public static class MetalMath
    {
        /// <summary>FMath::FInterpTo: moves towards target at a speed proportional to the distance.</summary>
        public static float InterpTo(float current, float target, float dt, float speed)
        {
            if (speed <= 0f) return target;
            float dist = target - current;
            if (dist * dist < 1e-8f) return target;
            return current + dist * Mathf.Clamp01(dt * speed);
        }

        public static Vector3 InterpTo(Vector3 current, Vector3 target, float dt, float speed)
        {
            if (speed <= 0f) return target;
            Vector3 dist = target - current;
            if (dist.sqrMagnitude < 1e-8f) return target;
            return current + dist * Mathf.Clamp01(dt * speed);
        }

        /// <summary>FMath::FInterpConstantTo.</summary>
        public static float InterpConstantTo(float current, float target, float dt, float speed)
        {
            return Mathf.MoveTowards(current, target, dt * speed);
        }

        /// <summary>FMath::SmoothStep(a, b, x): 0 below a, 1 above b, smooth in between.</summary>
        public static float SmoothStep(float a, float b, float x)
        {
            if (x < a) return 0f;
            if (x >= b) return 1f;
            float t = (x - a) / (b - a);
            return t * t * (3f - 2f * t);
        }

        /// <summary>FMath::InterpEaseInOut.</summary>
        public static float EaseInOut(float a, float b, float alpha, float exp)
        {
            float t = alpha < 0.5f
                ? 0.5f * Mathf.Pow(2f * alpha, exp)
                : 1f - 0.5f * Mathf.Pow(2f * (1f - alpha), exp);
            return Mathf.Lerp(a, b, t);
        }

        /// <summary>FMath::VRandCone: a random unit vector within halfAngle (radians) of dir.</summary>
        public static Vector3 RandomCone(Vector3 dir, float halfAngleRad)
        {
            if (halfAngleRad <= 0f) return dir.normalized;
            float cosMax = Mathf.Cos(halfAngleRad);
            float cos = Mathf.Lerp(cosMax, 1f, UnityEngine.Random.value);
            float sin = Mathf.Sqrt(Mathf.Max(0f, 1f - cos * cos));
            float phi = UnityEngine.Random.value * Mathf.PI * 2f;
            Quaternion toDir = Quaternion.FromToRotation(Vector3.forward, dir.normalized);
            return toDir * new Vector3(sin * Mathf.Cos(phi), sin * Mathf.Sin(phi), cos);
        }

        /// <summary>Distance from point to the infinite line through origin along direction.</summary>
        public static float PointDistToLine(Vector3 point, Vector3 direction, Vector3 origin)
        {
            Vector3 d = direction.normalized;
            Vector3 v = point - origin;
            return (v - Vector3.Dot(v, d) * d).magnitude;
        }

        public static Vector3 Flat(Vector3 v) => new Vector3(v.x, 0f, v.z);

        /// <summary>Yaw of a direction in degrees (0 = +Z, 90 = +X).</summary>
        public static float Yaw(Vector3 dir) => Mathf.Atan2(dir.x, dir.z) * Mathf.Rad2Deg;

        /// <summary>Pitch of a direction in degrees (positive = up).</summary>
        public static float Pitch(Vector3 dir) => Mathf.Atan2(dir.y, new Vector2(dir.x, dir.z).magnitude) * Mathf.Rad2Deg;

        /// <summary>Forward vector for a pitch (positive up) and yaw.</summary>
        public static Vector3 Forward(float pitch, float yaw) => Quaternion.Euler(-pitch, yaw, 0f) * Vector3.forward;

        public static float NormalizeAngle(float a)
        {
            a %= 360f;
            if (a > 180f) a -= 360f;
            if (a < -180f) a += 360f;
            return a;
        }

        /// <summary>
        /// UGameplayStatics::SuggestProjectileVelocity_CustomArc: launch velocity reaching end from start under
        /// gravity (negative y), with arc 0..1 (0.5 = 45 degrees).
        /// </summary>
        public static bool SuggestArcVelocity(Vector3 start, Vector3 end, float gravityY, float arc, out Vector3 velocity)
        {
            velocity = Vector3.zero;
            Vector3 delta = end - start;
            Vector3 flat = Flat(delta);
            float dx = flat.magnitude;
            float dy = delta.y;
            if (dx < 0.001f || gravityY >= 0f) return false;
            float g = -gravityY;
            // Launch angle: arc 0 is the straight line to the target, 1 is straight up.
            float direct = Mathf.Atan2(dy, dx);
            float launch = direct + (Mathf.PI * 0.5f - direct) * arc;
            float cos = Mathf.Cos(launch), tan = Mathf.Tan(launch);
            float denom = 2f * cos * cos * (dx * tan - dy);
            if (denom <= 0f) return false;
            float speed = Mathf.Sqrt(g * dx * dx / denom);
            if (float.IsNaN(speed)) return false;
            velocity = flat.normalized * (speed * cos) + Vector3.up * (speed * Mathf.Sin(launch));
            return true;
        }
    }
}
