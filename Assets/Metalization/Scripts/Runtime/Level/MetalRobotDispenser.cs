// Robot dispenser: a Villain machine that welds robots together on its pad, either rebuilding the units
// assigned to it when they are destroyed or turning out new ones on a timer, until a console shuts it down.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    public enum MetalDispenserMode
    {
        /// <summary>Rebuilds each assigned unit when it is destroyed; the copy walks back to the original's post.</summary>
        Respawn,
        /// <summary>Builds a robot from spawnChoices every spawnInterval seconds while fewer than maxAlive are standing.</summary>
        Continuous,
    }

    [Serializable]
    public class MetalDispenserChoice
    {
        public MetalBotArchetype archetype;
        [Min(0f)] public float weight = 1f;
    }

    /// <summary>How to rebuild one assigned unit (recorded by the level builder, so it survives the original).</summary>
    [Serializable]
    public class MetalDispenserUnit
    {
        public MetalBotArchetype archetype;
        public List<string> tags = new List<string>();
        public Vector3 post;
        public float patrolRadius;
        public bool overrideTeam;
        public MetalTeam teamOverride = MetalTeam.Villains;
    }

    /// <summary>
    /// Builds robots on its pad. Each takes assemblyTime seconds: it appears part by part (legs and torso, left arm,
    /// right arm, head, then its gun) under a shower of welding sparks with drill and hammer sounds, and only powers on
    /// once finished. A console that lists the dispenser shuts it down for good; a robot half-built then falls apart.
    /// </summary>
    public class MetalRobotDispenser : MonoBehaviour
    {
        static readonly Color SparkColor = new Color(1f, 0.65f, 0.25f);
        static readonly Color ArcColor = new Color(0.55f, 0.8f, 1f);
        static readonly Color WorkingColor = new Color(1f, 0.15f, 0.05f);
        const float CheckInterval = 0.25f;
        /// <summary>A standing robot this close to the pad (2D, m) blocks the next build.</summary>
        const float PadClearRadius = 1.1f;

        public MetalDispenserMode mode = MetalDispenserMode.Respawn;
        [Tooltip("Respawn mode: placed robots this dispenser rebuilds when they are destroyed.")]
        public List<MetalBot> assignedUnits = new List<MetalBot>();
        [Tooltip("What each assigned unit is rebuilt as (filled by RecordAssignedUnits).")]
        public List<MetalDispenserUnit> unitRecords = new List<MetalDispenserUnit>();
        public List<MetalDispenserChoice> spawnChoices = new List<MetalDispenserChoice>();
        [Min(0.5f)] public float spawnInterval = 5f;
        [Min(1)] public int maxAlive = 4;
        public List<string> spawnTags = new List<string>();
        [Tooltip("Continuous mode: patrol radius of its robots (m), centred launchDistance in front of the pad.")]
        public float spawnPatrolRadius = 9f;
        public float launchDistance = 6f;
        [Min(0.5f)] public float assemblyTime = 2f;
        public float respawnDelay = 1.5f;
        public bool startsActive = true;
        [Tooltip("If set, the dispenser idles until this gate starts opening.")]
        public MetalGate activationGate;

        class Part
        {
            public List<string> bones = new List<string>();
            public string weldBone;
            public bool final;
        }

        class Slot
        {
            public MetalBot robot;
            public float rebuildAt = -1f;
        }

        readonly List<Slot> slots = new List<Slot>();
        readonly List<MetalBot> spawned = new List<MetalBot>();
        readonly List<Part> parts = new List<Part>();
        readonly List<Renderer> glowRenderers = new List<Renderer>();
        MetalBot building;
        int buildingSlot = -1;
        bool buildSetupPending;
        float buildStartTime;
        int nextPart;
        int drilledPart = -1;
        float nextSparkTime;
        float nextCheckTime;
        float nextSpawnTime;
        bool active;
        Light statusLight;

        public bool IsDisabled { get; private set; }
        public bool IsWorking => active && !IsDisabled;
        public bool IsAssembling => building != null;

        /// <summary>Copies the assigned units' archetype, tags, post and patrol into unitRecords.</summary>
        public void RecordAssignedUnits()
        {
            unitRecords.Clear();
            foreach (MetalBot unit in assignedUnits)
            {
                var r = new MetalDispenserUnit();
                if (unit != null)
                {
                    r.archetype = unit.archetype;
                    r.tags = new List<string>(unit.tags);
                    r.post = unit.transform.position;
                    r.patrolRadius = unit.patrolRadius;
                    r.overrideTeam = unit.overrideTeam;
                    r.teamOverride = unit.teamOverride;
                }
                unitRecords.Add(r);
            }
        }

        Transform Socket(string socketName)
        {
            Transform t = transform.Find("Model/SOCKET_" + socketName);
            if (t != null) return t;
            foreach (Transform c in GetComponentsInChildren<Transform>())
            {
                if (c.name == "SOCKET_" + socketName) return c;
            }
            return null;
        }

        Vector3 SocketLocation(string socketName, Vector3 fallbackLocal)
        {
            Transform t = Socket(socketName);
            return t != null ? t.position : transform.TransformPoint(fallbackLocal);
        }

        void Awake()
        {
            var lightGo = new GameObject("Status Light");
            lightGo.transform.SetParent(transform, false);
            Transform lightSocket = Socket("light");
            lightGo.transform.position = lightSocket != null ? lightSocket.position : transform.TransformPoint(UE.Vec(90f, 0f, 260f));
            statusLight = lightGo.AddComponent<Light>();
            statusLight.type = LightType.Point;
            statusLight.color = WorkingColor;
            statusLight.range = 5f;
            statusLight.shadows = LightShadows.None;
            foreach (Renderer r in GetComponentsInChildren<Renderer>())
            {
                foreach (Material m in r.sharedMaterials)
                {
                    if (m != null && m.name.Contains("Glow"))
                    {
                        glowRenderers.Add(r);
                        break;
                    }
                }
            }
        }

        void Start()
        {
            if (unitRecords.Count != assignedUnits.Count) RecordAssignedUnits();
            // A checkpoint restore removes units that were already destroyed; their slots then start empty and get
            // rebuilt as soon as the dispenser is working.
            slots.Clear();
            for (int i = 0; i < unitRecords.Count; i++)
            {
                MetalBot unit = i < assignedUnits.Count ? assignedUnits[i] : null;
                slots.Add(new Slot { robot = unit != null && unit.gameObject.activeInHierarchy ? unit : null });
            }
            active = activationGate == null && startsActive;
            nextSpawnTime = Time.time + 1f;
            UpdateGlow();
        }

        void UpdateGlow()
        {
            bool working = IsWorking;
            statusLight.intensity = IsDisabled ? 0f : working ? 3f : 0.8f;
            foreach (Renderer r in glowRenderers)
            {
                var block = new MaterialPropertyBlock();
                r.GetPropertyBlock(block);
                float k = IsDisabled ? 0.03f : working ? 2.5f : 0.6f;
                block.SetColor("_EmissionColor", WorkingColor * k);
                r.SetPropertyBlock(block);
            }
        }

        /// <summary>Shuts the dispenser down for good: no more robots, lights out, a robot on the pad falls apart.</summary>
        public void Disable(bool quiet = false)
        {
            if (IsDisabled) return;
            IsDisabled = true;
            if (building != null)
            {
                if (!quiet)
                {
                    Vector3 at = building.transform.position;
                    MetalFX.SpawnBurst(at, SparkColor, 0.9f, 0.4f);
                    Weld(at, 10);
                    MetalAudio.PlayAt("robot_explode", at, 0.7f);
                }
                Destroy(building.gameObject);
            }
            building = null;
            if (!quiet)
            {
                Vector3 nozzle = SocketLocation("nozzle", UE.Vec(15f, 0f, 260f));
                MetalFX.SpawnBurst(nozzle, WorkingColor, 1.2f, 0.5f);
                MetalAudio.PlayAt("dispenser_shutdown", nozzle, 1f, 0f);
            }
            UpdateGlow();
        }

        bool IsPadClear()
        {
            Vector3 pad = SocketLocation("assembly", UE.Vec(25f, 0f, 30f));
            foreach (MetalBot b in MetalBot.All)
            {
                if (b.IsAlive && b != building && MetalMath.Flat(b.transform.position - pad).magnitude < PadClearRadius + b.Capsule.radius
                    && Mathf.Abs(b.transform.position.y - pad.y) < 3f)
                {
                    return false;
                }
            }
            return true;
        }

        MetalBotArchetype PickChoice()
        {
            float total = 0f;
            foreach (MetalDispenserChoice c in spawnChoices) total += c.archetype != null ? Mathf.Max(0f, c.weight) : 0f;
            float roll = UnityEngine.Random.Range(0f, total);
            foreach (MetalDispenserChoice c in spawnChoices)
            {
                if (c.archetype == null || c.weight <= 0f) continue;
                roll -= c.weight;
                if (roll <= 0f) return c.archetype;
            }
            return null;
        }

        void Update()
        {
            float now = Time.time;
            if (IsDisabled) return;
            if (building != null)
            {
                TickBuild(now);
            }
            else if (buildingSlot >= 0 || parts.Count > 0)
            {
                // The robot on the pad vanished: forget it.
                parts.Clear();
                buildingSlot = -1;
            }
            if (now < nextCheckTime) return;
            nextCheckTime = now + CheckInterval;
            if (!active)
            {
                if (activationGate != null && activationGate.IsOpen)
                {
                    active = true;
                    nextSpawnTime = now + 0.5f;
                    UpdateGlow();
                    MetalAudio.PlayAt("dispenser_hammer", transform.position, 0.8f);
                }
                return;
            }
            if (mode == MetalDispenserMode.Respawn)
            {
                int ready = -1;
                for (int i = 0; i < slots.Count; i++)
                {
                    Slot s = slots[i];
                    if (s.robot != null && s.robot.IsAlive)
                    {
                        s.rebuildAt = -1f;
                        continue;
                    }
                    if (s.rebuildAt < 0f) s.rebuildAt = now + respawnDelay;
                    else if (now >= s.rebuildAt && ready < 0) ready = i;
                }
                if (ready >= 0 && building == null && IsPadClear()) StartBuild(unitRecords[ready].archetype, ready);
            }
            else
            {
                spawned.RemoveAll(r => r == null || !r.IsAlive);
                if (now >= nextSpawnTime && building == null && spawned.Count < maxAlive && IsPadClear())
                {
                    if (StartBuild(PickChoice(), -1)) nextSpawnTime = now + spawnInterval;
                }
            }
        }

        bool StartBuild(MetalBotArchetype archetype, int slot)
        {
            if (archetype == null) return false;
            Vector3 pad = SocketLocation("assembly", UE.Vec(25f, 0f, 30f));
            Vector3 location = pad + Vector3.up * (archetype.capsuleHalfHeight + 0.03f);
            MetalBot robot = SpawnRobot(archetype, slot, location, transform.eulerAngles.y, true);
            if (robot == null) return false;
            building = robot;
            buildingSlot = slot;
            buildStartTime = Time.time;
            buildSetupPending = true;
            nextPart = 0;
            drilledPart = -1;
            nextSparkTime = 0f;
            parts.Clear();
            return true;
        }

        /// <summary>
        /// Once the robot has built its model (its first frame): hide it, and sort the bones off the pelvis into limbs:
        /// legs come with the torso, then each arm, then the head with anything else.
        /// </summary>
        void SetUpParts(MetalBot robot)
        {
            buildSetupPending = false;
            foreach (Renderer r in robot.MeshRoot.GetComponentsInChildren<Renderer>(true)) r.forceRenderingOff = true;
            string pelvis = robot.HasBone("bip_pelvis") ? "bip_pelvis" : robot.Rig.pelvis;
            Transform pelvisBone = robot.GetBone(pelvis);
            var torso = new Part { weldBone = pelvis };
            var armL = new Part();
            var armR = new Part();
            var head = new Part();
            var final = new Part { final = true, weldBone = robot.HasBone("weapon_bone") ? "weapon_bone" : pelvis };
            if (pelvisBone != null)
            {
                foreach (Transform child in pelvisBone)
                {
                    string n = child.name;
                    if (!robot.HasBone(n)) continue;
                    string lower = n.ToLowerInvariant();
                    if (lower.Contains("hip") || lower.Contains("leg") || lower.Contains("thigh")) continue;
                    bool arm = lower.Contains("arm") || lower.Contains("shoulder");
                    Part p = arm && n.EndsWith("_L") ? armL : arm ? armR : head;
                    p.bones.Add(n);
                    if (string.IsNullOrEmpty(p.weldBone) || lower.Contains("head")) p.weldBone = n;
                }
            }
            foreach (Part p in new[] { torso, armL, armR, head, final })
            {
                if (p == torso || p.final || p.bones.Count > 0) parts.Add(p);
            }
            foreach (Part p in parts)
            {
                foreach (string b in p.bones) robot.Animator.SetBoneHidden(b, true);
            }
        }

        MetalBot SpawnRobot(MetalBotArchetype archetype, int slot, Vector3 position, float yaw, bool assembling)
        {
            MetalBot robot = MetalGameMode.SpawnBot(archetype, position, yaw, true, false, "Dispensed " + archetype.displayName);
            if (slot >= 0 && slot < unitRecords.Count)
            {
                MetalDispenserUnit r = unitRecords[slot];
                robot.tags = new List<string>(r.tags);
                robot.patrolRadius = r.patrolRadius;
                robot.overrideTeam = r.overrideTeam;
                robot.teamOverride = r.teamOverride;
            }
            else
            {
                robot.tags = new List<string>(spawnTags);
                robot.patrolRadius = spawnPatrolRadius;
            }
            if (assembling) robot.SetAssembling(true);
            if (slot >= 0 && slot < slots.Count)
            {
                slots[slot].robot = robot;
                slots[slot].rebuildAt = -1f;
            }
            else
            {
                spawned.Add(robot);
            }
            return robot;
        }

        Vector3 PostFor(int slot) => slot >= 0 && slot < unitRecords.Count ? unitRecords[slot].post : transform.position + transform.forward * launchDistance;

        /// <summary>Calls visit for every finished robot it built that is still standing (placed originals skipped).</summary>
        public void ForEachBuiltRobot(Action<MetalBot, int> visit)
        {
            bool Standing(MetalBot r) => r != null && r.IsAlive && r != building && !r.IsAssembling;
            for (int i = 0; i < slots.Count; i++)
            {
                MetalBot r = slots[i].robot;
                bool original = i < assignedUnits.Count && assignedUnits[i] == r;
                if (Standing(r) && !original) visit(r, i);
            }
            foreach (MetalBot r in spawned)
            {
                if (Standing(r)) visit(r, -1);
            }
        }

        /// <summary>Puts back a robot it had built when a checkpoint was saved: finished, in its slot or counted.</summary>
        public MetalBot RestoreBuiltRobot(MetalBotArchetype archetype, int slot, Vector3 position, float yaw)
        {
            if (archetype == null || (slot >= 0 && slot >= slots.Count)) return null;
            MetalBot robot = SpawnRobot(archetype, slot, position, yaw, false);
            StartCoroutine(SetHomeLater(robot, PostFor(slot)));
            return robot;
        }

        static System.Collections.IEnumerator SetHomeLater(MetalBot robot, Vector3 post)
        {
            yield return null;
            if (robot != null && robot.Controller is MetalBotAI ai) ai.SetHome(post);
        }

        void TickBuild(float now)
        {
            MetalBot robot = building;
            if (buildSetupPending)
            {
                if (robot.BodyInstance == null || robot.Animator == null) return;
                SetUpParts(robot);
            }
            float partTime = assemblyTime / Mathf.Max(1, parts.Count);
            float elapsed = now - buildStartTime;
            // Each part is drilled for partTime, then hammered on.
            int working = Mathf.Min(parts.Count - 1, Mathf.FloorToInt(elapsed / partTime));
            if (working > drilledPart && nextPart < parts.Count)
            {
                drilledPart = working;
                MetalAudio.PlayAt("dispenser_drill", robot.transform.position, 0.75f, 0.08f);
            }
            if (nextPart >= parts.Count) return;
            Part part = parts[nextPart];
            Vector3 weldAt = robot.GetBoneLocation(part.weldBone);
            if (now >= nextSparkTime)
            {
                nextSparkTime = now + 0.07f;
                Weld(weldAt, 4);
            }
            if (elapsed < partTime * (nextPart + 1)) return;
            if (nextPart == 0)
            {
                foreach (Renderer r in robot.BodyInstance.GetComponentsInChildren<Renderer>(true)) r.forceRenderingOff = false;
            }
            foreach (string b in part.bones) robot.Animator.SetBoneHidden(b, false);
            MetalFX.SpawnBurst(weldAt, SparkColor, 0.28f, 0.18f);
            Weld(weldAt, 6);
            MetalAudio.PlayAt("dispenser_hammer", weldAt, 0.9f, 0.1f);
            nextPart++;
            if (part.final || nextPart >= parts.Count) FinishBuild();
        }

        void FinishBuild()
        {
            MetalBot robot = building;
            building = null;
            parts.Clear();
            int slot = buildingSlot;
            buildingSlot = -1;
            if (robot == null) return;
            foreach (Renderer r in robot.MeshRoot.GetComponentsInChildren<Renderer>(true)) r.forceRenderingOff = false;
            robot.SetAssembling(false);
            robot.Speak(MetalBark.Chatter);
            // Off the pad and on to its post: the original's for a rebuilt unit, a spot in front of the machine otherwise.
            Vector3 post = PostFor(slot);
            if (robot.Controller is MetalBotAI ai)
            {
                ai.SetHome(post);
                ai.DevMoveTo(post, 0.8f);
            }
        }

        /// <summary>Welding sparks and arcs from the nozzle and the torches to at.</summary>
        void Weld(Vector3 at, int sparks)
        {
            for (int i = 0; i < sparks; i++)
            {
                // Short bright streaks flying out and mostly down, like sparks off a weld.
                Vector3 dir = UnityEngine.Random.onUnitSphere;
                dir.y = dir.y * 0.6f - 0.35f;
                Vector3 from = at + UnityEngine.Random.onUnitSphere * 0.06f;
                MetalFX.SpawnBeam(from, from + dir.normalized * UnityEngine.Random.Range(0.25f, 0.7f), SparkColor, 0.025f, UnityEngine.Random.Range(0.08f, 0.16f));
            }
            MetalFX.SpawnBurst(at, new Color(1f, 0.85f, 0.6f), 0.09f, 0.1f);
            MetalFX.SpawnBeam(SocketLocation("nozzle", UE.Vec(15f, 0f, 260f)), at, ArcColor, 0.02f, 0.06f);
            Transform torch = Socket(UnityEngine.Random.value < 0.5f ? "torch_L" : "torch_R");
            if (torch != null) MetalFX.SpawnBeam(torch.position, at, ArcColor, 0.015f, 0.05f);
        }
    }
}
