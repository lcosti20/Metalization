// The hangar inside a capital ship's interior scene: pads with parked Stalkers players can link into, the AI
// pilots the ship keeps in the fight, and the life-support core enemy raiders try to destroy.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    public class MetalHangar : MonoBehaviour
    {
        [Tooltip("Used when there is no owning ship (the interior scene opened on its own).")]
        public MetalTeam team = MetalTeam.Heroes;
        [Tooltip("Archetype of parked robots and AI pilots. Replaced by the ship data's hangar robot when set.")]
        public MetalBotArchetype robotArchetype;
        [Tooltip("Seconds before a destroyed pad robot or AI pilot is replaced (doubled when the ship loses its engines).")]
        public float respawnDelay = 20f;
        [Tooltip("Room model: needs SOCKET_pad_N children on the floor and a SOCKET_hangar_portal at the mouth.")]
        public GameObject shellModel;
        [Tooltip("Placed on every pad socket; robots spawn standing on its top.")]
        public GameObject padModel;
        [Tooltip("Base of the life-support core, relative to the hangar (m).")]
        public Vector3 lifeSupportLocation = UE.Vec(-4300f, -9300f, 0f);
        [Tooltip("Core diameter and height (m).")]
        public Vector2 lifeSupportSize = new Vector2(12f, 26f);
        public Color lifeSupportColor = new Color(0.3f, 1f, 0.6f);

        const float CheckInterval = 0.5f;

        class Pad
        {
            public Transform transform;
            public MetalBot robot;
            public float respawnAt = -1f;
        }

        class PilotSlot
        {
            public MetalBot robot;
            public float respawnAt = -1f;
        }

        readonly List<Pad> pads = new List<Pad>();
        readonly List<PilotSlot> pilots = new List<PilotSlot>();
        Transform shell;
        float padTop;
        float nextCheckTime;
        bool active;

        public MetalCapitalShip OwningShip { get; private set; }
        public MetalShipSubsystem LifeSupport { get; private set; }

        /// <summary>The hangar mouth (where the interior portal goes).</summary>
        public Transform MouthTransform
        {
            get
            {
                Transform t = FindSocket(shell != null ? shell : transform, "hangar_portal");
                return t != null ? t : transform;
            }
        }

        static Transform FindSocket(Transform root, string socket)
        {
            foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            {
                if (t.name == "SOCKET_" + socket) return t;
            }
            return null;
        }

        void Awake()
        {
            if (shellModel != null)
            {
                GameObject s = Instantiate(shellModel, transform, false);
                s.name = "Shell";
                shell = s.transform;
            }
            for (int i = 1; i <= 16; i++)
            {
                Transform socket = FindSocket(shell != null ? shell : transform, "pad_" + i);
                if (socket == null) break;
                if (padModel != null)
                {
                    GameObject p = Instantiate(padModel, socket, false);
                    p.name = "Pad " + i;
                    Renderer[] rs = p.GetComponentsInChildren<Renderer>();
                    if (rs.Length > 0)
                    {
                        Bounds b = rs[0].bounds;
                        foreach (Renderer r in rs) b.Encapsulate(r.bounds);
                        padTop = Mathf.Max(padTop, b.max.y - socket.position.y);
                    }
                }
                pads.Add(new Pad { transform = socket });
            }
            // Life-support core: a glowing column against the hangar's back wall.
            GameObject core = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
            core.name = "Life Support";
            core.transform.SetParent(transform, false);
            core.transform.localPosition = lifeSupportLocation + Vector3.up * lifeSupportSize.y * 0.5f;
            core.transform.localScale = new Vector3(lifeSupportSize.x, lifeSupportSize.y * 0.5f, lifeSupportSize.x);
            MetalMaterials.SetGlow(core.GetComponent<Renderer>(), lifeSupportColor, 2f);
            LifeSupport = core.AddComponent<MetalShipSubsystem>();
            LifeSupport.type = MetalShipSubsystemType.LifeSupport;
        }

        void Start()
        {
            // Opened on its own (testing the interior scene), nobody will call InitForShip.
            if (OwningShip == null && MetalCapitalShip.All.Count == 0) InitForShip(null);
        }

        /// <summary>
        /// Activates the hangar: takes the ship's team and settings, registers life support with the ship, parks a robot
        /// on every pad and schedules the AI pilots' staggered launches.
        /// </summary>
        public void InitForShip(MetalCapitalShip ship)
        {
            OwningShip = ship;
            if (ship != null)
            {
                team = ship.Team;
                MetalCapitalShipData data = ship.shipData;
                if (data != null)
                {
                    if (data.hangarRobot != null) robotArchetype = data.hangarRobot;
                    respawnDelay = data.hangarRespawnDelay;
                    ship.RegisterSubsystem(LifeSupport, data.lifeSupportHealth);
                }
            }
            active = true;
            foreach (Pad p in pads) SpawnParked(p);
            // Pilots launch a couple of seconds in, staggered, so they don't all leave as one clump.
            pilots.Clear();
            int count = ship != null ? Mathf.Max(0, ship.aiPilots) : 0;
            for (int i = 0; i < count; i++) pilots.Add(new PilotSlot { respawnAt = Time.time + 2f + i * 1.5f });
        }

        void MoveToMyScene(MetalBot robot)
        {
            if (robot != null && robot.gameObject.scene != gameObject.scene)
            {
                UnityEngine.SceneManagement.SceneManager.MoveGameObjectToScene(robot.gameObject, gameObject.scene);
            }
        }

        void SpawnParked(Pad pad)
        {
            pad.respawnAt = -1f;
            if (robotArchetype == null) return;
            // Stand on the pad: its top plus the robot's capsule.
            Vector3 location = pad.transform.position + Vector3.up * (padTop + robotArchetype.capsuleHalfHeight + 0.05f);
            var go = new GameObject("Parked " + robotArchetype.displayName);
            go.SetActive(false);
            go.transform.SetPositionAndRotation(location, Quaternion.Euler(0f, pad.transform.eulerAngles.y, 0f));
            MetalStalker robot = go.AddComponent<MetalStalker>();
            robot.archetype = robotArchetype;
            robot.overrideTeam = true;
            robot.teamOverride = team;
            robot.patrolRadius = 0f;
            robot.startParked = true;
            go.SetActive(true);
            MoveToMyScene(robot);
            pad.robot = robot;
        }

        void LaunchPilot(int slot)
        {
            PilotSlot pilot = pilots[slot];
            pilot.respawnAt = -1f;
            if (robotArchetype == null || pads.Count == 0) return;
            // Spawn hovering 15 m above a pad (clear of the parked robot), already flying.
            Transform pad = pads[slot % pads.Count].transform;
            Vector3 location = pad.position + Vector3.up * (15f + 4f * (slot / pads.Count));
            var go = new GameObject("Pilot " + slot);
            go.SetActive(false);
            go.transform.SetPositionAndRotation(location, Quaternion.Euler(0f, pad.eulerAngles.y, 0f));
            MetalStalker robot = go.AddComponent<MetalStalker>();
            robot.archetype = robotArchetype;
            robot.overrideTeam = true;
            robot.teamOverride = team;
            robot.startParked = false;
            go.SetActive(true);
            MoveToMyScene(robot);
            MetalFlyerAI brain = go.AddComponent<MetalFlyerAI>();
            // Alternate jobs: odd slots raid the enemy ship, even slots fly cover.
            brain.pilotRole = slot % 2 == 1 ? MetalPilotRole.Raider : MetalPilotRole.Fighter;
            brain.Possess(robot);
            pilot.robot = robot;
        }

        public MetalBot GetParkedRobot(int padIndex) => padIndex >= 0 && padIndex < pads.Count ? pads[padIndex].robot : null;
        public int PadCount => pads.Count;

        public int ActivePilots
        {
            get
            {
                int n = 0;
                foreach (PilotSlot p in pilots) n += p.robot != null && p.robot.IsAlive ? 1 : 0;
                return n;
            }
        }

        void Update()
        {
            float now = Time.time;
            if (!active || now < nextCheckTime) return;
            nextCheckTime = now + CheckInterval;
            // Same pattern for pilots and pads: a dead or missing robot schedules a replacement, spawned later.
            for (int i = 0; i < pilots.Count; i++)
            {
                PilotSlot p = pilots[i];
                if (p.robot != null && p.robot.IsAlive) continue;
                if (p.respawnAt < 0f) p.respawnAt = now + respawnDelay;
                else if (now >= p.respawnAt) LaunchPilot(i);
            }
            foreach (Pad pad in pads)
            {
                if (pad.robot != null && pad.robot.IsAlive) continue;
                if (pad.respawnAt < 0f) pad.respawnAt = now + respawnDelay;
                else if (now >= pad.respawnAt) SpawnParked(pad);
            }
        }
    }
}
