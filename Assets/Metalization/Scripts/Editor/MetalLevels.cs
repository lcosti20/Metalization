// The level scripts: every scene is generated from code (like the Unreal version's ue_setup_*.py), so make
// level changes here and rebuild rather than editing the scenes by hand. Numbers are the Unreal version's
// (centimetres; X forward / Y right / Z up), converted by MetalLevelWriter.
using System.Collections.Generic;
using System.Linq;
using UnityEditor;
using UnityEngine;
using static Metalization.Editor.MetalLevelWriter;

namespace Metalization.Editor
{
    public static partial class MetalLevels
    {
        /// <summary>Scenes in build order; the title screen first (a player build starts there).</summary>
        public static readonly string[] SceneNames =
        {
            "MZ_Title", "MZ_ProductionAlpha", "MZ_Outpost_Demo", "MZ_Arena", "MZ_FlightTest", "MZ_SpaceBattle", "MZ_CapitalShip_Interior", "MZ_DispenserTest",
        };

        public static Dictionary<string, MetalBotArchetype> Bots { get; private set; }
        public static Dictionary<string, MetalWeaponData> Weapons { get; private set; }

        static void LoadData()
        {
            Bots = new Dictionary<string, MetalBotArchetype>();
            foreach (string guid in AssetDatabase.FindAssets("t:MetalBotArchetype", new[] { MetalAssetPipeline.Data }))
            {
                var a = AssetDatabase.LoadAssetAtPath<MetalBotArchetype>(AssetDatabase.GUIDToAssetPath(guid));
                // DA_Bot_Marksman shares the Shooter's id, so key by asset name.
                string key = a.name.Substring("DA_Bot_".Length);
                Bots[ToSnake(key)] = a;
            }
            Weapons = new Dictionary<string, MetalWeaponData>();
            foreach (string guid in AssetDatabase.FindAssets("t:MetalWeaponData", new[] { MetalAssetPipeline.Data }))
            {
                var w = AssetDatabase.LoadAssetAtPath<MetalWeaponData>(AssetDatabase.GUIDToAssetPath(guid));
                Weapons[w.name.Substring(3)] = w;
            }
        }

        static string ToSnake(string pascal)
        {
            var sb = new System.Text.StringBuilder();
            for (int i = 0; i < pascal.Length; i++)
            {
                char c = pascal[i];
                if (char.IsUpper(c) && i > 0) sb.Append('_');
                sb.Append(char.ToLowerInvariant(c));
            }
            return sb.ToString();
        }

        [MenuItem("Metalization/Build Levels", priority = 2)]
        public static void BuildAll()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildOutpost();
            BuildArena();
            BuildFlightTest();
            BuildSpace();
            BuildAlpha();
            BuildTitle();
            BuildDispenserTest();
            RegisterScenes();
        }

        /// <summary>The Level Select list (the first entry is the title screen's Start Mission).</summary>
        public static MetalLevelSettings BuildLevelSettings()
        {
            MetalLevelSettings s = MetalAssetPipeline.DataAsset<MetalLevelSettings>("LevelSettings");
            s.levels = new List<MetalLevelSettings.Entry>
            {
                new MetalLevelSettings.Entry { displayName = "Operation Redline (Production Alpha)", sceneName = "MZ_ProductionAlpha" },
                new MetalLevelSettings.Entry { displayName = "Outpost - The Gate Job", sceneName = "MZ_Outpost_Demo" },
                new MetalLevelSettings.Entry { displayName = "Arena", sceneName = "MZ_Arena" },
                new MetalLevelSettings.Entry { displayName = "Flight Test", sceneName = "MZ_FlightTest" },
                new MetalLevelSettings.Entry { displayName = "Space Battle", sceneName = "MZ_SpaceBattle" },
                new MetalLevelSettings.Entry { displayName = "Robot Dispenser Test", sceneName = "MZ_DispenserTest" },
            };
            EditorUtility.SetDirty(s);
            return s;
        }

        /// <summary>Puts every generated scene in the build profile's scene list, title screen first.</summary>
        public static void RegisterScenes()
        {
            var scenes = new List<EditorBuildSettingsScene>();
            foreach (string n in SceneNames)
            {
                string path = $"{ScenesFolder}/{n}.unity";
                if (System.IO.File.Exists(path)) scenes.Add(new EditorBuildSettingsScene(path, true));
            }
            EditorBuildSettings.scenes = scenes.ToArray();
        }

        static MetalBotArchetype B(string id) => Bots.TryGetValue(id, out MetalBotArchetype a) ? a : null;
        static MetalWeaponData W(string id) => Weapons.TryGetValue(id, out MetalWeaponData w) ? w : null;

        // ==================================================================================================
        // MZ_Outpost_Demo
        // ==================================================================================================

        [MenuItem("Metalization/Build Level/Outpost")]
        public static void BuildOutpostMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildOutpost();
        }

        static void BuildOutpost()
        {
            var L = new MetalLevelWriter("MZ_Outpost_Demo");
            const float X0 = -9500, X1 = 15500, Y = 1500, HangarX0 = -3000;

            // --- Sky, sun and haze: late-afternoon industrial smog ---
            L.Sun(-55f, -135f, new Color(1f, 0.9f, 0.8f), 1.4f);
            L.Atmosphere(new Color(0.55f, 0.55f, 0.6f), new Color(0.5f, 0.45f, 0.4f), new Color(0.25f, 0.22f, 0.2f), 0.004f, new Color(0.55f, 0.5f, 0.45f));
            L.PostProcess(-0.3f, -10f, 0.3f);
            L.MainCamera(P(-700, 0, 300), Quaternion.identity);
            L.GameMode<MetalGameMode>(B("player"));

            // --- Shell: floor slabs per area and perimeter walls ---
            L.Floor("Floor_Boneyard", X0, HangarX0, -Y, Y, material: "MI_Env_Dark");
            L.Floor("Floor_Hangar", HangarX0, 1500, -Y, Y);
            L.Floor("Floor_Courtyard", 1500, 5500, -Y, Y, material: "MI_Env_Dark");
            L.Floor("Floor_ScrapHeap", 5500, 7500, -Y, Y);
            L.Floor("Floor_GateYard", 7500, 10500, -Y, Y, material: "MI_Env_Dark");
            L.Floor("Floor_Arena", 10500, X1, -Y, Y);
            L.Wall("Wall_North", X0, X1, Y, Y + 100, 700);
            L.Wall("Wall_South", X0, X1, -Y - 100, -Y, 700);
            L.Wall("Wall_West", X0 - 100, X0, -Y - 100, Y + 100, 1200);
            L.Wall("Wall_East", X1, X1 + 100, -Y - 100, Y + 100, 700);
            for (float x = X0 + 500; x < X1; x += 1000)
            {
                foreach (float y in new[] { Y - 10, -Y + 10 })
                {
                    L.Box($"GlowStrip_{x}_{y}", (x, y, 320), (600, 20, 20), "MI_Env_Glow", "Decor");
                    L.Pillar($"Buttress_{x}_{y}", x + 500, y, 700, 70);
                }
            }

            // --- A: Docking hangar + robot showcase. Every imported robot stands on display; giants get the boneyard. ---
            L.PlayerStart(new Vector3(-300, 0, 120), 0f);
            Dictionary<string, object> chars = MetalAssetPipeline.Manifest.O("characters") ?? new Dictionary<string, object>();
            (float w, float d, float h) SizeOf(string cid)
            {
                float[] mn = chars[cid].A("bounds_min_m").Floats();
                float[] mx = chars[cid].A("bounds_max_m").Floats();
                return ((mx[0] - mn[0]) * 100f, (mx[1] - mn[1]) * 100f, (mx[2] - mn[2]) * 100f);
            }
            void Showcase(string cid, (float x, float y, float z) loc, float yaw)
            {
                L.Bot($"Showcase_{string.Concat(cid.Split('_').Select(p => char.ToUpperInvariant(p[0]) + p.Substring(1)))}", B(cid), loc, yaw, patrol: 0f, passive: true,
                    team: MetalTeam.Neutral, folder: "Robots/Showcase", flyer: B(cid) != null && B(cid).rig.IsFlyer);
            }
            List<string> shown = chars.Keys.Where(c => c != "player" && c != "guide").OrderBy(c => c, System.StringComparer.Ordinal).ToList();
            List<string> giants = shown.Where(c => SizeOf(c).h > 300 || SizeOf(c).w > 400).ToList();
            foreach (var side in new[] { ("S", "Heroes", -1150f, 90f), ("N", "Villains", 1150f, -90f) })
            {
                float x = HangarX0 + 200;
                foreach (string cid in shown.Where(c => !giants.Contains(c) && chars[c].S("faction") == side.Item2))
                {
                    float width = Mathf.Max(SizeOf(cid).w, 80f);
                    Showcase(cid, (x + width / 2, side.Item3, 0), side.Item4);
                    x += width + 60;
                }
                L.Box($"Showcase_Plinth_{side.Item1}", ((HangarX0 + x) / 2, side.Item3, 5), (x - HangarX0, 300, 10), "MI_Env_Hazard", "Decor");
            }
            {
                float x = HangarX0 - 700;
                foreach (string cid in giants)
                {
                    float depth = SizeOf(cid).d;
                    x -= depth / 2;
                    Showcase(cid, (x, 0, 0), 0f);
                    x -= depth / 2 + 500;
                }
            }
            L.Wall("Boneyard_Divider_N", HangarX0 - 50, HangarX0 + 50, 600, Y, 700);
            L.Wall("Boneyard_Divider_S", HangarX0 - 50, HangarX0 + 50, -Y, -600, 700);
            L.PointLight("BoneyardLight_A", (HangarX0 - 2500, 0, 1400), (0.6f, 0.7f, 1f), 9000f, 4000f);
            L.PointLight("BoneyardLight_B", (X0 + 1500, 0, 1400), (1f, 0.65f, 0.4f), 9000f, 4000f);
            L.Bot("Guide", B("guide"), (300, -500, 0), 150f, patrol: 0f, passive: true, team: MetalTeam.Neutral);
            foreach (var (i, x, y) in new[] { (0, 700f, 400f), (1, 900f, 600f), (2, -900f, 300f) }) L.Crate($"HangarCrate_{i}", x, y);
            L.Pickup("Pickup_Health_Hangar", MetalPickupType.Health, (1000, -300, 0), 50f);
            L.PointLight("HangarLight_A", (-1500, 0, 550), (1f, 0.85f, 0.65f), 3000f);
            L.PointLight("HangarLight_B", (0, 0, 550), (1f, 0.85f, 0.65f), 3000f);

            // --- B: Courtyard firefight ---
            L.Wall("Courtyard_Gateway_N", 1450, 1550, 700, Y, 500);
            L.Wall("Courtyard_Gateway_S", 1450, 1550, -Y, -700, 500);
            var cover = new[] { (2400f, -500f), (2700f, 450f), (3200f, -100f), (3600f, 800f), (3900f, -800f), (4400f, 200f), (4900f, -400f), (2100f, 1000f) };
            for (int i = 0; i < cover.Length; i++) L.Crate($"CourtyardCover_{i}", cover[i].Item1, cover[i].Item2, i % 2 == 1 ? (160f, 160f, 130f) : (220f, 120f, 110f));
            L.Pillar("CourtyardPillar_0", 3000, 1100, 450, 80);
            L.Pillar("CourtyardPillar_1", 4200, -1100, 450, 80);
            L.Floor("Catwalk", 3300, 5200, 900, 1450, top: 250, material: "MI_Env_Dark");
            Rotate(L.Box("Catwalk_Ramp", (2950, 1175, 125), (800, 550, 30), "MI_Env_Dark"), 17f, 0f, 0f);
            L.Box("Catwalk_Edge", (4250, 900, 290), (1900, 20, 30), "MI_Env_Hazard", "Decor");
            string[] squad1 = { "Squad1" };
            L.Bot("Shooter_Courtyard_1", B("shooter"), (3500, -400, 0), 180f, squad1, 500f);
            L.Bot("Shooter_Courtyard_2", B("shooter"), (3900, 350, 0), 180f, squad1, 500f);
            L.Bot("Shooter_Courtyard_3", B("shooter"), (4600, -150, 0), 180f, squad1, 500f);
            L.Bot("Soldier_Courtyard", B("soldier"), (4600, 1150, 250), 180f, squad1, 300f);
            L.Pickup("Pickup_Ammo_Courtyard", MetalPickupType.Ammo, (2200, 1250, 0), 0.5f);
            L.Pickup("Pickup_Health_Courtyard", MetalPickupType.Health, (5000, -1250, 0), 60f);
            L.Pickup("Pickup_Grenades_Catwalk", MetalPickupType.Grenades, (5000, 1200, 250), 2f);
            L.PointLight("CourtyardLight", (3500, 0, 700), (1f, 0.65f, 0.4f), 4500f, 2500f);
            L.Zone("Checkpoint_ScrapHeap", (5350, 0, 150), (150, 1400, 150), checkpoint: true);

            // --- C: Scrap heap traversal ---
            L.Wall("ScrapWall", 6900, 7500, -Y, Y, 300, "MI_Env_Wall");
            L.Box("ScrapWall_Lip", (6900, 0, 305), (20, 2 * Y, 10), "MI_Env_Hazard", "Decor");
            var steps = new[] { (6000f, -700f, 100f), (6300f, -350f, 200f), (6600f, 0f, 290f) };
            for (int i = 0; i < steps.Length; i++)
            {
                L.Crate($"ScrapStep_{i}", steps[i].Item1, steps[i].Item2, (220f, 220f, steps[i].Item3), material: i == 2 ? "MI_Env_Hazard" : "MI_Env_Wall", folder: "Traversal");
            }
            L.Crate("ScrapStep_Secret_0", 5900, 900, (200f, 200f, 120f), folder: "Traversal");
            L.Box("ScrapPillar_Secret", (6250, 1150, 125), (240, 240, 250), "MI_Env_Wall", "Traversal");
            L.Pickup("Pickup_Shotgun", MetalPickupType.Weapon, (6250, 1150, 250), 1f, W("Shotgun"));
            L.Pickup("Pickup_LaserUpgrade", MetalPickupType.Weapon, (7200, 0, 300), 1f, W("Laser"));
            MetalTriggerZone topZone = L.Zone("Zone_ScrapWallTop", (7200, 0, 450), (300, 1400, 150), rgb: (1f, 0.6f, 0.1f));
            L.PointLight("ScrapLight", (6500, 0, 650), (0.5f, 0.7f, 1f), 3000f, 2000f);

            // --- D: Gate yard - possession puzzle ---
            var gyCover = new[] { (8200f, -600f), (8500f, 700f), (8900f, -100f) };
            for (int i = 0; i < gyCover.Length; i++) L.Crate($"GateYardCover_{i}", gyCover[i].Item1, gyCover[i].Item2);
            L.Bot("Shooter_GateYard_1", B("shooter"), (8700, -500, 0), 180f, new[] { "Squad2" }, 400f);
            L.Bot("Shooter_GateYard_2", B("shooter"), (8800, 600, 0), 180f, new[] { "Squad2" }, 400f);
            const float Gx = 9500;
            L.Wall("GateWall_N", Gx - 50, Gx + 50, 1000, Y, 500);
            L.Wall("GateWall_S", Gx - 50, Gx + 50, -Y, -1000, 500);
            foreach (var (side, y0, y1) in new[] { ("N", 300f, 1000f), ("S", -1000f, -300f) })
            {
                // Kept low so the tether's sight line clears it to the short Shooter guards behind.
                L.Wall($"FenceBase_{side}", Gx - 50, Gx + 50, y0, y1, 40, "MI_Env_Dark");
                // Energy fence: see-through for sight lines and the Hack Tether, but stops robots and shots.
                L.EnergyField($"EnergyFence_{side}", (Gx, (y0 + y1) / 2, 40 + 230), (20, y1 - y0, 460));
                // Thicker invisible wall so robots stay clear of the field (sight and shots pass it).
                L.Box($"EnergyFenceBlock_{side}", (Gx, (y0 + y1) / 2, 40 + 230), (100, y1 - y0, 460), folder: "Gate", invisibleWall: true);
            }
            MetalGate gate = L.Gate("MainGate", (Gx, 0, 0));
            L.Box("GateLintel", (Gx, 0, 525), (120, 640, 50), "MI_Env_Dark", "Gate");
            MetalControlConsole console = L.Console("GateConsole", (9850, -1150, 0), 90f);
            console.linkedGates.Add(gate);
            L.Bot("Shooter_GateGuard_1", B("shooter"), (10050, -700, 0), 180f, new[] { "GateGuard" }, 150f);
            L.Bot("Shooter_GateGuard_2", B("shooter"), (10150, 650, 0), 180f, new[] { "GateGuard" }, 150f);
            L.PointLight("GateLight", (9300, 0, 600), (1f, 0.4f, 0.2f), 4000f, 1800f);
            // Radio call the first time the player drops into the yard.
            L.Zone("Dialogue_GateYard", (7900, 0, 150), (150, 1400, 150), marker: false, dialogue: new[]
            {
                Line("Guide", "Heads up - two more shooters dug in by the gate. Clear 'em out before you go poking at that fence."),
            });

            // --- E: Boss arena ---
            L.Zone("Checkpoint_Arena", (10750, 0, 150), (150, 1400, 150), checkpoint: true);
            L.Pickup("Pickup_RocketLauncher", MetalPickupType.Weapon, (11000, 1150, 0), 1f, W("RocketLauncher"));
            L.Pickup("Pickup_Health_Arena", MetalPickupType.Health, (11000, -1150, 0), 75f);
            L.Pickup("Pickup_Ammo_Arena", MetalPickupType.Ammo, (12800, 1250, 0), 0.6f);
            L.Pickup("Pickup_Grenades_Arena", MetalPickupType.Grenades, (12800, -1250, 0), 2f);
            var pillars = new[] { (11700f, -500f), (11700f, 500f), (12600f, 0f), (13300f, -700f), (13300f, 700f) };
            for (int i = 0; i < pillars.Length; i++) L.Pillar($"ArenaPillar_{i}", pillars[i].Item1, pillars[i].Item2, 600, 90);
            var arenaCover = new[] { (12100f, -1000f), (12100f, 1000f), (13700f, 0f) };
            for (int i = 0; i < arenaCover.Length; i++) L.Crate($"ArenaCover_{i}", arenaCover[i].Item1, arenaCover[i].Item2, (200f, 200f, 140f));
            L.Bot("Brute_Boss", B("brute"), (13300, 0, 0), 180f, new[] { "Boss" }, 400f);
            L.Bot("Soldier_Arena_1", B("soldier"), (12500, -900, 0), 180f, new[] { "Arena" }, 400f);
            L.Bot("Soldier_Arena_2", B("soldier"), (12500, 900, 0), 180f, new[] { "Arena" }, 400f);
            L.PointLight("ArenaLight_A", (12000, 0, 800), (1f, 0.5f, 0.3f), 6000f, 3000f);
            L.PointLight("ArenaLight_B", (14000, 0, 800), (0.3f, 0.6f, 1f), 5000f, 3000f);

            // --- F: Extraction ---
            MetalTriggerZone extraction = L.Zone("Zone_Extraction", (15000, 0, 150), (350, 350, 150), rgb: (0.2f, 1f, 0.3f));
            L.Box("ExtractionPad", (15000, 0, 5), (750, 750, 10), "MI_Env_Glow", "Mission");

            // --- Mission script ---
            const string K = "Guide";
            MetalMission mission = L.Mission("Mission_OutpostDemo", "OUTPOST - THE GATE JOB");
            mission.objectives = new List<MetalObjective>
            {
                Objective("Scrap the enemy patrol in the courtyard", MetalObjectiveType.EliminateTagged, "Squad1", null,
                    Line(K, "Hey, you're awake! Enemy shooters are all over the courtyard. Go scrap 'em - I'll guard this extremely important crate."),
                    Line(K, "WASD to move, mouse to aim, left click to shoot. 1-2 or the wheel swaps arms. G throws an EMP grenade."),
                    Line(K, "And don't mind the hangar behind you. Those bots are on display. Like art. Expensive art.")),
                Objective("Climb over the scrap wall", MetalObjectiveType.ReachZone, null, topZone,
                    Line(K, "Nice shooting! Now get over that scrap heap. Robots don't get vertigo. Mostly."),
                    Line(K, "Word is somebody stashed a Shotgun up on the pillar to the left. Finders keepers.")),
                Objective("Hijack a gate guard with the Hack Tether [F]", MetalObjectiveType.HijackTagged, "GateGuard", null,
                    Line(K, "Energy fence. You can't walk through it... but your Hack Tether can. Aim at a guard behind it and press F.")),
                Objective("Use the guard to open the gate console [E]", MetalObjectiveType.ActivateConsole, null, console,
                    Line(K, "Ha! You're wearing an enemy shooter! Walk him over to the console and press E. Try to look... evil-ish."),
                    Line(K, "When you let go with F he reboots and goes right back to their side. So don't let go next to him.")),
                Objective("Destroy the Brute (or borrow it)", MetalObjectiveType.EliminateTagged, "Boss", null,
                    Line(K, "Gate's open! Uh. Oh. That's a Brute. Big guns, bigger attitude. Grab the rocket launcher..."),
                    Line(K, "...or tether the big lug and give his buddies a taste of their own minigun.")),
                Objective("Reach the extraction pad", MetalObjectiveType.ReachZone, null, extraction,
                    Line(K, "That's how we do it at the outpost! Get to the extraction pad before the Warlord notices his gate is missing.")),
            };
            mission.completionDialogue = new List<MetalDialogueLine> { Line(K, "You magnificent pile of bolts. Drinks are on me. Oil. I mean oil.", 6f) };

            L.BuildNavMesh();
            L.Save();
        }

        static partial void BuildArenaImpl();
        static partial void BuildFlightTestImpl();
        static partial void BuildSpaceImpl();
        static partial void BuildAlphaImpl();
        static partial void BuildTitleImpl();
        static partial void BuildDispenserTestImpl();

        static void BuildArena() => BuildArenaImpl();
        static void BuildFlightTest() => BuildFlightTestImpl();
        static void BuildSpace() => BuildSpaceImpl();
        static void BuildAlpha() => BuildAlphaImpl();
        static void BuildTitle() => BuildTitleImpl();
        static void BuildDispenserTest() => BuildDispenserTestImpl();
    }
}
