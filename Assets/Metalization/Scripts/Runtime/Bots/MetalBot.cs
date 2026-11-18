// Every robot in the game - the player, enemies, possessed enemies, flyers - is a MetalBot configured by a
// MetalBotArchetype. Because possession just hands the same robot to the player's controller, a hijacked
// robot keeps its own movement, weapon, health and hit zones.
using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>A weapon in a robot's inventory: which weapon, its upgrade level and its ammo.</summary>
    [Serializable]
    public class MetalWeaponSlot
    {
        public MetalWeaponData data;
        public int level;
        public int clip;
        public int reserve;

        public MetalWeaponSlot Clone() => (MetalWeaponSlot)MemberwiseClone();
    }

    /// <summary>
    /// What a robot "says": wordless beeps and whistles, one mood each, from least to most urgent. A robot that is
    /// already talking is only interrupted by something more urgent.
    /// </summary>
    public enum MetalBark
    {
        Chatter,
        Orders,
        Laugh,
        Alert,
        Confused,
        Pain,
        Death,
    }

    /// <summary>
    /// Base class for every robot. Owns everything a robot does regardless of who drives it: health and hit zones,
    /// the weapon inventory and firing, grenades, stun/dormant/hijacked states, team paint and the third-person
    /// camera settings. Players drive it through MetalPlayerController and AI through MetalBotAI (walkers) or
    /// MetalFlyerAI (flyers); all of them only call the public API (StartFire, SetAITarget, AddMovementInput...).
    /// </summary>
    [RequireComponent(typeof(CharacterController))]
    [RequireComponent(typeof(MetalCharacterMovement))]
    [DefaultExecutionOrder(-50)]
    public class MetalBot : MonoBehaviour, IMetalDamageable
    {
        /// <summary>Fired for every robot death (bot, killer controller or null).</summary>
        public static event Action<MetalBot, MetalController> AnyBotDied;

        /// <summary>Every live robot in the scene.</summary>
        public static readonly List<MetalBot> All = new List<MetalBot>();

        [Header("Setup")]
        [Tooltip("The data asset defining this robot. Required.")]
        public MetalBotArchetype archetype;
        [Tooltip("Overrides the archetype's team for this instance.")]
        public bool overrideTeam;
        public MetalTeam teamOverride = MetalTeam.Heroes;
        [Tooltip("AI wanders this far (m) from where it was placed. 0 = holds position.")]
        public float patrolRadius = 6f;
        [Tooltip("Passive robots never start fights (friendly NPCs).")]
        public bool passive;
        [Tooltip("Ignores enemies it sees until it (or a squadmate) is shot, then fights normally.")]
        public bool docileUntilAttacked;
        [Tooltip("Fraction of the archetype's walk speed used while patrolling.")]
        public float patrolSpeedFraction = 0.4f;
        [Tooltip("Spawn a walker AI when nobody controls this robot at start.")]
        public bool autoPossessAI = true;
        [Tooltip("Tags missions use to pick out robots (EliminateTagged, HijackTagged).")]
        public List<string> tags = new List<string>();

        public bool HasTag(string tag) => !string.IsNullOrEmpty(tag) && tags.Contains(tag);

        /// <summary>Health fraction at or below which the player's robot flashes red and beeps.</summary>
        public const float LowHealthFraction = 0.25f;
        /// <summary>Length of the "reload" sound in seconds; the animation beats are authored against it.</summary>
        public const float ReloadSoundLength = 1.15f;
        const float DrawDuration = 0.45f;

        // Ground handling on foot, and crouched (gliding on the console base: slow to start, slow to stop).
        const float WalkFriction = 8f, WalkBraking = 20f, WalkAcceleration = 24f;
        const float SlideFriction = 0.6f, SlideBraking = 3.5f, SlideAcceleration = 9f;

        public MetalController Controller { get; private set; }
        public MetalCharacterMovement Movement { get; private set; }
        public CharacterController Capsule { get; private set; }
        public MetalBotAnimator Animator { get; private set; }

        /// <summary>Model root (Unreal's mesh component): positioned below the capsule centre, rotated by flips and tilts.</summary>
        public Transform MeshRoot { get; private set; }
        protected Quaternion meshBaseRotation = Quaternion.identity;
        protected Vector3 meshBaseLocation = new Vector3(0f, -0.68f, 0f);
        /// <summary>Extra model rotation around the capsule centre (flyers pitch and bank the body with it).</summary>
        protected Quaternion meshTilt = Quaternion.identity;
        float meshScale = 1f;

        readonly Dictionary<string, Transform> bones = new Dictionary<string, Transform>();
        readonly Dictionary<string, Matrix4x4> bindInMesh = new Dictionary<string, Matrix4x4>();
        GameObject bodyInstance;
        readonly List<GameObject> partInstances = new List<GameObject>();
        GameObject weaponInstance;
        GameObject weaponPrefab;
        GameObject unarmedInstance;
        GameObject carriedGun, rackGunA, rackGunB;
        readonly List<Renderer> renderers = new List<Renderer>();
        readonly List<Renderer> paintRenderers = new List<Renderer>();
        MaterialPropertyBlock block;

        // Jetpack
        readonly List<Transform> jetFlames = new List<Transform>();
        readonly List<Renderer> jetFlameRenderers = new List<Renderer>();
        Light jetLight;
        float jetFireEnd;

        MetalFX laserSight;
        bool hiddenForScope;

        // State
        readonly List<MetalWeaponSlot> weapons = new List<MetalWeaponSlot>();
        float health = 100f;
        float maxHealth = 100f;
        MetalTeam team = MetalTeam.Villains;
        int currentWeapon = -1;
        float[] zoneDamage = new float[0];
        bool dead;
        protected bool dormant;
        bool assembling;
        bool hijacked;
        float firewallEndTime = -1f;
        bool firewallGlow;
        bool weaponDisabled;
        bool fireHeld;
        bool secondaryHeld;
        bool aiming;
        float nextSecondaryFireTime;
        bool hasAITarget;
        Vector3 aiTargetPoint;
        float nextFireTime;
        float reloadEndTime;
        float reloadDuration;
        int reloadBeat;
        float stunEndTime;
        float hitFlash;
        float lowHealthFlash;
        float nextLowHealthPulseTime;
        float lastDamageTakenTime = -100f;
        float defaultArmLength = 3f;
        Vector3 defaultCameraOffset = new Vector3(0.55f, 0.55f, 0f);
        float lastFireTime = -100f;
        float flipStartTime = -1f;
        float flipTuck;
        float lowestFallSpeed;
        int muzzleCycle;
        readonly HashSet<string> brokenMuzzles = new HashSet<string>();
        protected bool hasVoice = true;
        MetalSoundHandle voice;
        MetalBark voiceBark = MetalBark.Chatter;
        float nextPainTime;
        bool canBeDamaged = true;

        bool wantsSprint;
        bool sprinting;
        float crouchAlpha;
        bool hackCrouch;
        float sprintAlpha;
        float stepDistance;
        float scopeAlpha;
        float swapStartTime = -1f;
        int swapFromSlot = -1;
        int swapTargetSlot = -1;
        int swapStage;
        float drawStartTime = -1f;
        bool drawSwapped;
        readonly List<int> rackOrder = new List<int>();
        bool begunPlay;

        /// <summary>Grenade thrown by ThrowGrenade(). Filled from the archetype at start if left empty.</summary>
        public MetalWeaponData GrenadeWeapon { get; set; }
        /// <summary>Grenades carried.</summary>
        public int Grenades { get; set; }

        // --- Camera boom (read by the player's camera rig) ------------------------------------------------
        public float CameraArmLength { get; set; } = 3f;
        public Vector3 CameraSocketOffset { get; set; } = new Vector3(0.55f, 0.55f, 0f);
        public float CameraFOV { get; set; } = 85f;
        public bool CameraLagEnabled { get; set; } = true;
        public float CameraLagSpeed { get; set; } = 18f;
        public float CameraLagMaxDistance { get; set; }

        // ==================================================================================================
        // Lifecycle
        // ==================================================================================================

        protected virtual void Awake()
        {
            Movement = GetComponent<MetalCharacterMovement>();
            Capsule = GetComponent<CharacterController>();
            block = new MaterialPropertyBlock();
            if (MetalPhysics.BotLayer >= 0) gameObject.layer = MetalPhysics.BotLayer;
            Movement.gravityScale = 1.6f;
            Movement.airControl = 0.6f;
            Movement.maxWalkSpeed = 5f;
            Movement.jumpVelocity = 6.5f;
            Movement.maxAcceleration = WalkAcceleration;
            Movement.brakingDecelerationWalking = WalkBraking;
            Movement.groundFriction = WalkFriction;
            Movement.rotationRate = 600f;
            Movement.Jumped += OnJumped;
            Movement.Landed += OnLanded;
        }

        protected virtual void OnEnable() => All.Add(this);
        protected virtual void OnDisable() => All.Remove(this);

        protected virtual void Start()
        {
            if (archetype == null)
            {
                Debug.LogError($"[Metal] {name} has no archetype - run Metalization > Build All Assets and assign one.", this);
                return;
            }
            ApplyArchetype();
            maxHealth = archetype.maxHealth;
            health = maxHealth;
            zoneDamage = new float[archetype.hitZones.Count];

            foreach (GameObject part in archetype.parts)
            {
                if (part != null) partInstances.Add(AttachFollower(part, "Part " + part.name));
            }
            if (archetype.unarmedPart != null)
            {
                unarmedInstance = AttachFollower(archetype.unarmedPart, "Unarmed");
            }
            if (GrenadeWeapon == null)
            {
                GrenadeWeapon = archetype.grenadeWeapon;
                Grenades = archetype.startingGrenades;
            }
            // Add the starting loadout without disturbing anything already given (e.g. a pickup touched on spawn).
            foreach (MetalWeaponData w in archetype.startingWeapons)
            {
                if (w != null && weapons.Find(s => s.data == w) == null) GiveWeapon(w, false);
            }
            begunPlay = true;
            EquipWeapon(weapons.Count > 0 ? Mathf.Max(0, currentWeapon) : -1);
            RefreshWeaponVisual();
            RefreshRack();
            BuildJetpack();
            ConfigureRotationForController();
            if (Controller == null && autoPossessAI)
            {
                SpawnDefaultAI();
            }
        }

        /// <summary>Creates the AI that drives this kind of robot when nobody else does (walker AI by default).</summary>
        public virtual MetalController SpawnDefaultAI()
        {
            MetalBotAI ai = gameObject.AddComponent<MetalBotAI>();
            ai.Possess(this);
            return ai;
        }

        protected virtual void OnDestroy()
        {
            if (laserSight != null) Destroy(laserSight.gameObject);
            if (Controller != null && !Controller.IsPlayer) Controller.UnPossess();
        }

        /// <summary>Called by MetalController.Possess / UnPossess.</summary>
        internal void SetController(MetalController c)
        {
            Controller = c;
            OnControllerChanged();
        }

        protected virtual void OnControllerChanged()
        {
            ConfigureRotationForController();
        }

        // ==================================================================================================
        // Archetype and model
        // ==================================================================================================

        public MetalBotRig Rig => archetype != null ? archetype.rig : DefaultRig;
        static readonly MetalBotRig DefaultRig = new MetalBotRig();
        public string ArchetypeId => archetype != null ? archetype.archetypeId : "";
        public string DisplayName => archetype != null && !string.IsNullOrEmpty(archetype.displayName) ? archetype.displayName : name;
        public float MeshScale => meshScale;

        /// <summary>
        /// Builds the model, capsule, movement and camera settings from the archetype and resets the team. Safe to
        /// call again (the model is only built once).
        /// </summary>
        public virtual void ApplyArchetype()
        {
            if (archetype == null) return;
            Movement.SetCapsuleSize(archetype.capsuleRadius, archetype.capsuleHalfHeight);
            meshScale = archetype.meshScale;
            meshBaseLocation = new Vector3(0f, -archetype.capsuleHalfHeight, 0f) + archetype.meshOffset;
            if (MeshRoot == null)
            {
                MeshRoot = new GameObject("Mesh").transform;
                MeshRoot.SetParent(transform, false);
            }
            MeshRoot.localPosition = meshBaseLocation;
            MeshRoot.localRotation = meshBaseRotation;
            MeshRoot.localScale = Vector3.one * meshScale;
            if (bodyInstance == null && archetype.model != null)
            {
                bodyInstance = Instantiate(archetype.model, MeshRoot, false);
                bodyInstance.name = "Body";
                StripImportedExtras(bodyInstance);
                CollectBones();
                if (Animator == null)
                {
                    Animator = gameObject.AddComponent<MetalBotAnimator>();
                }
                Animator.Init(this);
            }

            Movement.maxWalkSpeed = archetype.walkSpeed;
            Movement.jumpVelocity = archetype.jumpVelocity;
            Movement.jumpMaxCount = Mathf.Max(1, archetype.maxJumps);
            // Sneaking is a crouch with a shorter capsule; the animator bends the legs.
            Movement.canCrouch = archetype.crouchSpeed > 0f;
            Movement.maxWalkSpeedCrouched = archetype.crouchSpeed > 0f ? archetype.crouchSpeed : 2f;
            Movement.crouchedHalfHeight = archetype.capsuleHalfHeight * Mathf.Clamp(archetype.crouchHeightFraction, 0.3f, 1f);

            defaultArmLength = archetype.cameraDistance;
            defaultCameraOffset = archetype.cameraOffset;
            CameraArmLength = defaultArmLength;
            CameraSocketOffset = defaultCameraOffset;
            team = GetBaseTeam();
        }

        static void StripImportedExtras(GameObject instance)
        {
            foreach (UnityEngine.Animator a in instance.GetComponentsInChildren<UnityEngine.Animator>(true)) Destroy(a);
            foreach (Collider c in instance.GetComponentsInChildren<Collider>(true)) Destroy(c);
        }

        void CollectBones()
        {
            bones.Clear();
            bindInMesh.Clear();
            Matrix4x4 toMesh = MeshRoot.worldToLocalMatrix;
            foreach (Transform t in bodyInstance.GetComponentsInChildren<Transform>(true))
            {
                if (t == bodyInstance.transform || bones.ContainsKey(t.name)) continue;
                if (t.GetComponent<Renderer>() != null) continue;
                bones[t.name] = t;
                bindInMesh[t.name] = toMesh * t.localToWorldMatrix;
            }
        }

        /// <summary>The body bone with this name, or null.</summary>
        public Transform GetBone(string boneName)
        {
            if (string.IsNullOrEmpty(boneName)) return null;
            return bones.TryGetValue(boneName, out Transform t) ? t : null;
        }

        public bool HasBone(string boneName) => GetBone(boneName) != null;

        /// <summary>World position of a body bone (falls back to the capsule centre).</summary>
        public Vector3 GetBoneLocation(string boneName)
        {
            Transform t = GetBone(boneName);
            return t != null ? t.position : transform.position;
        }

        /// <summary>Bind-pose matrix of a bone in model (MeshRoot) space.</summary>
        public bool TryGetBindMatrix(string boneName, out Matrix4x4 m) => bindInMesh.TryGetValue(boneName ?? "", out m);

        public IReadOnlyDictionary<string, Transform> Bones => bones;
        public GameObject BodyInstance => bodyInstance;

        /// <summary>
        /// Instantiates a model skinned to the same skeleton and makes it follow the body's bones (Unreal leader
        /// pose / Source bonemerge): every SkinnedMeshRenderer's bones are remapped onto the body's by name.
        /// </summary>
        GameObject AttachFollower(GameObject prefab, string label)
        {
            GameObject inst = Instantiate(prefab, MeshRoot, false);
            inst.name = label;
            StripImportedExtras(inst);
            foreach (SkinnedMeshRenderer smr in inst.GetComponentsInChildren<SkinnedMeshRenderer>(true))
            {
                Transform[] src = smr.bones;
                var mapped = new Transform[src.Length];
                for (int i = 0; i < src.Length; i++)
                {
                    mapped[i] = src[i] != null && bones.TryGetValue(src[i].name, out Transform b) ? b : src[i];
                }
                smr.bones = mapped;
                if (smr.rootBone != null && bones.TryGetValue(smr.rootBone.name, out Transform root)) smr.rootBone = root;
                smr.updateWhenOffscreen = true;
            }
            return inst;
        }

        // ==================================================================================================
        // State
        // ==================================================================================================

        public MetalTeam Team => team;

        /// <summary>Switches the robot's side and repaints it with that team's colours.</summary>
        public virtual void SetTeam(MetalTeam newTeam)
        {
            team = newTeam;
            ApplyMaterialState();
        }

        /// <summary>Team from the archetype or the per-instance override, ignoring any hijack.</summary>
        public MetalTeam GetBaseTeam() => overrideTeam ? teamOverride : (archetype != null ? archetype.team : MetalTeam.Villains);

        /// <summary>True if other is a different robot on an opposing (non-neutral) team.</summary>
        public bool IsHostileTo(MetalBot other)
        {
            return other != null && other != this && team != MetalTeam.Neutral && other.team != MetalTeam.Neutral && team != other.team;
        }

        public bool IsAlive => !dead;
        public float Health => health;
        public float MaxHealth => maxHealth;
        public bool IsStunned => Time.time < stunEndTime;
        /// <summary>Powered down: body left behind during possession, or a robot that died.</summary>
        public bool IsSlumped => dormant || dead;
        /// <summary>Switched off (its controller drives another robot), or still being assembled.</summary>
        public bool IsDormant => dormant || assembling;
        public bool IsAssembling => assembling;
        public bool IsWeaponDisabled => weaponDisabled;
        public bool IsPlayerControlled => Controller != null && Controller.IsPlayer;
        public Vector3 Velocity => Movement != null ? Movement.Velocity : Vector3.zero;
        public bool CanBeDamaged => canBeDamaged;

        /// <summary>Marks the robot as being built by a dispenser: dormant-like, upright, can't be damaged or hacked.</summary>
        public void SetAssembling(bool value)
        {
            assembling = value;
            canBeDamaged = !assembling;
            StopFire();
            if (assembling) Movement.StopMovementImmediately();
        }

        /// <summary>Powers the robot down or back up. Dormant robots stop firing and moving and are drawn slumped.</summary>
        public virtual void SetDormant(bool value)
        {
            dormant = value;
            StopFire();
            if (dormant) Movement.StopMovementImmediately();
        }

        /// <summary>Disables movement and firing for a while (EMP). Overlapping stuns extend to the later end time.</summary>
        public void Stun(float duration)
        {
            if (dead) return;
            if (!IsStunned) Speak(MetalBark.Confused);
            stunEndTime = Mathf.Max(stunEndTime, Time.time + duration);
            StopFire();
            Movement.StopMovementImmediately();
        }

        public void Heal(float amount)
        {
            if (!dead) health = Mathf.Min(maxHealth, health + amount);
        }

        /// <summary>Sets health to a checkpoint snapshot's value, capped at max.</summary>
        public void RestoreHealth(float newHealth)
        {
            if (!dead) health = Mathf.Clamp(newHealth, 1f, maxHealth);
        }

        /// <summary>
        /// Enemy robots can be hijacked (a timed link). Friendly robots built for it (allyHackable, e.g. parked
        /// Stalkers) accept an open-ended link instead.
        /// </summary>
        public bool CanBeHackedBy(MetalBot hacker, bool ignoreFirewall = false)
        {
            if (archetype == null || hacker == null || hacker == this || dead || assembling || hijacked || IsPlayerControlled
                || (!ignoreFirewall && IsFirewalled))
            {
                return false;
            }
            if (team == hacker.Team)
            {
                // Only idle ones (parked, or left hovering): never one an AI pilot is flying.
                return archetype.allyHackable && Controller == null;
            }
            return archetype.hackable && hacker.IsHostileTo(this);
        }

        /// <summary>Marks the robot as under the player's control: it joins the Heroes and its hack glow pulses.</summary>
        public void SetHijacked(bool value)
        {
            if (value && !hijacked) Speak(MetalBark.Confused);
            hijacked = value;
            SetTeam(hijacked ? MetalTeam.Heroes : GetBaseTeam());
        }

        public bool IsHijacked => hijacked;
        public float PossessionDuration => archetype != null ? archetype.possessionDuration : 20f;

        /// <summary>Blocks the Hack Tether for a while after a failed hack; the hack glow stutters meanwhile.</summary>
        public void RaiseFirewall(float duration)
        {
            firewallEndTime = Time.time + duration;
            firewallGlow = true;
        }

        public float FirewallTimeRemaining => Mathf.Max(0f, firewallEndTime - Time.time);
        public bool IsFirewalled => FirewallTimeRemaining > 0f;

        /// <summary>Powers the robot down, then after delay it explodes, damaging enemies of its current team nearby.</summary>
        public void SelfDestruct(float delay)
        {
            if (dead) return;
            SetDormant(true);
            StartCoroutine(SelfDestructRoutine(delay));
        }

        IEnumerator SelfDestructRoutine(float delay)
        {
            yield return new WaitForSeconds(delay);
            if (!dead)
            {
                MetalPhysics.ApplyRadialDamage(90f, 20f, transform.position, 1.5f, 4.5f, 1f, gameObject, null, new[] { gameObject });
                Die(null);
                Explode();
            }
        }

        // ==================================================================================================
        // Weapons
        // ==================================================================================================

        public IReadOnlyList<MetalWeaponSlot> Weapons => weapons;
        public MetalWeaponSlot CurrentWeapon => currentWeapon >= 0 && currentWeapon < weapons.Count ? weapons[currentWeapon] : null;
        public int CurrentWeaponIndex => currentWeapon;
        bool ValidSlot(int i) => i >= 0 && i < weapons.Count;

        /// <summary>Replaces the whole inventory (carries the player's loadout across a respawn).</summary>
        public void SetWeapons(List<MetalWeaponSlot> newWeapons, int equipIndex)
        {
            weapons.Clear();
            foreach (MetalWeaponSlot s in newWeapons) weapons.Add(s.Clone());
            currentWeapon = -1;
            swapStartTime = -1f;
            drawStartTime = -1f;
            reloadEndTime = 0f;
            SetVisible(carriedGun, false);
            rackOrder.Clear();
            EquipWeapon(equipIndex);
            RefreshRack();
        }

        /// <summary>
        /// Adds a new weapon (half reserve ammo), or upgrades it one level and refills it if already owned.
        /// Returns true if anything changed, so pickups know to vanish.
        /// </summary>
        public bool GiveWeapon(MetalWeaponData weapon, bool equip)
        {
            if (weapon == null) return false;
            for (int i = 0; i < weapons.Count; i++)
            {
                MetalWeaponSlot slot = weapons[i];
                if (slot.data != weapon) continue;
                bool upgraded = slot.level < weapon.MaxLevel;
                slot.level = Mathf.Min(slot.level + 1, weapon.MaxLevel);
                MetalWeaponLevel stats = weapon.GetLevel(slot.level);
                bool ammo = slot.reserve < stats.maxReserveAmmo;
                slot.clip = stats.clipSize;
                slot.reserve = stats.maxReserveAmmo;
                if (equip || i == currentWeapon)
                {
                    // Clear first so EquipWeapon doesn't early-out, which refreshes the model for the new level.
                    currentWeapon = -1;
                    EquipWeapon(i);
                }
                return upgraded || ammo;
            }
            weapons.Add(new MetalWeaponSlot
            {
                data = weapon,
                clip = weapon.GetLevel(0).clipSize,
                reserve = weapon.GetLevel(0).maxReserveAmmo / 2,
            });
            if (equip) EquipWeapon(weapons.Count - 1);
            RefreshRack();
            return true;
        }

        /// <summary>Tops up the reserve ammo of every finite-ammo weapon by a share of its max.</summary>
        public bool AddAmmoFraction(float fraction)
        {
            bool changed = false;
            foreach (MetalWeaponSlot slot in weapons)
            {
                if (slot.data == null || slot.data.infiniteAmmo) continue;
                int max = slot.data.GetLevel(slot.level).maxReserveAmmo;
                int newReserve = Mathf.Min(max, slot.reserve + Mathf.CeilToInt(max * fraction));
                changed |= newReserve != slot.reserve;
                slot.reserve = newReserve;
            }
            return changed;
        }

        /// <summary>Switches to a weapon slot, cancelling any reload and adding a short draw delay.</summary>
        public void EquipWeapon(int slotIndex)
        {
            if (!ValidSlot(slotIndex)) return;
            if (IsSwappingWeapon)
            {
                // Changed their mind mid-swap: whatever was picked last is what gets drawn and attached.
                if (slotIndex != swapTargetSlot)
                {
                    swapTargetSlot = slotIndex;
                    if (swapStage == 2) PlaceWeaponOnBone(ref carriedGun, swapTargetSlot, Rig.handL, null);
                    RefreshRack();
                }
                return;
            }
            if (slotIndex == currentWeapon) return;
            reloadEndTime = 0f;
            if (drawStartTime >= 0f)
            {
                // Picked another gun mid-draw: the draw carries on and brings that one up instead.
                currentWeapon = slotIndex;
                if (drawSwapped) RefreshWeaponVisual();
                return;
            }
            // Players with an animated rig act the swap out; everything else swaps with a quick draw.
            bool actOut = begunPlay && archetype != null && archetype.animatedWeaponSwap && IsPlayerControlled && !dead
                && !weaponDisabled && ValidSlot(currentWeapon);
            if (actOut)
            {
                swapFromSlot = currentWeapon;
                swapTargetSlot = slotIndex;
                swapStartTime = Time.time;
                swapStage = 0;
                PlaySound("swap_servo");
                return;
            }
            bool quickDraw = begunPlay && !dead && !weaponDisabled && ValidSlot(currentWeapon);
            currentWeapon = slotIndex;
            if (quickDraw)
            {
                drawStartTime = Time.time;
                drawSwapped = false;
                nextFireTime = Mathf.Max(nextFireTime, drawStartTime + DrawDuration);
                PlaySound("swap_servo", 0.6f);
                return;
            }
            nextFireTime = Time.time + 0.2f;
            RefreshWeaponVisual();
            RefreshRack();
        }

        /// <summary>0..1 through the quick weapon draw, or -1 when not drawing.</summary>
        public float DrawProgress => drawStartTime < 0f ? -1f : Mathf.Clamp01((Time.time - drawStartTime) / DrawDuration);

        void TickWeaponDraw()
        {
            if (drawStartTime < 0f) return;
            float t = DrawProgress;
            if (!drawSwapped && (t >= 0.5f || dead || dormant))
            {
                // The arm is at the bottom of its drop, out of the way: change the gun.
                drawSwapped = true;
                RefreshWeaponVisual();
                RefreshRack();
                PlaySound("swap_attach", 0.6f);
            }
            if (t >= 1f || dead || dormant) drawStartTime = -1f;
        }

        public bool IsSwappingWeapon => swapStartTime >= 0f;

        /// <summary>0..1 progress through the acted-out swap, or -1 when not swapping.</summary>
        public float SwapProgress
        {
            get
            {
                if (!IsSwappingWeapon) return -1f;
                float duration = archetype != null ? Mathf.Max(0.2f, archetype.weaponSwapDuration) : 0.8f;
                return Mathf.Clamp01((Time.time - swapStartTime) / duration);
            }
        }

        public int SwapTargetSlot => IsSwappingWeapon ? swapTargetSlot : -1;

        void TickWeaponSwap(float now)
        {
            if (!IsSwappingWeapon) return;
            if (dead || dormant || !ValidSlot(swapTargetSlot))
            {
                // Interrupted: finish instantly so the robot isn't left armless.
                swapStartTime = -1f;
                SetVisible(carriedGun, false);
                currentWeapon = ValidSlot(swapTargetSlot) ? swapTargetSlot : currentWeapon;
                RefreshWeaponVisual();
                RefreshRack();
                return;
            }
            // Beats match the animator: twist the gun off (0.28), rack it and pull the new one (0.5), snap it on (0.8).
            float t = SwapProgress;
            if (swapStage == 0 && t >= 0.28f)
            {
                swapStage = 1;
                RefreshWeaponVisual(); // bare forearm mount while the gun is off
                PlaceWeaponOnBone(ref carriedGun, swapFromSlot, Rig.handL, null);
                PlaySound("swap_detach");
            }
            if (swapStage == 1 && t >= 0.5f)
            {
                swapStage = 2;
                rackOrder.Remove(swapFromSlot);
                rackOrder.Insert(0, swapFromSlot);
                PlaceWeaponOnBone(ref carriedGun, swapTargetSlot, Rig.handL, null);
                RefreshRack();
                PlaySound("swap_rack", 0.7f);
            }
            if (swapStage == 2 && t >= 0.8f)
            {
                swapStage = 3;
                currentWeapon = swapTargetSlot;
                SetVisible(carriedGun, false);
                RefreshWeaponVisual();
                RefreshRack();
                if (Animator != null) Animator.PlayRecoil();
                PlaySound("swap_attach");
            }
            if (t >= 1f)
            {
                swapStartTime = -1f;
                nextFireTime = Mathf.Max(nextFireTime, now);
            }
        }

        GameObject HeldModel(int slot)
        {
            if (!ValidSlot(slot) || weapons[slot].data == null) return null;
            return weapons[slot].data.GetLevel(weapons[slot].level).GetHeldModel(ArchetypeId);
        }

        static void SetVisible(GameObject go, bool visible)
        {
            if (go != null && go.activeSelf != visible) go.SetActive(visible);
        }

        /// <summary>
        /// Shows a weapon's held model stand-alone (not following the body's bones) so that its grip sits on a bone.
        /// gripInMesh null means "in the hand": grip on the bone, gun pointing the way it points on the arm.
        /// </summary>
        void PlaceWeaponOnBone(ref GameObject holder, int slot, string boneName, Matrix4x4? gripInMesh)
        {
            GameObject held = HeldModel(slot);
            if (GetBone(boneName) == null)
            {
                boneName = ReferenceEquals(holder, carriedGun) && holder != null ? Rig.forearmL : Rig.pelvis;
                if (GetBone(boneName) == null) boneName = Rig.forearmL;
            }
            Transform bone = GetBone(boneName);
            if (held == null || bone == null || !TryGetBindMatrix(boneName, out Matrix4x4 boneBind))
            {
                SetVisible(holder, false);
                return;
            }
            if (holder == null || holder.name != held.name)
            {
                if (holder != null) Destroy(holder);
                holder = Instantiate(held, bone, false);
                holder.name = held.name;
                StripImportedExtras(holder);
                foreach (Renderer r in holder.GetComponentsInChildren<Renderer>(true))
                {
                    r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
                    if (r is SkinnedMeshRenderer smr) smr.updateWhenOffscreen = true;
                }
            }
            // The held models are skinned to this robot's skeleton, so stand-alone they show the gun where it sits
            // on the gun arm in the bind pose. Find that grip (the weapon bone in the gun's own bind pose) and move
            // the whole model so the grip lands on the wanted spot, relative to the bone so it follows it.
            Transform gunGrip = FindChild(holder.transform, Rig.weaponBone);
            Matrix4x4 gripInGun = gunGrip != null ? holder.transform.worldToLocalMatrix * gunGrip.localToWorldMatrix : Matrix4x4.identity;
            if (gunGrip != null && TryGetBindMatrix(Rig.weaponBone, out Matrix4x4 bodyGrip))
            {
                // The gun's skeleton is a copy of this robot's, so its bind grip matches the body's weapon bone.
                gripInGun = bodyGrip;
            }
            Matrix4x4 target = gripInMesh ?? Matrix4x4.TRS(boneBind.GetColumn(3), gripInGun.rotation, Vector3.one);
            Matrix4x4 modelInMesh = target * gripInGun.inverse;
            Matrix4x4 rel = boneBind.inverse * modelInMesh;
            holder.transform.SetParent(bone, false);
            holder.transform.localPosition = rel.GetColumn(3);
            holder.transform.localRotation = rel.rotation;
            holder.transform.localScale = Vector3.one;
            SetVisible(holder, true);
        }

        static Transform FindChild(Transform root, string childName)
        {
            if (string.IsNullOrEmpty(childName)) return null;
            foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            {
                if (t.name == childName) return t;
            }
            return null;
        }

        /// <summary>Shows the most recently stowed weapons on the back rack (acted-out swappers only).</summary>
        void RefreshRack()
        {
            if (archetype == null || !archetype.animatedWeaponSwap || weaponDisabled || bodyInstance == null)
            {
                SetVisible(rackGunA, false);
                SetVisible(rackGunB, false);
                return;
            }
            rackOrder.RemoveAll(s => !ValidSlot(s));
            for (int s = 0; s < weapons.Count; s++)
            {
                if (!rackOrder.Contains(s)) rackOrder.Add(s);
            }
            // Whatever is on the arm or in the off hand right now isn't on the rack.
            int onArm = currentWeapon;
            int inHand = -1;
            if (IsSwappingWeapon)
            {
                onArm = swapStage == 0 ? swapFromSlot : (swapStage >= 3 ? swapTargetSlot : -1);
                inHand = swapStage == 1 ? swapFromSlot : (swapStage == 2 ? swapTargetSlot : -1);
            }
            var shown = new List<int>(2);
            foreach (int s in rackOrder)
            {
                if (s != onArm && s != inHand && shown.Count < 2 && HeldModel(s) != null) shown.Add(s);
            }
            MetalBotRig rig = Rig;
            string boneName = string.IsNullOrEmpty(rig.backBone) ? rig.pelvis : rig.backBone;
            Quaternion gripRotation = TryGetBindMatrix(rig.weaponBone, out Matrix4x4 grip) ? grip.rotation : Quaternion.identity;
            // Two guns slung barrel-up behind the shoulders, crossed into a shallow X.
            for (int i = 0; i < 2; i++)
            {
                float side = i == 0 ? 1f : -1f;
                Quaternion lean = Quaternion.AngleAxis(10f * side, Vector3.forward) * Quaternion.AngleAxis(100f, Vector3.left);
                int slot = i < shown.Count ? shown[i] : -1;
                Vector3 pos = rig.rackOffset + UE.MeshVec(6f * side, -3f * i, 0f);
                Matrix4x4 target = Matrix4x4.TRS(pos, lean * gripRotation, Vector3.one);
                if (i == 0) PlaceWeaponOnBone(ref rackGunA, slot, boneName, target);
                else PlaceWeaponOnBone(ref rackGunB, slot, boneName, target);
            }
        }

        /// <summary>Equips the next or previous weapon, wrapping around.</summary>
        public void CycleWeapon(int direction)
        {
            if (weapons.Count > 1) EquipWeapon((Mathf.Max(0, currentWeapon) + direction + weapons.Count) % weapons.Count);
        }

        /// <summary>Shows the equipped weapon's model for this archetype (or the unarmed part) and refreshes the materials.</summary>
        void RefreshWeaponVisual()
        {
            GameObject held = null;
            MetalWeaponSlot slot = CurrentWeapon;
            if (slot != null && slot.data != null && !weaponDisabled)
            {
                held = slot.data.GetLevel(slot.level).GetHeldModel(ArchetypeId);
            }
            if (held != weaponPrefab)
            {
                if (weaponInstance != null) Destroy(weaponInstance);
                weaponInstance = held != null && MeshRoot != null ? AttachFollower(held, "Weapon " + held.name) : null;
                weaponPrefab = held;
            }
            // Mid-swap the gun is in the other hand and the forearm ends in a bare mount: no gun, no hand.
            bool bareMount = IsSwappingWeapon && (swapStage == 1 || swapStage == 2);
            SetVisible(weaponInstance, held != null && !bareMount);
            SetVisible(unarmedInstance, held == null && !bareMount);
            RefreshMaterials();
        }

        public void StartFire()
        {
            // Tucked into the console disguise: the gun arm is folded away.
            if (IsSneaking) return;
            fireHeld = true;
            if (UsesActionCamera && Controller != null)
            {
                // Running freely the robot may face the camera; square up to the crosshair before the first shot.
                transform.rotation = Quaternion.Euler(0f, Controller.ControlYaw, 0f);
            }
        }

        public void StopFire() => fireHeld = false;
        public bool IsFireHeld => fireHeld;

        public bool HasSecondaryWeapon => archetype != null && archetype.secondaryWeapon != null;

        public void StartSecondaryFire()
        {
            if (!HasSecondaryWeapon || IsSneaking) return;
            secondaryHeld = true;
            if (UsesActionCamera && Controller != null)
            {
                transform.rotation = Quaternion.Euler(0f, Controller.ControlYaw, 0f);
            }
        }

        public void StopSecondaryFire() => secondaryHeld = false;

        public bool IsSecondaryReady => HasSecondaryWeapon && !dead && !dormant && !assembling && !IsStunned && !IsSwappingWeapon
            && Time.time >= nextSecondaryFireTime;

        /// <summary>0 just fired .. 1 ready, for the HUD's cooldown bar.</summary>
        public float SecondaryReadyFraction
        {
            get
            {
                if (!HasSecondaryWeapon) return 0f;
                float interval = Mathf.Max(0.01f, archetype.secondaryWeapon.GetLevel(0).fireInterval);
                return Mathf.Clamp01(1f - (nextSecondaryFireTime - Time.time) / interval);
            }
        }

        void FireSecondaryOnce()
        {
            MetalWeaponData data = archetype.secondaryWeapon;
            MetalWeaponLevel stats = data.GetLevel(0);
            nextSecondaryFireTime = Time.time + stats.fireInterval;
            lastFireTime = Time.time;
            Vector3 forward = GetAimForward();
            Transform bone = GetBone(archetype.secondaryMuzzleBone);
            Vector3 basePos = bone != null ? bone.position : GetEyesLocation();
            Vector3 muzzle = basePos + forward * data.muzzleOffset * meshScale;
            Vector3 aimDir = (GetAimPoint() - muzzle).normalized;
            if (Vector3.Dot(aimDir, forward) < 0.2f) aimDir = forward;
            float aiError = !IsPlayerControlled ? archetype.aimErrorDegrees : 0f;
            float cone = (data.spreadDegrees + aiError) * Mathf.Deg2Rad;
            for (int p = 0; p < Mathf.Max(1, data.pellets); p++)
            {
                Vector3 dir = MetalMath.RandomCone(aimDir, cone);
                MetalProjectile.Spawn(muzzle, dir, this, gameObject, data, stats.damage, dir * data.projectileSpeed);
            }
            MetalFX.SpawnMuzzleFlash(muzzle, aimDir, data.tracerColor, 0.14f);
            MetalAudio.PlayAt(data.fireSound, muzzle, IsPlayerControlled ? 1f : 0.8f);
            if (IsPlayerControlled) MetalBotAI.ReportNoise(this, muzzle, data.noiseRadius);
        }

        /// <summary>Starts a reload if the clip isn't full and there is reserve ammo.</summary>
        public void Reload()
        {
            MetalWeaponSlot slot = CurrentWeapon;
            if (slot == null || slot.data == null || slot.data.infiniteAmmo || IsReloading || IsSwappingWeapon || drawStartTime >= 0f) return;
            if (slot.clip < slot.data.GetLevel(slot.level).clipSize && slot.reserve > 0)
            {
                reloadDuration = Mathf.Max(0.01f, slot.data.reloadTime);
                reloadEndTime = Time.time + reloadDuration;
                reloadBeat = 0;
                TickReload();
            }
        }

        public bool IsReloading => reloadEndTime > 0f;

        /// <summary>
        /// Timeline of the reload animation: the reload sound (~1.15 s) ends with the reload, so longer reloads open
        /// with a pre-roll and the eject / seat / rack beats always land on the sound's clicks.
        /// </summary>
        public bool GetReloadTimeline(out float elapsed, out float soundTime)
        {
            elapsed = 0f;
            soundTime = 0f;
            if (!IsReloading) return false;
            elapsed = Mathf.Max(0f, reloadDuration - (reloadEndTime - Time.time));
            float soundStart = Mathf.Max(0f, reloadDuration - ReloadSoundLength);
            float squeeze = ReloadSoundLength / Mathf.Min(ReloadSoundLength, reloadDuration);
            soundTime = (elapsed - soundStart) * squeeze;
            return true;
        }

        void TickReload()
        {
            if (!GetReloadTimeline(out _, out float s)) return;
            MetalWeaponSlot slot = CurrentWeapon;
            if (reloadBeat == 0 && s >= 0f)
            {
                reloadBeat = 1;
                PlaySound(slot != null && slot.data != null ? slot.data.reloadSound : "reload", IsPlayerControlled ? 1f : 0.8f);
            }
            if (reloadBeat == 1 && s >= 0.06f)
            {
                reloadBeat = 2;
                SparkAtGuns(0.05f, new Color(0.35f, 0.3f, 0.25f)); // the spent cell, dull
            }
            if (reloadBeat == 2 && s >= 0.48f)
            {
                reloadBeat = 3;
                SparkAtGuns(0.07f, slot != null && slot.data != null ? slot.data.tracerColor : new Color(1f, 0.4f, 0.1f));
            }
        }

        void SparkAtGuns(float radius, Color color)
        {
            var boneNames = new List<string>();
            if (archetype != null && archetype.muzzleBones.Count > 0) boneNames.AddRange(archetype.muzzleBones);
            else boneNames.Add(HasBone(Rig.weaponBone) ? Rig.weaponBone : Rig.forearmR);
            foreach (string b in boneNames)
            {
                Transform t = GetBone(b);
                if (t != null) MetalFX.SpawnBurst(t.position, color, radius * meshScale, 0.15f);
            }
        }

        /// <summary>How far the current reload has got, 0-1.</summary>
        public float ReloadFraction => !IsReloading || reloadDuration <= 0f ? 0f : Mathf.Clamp01(1f - (reloadEndTime - Time.time) / reloadDuration);

        void FinishReload()
        {
            reloadEndTime = 0f;
            if (!ValidSlot(currentWeapon)) return;
            MetalWeaponSlot slot = weapons[currentWeapon];
            int taken = Mathf.Min(slot.data.GetLevel(slot.level).clipSize - slot.clip, slot.reserve);
            slot.clip += taken;
            slot.reserve -= taken;
        }

        /// <summary>World position of the head bone, or above the capsule centre.</summary>
        public Vector3 GetEyesLocation()
        {
            Transform head = GetBone(Rig.head);
            return head != null ? head.position : transform.position + Vector3.up * (Movement != null ? Movement.CurrentHalfHeight * 0.8f : 0.6f);
        }

        /// <summary>
        /// Where the next shot leaves from: the next working built-in gun bone, else a point muzzleOffset in front of
        /// the weapon bone, else just in front of the capsule.
        /// </summary>
        public virtual Vector3 GetMuzzleLocation()
        {
            if (archetype != null && archetype.muzzleBones.Count > 0)
            {
                Transform b = GetBone(archetype.muzzleBones[muzzleCycle % archetype.muzzleBones.Count]);
                if (b != null) return b.position;
            }
            Vector3 aimDir = MetalMath.Forward(GetAimPitch(), transform.eulerAngles.y);
            float offset = 0.45f;
            MetalWeaponSlot slot = CurrentWeapon;
            if (slot != null && slot.data != null) offset = slot.data.muzzleOffset;
            Transform wb = GetBone(Rig.weaponBone);
            if (wb != null) return wb.position + aimDir * offset * meshScale;
            return transform.position + aimDir * Capsule.radius * 1.5f;
        }

        /// <summary>Round-robin counter selecting which built-in gun fires next.</summary>
        protected int MuzzleCycleIndex => muzzleCycle;
        public int MuzzleCount => archetype != null ? archetype.muzzleBones.Count : 0;
        public int WorkingMuzzleCount => weaponDisabled ? 0 : Mathf.Max(0, MuzzleCount - brokenMuzzles.Count);

        /// <summary>Direction the robot's guns face: its yaw, pitched with the aim.</summary>
        public virtual Vector3 GetAimForward() => MetalMath.Forward(GetAimPitch(), transform.eulerAngles.y);

        /// <summary>AI sets this to the point it wants to shoot at (hasTarget false clears it).</summary>
        public void SetAITarget(Vector3 worldPoint, bool hasTarget)
        {
            aiTargetPoint = worldPoint;
            hasAITarget = hasTarget;
        }

        public bool HasAITarget => hasAITarget;

        /// <summary>Where the robot is aiming in world space (camera for players, target for AI).</summary>
        public Vector3 GetAimPoint()
        {
            if (IsPlayerControlled)
            {
                Controller.GetViewPoint(out Vector3 viewLoc, out Quaternion viewRot);
                Vector3 dir = viewRot * Vector3.forward;
                // Trace along the crosshair from level with the robot, so walls between the camera and the robot are ignored.
                Vector3 start = viewLoc + dir * Vector3.Dot(transform.position - viewLoc, dir);
                Vector3 end = viewLoc + dir * 300f;
                return MetalPhysics.Raycast(start, end, out RaycastHit hit, this) ? hit.point : end;
            }
            if (hasAITarget) return aiTargetPoint;
            return GetEyesLocation() + transform.forward * 10f;
        }

        /// <summary>Aim pitch in degrees for the animator (positive up).</summary>
        public float GetAimPitch()
        {
            if (dead || dormant) return 0f;
            if (IsPlayerControlled) return Mathf.Clamp(MetalMath.NormalizeAngle(Controller.ControlPitch), -60f, 60f);
            if (hasAITarget) return Mathf.Clamp(MetalMath.Pitch(aiTargetPoint - GetEyesLocation()), -60f, 60f);
            return 0f;
        }

        /// <summary>Fires the equipped weapon once. Falls back to melee when the gun is disabled.</summary>
        protected virtual void FireOnce()
        {
            if (weaponDisabled || !ValidSlot(currentWeapon))
            {
                if (archetype != null && archetype.meleeWeapon != null)
                {
                    var melee = new MetalWeaponSlot { data = archetype.meleeWeapon };
                    FireMelee(melee);
                    nextFireTime = Time.time + melee.data.GetLevel(0).fireInterval;
                }
                return;
            }
            lastFireTime = Time.time;
            MetalWeaponSlot slot = weapons[currentWeapon];
            MetalWeaponData data = slot.data;
            MetalWeaponLevel stats = data.GetLevel(slot.level);
            if (data.fireMode == MetalFireMode.Melee)
            {
                FireMelee(slot);
                nextFireTime = Time.time + stats.fireInterval;
                return;
            }
            if (!data.infiniteAmmo && slot.clip <= 0)
            {
                Reload();
                return;
            }
            // Guns whose arm was shot off are skipped in the firing order.
            for (int skip = 0; skip < MuzzleCount && brokenMuzzles.Contains(archetype.muzzleBones[muzzleCycle % MuzzleCount]); skip++)
            {
                muzzleCycle++;
            }
            Vector3 muzzle = GetMuzzleLocation();
            muzzleCycle++;
            Vector3 aimPoint = GetAimPoint();
            Vector3 aimDir = (aimPoint - muzzle).normalized;
            // A player's hitscan shot is traced along the camera's aim ray (from level with the robot) and only drawn
            // from the muzzle, so a gun poking into a ledge the player aims over still hits.
            Vector3 traceStart = muzzle;
            Vector3 traceDir = aimDir;
            if (IsPlayerControlled && data.fireMode == MetalFireMode.Hitscan)
            {
                Controller.GetViewPoint(out Vector3 viewLoc, out Quaternion viewRot);
                Vector3 viewDir = viewRot * Vector3.forward;
                traceStart = viewLoc + viewDir * Mathf.Max(0f, Vector3.Dot(transform.position - viewLoc, viewDir));
                traceDir = (aimPoint - traceStart).normalized;
            }
            // Never shoot backwards through the robot.
            if (Vector3.Dot(aimDir, GetAimForward()) < 0.2f)
            {
                aimDir = GetAimForward();
                traceStart = muzzle;
                traceDir = aimDir;
            }
            float aiError = !IsPlayerControlled && archetype != null ? archetype.aimErrorDegrees : 0f;
            // Precision weapons (sniper) are only accurate down the scope.
            float spread = IsPlayerControlled && !aiming && data.hipSpreadDegrees >= 0f ? data.hipSpreadDegrees : data.spreadDegrees;
            float cone = (spread + aiError) * Mathf.Deg2Rad;

            bool hitRobot = false;
            Quaternion toTrace = Quaternion.FromToRotation(aimDir, traceDir);
            for (int p = 0; p < Mathf.Max(1, data.pellets); p++)
            {
                Vector3 dir = MetalMath.RandomCone(aimDir, cone);
                if (data.fireMode == MetalFireMode.Hitscan)
                {
                    Vector3 tdir = toTrace * dir;
                    Vector3 end = traceStart + tdir * data.range;
                    bool hit = MetalPhysics.Raycast(traceStart, end, out RaycastHit h, this);
                    Vector3 impact = hit ? h.point : end;
                    MetalFX.SpawnBeam(muzzle, impact, data.tracerColor, 0.025f, 0.07f);
                    if (hit)
                    {
                        MetalFX.SpawnImpact(impact, h.normal, data.tracerColor, 0.1f);
                        if (p == 0) MetalAudio.PlayAt(data.impactSound, impact, 0.6f, 0.1f);
                        hitRobot |= MetalPhysics.FindBot(h.collider) != null;
                        MetalPhysics.ApplyDamage(h.collider, new MetalDamageEvent
                        {
                            amount = stats.damage,
                            isPoint = true,
                            hitPoint = h.point,
                            shotDirection = dir,
                            traceStart = traceStart,
                            hitCollider = h.collider,
                            instigator = Controller,
                            causer = gameObject,
                        });
                    }
                }
                else
                {
                    Vector3 velocity = dir * data.projectileSpeed;
                    if (data.fireMode == MetalFireMode.Thrown) velocity += Vector3.up * 3.5f;
                    MetalProjectile.Spawn(muzzle, dir, this, gameObject, data, stats.damage, velocity);
                }
            }

            if (data.fireMode == MetalFireMode.Thrown) MetalFX.SpawnBurst(muzzle, data.tracerColor, 0.08f, 0.06f);
            else MetalFX.SpawnMuzzleFlash(muzzle, aimDir, data.tracerColor, 0.08f);
            MetalAudio.PlayAt(data.fireSound, muzzle, IsPlayerControlled ? 1f : 0.7f);
            if (IsPlayerControlled)
            {
                // Gunfire carries: enemies within the weapon's noise radius come looking.
                MetalBotAI.ReportNoise(this, muzzle, data.noiseRadius);
            }
            if (Animator != null) Animator.PlayRecoil();
            if (Controller is MetalPlayerController pc)
            {
                // Projectiles land later; only hitscan shots count towards accuracy.
                if (data.fireMode == MetalFireMode.Hitscan) pc.NotifyShotFired(hitRobot);
                if (hitRobot) pc.NotifyHitConfirmed();
            }
            if (!data.infiniteAmmo)
            {
                slot.clip--;
                if (slot.clip <= 0) Reload();
            }
            nextFireTime = Time.time + stats.fireInterval;
        }

        /// <summary>Damages every hostile robot in a sphere in front of this one.</summary>
        void FireMelee(MetalWeaponSlot slot)
        {
            MetalWeaponLevel stats = slot.data.GetLevel(slot.level);
            Vector3 center = transform.position + transform.forward * (Capsule.radius + slot.data.range * 0.5f);
            foreach (MetalBot victim in MetalPhysics.OverlapBots(center, slot.data.range * 0.6f))
            {
                if (IsHostileTo(victim) && victim.IsAlive)
                {
                    victim.TakeDamage(new MetalDamageEvent
                    {
                        amount = stats.damage,
                        hitPoint = victim.transform.position,
                        shotDirection = transform.forward,
                        instigator = Controller,
                        causer = gameObject,
                    });
                }
            }
            MetalFX.SpawnBurst(center, slot.data.tracerColor, 0.25f, 0.12f);
            if (Animator != null) Animator.PlayRecoil();
        }

        /// <summary>Lobs one grenade towards the aim point, if any are left.</summary>
        public void ThrowGrenade()
        {
            if (GrenadeWeapon == null || Grenades <= 0 || dead || dormant || IsStunned || IsSneaking) return;
            Grenades--;
            Vector3 start = GetEyesLocation() + transform.forward * 0.4f;
            Vector3 target = GetAimPoint();
            Vector3 dir = (target - start).normalized;
            Vector3 velocity = dir * GrenadeWeapon.projectileSpeed + Vector3.up * 3.5f;
            float fuse = -1f;
            // Players lob onto whatever the crosshair is on (within throwing range), arcing over cover, and the fuse
            // waits for it to land. Beyond that range it's a long throw.
            const float MaxLob = 22f;
            if (IsPlayerControlled && Vector3.Distance(start, target) < MaxLob)
            {
                float gravity = Physics.gravity.y * GrenadeWeapon.projectileGravityScale;
                if (MetalMath.SuggestArcVelocity(start, target, gravity, 0.45f, out Vector3 lob))
                {
                    velocity = lob;
                    float horizontal = MetalMath.Flat(target - start).magnitude;
                    fuse = horizontal / Mathf.Max(0.01f, MetalMath.Flat(velocity).magnitude) + 0.35f;
                }
            }
            MetalProjectile proj = MetalProjectile.Spawn(start, velocity, this, gameObject, GrenadeWeapon, GrenadeWeapon.GetLevel(0).damage, velocity);
            if (proj != null && fuse > 0f) proj.SetFuse(fuse);
            PlaySound("grenade_throw");
        }

        // ==================================================================================================
        // Damage
        // ==================================================================================================

        /// <summary>Which hit zone a shot struck: the zone bone passing closest to the shot's ray, within its radius.</summary>
        int FindHitZone(MetalDamageEvent e)
        {
            if (archetype == null || !e.isPoint) return -1;
            // A hit collider on a bone (flyers' arms) names its zone directly.
            if (e.hitCollider != null && e.hitCollider.transform != transform)
            {
                for (int i = 0; i < archetype.hitZones.Count; i++)
                {
                    Transform zb = GetBone(archetype.hitZones[i].bone);
                    if (zb != null && e.hitCollider.transform.IsChildOf(zb)) return i;
                }
            }
            Vector3 rayStart = e.traceStart ?? e.hitPoint - e.shotDirection * 0.5f;
            int best = -1;
            float bestDist = float.MaxValue;
            for (int i = 0; i < archetype.hitZones.Count; i++)
            {
                MetalHitZone zone = archetype.hitZones[i];
                Transform zb = GetBone(zone.bone);
                if (zb == null) continue;
                float d = MetalMath.PointDistToLine(zb.position, e.shotDirection, rayStart);
                if (d < zone.radius * meshScale && d < bestDist)
                {
                    best = i;
                    bestDist = d;
                }
            }
            return best;
        }

        /// <summary>
        /// Applies damage after the friendly-fire check, scaling it by the hit zone struck and breaking zones whose
        /// durability runs out. Notifies the controller and kills the robot at 0 health.
        /// </summary>
        public virtual float TakeDamage(MetalDamageEvent e)
        {
            if (dead || assembling || !canBeDamaged || e.amount <= 0f) return 0f;
            // Who is responsible: the causer itself (hitscan, melee), the shooter of a projectile, or the
            // instigating controller's robot (radial damage).
            MetalBot attacker = e.causer != null ? e.causer.GetComponent<MetalBot>() : null;
            if (attacker == null && e.causer != null)
            {
                MetalProjectile p = e.causer.GetComponent<MetalProjectile>();
                if (p != null) attacker = p.InstigatorBot;
            }
            if (attacker == null && e.instigator != null) attacker = e.instigator.Pawn;
            if (attacker != null && attacker != this && !attacker.IsHostileTo(this))
            {
                return 0f; // no friendly fire
            }
            // Shots from a capital ship's turrets carry the ship as their owner.
            if (e.causerOwner != null)
            {
                MetalCapitalShip ship = e.causerOwner.GetComponent<MetalCapitalShip>();
                if (ship != null && ship.Team == team) return 0f;
            }

            float damage = e.amount;
            int zone = FindHitZone(e);
            if (zone >= 0)
            {
                MetalHitZone hz = archetype.hitZones[zone];
                damage *= hz.damageMultiplier;
                // Break the zone only on the hit that pushes it past its durability.
                bool wasIntact = hz.durability <= 0f || zoneDamage[zone] < hz.durability;
                zoneDamage[zone] += damage;
                if (wasIntact && hz.durability > 0f && zoneDamage[zone] >= hz.durability) BreakZone(hz);
            }
            if (attacker == this)
            {
                // Splash from your own grenades and rockets still hurts, but only half as much.
                damage *= 0.5f;
            }

            health -= damage;
            hitFlash = 1f;
            if (Time.time - lastDamageTakenTime > 0.1f) PlaySound(IsPlayerControlled ? "player_hit" : "robot_hit", 0.8f);
            lastDamageTakenTime = Time.time;
            // Cry out, but not on every round of a burst.
            if (health > 0f && lastDamageTakenTime >= nextPainTime && Speak(MetalBark.Pain))
            {
                nextPainTime = lastDamageTakenTime + UnityEngine.Random.Range(0.7f, 1.4f);
            }
            Vector3 from = attacker != null ? attacker.transform.position
                : (e.causer != null ? e.causer.transform.position : transform.position - transform.forward);
            if (Animator != null) Animator.PlayHitReact(MetalMath.Flat(transform.position - from).normalized);
            if (Controller != null)
            {
                Controller.NotifyDamaged(attacker);
            }
            if (health <= 0f) Die(e.instigator);
            return damage;
        }

        /// <summary>Damage taken recently (HUD flash).</summary>
        public float LastDamageTakenTime => lastDamageTakenTime;

        void DisableWeapon()
        {
            if (weaponDisabled) return;
            weaponDisabled = true;
            MetalFX.SpawnExplosion(GetMuzzleLocation(), new Color(1f, 0.6f, 0.1f), 0.4f, 0.4f);
            RefreshWeaponVisual();
        }

        void BreakZone(MetalHitZone zone)
        {
            if (zone.disablesWeaponWhenBroken) DisableWeapon();
            if (!string.IsNullOrEmpty(zone.disablesMuzzleBone) && brokenMuzzles.Add(zone.disablesMuzzleBone))
            {
                MetalFX.SpawnExplosion(GetBoneLocation(zone.disablesMuzzleBone), new Color(1f, 0.6f, 0.1f), 1.2f * meshScale, 0.5f);
                // The arm comes off: hide it so shots pass through the gap.
                if (Animator != null) Animator.HideBone(zone.bone);
                if (WorkingMuzzleCount == 0) DisableWeapon();
            }
        }

        /// <summary>
        /// Kills the robot: disables movement and collision, broadcasts AnyBotDied and schedules Explode(). Safe to
        /// call more than once.
        /// </summary>
        protected virtual void Die(MetalController killer)
        {
            if (dead) return;
            dead = true;
            health = 0f;
            StopFire();
            Movement.StopMovementImmediately();
            Movement.DisableMovement();
            Capsule.enabled = false;
            // The last scream, and a moment later the killer gloats.
            Speak(MetalBark.Death);
            MetalBot victor = killer != null ? killer.Pawn : null;
            if (victor != null && victor != this) victor.Speak(MetalBark.Laugh, UnityEngine.Random.Range(0.5f, 0.9f));
            AnyBotDied?.Invoke(this, killer);
            if (Controller != null && !Controller.IsPlayer) Controller.NotifyPawnDied();
            // The wreck lies there for a moment, then blows apart and is removed.
            Invoke(nameof(Explode), 2.5f);
        }

        /// <summary>Plays the death explosion and destroys the robot (or only hides it while the player still controls it).</summary>
        void Explode()
        {
            if (this == null || hiddenAfterDeath) return;
            Vector3 center = MeshRoot != null ? MeshRoot.position + Vector3.up * (archetype != null ? archetype.capsuleHalfHeight : 0.7f) : transform.position;
            MetalFX.SpawnExplosion(center, new Color(1f, 0.45f, 0.1f), 1.4f * meshScale, 0.5f);
            MetalAudio.PlayAt("robot_explode", center, Mathf.Clamp(meshScale, 0.8f, 2f));
            if (IsPlayerControlled)
            {
                // The game mode respawns the player and cleans this body up.
                hiddenAfterDeath = true;
                if (MeshRoot != null) MeshRoot.gameObject.SetActive(false);
                return;
            }
            Destroy(gameObject);
        }

        bool hiddenAfterDeath;
        public bool IsHiddenAfterDeath => hiddenAfterDeath;

        // ==================================================================================================
        // Movement feel: action camera, double-jump flip, landing
        // ==================================================================================================

        /// <summary>Player robot in a level whose game mode uses the action camera.</summary>
        public virtual bool UsesActionCamera => MetalGameMode.Current != null && MetalGameMode.Current.actionCamera && IsPlayerControlled;

        /// <summary>Gun up and facing the aim (aiming, shooting, or an AI with a target), as opposed to running freely.</summary>
        public bool IsInCombatStance
        {
            get
            {
                if (!IsPlayerControlled) return hasAITarget || fireHeld;
                return !UsesActionCamera || aiming || fireHeld || secondaryHeld || Time.time - lastFireTime < 1.5f;
            }
        }

        /// <summary>Players turn with the controller (strafe); AI turns smoothly towards its focus.</summary>
        protected virtual void ConfigureRotationForController()
        {
            bool player = IsPlayerControlled;
            Movement.useControllerDesiredRotation = !player && Controller != null;
            Movement.orientRotationToMovement = false;
            Movement.rotationRate = 600f;
        }

        /// <summary>Pawn-level "use controller yaw" (players without the action camera strafe with the camera).</summary>
        protected virtual bool UseControllerRotationYaw => IsPlayerControlled && !UsesActionCamera;

        void TickActionCamera(float dt)
        {
            bool action = UsesActionCamera;
            bool combat = IsInCombatStance;
            if (action)
            {
                // Free running: face the movement. Combat: turn quickly to the camera and strafe.
                Movement.orientRotationToMovement = !combat;
                Movement.useControllerDesiredRotation = combat;
                Movement.rotationRate = combat ? 1080f : 720f;
            }
            // Exploring: centred behind and a little further out. Combat: over the shoulder. Aiming: zoomed by the
            // weapon's own amount; a scoped weapon pulls the camera right into the robot's head.
            MetalWeaponSlot slot = IsSwappingWeapon ? null : CurrentWeapon;
            MetalWeaponData weapon = slot != null ? slot.data : null;
            bool scoped = aiming && weapon != null && weapon.scopeOverlay && IsPlayerControlled;
            float aimArm = weapon != null ? weapon.aimArmLengthScale : 0.55f;
            float aimFov = weapon != null ? weapon.aimFOV : 60f;
            bool relaxed = action && !combat;
            float armTarget = aiming ? defaultArmLength * aimArm : (relaxed ? defaultArmLength * (1.1f + 0.1f * sprintAlpha) : defaultArmLength);
            Vector3 offsetTarget = relaxed ? new Vector3(0f, defaultCameraOffset.y, 0f) : defaultCameraOffset;
            if (scoped)
            {
                // Eye level: from the capsule centre most of the way up to the top of the head.
                offsetTarget = new Vector3(0f, (archetype != null ? archetype.capsuleHalfHeight : 0.68f) * 0.45f, 0f);
            }
            float blend = scoped ? 18f : (relaxed ? 4f : 10f);
            CameraArmLength = MetalMath.InterpTo(CameraArmLength, armTarget, dt, blend);
            CameraSocketOffset = MetalMath.InterpTo(CameraSocketOffset, offsetTarget, dt, scoped ? 18f : 6f);
            CameraLagEnabled = !scoped;
            float fovTarget = aiming ? aimFov : 85f + 7f * sprintAlpha;
            CameraFOV = MetalMath.InterpTo(CameraFOV, fovTarget, dt, scoped ? 14f : 10f);

            scopeAlpha = MetalMath.InterpTo(scopeAlpha, scoped ? 1f : 0f, dt, 12f);
            bool hide = scopeAlpha > 0.6f;
            if (hide != hiddenForScope)
            {
                // Looking down the scope from inside the head: hide our own body.
                hiddenForScope = hide;
                foreach (Renderer r in GetComponentsInChildren<Renderer>(true))
                {
                    if (!(r is ParticleSystemRenderer) && !(r is LineRenderer)) r.forceRenderingOff = hide;
                }
            }
        }

        public float ScopeAlpha => scopeAlpha;

        /// <summary>Look speed multiplier from the aimed weapon (sniper scopes slow the mouse down).</summary>
        public float LookSensitivityScale
        {
            get
            {
                MetalWeaponSlot slot = CurrentWeapon;
                return aiming && slot != null && slot.data != null ? slot.data.aimSensitivity : 1f;
            }
        }

        public float LastFireTime => lastFireTime;
        public Quaternion MeshBaseRotation => meshBaseRotation;
        public float FlipTuck => flipTuck;

        /// <summary>Held aim button (zooms the action camera). Ignored while crouched.</summary>
        public void SetAiming(bool value) => aiming = value && !IsSneaking;
        public bool IsAiming => aiming;

        public bool CanSprint => archetype != null && archetype.sprintSpeed > 0f;
        public bool CanSneak => archetype != null && archetype.crouchSpeed > 0f;

        /// <summary>Holds or releases sprint; the robot only sprints while moving forward on the ground and free.</summary>
        public void SetWantsSprint(bool value) => wantsSprint = value;
        public bool IsSprinting => sprinting;

        /// <summary>
        /// Crouches into a sneak (the console disguise) or stands back up. Crouched, the robot can't shoot, aim,
        /// throw grenades, jump or sprint, but it can still hack.
        /// </summary>
        public void SetSneaking(bool value)
        {
            if (!CanSneak || dead || dormant) return;
            if (value)
            {
                fireHeld = false;
                secondaryHeld = false;
                aiming = false;
                Movement.Crouch();
            }
            else
            {
                Movement.UnCrouch();
            }
        }

        public bool IsSneaking => Movement != null && Movement.IsCrouched;

        /// <summary>Marks the crouch as the automatic one taken while hacking (enemies don't find it suspicious).</summary>
        public void SetHackCrouch(bool value) => hackCrouch = value;
        public bool IsHackCrouching => hackCrouch && IsSneaking;
        public float CrouchAlpha => crouchAlpha;
        public float SprintAlpha => sprintAlpha;

        /// <summary>Movement input in world space (forwarded to the movement component unless powered down).</summary>
        public void AddMovementInput(Vector3 worldDirection, float scale = 1f)
        {
            if (dead || IsDormant || IsStunned) return;
            Movement.AddInputVector(worldDirection, scale);
        }

        /// <summary>Jumps (a second press in mid-air is the double jump). Ignored while crouched or powered down.</summary>
        public virtual void Jump()
        {
            if (dead || IsDormant || IsStunned || IsSneaking) return;
            Movement.Jump();
        }

        /// <summary>Unreal's LaunchCharacter.</summary>
        public void LaunchCharacter(Vector3 velocity, bool overrideXY, bool overrideY) => Movement.Launch(velocity, overrideXY, overrideY);

        /// <summary>Moves the robot (and its controller's view) to a new place.</summary>
        public void TeleportTo(Vector3 position, float yaw)
        {
            bool wasEnabled = Capsule.enabled;
            Capsule.enabled = false;
            transform.SetPositionAndRotation(position, Quaternion.Euler(0f, yaw, 0f));
            Capsule.enabled = wasEnabled;
            Movement.StopMovementImmediately();
            if (Controller != null) Controller.ControlYaw = yaw;
        }

        void TickLocomotion(float dt)
        {
            if (archetype == null || dead)
            {
                sprinting = false;
                return;
            }
            bool player = IsPlayerControlled;
            Vector3 v2 = MetalMath.Flat(Velocity);
            float speed = v2.magnitude;
            float now = Time.time;
            if (player)
            {
                // Sprint only while running forward-ish with nothing else going on. In the air, keep the momentum.
                Vector3 viewForward = Controller != null ? Quaternion.Euler(0f, Controller.ControlYaw, 0f) * Vector3.forward : transform.forward;
                bool forward = speed > 0.5f && Vector3.Dot(v2 / speed, viewForward) > 0.3f;
                bool free = !aiming && !fireHeld && !secondaryHeld && now - lastFireTime > 0.4f && !IsSwappingWeapon && !IsStunned && !dormant;
                if (Movement.IsFalling) sprinting = sprinting && wantsSprint;
                else sprinting = wantsSprint && CanSprint && !IsSneaking && free && forward;
                // Also restores full speed to a hijacked robot that was walking at its slow patrol pace.
                Movement.maxWalkSpeed = sprinting ? archetype.sprintSpeed : archetype.walkSpeed;
            }
            if (CanSneak)
            {
                // Crouched, the robot glides on its base: slow to start, drifting to a stop.
                bool crouched = IsSneaking;
                Movement.groundFriction = crouched ? SlideFriction : WalkFriction;
                Movement.brakingDecelerationWalking = crouched ? SlideBraking : WalkBraking;
                Movement.maxAcceleration = crouched ? SlideAcceleration : WalkAcceleration;
            }
            else
            {
                sprinting = false;
            }

            crouchAlpha = MetalMath.InterpTo(crouchAlpha, IsSneaking ? 1f : 0f, dt, 10f);
            float sprintRange = Mathf.Max(0.01f, archetype.sprintSpeed - archetype.walkSpeed);
            float sprintTarget = sprinting ? Mathf.Clamp01((speed - archetype.walkSpeed * 0.8f) / sprintRange) : 0f;
            sprintAlpha = MetalMath.InterpTo(sprintAlpha, sprintTarget, dt, 6f);

            // Footsteps every half stride; walking and especially sprinting carry to enemies. Crouched robots slide silently.
            if (Movement.IsMovingOnGround && speed > 0.4f && !dormant && !IsSneaking)
            {
                stepDistance += speed * dt;
                float stepLength = Mathf.Max(0.4f, Rig.strideLength * meshScale * 0.5f * (1f + 0.4f * sprintAlpha));
                if (stepDistance >= stepLength)
                {
                    stepDistance = 0f;
                    bool heavy = meshScale > 1.5f || archetype.capsuleHalfHeight > 1f;
                    float volume = player ? 0.55f : 0.4f;
                    Vector3 feet = transform.position - Vector3.up * Movement.FullHalfHeight;
                    MetalAudio.PlayAt(heavy ? "footstep_heavy" : (sprinting ? "footstep_sprint" : "footstep"), feet, volume, 0.12f);
                    if (player) MetalBotAI.ReportNoise(this, feet, sprinting ? 13f : 4.5f);
                }
            }
            else
            {
                stepDistance = 0f;
            }

            // AI marksmen show where they are aiming with a laser sight.
            MetalWeaponSlot slot = CurrentWeapon;
            bool laser = !player && hasAITarget && !dormant && !IsStunned && !weaponDisabled && slot != null && slot.data != null && slot.data.laserSight;
            if (laser)
            {
                Vector3 muzzle = GetMuzzleLocation();
                Vector3 dir = (aiTargetPoint - muzzle).normalized;
                Vector3 end = muzzle + dir * slot.data.range;
                Vector3 impact = MetalPhysics.Raycast(muzzle, end, out RaycastHit hit, this) ? hit.point : end;
                if (laserSight == null) laserSight = MetalFX.SpawnBeam(muzzle, impact, new Color(1f, 0.05f, 0.05f), 0.006f, 0f);
                else laserSight.SetBeamEndpoints(muzzle, impact);
            }
            else if (laserSight != null)
            {
                Destroy(laserSight.gameObject);
                laserSight = null;
            }
        }

        void OnJumped()
        {
            lowestFallSpeed = 0f;
            // The count is incremented before this is called, so 2+ means a mid-air jump.
            if (Movement.JumpCurrentCount >= 2)
            {
                flipStartTime = Time.time;
                PlaySound("double_jump", 0.8f);
                FireJetpack();
            }
            else
            {
                PlaySound("jump", 0.6f);
            }
        }

        void TickFlip(float now)
        {
            const float FlipDuration = 0.5f;
            float angle = 0f;
            flipTuck = 0f;
            if (flipStartTime >= 0f)
            {
                float t = (now - flipStartTime) / FlipDuration;
                if (t >= 1f || dead || !Movement.IsFalling)
                {
                    flipStartTime = -1f;
                }
                else
                {
                    // One full forward somersault, eased in and out; the tuck peaks halfway through.
                    angle = 360f * MetalMath.EaseInOut(0f, 1f, t, 1.6f);
                    flipTuck = Mathf.Sin(t * Mathf.PI);
                }
            }
            if (MeshRoot == null) return;
            // Spin the whole body forward around the capsule centre (the model sits below it, so rotate the offset too).
            Quaternion flip = Quaternion.AngleAxis(angle, Vector3.right);
            Quaternion turn = meshTilt * flip;
            MeshRoot.localPosition = turn * meshBaseLocation;
            MeshRoot.localRotation = turn * meshBaseRotation;
        }

        void OnLanded()
        {
            flipStartTime = -1f;
            if (Animator != null) Animator.PlayLand(Mathf.Clamp(-lowestFallSpeed / 14f, 0.25f, 1f));
            if (lowestFallSpeed < -5f) PlaySound("land", Mathf.Clamp(-lowestFallSpeed / 14f, 0.3f, 1f));
            lowestFallSpeed = 0f;
        }

        // ==================================================================================================
        // Jetpack
        // ==================================================================================================

        void BuildJetpack()
        {
            if (jetFlames.Count > 0 || archetype == null || !archetype.hasJetpack || bodyInstance == null) return;
            MetalBotRig rig = Rig;
            Transform pelvis = GetBone(rig.pelvis);
            if (pelvis == null || !TryGetBindMatrix(rig.pelvis, out Matrix4x4 pelvisBind)) return;
            Material dark = MetalGameAssets.Environment("Dark");
            Matrix4x4 toPelvis = pelvisBind.inverse;
            // Everything is placed in model space of the bind pose and then made relative to the pelvis, so the pack
            // rides along with the body, the double-jump flip included.
            Transform Attach(Transform t, Vector3 posInMesh, Quaternion rotInMesh, Vector3 scale)
            {
                Matrix4x4 rel = toPelvis * Matrix4x4.TRS(posInMesh, rotInMesh, Vector3.one);
                t.SetParent(pelvis, false);
                t.localPosition = rel.GetColumn(3);
                t.localRotation = rel.rotation;
                t.localScale = scale / Mathf.Max(0.001f, pelvis.lossyScale.x / Mathf.Max(0.001f, MeshRoot.lossyScale.x));
                return t;
            }
            GameObject Part(PrimitiveType type, string partName)
            {
                GameObject go = MetalMaterials.Shape(type, null, partName);
                go.GetComponent<Renderer>().shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.On;
                if (dark != null) go.GetComponent<Renderer>().sharedMaterial = dark;
                return go;
            }
            foreach (float side in new[] { 1f, -1f })
            {
                Vector3 pod = rig.jetpackOffset + new Vector3(-rig.jetpackSpread * side, 0f, 0f);
                Attach(Part(PrimitiveType.Cylinder, "Jet Pod").transform, pod, Quaternion.identity, new Vector3(0.09f, 0.12f, 0.09f));
                Attach(Part(PrimitiveType.Cylinder, "Jet Strap").transform, pod + new Vector3(0f, 0.06f, 0.04f),
                    Quaternion.AngleAxis(90f, Vector3.right), new Vector3(0.04f, 0.04f, 0.04f));
                Vector3 nozzle = pod - new Vector3(0f, 0.15f, 0f);
                Attach(Part(PrimitiveType.Cylinder, "Jet Nozzle").transform, nozzle, Quaternion.identity, new Vector3(0.085f, 0.03f, 0.085f));
                // Flame: a glowing capsule hung below the nozzle, stretched while it burns.
                var pivot = new GameObject("Jet Flame Pivot").transform;
                Attach(pivot, nozzle - new Vector3(0f, 0.03f, 0f), Quaternion.identity, Vector3.one);
                GameObject flame = MetalMaterials.Shape(PrimitiveType.Capsule, pivot, "Jet Flame");
                // The capsule is 2 units tall: halved and lowered it spans 0..-1 below the pivot, so the pivot's Y
                // scale is the flame length.
                flame.transform.localPosition = new Vector3(0f, -0.5f, 0f);
                flame.transform.localScale = new Vector3(1f, 0.5f, 1f);
                Renderer fr = flame.GetComponent<Renderer>();
                MetalMaterials.SetGlow(fr, new Color(1f, 0.55f, 0.15f), 3f);
                jetFlameRenderers.Add(fr);
                pivot.gameObject.SetActive(false);
                jetFlames.Add(pivot);
            }
            var lightGo = new GameObject("Jet Light");
            Attach(lightGo.transform, rig.jetpackOffset - new Vector3(0f, 0.30f, 0.04f), Quaternion.identity, Vector3.one);
            jetLight = lightGo.AddComponent<Light>();
            jetLight.type = LightType.Point;
            jetLight.color = new Color(1f, 0.55f, 0.2f);
            jetLight.range = 4.5f;
            jetLight.shadows = LightShadows.None;
            jetLight.intensity = 0f;
        }

        void FireJetpack()
        {
            if (jetFlames.Count == 0 || dead) return;
            jetFireEnd = Time.time + 0.55f;
            foreach (Transform pivot in jetFlames)
            {
                pivot.gameObject.SetActive(true);
                MetalFX.SpawnBurst(pivot.position - Vector3.up * 0.1f, new Color(1f, 0.6f, 0.2f), 0.18f, 0.2f);
            }
        }

        void TickJetpack(float now)
        {
            if (jetFlames.Count == 0 || jetFireEnd <= 0f) return;
            // A hard kick that tails off: full length at ignition, gone after ~0.55 s (or as soon as it lands).
            float alpha = Mathf.Clamp01((jetFireEnd - now) / 0.55f);
            bool burning = alpha > 0.02f && !dead && !dormant && Movement.IsFalling;
            for (int i = 0; i < jetFlames.Count; i++)
            {
                Transform pivot = jetFlames[i];
                if (!burning)
                {
                    pivot.gameObject.SetActive(false);
                    continue;
                }
                float flicker = 0.85f + 0.15f * Mathf.Sin(now * 55f + i * 1.7f);
                float length = (0.08f + 0.42f * Mathf.Sqrt(alpha)) * flicker;
                float width = 0.07f * (0.7f + 0.3f * alpha);
                pivot.localScale = new Vector3(width, length, width) / Mathf.Max(0.001f, pivot.parent.lossyScale.x / Mathf.Max(0.001f, MeshRoot.lossyScale.x));
                MetalMaterials.SetGlowColor(jetFlameRenderers[i], new Color(1f, 0.55f, 0.15f), 2f + 4f * alpha);
            }
            if (jetLight != null) jetLight.intensity = burning ? 6f * alpha : 0f;
            if (!burning) jetFireEnd = 0f;
        }

        // ==================================================================================================
        // Voice and sounds
        // ==================================================================================================

        public bool IsSpeaking => voice != null && voice.IsPlaying;

        /// <summary>
        /// Makes the robot "say" something: a random variant of the mood's voice sound at its type's pitch. Robots the
        /// player drives stay quiet, and so do dead ones (except the death scream). Returns true if it spoke.
        /// </summary>
        public bool Speak(MetalBark bark, float delay = 0f)
        {
            bool death = bark == MetalBark.Death;
            if (!hasVoice || IsPlayerControlled || (dead && !death) || this == null) return false;
            if (delay > 0f)
            {
                StartCoroutine(SpeakLater(bark, delay));
                return true;
            }
            if (IsSpeaking)
            {
                if (bark <= voiceBark) return false;
                voice.Stop();
            }
            string[] bases = { "voice_chatter", "voice_orders", "voice_laugh", "voice_alert", "voice_confused", "voice_pain", "voice_death" };
            string soundName = MetalAudio.PickVariant(bases[(int)bark]);
            if (soundName == null) return false;
            // Idle muttering is quieter than the rest, and every line wavers a little in pitch.
            float volume = bark == MetalBark.Chatter ? 0.45f : (bark == MetalBark.Orders ? 0.6f : 0.75f);
            float typePitch = archetype != null ? archetype.voicePitch : 1f;
            voice = MetalAudio.PlayAttached(MeshRoot != null ? MeshRoot : transform, soundName, volume, typePitch * UnityEngine.Random.Range(0.96f, 1.04f));
            if (voice != null && death) voice.transform.SetParent(null, true); // the scream outlives the wreck
            voiceBark = bark;
            return true;
        }

        IEnumerator SpeakLater(MetalBark bark, float delay)
        {
            yield return new WaitForSeconds(delay);
            Speak(bark);
        }

        protected void PlaySound(string soundName, float volume = 1f) => MetalAudio.PlayAt(soundName, transform.position, volume);

        // ==================================================================================================
        // Visual feedback (materials)
        // ==================================================================================================

        static readonly int EmissionId = Shader.PropertyToID("_EmissionColor");
        static readonly int BaseColorId = Shader.PropertyToID("_BaseColor");
        static readonly Color HackGlowColor = new Color(0.25f, 0.85f, 1f) * 2.5f;
        float hackGlow;
        Color flashColor;
        float flashAmount;

        /// <summary>Collects every renderer on the robot so the hit flash, hack glow and team paint reach them all.</summary>
        void RefreshMaterials()
        {
            renderers.Clear();
            paintRenderers.Clear();
            if (MeshRoot == null) return;
            foreach (Renderer r in MeshRoot.GetComponentsInChildren<Renderer>(true))
            {
                if (r is ParticleSystemRenderer || r is LineRenderer || jetFlameRenderers.Contains(r)) continue;
                renderers.Add(r);
                if (weaponInstance == null || !r.transform.IsChildOf(weaponInstance.transform)) paintRenderers.Add(r);
            }
            ApplyMaterialState();
        }

        /// <summary>Writes the hit flash, hack glow and team paint to every renderer's property blocks.</summary>
        void ApplyMaterialState()
        {
            MetalTeamColors colors = archetype != null ? archetype.GetTeamColors(team) : null;
            Color emission = flashColor * flashAmount + HackGlowColor * hackGlow;
            foreach (Renderer r in renderers)
            {
                if (r == null) continue;
                Material[] mats = r.sharedMaterials;
                for (int i = 0; i < mats.Length; i++)
                {
                    r.GetPropertyBlock(block, i);
                    block.SetColor(EmissionId, emission);
                    string matName = mats[i] != null ? mats[i].name : "";
                    if (colors != null && paintRenderers.Contains(r))
                    {
                        // Paintable material slots are the ones whose names end in "_Body" or "_Accent".
                        if (matName.EndsWith("_Body")) block.SetColor(BaseColorId, colors.body);
                        else if (matName.EndsWith("_Accent")) block.SetColor(BaseColorId, colors.accent);
                    }
                    r.SetPropertyBlock(block, i);
                }
            }
        }

        void TickLowHealthWarning(float now)
        {
            bool low = IsPlayerControlled && !dead && !dormant && health <= maxHealth * LowHealthFraction;
            if (!low)
            {
                nextLowHealthPulseTime = 0f;
                return;
            }
            if (now >= nextLowHealthPulseTime)
            {
                nextLowHealthPulseTime = now + 1f;
                // Above 1 so the red holds at full for a beat before fading.
                lowHealthFlash = 1.25f;
                MetalAudio.Play2D("low_health", 0.6f);
            }
        }

        void TickMaterials(float dt, float now)
        {
            TickLowHealthWarning(now);
            bool dirty = false;
            if (hitFlash > 0f || lowHealthFlash > 0f || flashAmount > 0f)
            {
                hitFlash = Mathf.Max(0f, hitFlash - dt * 6f);
                lowHealthFlash = Mathf.Max(0f, lowHealthFlash - dt * 5f);
                // Both flashes share one emission; whichever is brighter picks the colour.
                bool lowShows = lowHealthFlash > hitFlash;
                flashColor = lowShows ? new Color(3f, 0.05f, 0.03f) : new Color(2f, 0.8f, 0.3f);
                flashAmount = Mathf.Max(hitFlash, Mathf.Min(1f, lowHealthFlash));
                dirty = true;
            }
            float glow = 0f;
            if (hijacked && !dead)
            {
                glow = 0.65f + 0.35f * Mathf.Sin(now * 6f);
            }
            else if (firewallGlow && !dead)
            {
                // A firewall after a failed hack: the hack glow stutters on and off until it drops.
                firewallGlow = IsFirewalled;
                glow = firewallGlow && Mathf.Repeat(now * 5f, 1f) < 0.35f ? 0.8f : 0f;
            }
            if (!Mathf.Approximately(glow, hackGlow))
            {
                hackGlow = glow;
                dirty = true;
            }
            if (dirty) ApplyMaterialState();
        }

        // ==================================================================================================
        // Tick
        // ==================================================================================================

        protected virtual void Update()
        {
            if (archetype == null) return;
            float dt = Time.deltaTime;
            float now = Time.time;
            if (UseControllerRotationYaw && Controller != null && !dead && !IsDormant)
            {
                transform.rotation = Quaternion.Euler(0f, Controller.ControlYaw, 0f);
            }
            TickMaterials(dt, now);
            // Movement state first, then the things that pose or move the body, and the flip last (it sets the
            // final model transform).
            TickLocomotion(dt);
            TickWeaponSwap(now);
            TickWeaponDraw();
            TickReload();
            TickJetpack(now);
            TickActionCamera(dt);
            TickFlip(now);
            if (Movement.IsFalling) lowestFallSpeed = Mathf.Min(lowestFallSpeed, Velocity.y);
            if (reloadEndTime > 0f && now >= reloadEndTime) FinishReload();

            // The trigger: fire whenever held and ready. Players click again for each shot of a semi-automatic weapon.
            if (fireHeld && !dead && !dormant && !IsSneaking && !IsStunned && !IsReloading && !IsSwappingWeapon && now >= nextFireTime)
            {
                FireOnce();
                MetalWeaponSlot slot = CurrentWeapon;
                if (slot != null && slot.data != null && !slot.data.automatic && IsPlayerControlled) fireHeld = false;
            }
            // The secondary trigger (built-in rockets) has its own cooldown and fires alongside the main gun.
            if (secondaryHeld && IsSecondaryReady) FireSecondaryOnce();

            // Fell out of the world.
            if (!dead && transform.position.y < MetalGameMode.KillY)
            {
                Die(null);
                Explode();
            }
        }
    }
}
