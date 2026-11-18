// The Stalker: a four-armed hover gunship that fights on both sides. Players link into parked ones with the
// Hack Tether (an open-ended "ally link"); AI pilots (MetalFlyerAI) fly the rest.
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// A flying robot. Same weapons, health and hacking as any MetalBot, but it flies: climb/dive input, a boost meter,
    /// barrel-roll dodges, and a body that pitches with the aim and banks into turns (visual only; the capsule stays
    /// upright).
    /// </summary>
    public class MetalStalker : MetalBot
    {
        const float DodgeDuration = 0.45f;
        const float BoostRechargeDelay = 0.6f;

        [Tooltip("Sits dormant on its pad until a pilot links in.")]
        public bool startParked = true;

        bool parked;
        bool boostHeld;
        bool boosting;
        float boostMeter = 1f;
        float lastBoostTime = -100f;
        float bodyPitch;
        float bank;
        float lastYaw;
        float dodgeStartTime = -1f;
        float dodgeDirection;
        Collider[] armColliders = new Collider[0];

        public MetalFlightTuning Flight => archetype != null ? archetype.flight : DefaultFlight;
        static readonly MetalFlightTuning DefaultFlight = new MetalFlightTuning();

        public bool IsBoosting => boosting;
        public bool IsParked => parked;
        /// <summary>Boost meter, 0..1.</summary>
        public float BoostFraction => boostMeter;
        public void SetBoostHeld(bool held) => boostHeld = held;
        public override bool UsesActionCamera => false;
        protected override bool UseControllerRotationYaw => false;

        protected override void Awake()
        {
            base.Awake();
            autoPossessAI = false; // parked Stalkers sit idle until a pilot links in
            hasVoice = false;
            CameraLagSpeed = 8f;
            CameraLagMaxDistance = 7f;
        }

        public override void ApplyArchetype()
        {
            base.ApplyArchetype();
            MetalFlightTuning f = Flight;
            Movement.maxFlySpeed = f.maxSpeed;
            Movement.maxAcceleration = f.acceleration;
            Movement.brakingDecelerationFlying = f.braking;
            Movement.flightGrip = f.grip;
            BuildArmColliders();
        }

        /// <summary>
        /// The arms reach far outside the capsule, so they take shots through trigger-less colliders on the arm bones.
        /// They never block movement (the capsule does that).
        /// </summary>
        void BuildArmColliders()
        {
            if (armColliders.Length > 0 || archetype == null) return;
            MetalBotRig rig = Rig;
            var list = new System.Collections.Generic.List<Collider>();
            Transform pelvis = GetBone(rig.pelvis);
            foreach (string boneName in new[] { rig.flyerArmUpperL, rig.flyerArmUpperR, rig.flyerArmLowerL, rig.flyerArmLowerR })
            {
                Transform bone = GetBone(boneName);
                if (bone == null || pelvis == null) continue;
                // A capsule from the arm root out to its furthest child.
                Transform tip = bone;
                foreach (Transform t in bone.GetComponentsInChildren<Transform>())
                {
                    if ((t.position - bone.position).sqrMagnitude > (tip.position - bone.position).sqrMagnitude) tip = t;
                }
                var go = new GameObject("Arm Hitbox");
                go.layer = gameObject.layer;
                go.transform.SetParent(bone, false);
                CapsuleCollider c = go.AddComponent<CapsuleCollider>();
                Vector3 localTip = bone.InverseTransformPoint(tip.position);
                float length = Mathf.Max(0.2f, localTip.magnitude);
                c.direction = 2;
                go.transform.localRotation = Quaternion.FromToRotation(Vector3.forward, localTip.sqrMagnitude > 1e-6f ? localTip : Vector3.forward);
                c.center = new Vector3(0f, 0f, length * 0.5f);
                c.height = length + 0.4f;
                c.radius = 0.35f;
                list.Add(c);
                // The capsule must not collide with its own body.
                Physics.IgnoreCollision(c, Capsule);
            }
            armColliders = list.ToArray();
        }

        protected override void Start()
        {
            base.Start();
            lastYaw = transform.eulerAngles.y;
            if (startParked && Controller == null)
            {
                parked = true;
                SetDormant(true);
            }
            else
            {
                Movement.DefaultLandMode = MetalMoveMode.Flying;
                Movement.SetMovementMode(MetalMoveMode.Flying);
            }
        }

        public override MetalController SpawnDefaultAI()
        {
            MetalFlyerAI ai = gameObject.AddComponent<MetalFlyerAI>();
            ai.Possess(this);
            return ai;
        }

        protected override void OnControllerChanged()
        {
            base.OnControllerChanged();
            if (Controller == null) return;
            Movement.DefaultLandMode = MetalMoveMode.Flying;
            if (parked)
            {
                // Lift off the pad.
                parked = false;
                Movement.SetMovementMode(MetalMoveMode.Flying);
                Movement.Velocity = new Vector3(0f, 7f, 0f);
            }
            else if (!Movement.IsFlying)
            {
                Movement.SetMovementMode(MetalMoveMode.Flying);
            }
        }

        /// <summary>The body yaws toward the aim at a limited rate; pitch and bank are visual only.</summary>
        protected override void ConfigureRotationForController()
        {
            Movement.orientRotationToMovement = false;
            Movement.useControllerDesiredRotation = Controller != null;
            Movement.rotationRate = Flight.turnRate;
        }

        /// <summary>Unlike walkers, a released Stalker coasts to a hover instead of stopping dead.</summary>
        public override void SetDormant(bool value)
        {
            dormant = value;
            StopFire();
            boostHeld = false;
        }

        /// <summary>Puts a dormant Stalker back on its pad (hangars do this for robots they rebuild).</summary>
        public void Park()
        {
            parked = true;
            SetDormant(true);
            Movement.StopMovementImmediately();
        }

        /// <summary>Climb (+) / dive (-).</summary>
        public void AddVerticalInput(float value) => AddMovementInput(Vector3.up, value);

        /// <summary>Barrel-roll sideways (-1 left, +1 right), costing boost.</summary>
        public void Dodge(float direction)
        {
            MetalFlightTuning f = Flight;
            if (!IsAlive || IsDormant || parked || IsStunned || dodgeStartTime >= 0f || boostMeter < f.dodgeCost || Mathf.Approximately(direction, 0f))
            {
                return;
            }
            boostMeter -= f.dodgeCost;
            lastBoostTime = Time.time;
            dodgeStartTime = lastBoostTime;
            dodgeDirection = Mathf.Sign(direction);
            // Over the speed limit the movement brakes back down, so the burst fades on its own.
            Vector3 right = Quaternion.Euler(0f, transform.eulerAngles.y, 0f) * Vector3.right;
            Movement.Velocity += right * dodgeDirection * f.dodgeSpeed;
        }

        public override Vector3 GetAimForward() => MetalMath.Forward(bodyPitch, transform.eulerAngles.y);

        public override Vector3 GetMuzzleLocation()
        {
            // The guns point outward along the arms, so shots leave from the tip: the gun bone pushed out along the
            // body-to-gun direction by the weapon's muzzle offset.
            if (archetype != null && archetype.muzzleBones.Count > 0)
            {
                string boneName = archetype.muzzleBones[MuzzleCycleIndex % archetype.muzzleBones.Count];
                Transform gun = GetBone(boneName);
                Transform center = GetBone(Rig.pelvis);
                if (gun != null && center != null)
                {
                    MetalWeaponSlot slot = CurrentWeapon;
                    float offset = (slot != null && slot.data != null ? slot.data.muzzleOffset : 0f) * MeshScale;
                    return gun.position + (gun.position - center.position).normalized * offset;
                }
            }
            return base.GetMuzzleLocation();
        }

        void TickBoost(float dt, float now)
        {
            MetalFlightTuning f = Flight;
            boosting = boostHeld && boostMeter > 0f && IsAlive && !IsDormant && !IsStunned && Movement.IsFlying;
            if (boosting)
            {
                boostMeter = Mathf.Max(0f, boostMeter - dt / Mathf.Max(0.1f, f.boostDuration));
                lastBoostTime = now;
                // Like sprinting: boost always drives the craft forward along its aim.
                AddMovementInput(GetAimForward(), 1f);
            }
            else if (now - lastBoostTime > BoostRechargeDelay)
            {
                boostMeter = Mathf.Min(1f, boostMeter + dt / Mathf.Max(0.1f, f.boostRechargeTime));
            }
            Movement.maxFlySpeed = boosting ? f.boostSpeed : f.maxSpeed;
            Movement.maxAcceleration = boosting ? f.boostAcceleration : f.acceleration;
        }

        void TickTilt(float dt, float now)
        {
            if (!IsAlive) return;
            MetalFlightTuning f = Flight;
            bool piloted = Controller != null && !IsDormant;
            // Nose follows the aim pitch, rate-limited like the yaw.
            float targetPitch = piloted ? Mathf.Clamp(MetalMath.NormalizeAngle(Controller.ControlPitch), -80f, 80f) : 0f;
            float pitchStep = Mathf.Clamp(targetPitch - bodyPitch, -f.turnRate * dt, f.turnRate * dt);
            bodyPitch = MetalMath.InterpTo(bodyPitch, bodyPitch + pitchStep, dt, 12f);

            // Bank into strafes and turns; with no input it eases back to level.
            float yaw = transform.eulerAngles.y;
            float yawRate = dt > 0f ? Mathf.DeltaAngle(lastYaw, yaw) / dt : 0f;
            lastYaw = yaw;
            float lateral = transform.InverseTransformDirection(Velocity).x;
            float targetBank = Mathf.Clamp(
                (Mathf.Clamp(lateral / Mathf.Max(0.01f, f.maxSpeed), -1f, 1f) + Mathf.Clamp(yawRate / Mathf.Max(1f, f.turnRate), -1f, 1f)) * 0.6f * f.maxBank,
                -f.maxBank, f.maxBank);
            bank = MetalMath.InterpTo(bank, targetBank, dt, 4f);

            float roll = bank;
            if (dodgeStartTime >= 0f)
            {
                float t = (now - dodgeStartTime) / DodgeDuration;
                if (t >= 1f) dodgeStartTime = -1f;
                else roll += 360f * dodgeDirection * MetalMath.EaseInOut(0f, 1f, t, 2f);
            }
            // Positive roll dips the right side, the way a rightward strafe or turn should lean.
            meshTilt = UE.Rot(bodyPitch, 0f, roll);
        }

        protected override void Update()
        {
            if (archetype == null) return;
            float now = Time.time;
            TickBoost(Time.deltaTime, now);
            // Before the base tick, which applies meshTilt to the model.
            TickTilt(Time.deltaTime, now);
            base.Update();
        }
    }
}
