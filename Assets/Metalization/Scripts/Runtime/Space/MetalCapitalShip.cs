// Capital ships for the space battles. One per team: a big hull assembled from the modular graybox models
// (MetalCapitalShipData), destructible subsystems, AI turrets, and an interior scene loaded additively far
// above the battle, so both teams can share one interior scene.
using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Metalization
{
    public enum MetalShipSubsystemType
    {
        /// <summary>Two per ship; while either stands the hull takes reduced damage.</summary>
        ShieldGenerator,
        /// <summary>Losing them doubles the hangar's robot respawn delay.</summary>
        Engines,
        Comms,
        /// <summary>Lives in the interior hangar, reached by flying in through the hangar mouth.</summary>
        LifeSupport,
    }

    /// <summary>
    /// One team's capital ship. Builds itself from MetalCapitalShipData (hull, four subsystems, socketed turrets), loads
    /// its interior scene at interiorLocation, links hull and interior through a pair of portals, runs its turrets and
    /// tracks hull and subsystem damage.
    /// </summary>
    public class MetalCapitalShip : MonoBehaviour, IMetalDamageable
    {
        public static readonly List<MetalCapitalShip> All = new List<MetalCapitalShip>();
        public static event Action<MetalCapitalShip> AnyInteriorReady;
        public static event Action<MetalCapitalShip> AnyShipDestroyed;
        public static event Action<MetalCapitalShip, MetalShipSubsystem> AnySubsystemDestroyed;

        /// <summary>Ship interiors sit this high (m) above the battle plane.</summary>
        public const float InteriorAltitude = 20000f;

        static readonly Color Burnt = new Color(0.03f, 0.03f, 0.035f);
        static readonly Color Fire = new Color(1f, 0.45f, 0.1f);

        public MetalCapitalShipData shipData;
        public MetalTeam team = MetalTeam.Heroes;
        [Tooltip("Where this ship's interior instance is placed. Keep every ship's interior well apart.")]
        public Vector3 interiorLocation = new Vector3(0f, InteriorAltitude, 0f);
        [Tooltip("AI-piloted Stalkers this ship's hangar keeps in the fight.")]
        public int aiPilots = 3;

        public MetalTeam Team => team;
        public float HullHealth { get; private set; }
        public float maxHullHealth = 20000f;
        public bool IsShipDestroyed { get; private set; }
        public bool IsInteriorReady { get; private set; }
        public Vector3 InteriorLocation => interiorLocation;
        public MetalHangar Hangar { get; private set; }
        public MetalPortal OuterPortal { get; private set; }
        public MetalPortal InnerPortal { get; private set; }
        public IReadOnlyList<MetalShipSubsystem> Subsystems => subsystems;
        public Scene InteriorScene { get; private set; }

        readonly List<MetalShipSubsystem> subsystems = new List<MetalShipSubsystem>();
        readonly Dictionary<Renderer, float> hitFlash = new Dictionary<Renderer, float>();
        readonly List<Turret> turrets = new List<Turret>();
        Transform hull;
        float nextTurretThinkTime;
        float destroyedTime = -1f;
        int wreckBursts;
        MaterialPropertyBlock block;

        class Turret
        {
            public Transform mount;
            public Transform gun;
            public float yaw, pitch;
            public float nextFireTime;
            public MetalBot target;
        }

        public bool IsShielded
        {
            get
            {
                foreach (MetalShipSubsystem s in subsystems)
                {
                    if (s.type == MetalShipSubsystemType.ShieldGenerator && !s.IsDestroyed) return true;
                }
                return false;
            }
        }

        void OnEnable() => All.Add(this);
        void OnDisable() => All.Remove(this);

        static Transform FindSocket(Transform root, string socket)
        {
            foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            {
                if (t.name == "SOCKET_" + socket) return t;
            }
            return null;
        }

        GameObject Mount(GameObject prefab, Transform parent, string objectName)
        {
            if (prefab == null || parent == null) return null;
            GameObject go = Instantiate(prefab, parent, false);
            go.name = objectName;
            return go;
        }

        MetalShipSubsystem MakeSubsystem(GameObject prefab, string socket, MetalShipSubsystemType type, float health, string objectName)
        {
            Transform s = FindSocket(hull, socket);
            if (s == null || prefab == null) return null;
            GameObject go = Mount(prefab, s, objectName);
            MetalShipSubsystem sub = go.AddComponent<MetalShipSubsystem>();
            sub.type = type;
            sub.Ship = this;
            sub.ResetHealth(health);
            subsystems.Add(sub);
            return sub;
        }

        void Awake()
        {
            block = new MaterialPropertyBlock();
            if (shipData == null)
            {
                Debug.LogError($"[Metal] {name} has no ship data.", this);
                return;
            }
            maxHullHealth = shipData.hullHealth;
            HullHealth = maxHullHealth;
            GameObject h = Mount(shipData.hullModel, transform, "Hull");
            hull = h != null ? h.transform : transform;
            MakeSubsystem(shipData.enginesModel, "engines", MetalShipSubsystemType.Engines, shipData.enginesHealth, "Engines");
            MakeSubsystem(shipData.shieldGeneratorModel, "shieldgen_L", MetalShipSubsystemType.ShieldGenerator, shipData.shieldGeneratorHealth, "Shield Generator L");
            MakeSubsystem(shipData.shieldGeneratorModel, "shieldgen_R", MetalShipSubsystemType.ShieldGenerator, shipData.shieldGeneratorHealth, "Shield Generator R");
            MakeSubsystem(shipData.commsModel, "comms", MetalShipSubsystemType.Comms, shipData.commsHealth, "Comms");
            // One turret per turret_NN socket on the hull, the gun on top of the base's "gun" socket.
            for (int i = 1; i <= 32; i++)
            {
                Transform socket = FindSocket(hull, $"turret_{i:00}");
                if (socket == null) break;
                GameObject b = Mount(shipData.turretBaseModel, socket, "Turret " + i);
                if (b == null) break;
                Transform gunSocket = FindSocket(b.transform, "gun");
                var pivot = new GameObject("Gun Pivot").transform;
                pivot.SetParent(gunSocket != null ? gunSocket : b.transform, false);
                Mount(shipData.turretGunModel, pivot, "Gun");
                turrets.Add(new Turret { mount = socket, gun = pivot });
            }
            ApplyTeamColors();
        }

        void Start()
        {
            LoadInterior();
        }

        void ApplyTeamColors()
        {
            MetalTeamColors colors = null;
            foreach (MetalTeamColors c in shipData.teamColors)
            {
                if (c.team == team) colors = c;
            }
            if (colors == null) return;
            foreach (Renderer r in GetComponentsInChildren<Renderer>())
            {
                Material[] mats = r.sharedMaterials;
                for (int i = 0; i < mats.Length; i++)
                {
                    if (mats[i] == null) continue;
                    r.GetPropertyBlock(block, i);
                    if (mats[i].name.EndsWith("_Hull")) block.SetColor("_BaseColor", colors.body);
                    else if (mats[i].name.EndsWith("_Accent")) block.SetColor("_BaseColor", colors.accent);
                    r.SetPropertyBlock(block, i);
                }
            }
        }

        // --- Interior ---------------------------------------------------------------------------------------

        void LoadInterior()
        {
            if (string.IsNullOrEmpty(shipData.interiorScene))
            {
                Debug.LogWarning($"[Metal] {name} has no interior scene; nobody can spawn aboard.");
                return;
            }
            StartCoroutine(LoadInteriorRoutine());
        }

        /// <summary>
        /// Each ship loads its own copy of the interior scene additively and moves it to interiorLocation, so two ships
        /// can share one interior asset.
        /// </summary>
        IEnumerator LoadInteriorRoutine()
        {
            int before = SceneManager.sceneCount;
            AsyncOperation op = SceneManager.LoadSceneAsync(shipData.interiorScene, LoadSceneMode.Additive);
            if (op == null)
            {
                Debug.LogError($"[Metal] {name} could not load its interior {shipData.interiorScene} (is it in the build profile?).");
                yield break;
            }
            yield return op;
            // Both ships' loads can report done in the same frame while the second copy is still loading: claim a copy
            // only once it has finished loading.
            Scene loaded = default;
            float giveUpAt = Time.realtimeSinceStartup + 15f;
            while (!loaded.IsValid())
            {
                for (int i = SceneManager.sceneCount - 1; i >= 0; i--)
                {
                    Scene s = SceneManager.GetSceneAt(i);
                    if (s.name == shipData.interiorScene && s.isLoaded && !MetalCapitalShip.IsClaimed(s))
                    {
                        loaded = s;
                        break;
                    }
                }
                if (loaded.IsValid()) break;
                if (Time.realtimeSinceStartup > giveUpAt)
                {
                    Debug.LogError($"[Metal] {name}: interior scene did not appear ({before} scenes before).");
                    yield break;
                }
                yield return null;
            }
            InteriorScene = loaded;
            foreach (GameObject root in loaded.GetRootGameObjects())
            {
                root.transform.position += interiorLocation;
                // The interior's own lights and cameras would fight the battle scene's.
                foreach (Camera c in root.GetComponentsInChildren<Camera>()) c.gameObject.SetActive(false);
                foreach (AudioListener l in root.GetComponentsInChildren<AudioListener>()) Destroy(l);
                foreach (MetalGameMode g in root.GetComponentsInChildren<MetalGameMode>()) Destroy(g.gameObject);
                if (Hangar == null) Hangar = root.GetComponentInChildren<MetalHangar>();
            }
            // Moved navmesh surfaces have to be rebuilt where they are now.
            foreach (GameObject root in loaded.GetRootGameObjects())
            {
                foreach (Unity.AI.Navigation.NavMeshSurface surface in root.GetComponentsInChildren<Unity.AI.Navigation.NavMeshSurface>())
                {
                    surface.BuildNavMesh();
                }
            }
            if (Hangar != null) Hangar.InitForShip(this);
            else Debug.LogWarning($"[Metal] {name}: interior {shipData.interiorScene} has no MetalHangar.");
            CreatePortals();
            IsInteriorReady = true;
            AnyInteriorReady?.Invoke(this);
        }

        static bool IsClaimed(Scene s)
        {
            foreach (MetalCapitalShip ship in All)
            {
                if (ship.InteriorScene.IsValid() && ship.InteriorScene.handle == s.handle) return true;
            }
            return false;
        }

        /// <summary>True if the object belongs to this ship's interior scene.</summary>
        public bool IsInInterior(GameObject go) => go != null && InteriorScene.IsValid() && go.scene.handle == InteriorScene.handle;

        void CreatePortals()
        {
            Transform socket = FindSocket(hull, "hangar_portal");
            if (Hangar == null || socket == null)
            {
                Debug.LogWarning($"[Metal] {name}: no hangar portal (hangar {(Hangar != null ? Hangar.name : "missing")}, hull socket {socket != null})");
                return;
            }
            // The hull end rides along with the ship; the interior end stays put in the interior.
            var outerGo = new GameObject("Hangar Portal (hull)");
            outerGo.transform.SetParent(socket, false);
            MetalPortal outer = outerGo.AddComponent<MetalPortal>();
            var innerGo = new GameObject("Hangar Portal (interior)");
            Transform mouth = Hangar.MouthTransform;
            innerGo.transform.SetPositionAndRotation(mouth.position, mouth.rotation);
            SceneManager.MoveGameObjectToScene(innerGo, InteriorScene);
            MetalPortal inner = innerGo.AddComponent<MetalPortal>();
            Vector2 size = shipData.hangarMouthSize;
            // Open space is in front of the hull end; the hangar is behind the interior end.
            outer.Link(inner, 1f, size);
            inner.Link(outer, -1f, size);
            // Looking out of the hangar, the view starts inside our own hull: don't draw it.
            inner.HideFromCapture(gameObject);
            OuterPortal = outer;
            InnerPortal = inner;
        }

        /// <summary>Where this ship's players (re)spawn: the player starts in its interior.</summary>
        public List<Transform> GetSpawnPoints()
        {
            var list = new List<Transform>();
            if (!InteriorScene.IsValid()) return list;
            foreach (GameObject root in InteriorScene.GetRootGameObjects())
            {
                foreach (MetalPlayerStart s in root.GetComponentsInChildren<MetalPlayerStart>()) list.Add(s.transform);
            }
            return list;
        }

        // --- Damage -----------------------------------------------------------------------------------------

        MetalBot FindAttacker(MetalDamageEvent e)
        {
            MetalBot attacker = e.causer != null ? e.causer.GetComponent<MetalBot>() : null;
            if (attacker == null && e.causer != null)
            {
                MetalProjectile p = e.causer.GetComponent<MetalProjectile>();
                if (p != null) attacker = p.InstigatorBot;
            }
            if (attacker == null && e.instigator != null) attacker = e.instigator.Pawn;
            return attacker;
        }

        bool IsFriendly(MetalDamageEvent e)
        {
            MetalBot attacker = FindAttacker(e);
            if (attacker != null && attacker.Team == team) return true;
            MetalCapitalShip ship = e.causerOwner != null ? e.causerOwner.GetComponent<MetalCapitalShip>() : null;
            return ship != null && ship.team == team;
        }

        /// <summary>Hull hits (subsystems route their own hits through ApplySubsystemDamage), scaled down while shielded.</summary>
        public float TakeDamage(MetalDamageEvent e)
        {
            if (IsShipDestroyed || e.amount <= 0f || IsFriendly(e)) return 0f;
            float damage = e.amount * (IsShielded ? shipData.shieldedHullDamageScale : 1f);
            HullHealth = Mathf.Max(0f, HullHealth - damage);
            Flash(e.hitCollider != null ? e.hitCollider.GetComponentInParent<Renderer>() : null);
            if (HullHealth <= 0f) DestroyShip();
            return damage;
        }

        /// <summary>Adds a subsystem that lives outside the ship (the interior's life support) and resets its health.</summary>
        public void RegisterSubsystem(MetalShipSubsystem s, float health)
        {
            if (s == null) return;
            s.Ship = this;
            s.ResetHealth(health);
            if (!subsystems.Contains(s)) subsystems.Add(s);
        }

        public float ApplySubsystemDamage(MetalShipSubsystem s, MetalDamageEvent e)
        {
            if (IsShipDestroyed || s == null || s.IsDestroyed || e.amount <= 0f || IsFriendly(e)) return 0f;
            foreach (Renderer r in s.GetComponentsInChildren<Renderer>()) Flash(r);
            if (s.ApplyDamage(e.amount)) DestroySubsystem(s);
            return e.amount;
        }

        void Flash(Renderer r)
        {
            if (r != null) hitFlash[r] = 1f;
        }

        void DestroySubsystem(MetalShipSubsystem s)
        {
            // Burnt out: blackened.
            foreach (Renderer r in s.GetComponentsInChildren<Renderer>())
            {
                hitFlash.Remove(r);
                for (int i = 0; i < r.sharedMaterials.Length; i++)
                {
                    r.GetPropertyBlock(block, i);
                    block.SetColor("_BaseColor", Burnt);
                    block.SetColor("_EmissionColor", new Color(0.25f, 0.02f, 0f));
                    r.SetPropertyBlock(block, i);
                }
            }
            MetalFX.SpawnExplosion(s.Center, Fire, s.Radius * 1.2f, 1.2f);
            Debug.Log($"[Metal] {name} lost subsystem {s.name}");
            // Without engines the hangar crews work slower: replacement robots take twice as long.
            if (Hangar != null && s.type == MetalShipSubsystemType.Engines) Hangar.respawnDelay *= 2f;
            AnySubsystemDestroyed?.Invoke(this, s);
        }

        void DestroyShip()
        {
            if (IsShipDestroyed) return;
            IsShipDestroyed = true;
            destroyedTime = Time.time;
            Debug.Log($"[Metal] {name} destroyed");
            AnyShipDestroyed?.Invoke(this);
        }

        // --- Turrets ----------------------------------------------------------------------------------------

        void TickTurrets(float dt)
        {
            MetalWeaponData weapon = shipData != null ? shipData.turretWeapon : null;
            if (weapon == null || IsShipDestroyed || turrets.Count == 0) return;
            float now = Time.time;
            // Pick targets a few times a second: the nearest enemy flyer each gun can see.
            if (now >= nextTurretThinkTime)
            {
                nextTurretThinkTime = now + 0.3f;
                var enemies = new List<MetalStalker>();
                foreach (MetalBot b in MetalBot.All)
                {
                    if (b is MetalStalker s && s.IsAlive && !s.IsParked && s.Controller != null && s.Team != team && s.Team != MetalTeam.Neutral
                        && (s.transform.position - transform.position).sqrMagnitude < (shipData.turretRange + 300f) * (shipData.turretRange + 300f))
                    {
                        enemies.Add(s);
                    }
                }
                foreach (Turret t in turrets)
                {
                    Vector3 gun = t.gun.position;
                    Vector3 up = t.mount.up;
                    MetalStalker best = null;
                    float bestDist = shipData.turretRange;
                    foreach (MetalStalker e in enemies)
                    {
                        Vector3 to = e.transform.position - gun;
                        float d = to.magnitude;
                        // Above the turret's horizon, in range, and not behind our own hull.
                        if (d >= bestDist || Vector3.Dot(to / d, up) < -0.05f) continue;
                        if (MetalPhysics.Raycast(gun + up * 6f, e.transform.position, out _, e)) continue;
                        best = e;
                        bestDist = d;
                    }
                    t.target = best;
                }
            }
            foreach (Turret t in turrets)
            {
                float desiredYaw = 0f, desiredPitch = 0f;
                Vector3 aimWorld = Vector3.zero;
                MetalBot target = t.target;
                if (target != null)
                {
                    Vector3 gun = t.gun.position;
                    float flight = Vector3.Distance(gun, target.transform.position) / Mathf.Max(10f, weapon.projectileSpeed);
                    aimWorld = target.transform.position + target.Velocity * flight;
                    // The lead point in the mount's frame, so yaw and pitch are relative to how the turret is mounted.
                    Vector3 local = t.mount.InverseTransformDirection(aimWorld - gun);
                    desiredYaw = Mathf.Atan2(local.x, local.z) * Mathf.Rad2Deg;
                    desiredPitch = Mathf.Clamp(Mathf.Atan2(local.y, new Vector2(local.x, local.z).magnitude) * Mathf.Rad2Deg, -5f, 85f);
                }
                float step = shipData.turretTurnRate * dt;
                t.yaw += Mathf.Clamp(Mathf.DeltaAngle(t.yaw, desiredYaw), -step, step);
                t.pitch += Mathf.Clamp(desiredPitch - t.pitch, -step, step);
                t.gun.localRotation = Quaternion.Euler(-t.pitch, t.yaw, 0f);
                // Fire once the barrels are on the lead point.
                if (target != null && now >= t.nextFireTime)
                {
                    Vector3 barrel = t.gun.forward;
                    Vector3 muzzle = t.gun.position + barrel * 24f;
                    if (Vector3.Dot(barrel, (aimWorld - muzzle).normalized) > Mathf.Cos(6f * Mathf.Deg2Rad))
                    {
                        MetalWeaponLevel stats = weapon.GetLevel(0);
                        t.nextFireTime = now + stats.fireInterval * UnityEngine.Random.Range(0.85f, 1.15f);
                        Vector3 dir = MetalMath.RandomCone(barrel, (weapon.spreadDegrees + shipData.turretAimError) * Mathf.Deg2Rad);
                        MetalProjectile.Spawn(muzzle, dir, null, gameObject, weapon, stats.damage, dir * weapon.projectileSpeed);
                        MetalFX.SpawnMuzzleFlash(muzzle, dir, weapon.tracerColor, 1.5f);
                    }
                }
            }
        }

        void Update()
        {
            if (shipData == null) return;
            float dt = Time.deltaTime;
            TickTurrets(dt);
            foreach (Renderer r in new List<Renderer>(hitFlash.Keys))
            {
                float v = Mathf.Max(0f, hitFlash[r] - dt * 5f);
                if (r != null)
                {
                    for (int i = 0; i < r.sharedMaterials.Length; i++)
                    {
                        r.GetPropertyBlock(block, i);
                        block.SetColor("_EmissionColor", new Color(2f, 0.8f, 0.3f) * v * 0.6f);
                        r.SetPropertyBlock(block, i);
                    }
                }
                if (v <= 0f || r == null) hitFlash.Remove(r);
                else hitFlash[r] = v;
            }
            // A destroyed ship breaks up in a chain of explosions along the hull.
            if (IsShipDestroyed && wreckBursts < 24 && Time.time - destroyedTime > wreckBursts * 0.25f)
            {
                Bounds box = new Bounds(transform.position, Vector3.one * 100f);
                Renderer[] rs = hull.GetComponentsInChildren<Renderer>();
                if (rs.Length > 0)
                {
                    box = rs[0].bounds;
                    foreach (Renderer r in rs) box.Encapsulate(r.bounds);
                }
                Vector3 p = new Vector3(UnityEngine.Random.Range(box.min.x, box.max.x), UnityEngine.Random.Range(box.min.y, box.max.y), UnityEngine.Random.Range(box.min.z, box.max.z));
                MetalFX.SpawnExplosion(p, Fire, 25f + 20f * UnityEngine.Random.value, 1.5f);
                wreckBursts++;
            }
        }
    }
}
