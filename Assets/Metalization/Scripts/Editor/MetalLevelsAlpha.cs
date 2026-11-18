// Production Alpha levels (ue_setup_alpha.py, ue_setup_dispenser.py): OPERATION REDLINE, the title screen and the
// robot dispenser test. Same Unreal numbers (cm, X forward / Y right / Z up) as the original scripts.
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using static Metalization.Editor.MetalLevelWriter;

namespace Metalization.Editor
{
    public static partial class MetalLevels
    {
        // --- Alpha placement helpers (sizes and positions in cm) ------------------------------------------------

        static GameObject Block(MetalLevelWriter L, string label, float x0, float x1, float y0, float y1, float z0, float z1,
            string material = "MI_Alpha_Concrete", string folder = "Geometry", bool invisible = false)
            => L.Box(label, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), material, folder, invisibleWall: invisible);

        static GameObject Ground(MetalLevelWriter L, string label, float x0, float x1, float y0, float y1, float top, string material = "MI_Alpha_Ground", float thickness = 60)
            => Block(L, label, x0, x1, y0, y1, top - thickness, top, material);

        /// <summary>Walkable slab rising from (x0, z0) to (x1, z1) along X.</summary>
        static GameObject RampX(MetalLevelWriter L, string label, float x0, float z0, float x1, float z1, float y, float width, string material = "MI_Alpha_Concrete")
        {
            float dx = x1 - x0, dz = z1 - z0;
            float length = Mathf.Sqrt(dx * dx + dz * dz);
            float pitch = Mathf.Atan2(dz, dx) * Mathf.Rad2Deg;
            return Rotate(L.Box(label, ((x0 + x1) / 2, y, (z0 + z1) / 2 - 20), (length + 40, width, 40), material), pitch, 0f, 0f);
        }

        /// <summary>Walkable slab rising from (y0, z0) to (y1, z1) along Y.</summary>
        static GameObject RampY(MetalLevelWriter L, string label, float y0, float z0, float y1, float z1, float x, float width, string material = "MI_Alpha_Concrete")
        {
            float dy = y1 - y0, dz = z1 - z0;
            float length = Mathf.Sqrt(dy * dy + dz * dz);
            float pitch = Mathf.Atan2(dz, Mathf.Abs(dy)) * Mathf.Rad2Deg;
            return Rotate(L.Box(label, (x, (y0 + y1) / 2, (z0 + z1) / 2 - 20), (length + 40, width, 40), material), pitch, dy > 0 ? 90f : -90f, 0f);
        }

        static GameObject ACrate(MetalLevelWriter L, string label, float x, float y, float baseZ, (float x, float y, float z)? size = null, string material = "MI_Alpha_Concrete")
        {
            (float x, float y, float z) s = size ?? (160, 160, 130);
            return L.Box(label, (x, y, baseZ + s.z / 2), s, material, "Cover");
        }

        static GameObject APillar(MetalLevelWriter L, string label, float x, float y, float baseZ, float height, float radius, string material = "MI_Alpha_Rock")
            => L.Box(label, (x, y, baseZ + height / 2), (radius * 2, radius * 2, height), material, "Geometry", cylinder: true);

        static void Banner(MetalLevelWriter L, string label, float x, float y, float baseZ, string faction = "Blue", float height = 600)
        {
            bool blue = faction == "Blue";
            APillar(L, label + "_Pole", x, y, baseZ, height, 12, "MI_Env_Dark");
            L.Box(label + "_Flag", (x, y + 70, baseZ + height - 130), (10, 130, 220), blue ? "MI_Alpha_Blue" : "MI_Alpha_Red", "Decor");
            L.Box(label + "_Cap", (x, y, baseZ + height + 10), (40, 40, 20), blue ? "MI_Alpha_BlueGlow" : "MI_Alpha_RedGlow", "Decor");
        }

        static void Sandbags(MetalLevelWriter L, string label, float x, float y0, float y1, float baseZ, float height = 95, string material = "MI_Alpha_Blue")
        {
            Block(L, label, x - 40, x + 40, y0, y1, baseZ, baseZ + height, "MI_Alpha_Ground", "Cover");
            Block(L, label + "_Trim", x - 42, x + 42, y0, y1, baseZ + height, baseZ + height + 8, material, "Decor");
        }

        static void Tent(MetalLevelWriter L, string label, float x, float y, float baseZ, string faction = "Blue")
        {
            bool blue = faction == "Blue";
            Block(L, label, x - 220, x + 220, y - 160, y + 160, baseZ, baseZ + 260, "MI_Alpha_Concrete", "Decor");
            Block(L, label + "_Roof", x - 240, x + 240, y - 180, y + 180, baseZ + 260, baseZ + 290, blue ? "MI_Alpha_Blue" : "MI_Alpha_Red", "Decor");
            Block(L, label + "_Stripe", x - 222, x + 222, y - 162, y + 162, baseZ + 120, baseZ + 140, blue ? "MI_Alpha_BlueGlow" : "MI_Alpha_RedGlow", "Decor");
        }

        /// <summary>Square tower with a parapet; a marksman stands on top.</summary>
        static void Watchtower(MetalLevelWriter L, string label, float x, float y, float baseZ, float top, string faction = "Red")
        {
            bool red = faction == "Red";
            Block(L, label, x - 150, x + 150, y - 150, y + 150, baseZ, top, "MI_Alpha_Concrete");
            var parapets = new[] { (-170f, 170f, -170f, -150f), (-170f, 170f, 150f, 170f), (-170f, -150f, -150f, 150f), (150f, 170f, -150f, 150f) };
            for (int i = 0; i < parapets.Length; i++)
            {
                var (a, b, c, d) = parapets[i];
                Block(L, $"{label}_Parapet{i}", x + a, x + b, y + c, y + d, top, top + 60, red ? "MI_Alpha_Red" : "MI_Alpha_Blue");
            }
            L.Box(label + "_Lamp", (x, y, top + 140), (30, 30, 30), red ? "MI_Alpha_RedGlow" : "MI_Alpha_BlueGlow", "Decor");
        }

        static Light SpotLight(MetalLevelWriter L, string label, (float x, float y, float z) loc, (float r, float g, float b) rgb, float intensity, float cone, float radius)
        {
            GameObject go = L.Spawn(label, P(loc.x, loc.y, loc.z), 0f, "Lighting");
            go.transform.rotation = Quaternion.Euler(90f, 0f, 0f);
            Light l = go.AddComponent<Light>();
            l.type = LightType.Spot;
            l.color = new Color(rgb.r, rgb.g, rgb.b);
            l.spotAngle = cone * 2f;
            l.innerSpotAngle = cone * 1.4f;
            l.range = radius * UE.CmToM;
            l.intensity = Mathf.Clamp(intensity / 600f, 1f, 30f);
            l.shadows = LightShadows.Soft;
            return l;
        }

        static MetalBot ABot(MetalLevelWriter L, string label, MetalBotArchetype a, (float x, float y, float z) loc, float yaw, string[] tags = null,
            float patrol = 500f, bool passive = false, MetalTeam? team = null, bool docile = false)
        {
            MetalBot b = L.Bot(label, a, loc, yaw, tags, patrol, passive, team);
            b.docileUntilAttacked = docile;
            return b;
        }

        static MetalGravLift Lift(MetalLevelWriter L, string label, (float x, float y, float z) pad, float height, (float x, float y, float z) exit)
            => L.GravLift(label, pad, 0f, height, 140f, exit, folder: "Lifts");

        static GameObject Prefab(MetalLevelWriter L, string part, string label, Vector3 position, float yaw, string folder)
        {
            GameObject prefab = MetalAssetPipeline.ShipPrefab(part);
            if (prefab == null)
            {
                Debug.LogWarning($"[MZSetup] missing prefab {part} (run Build Assets)");
                return L.Spawn(label, position, yaw, folder);
            }
            var go = (GameObject)PrefabUtility.InstantiatePrefab(prefab, L.Folder(folder));
            go.name = label;
            go.transform.SetPositionAndRotation(position, Quaternion.Euler(0f, yaw, 0f));
            return go;
        }

        /// <summary>
        /// Robot dispenser. With assigned robots it rebuilds them when destroyed (Respawn); with choices it builds a random
        /// one every interval while fewer than maxAlive stand (Continuous). Gate: idle until that gate opens.
        /// </summary>
        static MetalRobotDispenser Dispenser(MetalLevelWriter L, string label, (float x, float y, float z) loc, float yaw, MetalBot[] assigned = null,
            (MetalBotArchetype a, float w)[] choices = null, float interval = 5f, int maxAlive = 4, MetalGate gate = null, string[] tags = null, float patrol = 900f)
        {
            GameObject go = Prefab(L, "RobotDispenser", label, P(loc.x, loc.y, loc.z), yaw, "Dispensers");
            MetalRobotDispenser d = go.AddComponent<MetalRobotDispenser>();
            if (assigned != null && assigned.Length > 0)
            {
                d.mode = MetalDispenserMode.Respawn;
                d.assignedUnits = new List<MetalBot>(assigned);
                d.RecordAssignedUnits();
            }
            else
            {
                d.mode = MetalDispenserMode.Continuous;
                d.spawnInterval = interval;
                d.maxAlive = maxAlive;
                d.spawnPatrolRadius = patrol * UE.CmToM;
                d.spawnTags = new List<string>(tags ?? new string[0]);
                d.spawnChoices = new List<MetalDispenserChoice>();
                foreach (var (a, w) in choices ?? new (MetalBotArchetype, float)[0]) d.spawnChoices.Add(new MetalDispenserChoice { archetype = a, weight = w });
            }
            d.activationGate = gate;
            go.AddComponent<MetalPlaced>().id = label;
            return d;
        }

        /// <summary>Console anyone (the player included) can use to shut the dispensers down. Blue screen.</summary>
        static MetalControlConsole DispenserConsole(MetalLevelWriter L, string label, (float x, float y, float z) loc, float yaw, params MetalRobotDispenser[] dispensers)
        {
            MetalControlConsole c = L.Console(label, loc, yaw, "Dispensers");
            bool one = dispensers.Length == 1;
            c.requiredFaction = MetalTeam.Neutral;
            c.linkedDispensers = new List<MetalRobotDispenser>(dispensers);
            c.lockedColor = new Color(0.1f, 0.45f, 1f);
            c.promptText = one ? "[E] Shut down the robot dispenser" : "[E] Shut down the robot dispensers";
            c.grantedText = one ? "DISPENSER OFFLINE - production halted." : "DISPENSERS OFFLINE - production halted.";
            c.doneText = "Already offline.";
            return c;
        }

        static MetalDialogueLine Cmd(string text, float duration = 5f) => Line("Command", text, duration);

        static MetalObjective Obj(string text, MetalObjectiveType type, string tag = null, Component target = null, string hint = "", MetalDialogueLine[] dialogue = null,
            MetalDialogueLine[] done = null, bool withNext = false, string id = null, string[] requires = null, bool checkpoint = false)
        {
            MetalObjective o = Objective(text, type, tag, target, dialogue ?? new MetalDialogueLine[0]);
            o.hint = hint;
            o.completeDialogue = new List<MetalDialogueLine>(done ?? new MetalDialogueLine[0]);
            o.withNext = withNext;
            o.id = id ?? "";
            o.requires = new List<string>(requires ?? new string[0]);
            o.checkpointOnComplete = checkpoint;
            return o;
        }

        /// <summary>Late-afternoon sun over a dusty valley, warm haze.</summary>
        static void AlphaEnvironment(MetalLevelWriter L, float sunPitch = -40f, float sunYaw = -145f, (float r, float g, float b)? sunRgb = null, float fog = 0.008f)
        {
            (float r, float g, float b) s = sunRgb ?? (255, 205, 160);
            L.Sun(sunPitch, sunYaw, new Color(s.r / 255f, s.g / 255f, s.b / 255f), 1.8f);
            L.Atmosphere(new Color(0.62f, 0.55f, 0.5f), new Color(0.55f, 0.45f, 0.36f), new Color(0.3f, 0.24f, 0.18f), fog * 0.5f, new Color(0.62f, 0.48f, 0.34f));
            L.PostProcess(0f, -5f, 0.5f, 0.5f);
        }

        /// <summary>A copy of an archetype with a few changes (DA_Bot_name).</summary>
        static MetalBotArchetype Variant(string name, MetalBotArchetype from, System.Action<MetalBotArchetype> edit)
        {
            MetalBotArchetype a = MetalAssetPipeline.DataAsset<MetalBotArchetype>("DA_Bot_" + name);
            MetalAssetPipeline.CopyKeepingName(from, a);
            edit(a);
            EditorUtility.SetDirty(a);
            return a;
        }

        // ==================================================================================================
        // MZ_ProductionAlpha: OPERATION REDLINE. One valley run west to east, every section teaching one thing.
        // ==================================================================================================

        [MenuItem("Metalization/Build Level/Production Alpha")]
        public static void BuildAlphaMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildAlphaImpl();
        }

        static partial void BuildAlphaImpl()
        {
            var L = new MetalLevelWriter("MZ_ProductionAlpha");
            AlphaEnvironment(L);
            L.MainCamera(P(-1800, 0, 300), Quaternion.identity);
            L.GameMode<MetalGameMode>(B("player"), actionCamera: true);

            // Blue allies are the same robot types on the Heroes side (team colours repaint them blue). The hangar
            // reinforcements see far, so they storm the yard once the gate opens.
            MetalBotArchetype allySoldier = B("soldier"), allyShooter = B("shooter");
            MetalBotArchetype blueSoldier = Variant("BlueSoldier", B("soldier"), a =>
            {
                a.displayName = "Blue Soldier"; a.team = MetalTeam.Heroes; a.maxHealth = 260f; a.sightRange = 65f; a.sightHalfAngle = 80f; a.hackable = false;
            });
            MetalBotArchetype blueShooter = Variant("BlueShooter", B("shooter"), a =>
            {
                a.displayName = "Blue Shooter"; a.team = MetalTeam.Heroes; a.maxHealth = 160f; a.sightRange = 65f; a.sightHalfAngle = 80f; a.hackable = false;
                a.retreatHealthFraction = 0f;
            });
            const MetalTeam Heroes = MetalTeam.Heroes;
            const float X0 = -2200, X1 = 38000, Y = 1600;
            const float UP = 1250;      // cliff-top plateau height (sections 6-9)
            const float LEDGE = 250;    // far side of the collapsed bridge

            // --- Valley shell: rock walls both sides, end caps ---
            Block(L, "Cliff_North", X0, X1, Y, Y + 400, -700, 3000, "MI_Alpha_Rock");
            Block(L, "Cliff_South", X0, X1, -Y - 400, -Y, -700, 3000, "MI_Alpha_Rock");
            Block(L, "Cliff_West", X0 - 400, X0, -Y, Y, -700, 3000, "MI_Alpha_Rock");
            Block(L, "Cliff_East", X1, X1 + 400, -Y, Y, -700, 3000, "MI_Alpha_Rock");
            for (float x = X0 + 1500; x < X1; x += 3000)
            {
                foreach (float y in new[] { Y - 60, -Y + 60 }) APillar(L, $"Buttress_{x}_{y}", x, y, -700, 3600, 180, "MI_Alpha_Rock");
            }

            // --- 1. Blue camp ---
            Ground(L, "Ground_Camp", X0, 1600, -Y, Y, 0);
            L.PlayerStart(new Vector3(-1500, 0, 120), 0f);
            var tents = new[] { (-1100f, 900f), (-300f, 950f), (400f, 900f), (-700f, -950f) };
            for (int i = 0; i < tents.Length; i++) Tent(L, $"Camp_Tent_{i}", tents[i].Item1, tents[i].Item2, 0);
            var banners = new[] { (-1800f, 600f), (-1800f, -600f), (1300f, 700f), (1300f, -700f) };
            for (int i = 0; i < banners.Length; i++) Banner(L, $"Camp_Banner_{i}", banners[i].Item1, banners[i].Item2, 0);
            var campCrates = new[] { (200f, -900f), (500f, -600f), (800f, -1000f) };
            for (int i = 0; i < campCrates.Length; i++) ACrate(L, $"Camp_Crate_{i}", campCrates[i].Item1, campCrates[i].Item2, 0, material: "MI_Alpha_Blue");
            ABot(L, "Blue_Commander", B("veteran") ?? B("guide"), (-900, -350, 0), 30f, patrol: 0f, passive: true, team: Heroes);
            var troopers = new[] { (-400f, 400f, 200f), (-200f, -500f, 150f), (700f, 300f, 180f), (900f, -350f, 160f) };
            for (int i = 0; i < troopers.Length; i++)
            {
                ABot(L, $"Blue_CampTrooper_{i}", i % 2 == 1 ? allySoldier : allyShooter, (troopers[i].Item1, troopers[i].Item2, 0), troopers[i].Item3, patrol: 0f, passive: true, team: Heroes);
            }
            L.PointLight("Camp_Light_A", (-800, 0, 500), (0.6f, 0.75f, 1f), 600f, 1200f);
            L.PointLight("Camp_Light_B", (800, 0, 500), (0.6f, 0.75f, 1f), 600f, 1200f);
            MetalTriggerZone zoneTrenchEntry = L.Zone("Zone_TrenchEntry", (1500, 0, 150), (150, 300, 150));

            // --- 2. Supply trench (sprint): cut into the ground ---
            const float TX0 = 1600, TX1 = 6500;
            Ground(L, "Ground_Trench", TX0, TX1, -300, 300, 0, "MI_Env_Dark");
            Block(L, "Trench_Bank_N", TX0, TX1, 300, Y, -60, 330, "MI_Alpha_Ground");
            Block(L, "Trench_Bank_S", TX0, TX1, -Y, -300, -60, 330, "MI_Alpha_Ground");
            Block(L, "Camp_Wall_N", 1400, TX0, 300, Y, 0, 330, "MI_Alpha_Ground");
            Block(L, "Camp_Wall_S", 1400, TX0, -Y, -300, 0, 330, "MI_Alpha_Ground");
            {
                int i = 0;
                for (float x = TX0 + 300; x < TX1; x += 700, i++)
                {
                    Block(L, $"Trench_Brace_{i}", x - 15, x + 15, -300, 300, 300, 330, "MI_Env_Dark", "Decor");
                    Sandbags(L, $"Trench_Sandbag_N{i}", x + 200, 300, 520, 330, 70, "MI_Alpha_Blue");
                    L.PointLight($"Trench_Light_{i}", (x, 250, 260), (1f, 0.7f, 0.4f), 300f, 700f);
                }
            }
            L.Barrage("Trench_Barrage", ((TX0 + TX1) / 2, 0, 150), ((TX1 - TX0) / 2 - 100, 280, 250));
            L.Pickup("Pickup_Health_TrenchEnd", MetalPickupType.Health, (6300, -200, 0), 50f);
            MetalTriggerZone zoneTrenchEnd = L.Zone("Zone_TrenchEnd", (6350, 0, 150), (150, 300, 150));

            // --- 3. Barricades (jump) ---
            const float BX0 = TX1, BX1 = 8900;
            Ground(L, "Ground_Barricades_A", BX0, 7950, -Y, Y, 0);
            Ground(L, "Ground_Barricades_B", 8250, BX1, -Y, Y, 0);
            Ground(L, "Ground_Barricades_Ditch", 7950, 8250, -Y, Y, -150, "MI_Env_Dark");
            Block(L, "Barricade_Wall_N", BX0, BX1, 1100, Y, 0, 330, "MI_Alpha_Ground");
            Block(L, "Barricade_Wall_S", BX0, BX1, -Y, -1100, 0, 330, "MI_Alpha_Ground");
            Block(L, "Barricade_1", 6950, 7020, -1100, 1100, 0, 110, "MI_Alpha_Blue", "Traversal");
            Block(L, "Barricade_2a", 7400, 7470, -1100, 100, 0, 120, "MI_Alpha_Concrete", "Traversal");
            Block(L, "Barricade_2b", 7650, 7720, -100, 1100, 0, 120, "MI_Alpha_Concrete", "Traversal");
            Block(L, "Barricade_Ditch_Lip", 7940, 7950, -1100, 1100, 0, 8, "MI_Env_Hazard", "Decor");
            // Line the ditch ends down to its floor and fill under the side walls, or the player can crawl under the ground.
            Block(L, "Barricade_Ditch_Wall_W", 7900, 7950, -Y, Y, -210, 0, "MI_Env_Dark");
            Block(L, "Barricade_Ditch_Wall_E", 8250, 8300, -Y, Y, -210, 0, "MI_Env_Dark");
            Block(L, "Barricade_Ditch_Fill_N", 7950, 8250, 1100, Y, -150, 0, "MI_Alpha_Ground");
            Block(L, "Barricade_Ditch_Fill_S", 7950, 8250, -Y, -1100, -150, 0, "MI_Alpha_Ground");
            MetalTriggerZone zoneBarricades = L.Zone("Zone_Barricades", (8650, 0, 150), (200, 1000, 150));

            // --- 4. Collapsed bridge (double jump) ---
            Ground(L, "Bridge_Deck_Near", BX1, 9400, -700, 700, 0, "MI_Alpha_Concrete");
            Block(L, "Bridge_Fill_N", BX1, 9400, 700, Y, -60, 330, "MI_Alpha_Rock");
            Block(L, "Bridge_Fill_S", BX1, 9400, -Y, -700, -60, 330, "MI_Alpha_Rock");
            Ground(L, "Chasm_Floor", 9400, 9800, -Y, Y, -500, "MI_Env_Dark");
            // Near wall of the chasm, under the bridge deck, so a robot that falls in can't walk under the deck.
            Block(L, "Chasm_Wall_Near", 9300, 9400, -Y, Y, -560, -60, "MI_Alpha_Rock");
            RampY(L, "Chasm_EscapeRamp", -1300, -500, 0, 0, 9550, 260, "MI_Alpha_Concrete");
            Block(L, "Chasm_Wall_Far", 9800, 9900, -Y, Y, -500, LEDGE, "MI_Alpha_Rock");
            Ground(L, "Ledge_Plateau", 9800, 11400, -Y, Y, LEDGE, "MI_Alpha_Ground");
            Block(L, "Ledge_Under", 9800, 11400, -Y, Y, -500, LEDGE - 60, "MI_Alpha_Rock");
            Rotate(L.Box("Bridge_Debris_A", (9600, 300, -420), (380, 500, 40), "MI_Alpha_Concrete"), -25f, 0f, 10f);
            Rotate(L.Box("Bridge_Debris_B", (9620, -500, -440), (300, 400, 40), "MI_Alpha_Concrete"), 18f, 10f, -8f);
            Block(L, "Bridge_Rail_N", BX1, 9400, 680, 700, 0, 100, "MI_Env_Hazard", "Decor");
            Block(L, "Bridge_Rail_S", BX1, 9400, -700, -680, 0, 100, "MI_Env_Hazard", "Decor");
            L.PointLight("Bridge_Light", (9600, 0, 400), (1f, 0.6f, 0.3f), 1500f, 1500f);
            MetalTriggerZone zoneLedge = L.Zone("Checkpoint_Ledge", (10100, 0, LEDGE + 150), (250, 1200, 150), checkpoint: true);

            // --- 5. Cliff + anti-gravity lift ---
            Block(L, "Cliff_Face", 11400, 11700, -Y, Y, LEDGE - 60, UP, "MI_Alpha_Rock");
            Lift(L, "Lift_Cliff", (11200, 0, LEDGE), UP - LEDGE + 80, (1, 0, 0));
            L.Box("Lift_Cliff_Sign", (11380, 0, LEDGE + 500), (20, 400, 60), "MI_Alpha_BlueGlow", "Decor");
            Ground(L, "Plateau", 11400, 23500, -Y, Y, UP, "MI_Alpha_Ground");
            MetalTriggerZone zoneCliffTop = L.Zone("Zone_CliffTop", (12000, 0, UP + 150), (250, 1200, 150));

            // --- 6. Forward post (shooting + first weapon swap) ---
            Sandbags(L, "Post_Sandbags_N", 12350, 150, 900, UP);
            Sandbags(L, "Post_Sandbags_S", 12350, -900, -150, UP);
            Banner(L, "Post_Banner", 11900, 1100, UP, "Blue", 500);
            // The Reds are kept 38 m+ away (beyond either side's sight): the firefight starts when the player pushes forward.
            var post = new[] { (11800f, 500f), (11850f, -500f), (11750f, 0f) };
            for (int i = 0; i < post.Length; i++) ABot(L, $"Blue_PostTrooper_{i}", i != 2 ? allySoldier : allyShooter, (post[i].Item1, post[i].Item2, UP), 0f, patrol: 80f, team: Heroes);
            L.Pickup("Pickup_Repeater", MetalPickupType.Weapon, (12650, 350, UP), 1f, W("Repeater"));
            L.Pickup("Pickup_Health_Post", MetalPickupType.Health, (12650, -400, UP), 50f);
            var postCover = new[] { (13800f, 600f, (200f, 200f, 140f)), (14300f, -500f, (260f, 140f, 120f)), (14900f, 200f, (200f, 200f, 150f)),
                (15500f, -800f, (160f, 300f, 130f)), (15700f, 700f, (200f, 200f, 140f)) };
            for (int i = 0; i < postCover.Length; i++) ACrate(L, $"Post_Cover_{i}", postCover[i].Item1, postCover[i].Item2, UP, postCover[i].Item3);
            string[] squad1 = { "Squad1" };
            ABot(L, "Red_PostShooter_1", B("shooter"), (15950, -300, UP), 180f, squad1, 250f);
            ABot(L, "Red_PostShooter_2", B("shooter"), (16200, 500, UP), 180f, squad1, 250f);
            ABot(L, "Red_PostShooter_3", B("shooter"), (16300, -900, UP), 180f, squad1, 250f);
            Banner(L, "Post_RedBanner", 16300, 1100, UP, "Red", 500);
            L.PointLight("Post_Light", (13800, 0, UP + 600), (1f, 0.75f, 0.5f), 2000f, 2200f);

            // --- 7. Bunker (grenades) ---
            const float BK = 17600;
            Block(L, "Bunker_Front", BK - 600, BK - 500, -800, 800, UP, UP + 95, "MI_Alpha_Red");
            Block(L, "Bunker_Side_N", BK - 600, BK + 500, 800, 900, UP, UP + 95);
            Block(L, "Bunker_Side_S", BK - 600, BK + 500, -900, -800, UP, UP + 95);
            Block(L, "Bunker_Back_N", BK + 400, BK + 500, 250, 800, UP, UP + 95);
            Block(L, "Bunker_Back_S", BK + 400, BK + 500, -800, -250, UP, UP + 95);
            Block(L, "Bunker_Stripe", BK - 605, BK - 600, -800, 800, UP + 70, UP + 82, "MI_Alpha_RedGlow", "Decor");
            // Hunkered down behind a chest-high wall: a poor target for bullets, a perfect one for a grenade.
            var bunker = new[] { (-300f, -250f), (-250f, 150f), (-100f, -50f) };
            for (int i = 0; i < bunker.Length; i++) ABot(L, $"Red_BunkerShooter_{i}", B("shooter"), (BK + bunker[i].Item1, bunker[i].Item2, UP), 180f, new[] { "Bunker" }, 0f, docile: true);
            L.Pickup("Pickup_Grenades_A", MetalPickupType.Grenades, (16600, 500, UP), 3f);
            L.Pickup("Pickup_Grenades_B", MetalPickupType.Grenades, (16600, -500, UP), 3f);
            L.Pickup("Pickup_Ammo_Bunker", MetalPickupType.Ammo, (18600, 1100, UP), 0.6f);
            L.PointLight("Bunker_Light", (BK, 0, UP + 600), (1f, 0.4f, 0.25f), 1800f, 1600f);

            // --- 8. Patrol yard (sneak) ---
            // A wall shuts the yard off from the bunker fight; its only gap lines up with the crawl pipe.
            Block(L, "Yard_Wall", 18750, 18850, -1000, Y, UP, UP + 450, "MI_Alpha_Red");
            Block(L, "Yard_Wall_Stripe", 18745, 18750, -1000, Y, UP + 300, UP + 320, "MI_Alpha_RedGlow", "Decor");
            L.Zone("Checkpoint_Yard", (19050, -1225, UP + 150), (150, 225, 150), checkpoint: true, marker: false);
            // Low walls: cover for a sneaking robot, not for a standing one.
            var lowWalls = new[] { (19700f, -400f, 500f), (20400f, -900f, -100f), (20400f, 400f, 1100f), (21200f, -300f, 700f), (21900f, -1000f, -300f), (22300f, 300f, 1000f) };
            for (int i = 0; i < lowWalls.Length; i++)
            {
                var (x, y0, y1) = lowWalls[i];
                Block(L, $"Yard_LowWall_{i}", x - 50, x + 50, y0, y1, UP, UP + 105, "MI_Alpha_Concrete", "Cover");
            }
            // Crawl pipe along the south wall: only fits a crouched robot (and blocks every sight line).
            const float PX0 = 19300, PX1 = 22700, PY0 = -1450, PY1 = -1000;
            Block(L, "Pipe_Wall_Out", PX0, PX1, PY0 - 60, PY0, UP, UP + 115, "MI_Env_Dark");
            Block(L, "Pipe_Wall_In", PX0, PX1, PY1, PY1 + 60, UP, UP + 115, "MI_Env_Dark");
            Block(L, "Pipe_Roof", PX0, PX1, PY0 - 60, PY1 + 60, UP + 115, UP + 175, "MI_Alpha_Blue");
            {
                int i = 0;
                for (float x = PX0 + 400; x < PX1; x += 800, i++) L.PointLight($"Pipe_Light_{i}", (x, (PY0 + PY1) / 2, UP + 80), (0.3f, 0.6f, 1f), 700f, 500f);
            }
            var yardSpots = new[] { (20000f, 300f, 180f), (20800f, -500f, 90f), (21600f, 500f, -90f), (22200f, -200f, 180f) };
            var yard = new MetalBot[yardSpots.Length];
            for (int i = 0; i < yardSpots.Length; i++)
            {
                yard[i] = ABot(L, $"Red_YardPatrol_{i}", i % 3 != 0 ? B("shooter") : B("soldier"), (yardSpots[i].Item1, yardSpots[i].Item2, UP), yardSpots[i].Item3, new[] { "Yard" }, 800f);
            }
            var lamps = new[] { (19900f, -200f), (20800f, 700f), (21500f, -500f), (22300f, 400f) };
            for (int i = 0; i < lamps.Length; i++)
            {
                APillar(L, $"Yard_LampPost_{i}", lamps[i].Item1, lamps[i].Item2, UP, 650, 15, "MI_Env_Dark");
                SpotLight(L, $"Yard_Spot_{i}", (lamps[i].Item1, lamps[i].Item2, UP + 640), (1f, 0.45f, 0.3f), 9000f, 30f, 1500f);
            }
            Banner(L, "Yard_Banner_A", 19300, 1300, UP, "Red");
            Banner(L, "Yard_Banner_B", 22700, 1300, UP, "Red");
            // The dispenser against the north wall rebuilds every patrol that gets scrapped: sneak to the console beside it.
            MetalRobotDispenser yardDispenser = Dispenser(L, "Yard_Dispenser", (21200, 1390, UP), -90f, assigned: yard);
            MetalControlConsole yardConsole = DispenserConsole(L, "Yard_DispenserConsole", (21600, 1490, UP), -90f, yardDispenser);

            // --- 9. Sniper perch (lift + sniper) ---
            const float PERCH = UP + 900;
            Block(L, "Perch_Tower", 22950, 23450, 600, 1400, UP, PERCH);
            Block(L, "Perch_Floor", 22950, 23450, 600, 1400, PERCH - 40, PERCH, "MI_Alpha_Blue");
            // Only a low lip on the valley side, so shots angled down at the towers clear the edge.
            Block(L, "Perch_Lip", 23440, 23470, 600, 1400, PERCH, PERCH + 20, "MI_Env_Hazard", "Decor");
            Block(L, "Perch_Rail", 22950, 23470, 1380, 1420, PERCH, PERCH + 90, "MI_Env_Hazard", "Decor");
            // Shields the lift from the valley so enemy snipers can't pick the player off on the way up.
            Block(L, "Perch_Tower2", 23420, 23460, 90, 1590, UP, PERCH);
            Lift(L, "Lift_Perch", (23200, 330, UP), PERCH - UP + 80, (0, 1, 0));
            L.Pickup("Pickup_Sniper", MetalPickupType.Weapon, (23380, 1000, PERCH), 1f, W("Sniper"));
            L.Pickup("Pickup_Health_Perch", MetalPickupType.Health, (23050, 750, PERCH), 40f);
            MetalTriggerZone zonePerch = L.Zone("Zone_Perch", (23200, 1000, PERCH + 120), (220, 380, 120), checkpoint: true, rgb: (0.3f, 0.9f, 1f));

            // --- Valley floor (sections 10-13) and the way down ---
            const float VX0 = 23500;
            Block(L, "Valley_CliffFace", VX0 - 100, VX0, -Y, Y, -60, UP, "MI_Alpha_Rock");
            Ground(L, "Valley_Floor", VX0, X1, -Y, Y, 0, "MI_Alpha_Ground");
            RampX(L, "Valley_Ramp", VX0, UP, VX0 + 4400, 0, -1350, 420, "MI_Alpha_Concrete");
            // Marksman towers covering the valley.
            var towers = new[] { (26600f, -400f), (28200f, 900f), (29700f, -200f) };
            for (int i = 0; i < towers.Length; i++)
            {
                Watchtower(L, $"Valley_Tower_{i}", towers[i].Item1, towers[i].Item2, 0, 650);
                ABot(L, $"Red_Marksman_{i}", B("marksman"), (towers[i].Item1, towers[i].Item2, 650), 180f, new[] { "Marksmen" }, 0f);
            }
            var valleyCover = new[] { (25500f, 300f), (27300f, -900f), (28900f, 200f) };
            for (int i = 0; i < valleyCover.Length; i++) ACrate(L, $"Valley_Cover_{i}", valleyCover[i].Item1, valleyCover[i].Item2, 0, (220, 200, 160));

            // --- 10/11. Shield gate, console and the Blue reinforcement hangar ---
            const float GX = 31000;
            L.Zone("Checkpoint_Gate", (GX - 700, 0, 150), (200, Y, 150), checkpoint: true);
            MetalGate mainGate = L.Gate("ShieldGate", (GX, 0, 0), 0f, "Gates");
            Block(L, "ShieldGate_Lintel", GX - 60, GX + 60, -320, 320, 450, 520, "MI_Alpha_Red");
            foreach (var (side, y0, y1) in new[] { ("N", 300f, 1000f), ("S", -1000f, -300f) })
            {
                Block(L, $"Fence_Base_{side}", GX - 50, GX + 50, y0, y1, 0, 40, "MI_Env_Dark");
                // Energy fence: see-through for sight lines and the Hack Tether, but stops robots and shots.
                L.EnergyField($"Fence_Field_{side}", (GX, (y0 + y1) / 2, 280), (20, y1 - y0, 480), "Gates", (1f, 0.25f, 0.1f));
                // Thicker invisible wall so robots stay clear of the field (sight and shots pass it).
                Block(L, $"Fence_Block_{side}", GX - 50, GX + 50, y0, y1, 40, 520, "MI_Alpha_Fence", "Gates", invisible: true);
                Block(L, $"Fence_Post_{side}", GX - 60, GX + 60, y1 > 0 ? y1 - 20 : y0, y1 > 0 ? y1 : y0 + 20, 0, 560, "MI_Alpha_Red");
            }
            Block(L, "GateWall_N", GX - 60, GX + 60, 1000, Y, 0, 600);
            Block(L, "GateWall_S", GX - 60, GX + 60, -Y, -1000, 0, 600);
            // Fills the valley above the gate wall, so the brute fight can't be cheesed with the sniper rifle from up the valley.
            Block(L, "GateWall_S2", GX - 60, GX + 60, -1687.5f, 1687.5f, 500, 3200);
            // The guard idles behind the see-through part of the fence (not the solid door), so the tether can reach.
            ABot(L, "Red_GateGuard", B("soldier"), (GX + 650, 650, 0), 180f, new[] { "GateGuard" }, 200f, docile: true);
            MetalControlConsole console = L.Console("GateConsole", (GX + 500, -1250, 0), 90f, "Gates");
            L.PointLight("Gate_Light", (GX - 300, 0, 600), (1f, 0.45f, 0.25f), 1200f, 1500f);
            // Blue reinforcements wait in a closed hangar; the console opens its door together with the gate.
            const float HX0 = 29500, HX1 = 30700, HY0 = 1000, HY1 = 1550;
            Block(L, "Hangar_Back", HX0, HX1, HY1, HY1 + 50, 0, 480);
            Block(L, "Hangar_End_W", HX0 - 50, HX0, HY0, HY1 + 50, 0, 480);
            Block(L, "Hangar_End_E", HX1, HX1 + 50, HY0, HY1 + 50, 0, 480);
            Block(L, "Hangar_Front_W", HX0 - 50, 29800, HY0 - 50, HY0, 0, 480, "MI_Alpha_Blue");
            Block(L, "Hangar_Front_E", 30400, HX1 + 50, HY0 - 50, HY0, 0, 480, "MI_Alpha_Blue");
            Block(L, "Hangar_Lintel", 29800, 30400, HY0 - 50, HY0, 450, 480, "MI_Alpha_Blue");
            Block(L, "Hangar_Roof", HX0 - 50, HX1 + 50, HY0 - 50, HY1 + 50, 480, 530, "MI_Alpha_Blue");
            Block(L, "Hangar_Stripe", HX0, HX1, HY0 - 55, HY0 - 50, 380, 400, "MI_Alpha_BlueGlow", "Decor");
            MetalGate hangarDoor = L.Gate("Hangar_Door", (30100, HY0 - 25, 0), 90f, "Gates");
            console.linkedGates = new List<MetalGate> { mainGate, hangarDoor };
            float[] reinforcements = { 29700, 29950, 30200, 30450 };
            for (int i = 0; i < reinforcements.Length; i++) ABot(L, $"Blue_Reinforcement_{i}", i % 2 == 1 ? blueSoldier : blueShooter, (reinforcements[i], 1280, 0), -90f, patrol: 300f);
            L.PointLight("Hangar_Light", (30100, 1280, 380), (0.5f, 0.7f, 1f), 1200f, 800f);

            // --- 12. Command yard ---
            L.Pickup("Pickup_RocketLauncher", MetalPickupType.Weapon, (GX - 400, -700, 0), 1f, W("RocketLauncher"));
            L.Pickup("Pickup_Health_Gate", MetalPickupType.Health, (GX - 400, 700, 0), 75f);
            L.Pickup("Pickup_Ammo_Gate", MetalPickupType.Ammo, (GX - 600, 0, 0), 0.7f);
            L.Pickup("Pickup_Grenades_Gate", MetalPickupType.Grenades, (GX - 600, -300, 0), 3f);
            var commandCover = new[] { (32300f, -600f, (240f, 200f, 150f)), (32600f, 700f, (200f, 240f, 150f)), (33400f, 0f, (300f, 160f, 140f)),
                (34200f, -900f, (220f, 220f, 160f)), (34400f, 800f, (220f, 220f, 160f)), (35300f, -200f, (260f, 180f, 150f)) };
            for (int i = 0; i < commandCover.Length; i++)
            {
                ACrate(L, $"Command_Cover_{i}", commandCover[i].Item1, commandCover[i].Item2, 0, commandCover[i].Item3, i % 2 == 1 ? "MI_Alpha_Red" : "MI_Alpha_Concrete");
            }
            APillar(L, "Command_Pylon_0", 33000, -1100, 0, 900, 110, "MI_Alpha_Red");
            APillar(L, "Command_Pylon_1", 35800, 1100, 0, 900, 110, "MI_Alpha_Red");
            // Flanking catwalk along the north wall, reached by lift.
            const float CW = 600;
            Block(L, "Catwalk", 32800, 36200, 1050, 1550, CW - 40, CW);
            Block(L, "Catwalk_Rail", 32800, 36200, 1030, 1050, CW, CW + 90, "MI_Env_Hazard", "Decor");
            {
                int i = 0;
                for (float x = 33200; x < 36200; x += 1000, i++) Block(L, $"Catwalk_Leg_{i}", x - 40, x + 40, 1250, 1330, 0, CW - 40, "MI_Env_Dark");
            }
            Lift(L, "Lift_Catwalk", (32550, 1250, 0), CW + 80, (1, 0, 0));
            // Kept well back from the gate (beyond their sight of the fence) so the hack puzzle plays out in peace.
            string[] command = { "Command" };
            ABot(L, "Red_CommandShooter_1", B("shooter"), (34400, -400, 0), 180f, command, 400f);
            ABot(L, "Red_CommandShooter_2", B("shooter"), (34700, 600, 0), 180f, command, 400f);
            ABot(L, "Red_CommandSoldier_1", B("soldier"), (35300, -600, 0), 180f, command, 350f);
            ABot(L, "Red_CommandSoldier_2", B("soldier"), (35500, 500, 0), 180f, command, 350f);
            ABot(L, "Red_CommandCatwalk", B("shooter"), (35800, 1300, CW), 180f, command, 0f);
            ABot(L, "Red_Commander_Brute", B("brute"), (36300, 0, 0), 180f, new[] { "Boss" }, 300f);
            // Two dispensers start welding Reds the moment the gate opens; their off switch is on the catwalk.
            var reds = new[] { (B("shooter"), 2f), (B("soldier"), 1f) };
            MetalRobotDispenser dispS = Dispenser(L, "Command_Dispenser_S", (35000, -1250, 0), 90f, choices: reds, maxAlive: 3, gate: mainGate, tags: command);
            MetalRobotDispenser dispN = Dispenser(L, "Command_Dispenser_N", (36700, 1250, 0), -90f, choices: reds, maxAlive: 3, gate: mainGate, tags: command);
            MetalControlConsole commandConsole = DispenserConsole(L, "Command_DispenserConsole", (36050, 1470, CW), -90f, dispS, dispN);
            Banner(L, "Command_Banner_0", 33500, 1300, 0, "Red", 800);
            Banner(L, "Command_Banner_1", 36500, -1300, 0, "Red", 800);
            L.PointLight("Command_Light_A", (33500, 0, 900), (1f, 0.45f, 0.25f), 3500f, 2800f);
            L.PointLight("Command_Light_B", (36000, 0, 900), (1f, 0.45f, 0.25f), 3500f, 2800f);

            // --- 13. Extraction ---
            MetalTriggerZone extraction = L.Zone("Zone_Extraction", (37400, 0, 150), (350, 350, 150), rgb: (0.2f, 1f, 0.3f));
            L.Box("Extraction_Pad", (37400, 0, 5), (750, 750, 10), "MI_Alpha_BlueGlow", "Mission");
            Banner(L, "Extraction_Banner", 37700, 500, 0, "Blue", 700);

            // --- Audio ---
            L.Ambient("music_background_loop", 0.5f);
            L.Ambient("battle_loop", 0.35f);

            // --- Mission script ---
            MetalMission mission = L.Mission("Mission_Redline", "OPERATION REDLINE");
            mission.playerStartingWeapons = new List<MetalWeaponData> { W("Laser") };
            mission.playerStartingGrenades = 0;
            const MetalObjectiveType Reach = MetalObjectiveType.ReachZone, Kill = MetalObjectiveType.EliminateTagged,
                Use = MetalObjectiveType.ActivateConsole, Hijack = MetalObjectiveType.HijackTagged;
            mission.objectives = new List<MetalObjective>
            {
                Obj("Head for the supply trench", Reach, target: zoneTrenchEntry, hint: "[W][A][S][D] move     [Mouse] look", dialogue: new[]
                {
                    Cmd("Morning, soldier. Blue Command here. The Reds dug into the valley overnight - today we take it back."),
                    Cmd("Shake the rust off first: WASD to move, the mouse to look around. Head east to the supply trench."),
                }),
                Obj("Sprint through the trench", Reach, target: zoneTrenchEnd, hint: "Hold [Shift] to sprint", dialogue: new[]
                {
                    Cmd("Red mortars have that trench zeroed in. Hold Shift and SPRINT - don't stop for anything!", 5.5f),
                }),
                Obj("Get over the barricades", Reach, target: zoneBarricades, hint: "[Space] jump", dialogue: new[]
                {
                    Cmd("Made it! Barricades ahead - Space to jump. Mind the ditch."),
                }),
                Obj("Cross the collapsed bridge", Reach, target: zoneLedge, hint: "[Space] jump, then [Space] again in mid-air", dialogue: new[]
                {
                    Cmd("Bridge is out and the far side's higher. Jump, then hit Space again at the top - double jump!", 5.5f),
                }),
                Obj("Ride the anti-grav lift up the cliff", Reach, target: zoneCliffTop, hint: "Walk into the glowing lift beam", dialogue: new[]
                {
                    Cmd("Our forward post is up on the cliff. Step into the anti-grav lift and it'll carry you up."),
                }),
                Obj("Help the forward post: scrap the Red squad", Kill, tag: "Squad1", hint: "[LMB] shoot   [RMB] aim   [1][2] / wheel swap arm   [R] reload", dialogue: new[]
                {
                    Cmd("Our squad's dug in behind the sandbags. Push up and scrap those Reds. Left click shoots, right click aims."),
                    Cmd("And grab that Repeater by the sandbags. 1 and 2, or the mouse wheel, swap your gun arm."),
                }),
                Obj("Flush out the bunker", Kill, tag: "Bunker", hint: "Aim and press [G] to lob an EMP grenade - it stuns robots", dialogue: new[]
                {
                    Cmd("Bunker ahead. Those Reds are hunkered down behind the wall - only their heads show. Hard to hit."),
                    Cmd("Grab the grenades and lob one on them - aim and press G. EMP stuns every robot in the blast. Then pick them off."),
                }),
                Obj("Sneak over to the robot dispenser and shut it down", Use, target: yardConsole,
                    hint: "Hold [Ctrl] / tap [C] to sneak     ? = suspicious     ! = spotted     [E] use the console", dialogue: new[]
                {
                    Cmd("Patrol yard. See that machine by the north wall? A robot dispenser - scrap a Red and it welds a new one.", 5.5f),
                    Cmd("So don't fight. Hold Ctrl, or tap C, to crouch: you're quiet and hard to see. Stay out of the lights.", 5.5f),
                    Cmd("Its off switch is the console against the wall right beside it. Sneak over and hit E. The pipe on your left gets you past the first guards.", 5.5f),
                    Cmd("A question mark means one's getting curious. If one spots you, the whole yard comes running."),
                }),
                Obj("Take the perch and grab the sniper rifle", Reach, target: zonePerch, hint: "Walk into the lift by the tower", dialogue: new[]
                {
                    Cmd("Dispenser's dead. Whatever you scrap now stays scrapped. Ride the lift up that tower - there's a sniper rifle on the perch.", 5.5f),
                }),
                Obj("Snipe the Red marksmen in the valley", Kill, tag: "Marksmen", hint: "[RMB] scope in   [LMB] fire   (the sniper is only accurate scoped)", dialogue: new[]
                {
                    Cmd("Three Red marksmen are covering the valley from their towers. Hold right click to scope in."),
                    Cmd("Watch for their red laser sights. Then take the ramp down to the valley floor."),
                }),
                Obj("Hack the gate guard", Hijack, tag: "GateGuard", hint: "Aim at the guard behind the fence and press [F]", dialogue: new[]
                {
                    Cmd("Shield gate. Nothing gets through that field, not even your shots... but your Hack Tether does."),
                    Cmd("Aim at that guard and press F. He's yours for half a minute - when you let go, he's theirs again."),
                }),
                Obj("Open the gate from the console", Use, target: console, hint: "[E] use the console     [F] let go", dialogue: new[]
                {
                    Cmd("You're driving a Red now. Walk him to the gate console and press E. Act natural."),
                    Cmd("The other Reds won't look twice at one of their own - unless you start shooting."),
                }),
                // Past the gate: three objectives at once, in any order, except that the yard can only be cleared once
                // the dispensers stop replacing what gets scrapped. Extraction waits for all three.
                Obj("Destroy the Red commander", Kill, tag: "Boss", hint: "Rockets and EMP grenades work wonders on a Brute", withNext: true, dialogue: new[]
                {
                    Cmd("Gate's open! Blue squad, go go go! Their commander's a Brute - bring it down and the valley's ours."),
                    Cmd("There's a rocket launcher by the gate. Use the lift to the catwalk if you need the high ground."),
                }, done: new[] { Cmd("The Brute's down! Their commander's scrap.") }),
                Obj("Shut down the robot dispensers", Use, target: commandConsole, hint: "Take the lift to the catwalk     [E] use the console", withNext: true,
                    id: "Dispensers", checkpoint: true, dialogue: new[]
                {
                    Cmd("Heads up - their robot dispensers just powered on. They'll keep welding Reds until we shut them down.", 5.5f),
                    Cmd("Their off switch is on the catwalk, past the marksman. Ride the lift up and hit it - Brute or no Brute.", 5.5f),
                }, done: new[] { Cmd("Dispensers are offline. Whatever you scrap now stays scrapped.") }),
                Obj("Clear the command yard", Kill, tag: "Command", requires: new[] { "Dispensers" }, dialogue: new[]
                {
                    Cmd("Now sweep the yard - scrap every Red still standing."),
                }, done: new[] { Cmd("Yard's clean. Nice work.") }),
                Obj("Reach the extraction pad", Reach, target: extraction, dialogue: new[]
                {
                    Cmd("Commander, dispensers, yard - that's everything. Get to the extraction pad, soldier.", 5.5f),
                }),
            };
            mission.completionDialogue = new List<MetalDialogueLine> { Cmd("Outstanding work. The valley's ours. Blue Command out.", 6f) };

            L.BuildNavMesh(voxelSize: 0.15f);
            L.Save();
        }

        // ==================================================================================================
        // MZ_Title: a Blue line and a Red line facing each other across no man's land, behind the title menu.
        // ==================================================================================================

        [MenuItem("Metalization/Build Level/Title")]
        public static void BuildTitleMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildTitleImpl();
        }

        static partial void BuildTitleImpl()
        {
            var L = new MetalLevelWriter("MZ_Title");
            AlphaEnvironment(L, -22f, -120f, (255, 190, 140), 0.01f);
            Ground(L, "Title_Ground", -3000, 3000, -3000, 3000, 0, "MI_Alpha_Ground");
            float[] ys = { -500, 0, 500 };
            for (int i = 0; i < ys.Length; i++)
            {
                Sandbags(L, $"Title_BlueBags_{i}", -700, ys[i] - 200, ys[i] + 200, 0, 90, "MI_Alpha_Blue");
                Sandbags(L, $"Title_RedBags_{i}", 700, ys[i] - 200, ys[i] + 200, 0, 90, "MI_Alpha_Red");
            }
            Banner(L, "Title_BlueBanner", -1300, -900, 0, "Blue", 700);
            Banner(L, "Title_RedBanner", 1400, 900, 0, "Red", 700);
            Tent(L, "Title_Tent", -1700, 600, 0, "Blue");
            Watchtower(L, "Title_Tower", 1700, -700, 0, 500);
            ABot(L, "Title_Player", B("player"), (-900, 0, 0), 0f, patrol: 0f, passive: true, team: MetalTeam.Heroes);
            ABot(L, "Title_BlueTrooper_0", B("shooter"), (-1000, -500, 0), 0f, patrol: 0f, passive: true, team: MetalTeam.Heroes);
            ABot(L, "Title_BlueTrooper_1", B("soldier"), (-1000, 500, 0), 0f, patrol: 0f, passive: true, team: MetalTeam.Heroes);
            ABot(L, "Title_RedShooter_0", B("shooter"), (950, -450, 0), 180f, patrol: 0f, passive: true);
            ABot(L, "Title_RedShooter_1", B("shooter"), (950, 450, 0), 180f, patrol: 0f, passive: true);
            ABot(L, "Title_RedBrute", B("brute"), (1500, 100, 0), 180f, patrol: 0f, passive: true);
            L.PointLight("Title_BlueLight", (-900, 0, 450), (0.4f, 0.6f, 1f), 4000f, 1200f);
            L.PointLight("Title_RedLight", (1000, 0, 450), (1f, 0.35f, 0.2f), 4000f, 1200f);
            Camera cam = L.MainCamera(P(0, -1600, 450), UE.Rot(-12f, 90f, 0f));
            MetalGameMode gm = L.GameMode<MetalGameMode>(B("player"));
            gm.spawnPlayer = false;
            MetalTitleScreen title = gm.gameObject.AddComponent<MetalTitleScreen>();
            title.titleCamera = cam;
            L.Ambient("music_background_loop", 0.6f);
            L.BuildNavMesh();
            L.Save();
        }

        // ==================================================================================================
        // MZ_DispenserTest: a walled yard with two robot dispensers. South: Respawn mode (rebuilds its two guards).
        // North: Continuous, started by the gate. The blue console at the back shuts both down.
        // ==================================================================================================

        [MenuItem("Metalization/Build Level/Dispenser Test")]
        public static void BuildDispenserMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildDispenserTestImpl();
        }

        static partial void BuildDispenserTestImpl()
        {
            var L = new MetalLevelWriter("MZ_DispenserTest");
            AlphaEnvironment(L);
            L.MainCamera(P(-2300, 0, 300), Quaternion.identity);
            L.GameMode<MetalGameMode>(B("player"), actionCamera: true);
            const float X0 = -2500, X1 = 3500, Y = 2000;
            Ground(L, "Ground", X0, X1, -Y, Y, 0);
            Block(L, "Wall_N", X0, X1, Y, Y + 100, 0, 500, "MI_Alpha_Rock");
            Block(L, "Wall_S", X0, X1, -Y - 100, -Y, 0, 500, "MI_Alpha_Rock");
            Block(L, "Wall_W", X0 - 100, X0, -Y, Y, 0, 500, "MI_Alpha_Rock");
            Block(L, "Wall_E", X1, X1 + 100, -Y, Y, 0, 500, "MI_Alpha_Rock");
            L.PlayerStart(new Vector3(-2000, 0, 120), 0f);
            var cover = new[] { (-300f, -700f), (-200f, 800f), (600f, 0f) };
            for (int i = 0; i < cover.Length; i++) ACrate(L, $"Cover_{i}", cover[i].Item1, cover[i].Item2, 0, (200, 200, 140));

            // South: Respawn mode with two assigned guards.
            MetalBot[] guards =
            {
                ABot(L, "Red_Guard_Shooter", B("shooter"), (900, -900, 0), 180f, new[] { "TestGuards" }, 300f),
                ABot(L, "Red_Guard_Soldier", B("soldier"), (900, -400, 0), 180f, new[] { "TestGuards" }, 300f),
            };
            MetalRobotDispenser respawner = Dispenser(L, "Dispenser_Respawn", (2600, -1100, 0), 180f, assigned: guards);

            // North: Continuous mode, started by the gate.
            MetalGate gate = L.Gate("TestGate", (-800, 1400, 0), 90f, "Gates");
            Block(L, "TestGate_Wall", -900, -700, 1700, Y, 0, 500, "MI_Alpha_Red");
            MetalRobotDispenser builder = Dispenser(L, "Dispenser_Continuous", (2600, 1100, 0), 180f, gate: gate, maxAlive: 3,
                choices: new[] { (B("shooter"), 2f), (B("soldier"), 1f) }, tags: new[] { "TestSpawn" });

            MetalControlConsole gateConsole = L.Console("GateConsole", (-1700, 600, 0), 0f, "Mission");
            gateConsole.requiredFaction = MetalTeam.Neutral;
            gateConsole.linkedGates = new List<MetalGate> { gate };
            MetalControlConsole offConsole = DispenserConsole(L, "DispenserConsole", (3200, 0, 0), 180f, respawner, builder);
            L.PointLight("Light_S", (2200, -1100, 600), (1f, 0.5f, 0.3f), 6000f, 2000f);
            L.PointLight("Light_N", (2200, 1100, 600), (1f, 0.5f, 0.3f), 6000f, 2000f);
            L.Pickup("Pickup_Health", MetalPickupType.Health, (-1800, -600, 0), 100f);
            L.Pickup("Pickup_Ammo", MetalPickupType.Ammo, (-1800, -900, 0), 1f);

            MetalMission mission = L.Mission("Mission_DispenserTest", "DISPENSER TEST");
            mission.objectives = new List<MetalObjective>
            {
                Obj("Open the gate to start the second dispenser", MetalObjectiveType.ActivateConsole, target: gateConsole, hint: "[E] use the console", dialogue: new[]
                {
                    Cmd("Dispenser test. The left machine rebuilds its two guards - scrap one and watch it weld a new one."),
                    Cmd("The right machine starts when the gate opens, then builds a robot every five seconds."),
                }),
                Obj("Shut down both robot dispensers", MetalObjectiveType.ActivateConsole, target: offConsole, hint: "[E] use the console", dialogue: new[]
                {
                    Cmd("Now get to the blue console at the back and shut them both down."),
                }),
            };
            L.BuildNavMesh();
            L.Save();
        }
    }
}
