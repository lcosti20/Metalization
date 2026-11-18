// Character movement on top of Unity's CharacterController, reproducing the parts of Unreal's
// CharacterMovementComponent the game was tuned against: walking with friction and braking, falling with air
// control and scaled gravity, multi-jumps, crouching with a shorter capsule, flying with "grip", and the
// rotation modes (face movement, face the controller's desired rotation).
using System;
using UnityEngine;

namespace Metalization
{
    public enum MetalMoveMode
    {
        None,
        Walking,
        Falling,
        Flying,
    }

    [RequireComponent(typeof(CharacterController))]
    public class MetalCharacterMovement : MonoBehaviour
    {
        [Header("Walking (m/s)")]
        public float maxWalkSpeed = 5f;
        public float maxWalkSpeedCrouched = 2f;
        public float maxAcceleration = 24f;
        public float brakingDecelerationWalking = 20f;
        public float groundFriction = 8f;

        [Header("Falling")]
        public float gravityScale = 1.6f;
        public float airControl = 0.6f;
        public float jumpVelocity = 6.5f;
        public int jumpMaxCount = 1;

        [Header("Flying")]
        public float maxFlySpeed = 45f;
        public float brakingDecelerationFlying = 40f;
        public float flightGrip = 3f;

        [Header("Rotation")]
        [Tooltip("Yaw rate when turning towards the movement or the controller, degrees per second.")]
        public float rotationRate = 600f;
        public bool orientRotationToMovement;
        public bool useControllerDesiredRotation;

        [Header("Crouch")]
        public bool canCrouch;
        public float crouchedHalfHeight = 0.44f;

        /// <summary>Unreal's BrakingFrictionFactor default.</summary>
        const float BrakingFrictionFactor = 2f;
        const float BrakeToStopVelocity = 0.1f;

        public MetalMoveMode Mode { get; private set; } = MetalMoveMode.Walking;
        public MetalMoveMode DefaultLandMode { get; set; } = MetalMoveMode.Walking;
        public Vector3 Velocity { get; set; }
        /// <summary>Acceleration applied this frame (input direction times max acceleration).</summary>
        public Vector3 Acceleration { get; private set; }
        public int JumpCurrentCount { get; private set; }
        public bool IsCrouched { get; private set; }
        public bool WantsToCrouch { get; private set; }
        public bool IsFalling => Mode == MetalMoveMode.Falling;
        public bool IsMovingOnGround => Mode == MetalMoveMode.Walking;
        public bool IsFlying => Mode == MetalMoveMode.Flying;
        public CharacterController Capsule { get; private set; }
        /// <summary>A gravlift sets the velocity while it carries the robot: flying keeps it as given (input still steers a little).</summary>
        public bool LiftRiding { get; set; }

        /// <summary>Gives back the mid-air jumps (a lift tossing its rider onto a landing).</summary>
        public void ResetJumpCount() => JumpCurrentCount = 0;
        public float FullHalfHeight { get; private set; } = 0.68f;
        public float CurrentHalfHeight => IsCrouched ? crouchedHalfHeight : FullHalfHeight;

        /// <summary>Fired after a jump starts (the count is already incremented, so 2+ is a mid-air jump).</summary>
        public event Action Jumped;
        /// <summary>Fired on touching the ground after falling.</summary>
        public event Action Landed;

        Vector3 pendingInput;
        bool jumpPressed;
        MetalBot bot;

        void Awake()
        {
            Capsule = GetComponent<CharacterController>();
            bot = GetComponent<MetalBot>();
        }

        /// <summary>Sets the standing capsule size; the transform sits at the capsule centre, like an Unreal character.</summary>
        public void SetCapsuleSize(float radius, float halfHeight)
        {
            if (Capsule == null) Capsule = GetComponent<CharacterController>();
            FullHalfHeight = halfHeight;
            Capsule.radius = radius;
            ApplyCapsuleHeight();
            Capsule.stepOffset = Mathf.Min(0.45f, halfHeight);
            Capsule.slopeLimit = 44.8f;
            Capsule.skinWidth = Mathf.Max(0.01f, radius * 0.08f);
            Capsule.minMoveDistance = 0f;
        }

        void ApplyCapsuleHeight()
        {
            float half = IsCrouched ? crouchedHalfHeight : FullHalfHeight;
            Capsule.height = half * 2f;
            // Crouching shrinks the capsule around its bottom, so the feet stay put.
            Capsule.center = new Vector3(0f, half - FullHalfHeight, 0f);
            Capsule.stepOffset = Mathf.Min(Capsule.stepOffset, Mathf.Max(0.01f, Capsule.height * 0.5f));
        }

        /// <summary>Adds movement input for this frame (a world direction scaled by 0..1).</summary>
        public void AddInputVector(Vector3 worldDirection, float scale = 1f)
        {
            pendingInput += worldDirection * scale;
        }

        public void SetMovementMode(MetalMoveMode mode)
        {
            Mode = mode;
            if (mode == MetalMoveMode.Walking || mode == MetalMoveMode.Flying)
            {
                JumpCurrentCount = 0;
            }
        }

        public void StopMovementImmediately()
        {
            Velocity = Vector3.zero;
            pendingInput = Vector3.zero;
        }

        public void DisableMovement()
        {
            Mode = MetalMoveMode.None;
            Velocity = Vector3.zero;
        }

        /// <summary>Jump (a second press in mid-air jumps again if jumpMaxCount allows).</summary>
        public bool Jump()
        {
            if (Mode == MetalMoveMode.None || Mode == MetalMoveMode.Flying || IsCrouched) return false;
            // Walking off a ledge counts as the first jump, like Unreal.
            if (JumpCurrentCount == 0 && Mode == MetalMoveMode.Falling) JumpCurrentCount = 1;
            if (JumpCurrentCount >= Mathf.Max(1, jumpMaxCount)) return false;
            jumpPressed = true;
            return true;
        }

        /// <summary>Unreal's LaunchCharacter: adds or overrides velocity and starts falling.</summary>
        public void Launch(Vector3 launchVelocity, bool overrideXY, bool overrideY)
        {
            Vector3 v = Velocity;
            if (overrideXY) { v.x = launchVelocity.x; v.z = launchVelocity.z; }
            else { v.x += launchVelocity.x; v.z += launchVelocity.z; }
            v.y = overrideY ? launchVelocity.y : v.y + launchVelocity.y;
            Velocity = v;
            if (Mode == MetalMoveMode.Walking) Mode = MetalMoveMode.Falling;
        }

        public void Crouch()
        {
            WantsToCrouch = true;
            UpdateCrouch();
        }

        public void UnCrouch()
        {
            WantsToCrouch = false;
            UpdateCrouch();
        }

        /// <summary>Applies the wanted stance now. Standing up fails (and is retried every update) while something is overhead.</summary>
        public void UpdateCrouch()
        {
            if (Capsule == null) return;
            if (WantsToCrouch && !IsCrouched && canCrouch && Mode == MetalMoveMode.Walking)
            {
                IsCrouched = true;
                ApplyCapsuleHeight();
            }
            else if (!WantsToCrouch && IsCrouched)
            {
                // Room to stand?
                Vector3 bottom = transform.position + Vector3.down * (FullHalfHeight - Capsule.radius);
                Vector3 top = transform.position + Vector3.up * (FullHalfHeight - Capsule.radius);
                Collider[] hits = Physics.OverlapCapsule(bottom + Vector3.up * 0.05f, top, Capsule.radius * 0.95f,
                    MetalPhysics.ShotMask & ~MetalPhysics.BotMask, QueryTriggerInteraction.Ignore);
                bool blocked = false;
                foreach (Collider c in hits)
                {
                    if (c != Capsule && !c.transform.IsChildOf(transform)) { blocked = true; break; }
                }
                if (!blocked)
                {
                    IsCrouched = false;
                    ApplyCapsuleHeight();
                }
            }
        }

        public float GravityY => Physics.gravity.y * gravityScale;

        float MaxSpeed => Mode == MetalMoveMode.Flying ? maxFlySpeed : (IsCrouched ? maxWalkSpeedCrouched : maxWalkSpeed);

        void Update()
        {
            float dt = Time.deltaTime;
            if (dt <= 0f || Capsule == null || !Capsule.enabled) { pendingInput = Vector3.zero; return; }
            UpdateCrouch();

            Vector3 input = Vector3.ClampMagnitude(pendingInput, 1f);
            pendingInput = Vector3.zero;
            if (Mode == MetalMoveMode.Walking || Mode == MetalMoveMode.Falling) input.y = 0f;
            Acceleration = input * maxAcceleration;

            switch (Mode)
            {
                case MetalMoveMode.Walking: TickWalking(dt, input); break;
                case MetalMoveMode.Falling: TickFalling(dt, input); break;
                case MetalMoveMode.Flying: TickFlying(dt, input); break;
                default: Velocity = Vector3.zero; break;
            }
            TickRotation(dt);
        }

        void TickWalking(float dt, Vector3 input)
        {
            if (jumpPressed)
            {
                jumpPressed = false;
                DoJump();
                TickFalling(dt, input);
                return;
            }
            Vector3 v = MetalMath.Flat(Velocity);
            v = CalcVelocity(v, Acceleration, dt, groundFriction, brakingDecelerationWalking, MaxSpeed, input.magnitude, false, 0f);
            Velocity = v;
            Vector3 start = transform.position;
            // Horizontal move plus enough of a downward push to follow slopes and step down small ledges.
            float snap = 0.05f + v.magnitude * dt + Capsule.stepOffset * 0.5f;
            CollisionFlags flags = Capsule.Move(v * dt + Vector3.down * snap);
            if ((flags & CollisionFlags.Below) == 0)
            {
                // Walked off a ledge: undo the snap and fall.
                Capsule.Move(Vector3.up * snap);
                Velocity = new Vector3(v.x, 0f, v.z);
                Mode = MetalMoveMode.Falling;
            }
            else if ((flags & CollisionFlags.Sides) != 0)
            {
                Vector3 moved = MetalMath.Flat(transform.position - start) / dt;
                Velocity = moved.sqrMagnitude < v.sqrMagnitude ? moved : v;
            }
        }

        void DoJump()
        {
            Velocity = new Vector3(Velocity.x, jumpVelocity, Velocity.z);
            JumpCurrentCount++;
            Mode = MetalMoveMode.Falling;
            Jumped?.Invoke();
        }

        void TickFalling(float dt, Vector3 input)
        {
            if (jumpPressed)
            {
                jumpPressed = false;
                DoJump();
            }
            Vector3 v = Velocity;
            Vector3 h = MetalMath.Flat(v);
            h = CalcVelocity(h, Acceleration * airControl, dt, 0f, 0f, maxWalkSpeed, input.magnitude, false, 0f);
            float vy = v.y + GravityY * dt;
            v = new Vector3(h.x, vy, h.z);
            Vector3 start = transform.position;
            CollisionFlags flags = Capsule.Move(v * dt);
            if ((flags & CollisionFlags.Above) != 0 && v.y > 0f) v.y = 0f;
            if ((flags & CollisionFlags.Sides) != 0)
            {
                Vector3 moved = MetalMath.Flat(transform.position - start) / dt;
                v = new Vector3(moved.x, v.y, moved.z);
            }
            Velocity = v;
            if ((flags & CollisionFlags.Below) != 0 && v.y <= 0f)
            {
                Mode = DefaultLandMode == MetalMoveMode.Flying ? MetalMoveMode.Flying : MetalMoveMode.Walking;
                JumpCurrentCount = 0;
                Landed?.Invoke();
                Velocity = MetalMath.Flat(v);
            }
        }

        void TickFlying(float dt, Vector3 input)
        {
            Vector3 v;
            if (LiftRiding)
            {
                // The lift owns the vertical speed; the rider can still drift out sideways on purpose.
                Vector3 h = MetalMath.Flat(Velocity) + MetalMath.Flat(Acceleration) * dt;
                h = Vector3.ClampMagnitude(h, maxWalkSpeed);
                v = new Vector3(h.x, Velocity.y, h.z);
            }
            else
            {
                v = CalcVelocity(Velocity, Acceleration, dt, flightGrip, brakingDecelerationFlying, maxFlySpeed, input.magnitude, true, 0f);
            }
            Vector3 start = transform.position;
            CollisionFlags flags = Capsule.Move(v * dt);
            if (flags != CollisionFlags.None)
            {
                Vector3 moved = (transform.position - start) / dt;
                if (moved.sqrMagnitude < v.sqrMagnitude) v = moved;
            }
            Velocity = v;
        }

        /// <summary>Unreal's CalcVelocity (analog input, friction steering, braking, speed clamps).</summary>
        Vector3 CalcVelocity(Vector3 velocity, Vector3 accel, float dt, float friction, float braking, float maxSpeed,
            float analog, bool separateBrakingFriction, float brakingFriction)
        {
            bool zeroAccel = accel.sqrMagnitude < 1e-8f;
            float maxInputSpeed = Mathf.Max(maxSpeed * Mathf.Clamp01(analog), 0f);
            bool overMax = velocity.sqrMagnitude > maxSpeed * maxSpeed * 1.0001f;
            if (zeroAccel || overMax)
            {
                Vector3 old = velocity;
                velocity = ApplyBraking(velocity, dt, separateBrakingFriction ? brakingFriction : friction, braking);
                if (overMax && velocity.sqrMagnitude < maxSpeed * maxSpeed && Vector3.Dot(accel, old) > 0f)
                {
                    velocity = old.normalized * maxSpeed;
                }
            }
            else
            {
                Vector3 dir = accel.normalized;
                float size = velocity.magnitude;
                velocity -= (velocity - dir * size) * Mathf.Min(dt * friction, 1f);
            }
            if (!zeroAccel)
            {
                float newMax = velocity.sqrMagnitude > maxInputSpeed * maxInputSpeed * 1.0001f ? velocity.magnitude : maxInputSpeed;
                velocity += accel * dt;
                velocity = Vector3.ClampMagnitude(velocity, newMax);
            }
            return velocity;
        }

        static Vector3 ApplyBraking(Vector3 velocity, float dt, float friction, float braking)
        {
            if (velocity.sqrMagnitude < 1e-8f) return velocity;
            friction = Mathf.Max(0f, friction * BrakingFrictionFactor);
            braking = Mathf.Max(0f, braking);
            if (friction == 0f && braking == 0f) return velocity;
            Vector3 old = velocity;
            Vector3 rev = braking == 0f ? Vector3.zero : -braking * velocity.normalized;
            float remaining = dt;
            const float maxStep = 1f / 33f;
            while (remaining >= 1e-5f)
            {
                float step = remaining > maxStep && friction != 0f ? Mathf.Min(maxStep, remaining * 0.5f) : remaining;
                remaining -= step;
                velocity += (-friction * velocity + rev) * step;
                if (Vector3.Dot(velocity, old) <= 0f) return Vector3.zero;
            }
            if (velocity.sqrMagnitude <= 1e-6f || (braking != 0f && velocity.sqrMagnitude <= BrakeToStopVelocity * BrakeToStopVelocity))
            {
                return Vector3.zero;
            }
            return velocity;
        }

        void TickRotation(float dt)
        {
            if (Mode == MetalMoveMode.None) return;
            float current = transform.eulerAngles.y;
            float? desired = null;
            if (orientRotationToMovement)
            {
                Vector3 a = MetalMath.Flat(Acceleration);
                if (a.sqrMagnitude > 1e-4f) desired = MetalMath.Yaw(a);
            }
            else if (useControllerDesiredRotation && bot != null && bot.Controller != null)
            {
                desired = bot.Controller.ControlYaw;
            }
            if (desired.HasValue)
            {
                float yaw = Mathf.MoveTowardsAngle(current, desired.Value, rotationRate * dt);
                transform.rotation = Quaternion.Euler(0f, yaw, 0f);
            }
        }
    }
}
