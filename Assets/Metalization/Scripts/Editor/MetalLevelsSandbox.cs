// Sandbox levels: the free-roam Arena (ue_setup_arena.py) and the Stalker flight test (ue_setup_flight.py).
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using static Metalization.Editor.MetalLevelWriter;

namespace Metalization.Editor
{
    public static partial class MetalLevels
    {
        /// <summary>A clear midday sky (the Arena and the flight test share it).</summary>
        static void MiddaySky(MetalLevelWriter L)
        {
            L.Sun(-55f, -20f, new Color(1f, 0.96f, 0.9f), 1.6f);
            L.Atmosphere(new Color(0.55f, 0.65f, 0.85f), new Color(0.5f, 0.52f, 0.55f), new Color(0.3f, 0.28f, 0.25f), 0.0015f, new Color(0.7f, 0.78f, 0.88f));
            L.PostProcess(0.4f, 0f, 0.2f);
        }

        /// <summary>Hovering or parked Stalker on a team (flight test, space battle).</summary>
        static MetalStalker Stalker(MetalLevelWriter L, string label, (float x, float y, float z) loc, float yaw, MetalTeam team, bool parked, string folder)
        {
            MetalBotArchetype a = B("stalker");
            float lift = parked ? 0f : -(a != null ? a.capsuleHalfHeight * 100f + 5f : 0f);
            var s = (MetalStalker)L.Bot(label, a, (loc.x, loc.y, loc.z + lift), yaw, null, 0f, false, team, folder, flyer: true);
            s.startParked = parked;
            s.autoPossessAI = false;
            return s;
        }

        // ==================================================================================================
        // MZ_Arena: free roam around a template-style arena (central platform, ramps, blocks, disc platform)
        // while enemy patrols and a few neutral heroes wander the floor; they fight back once shot at.
        // ==================================================================================================

        [MenuItem("Metalization/Build Level/Arena")]
        public static void BuildArenaMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildArenaImpl();
        }

        static partial void BuildArenaImpl()
        {
            var L = new MetalLevelWriter("MZ_Arena");
            MiddaySky(L);
            L.MainCamera(P(-600, 0, 500), Quaternion.identity);
            L.GameMode<MetalGameMode>(B("player"), actionCamera: true, minimalHUD: true, resetOnDeath: false);

            // The template arena: a 40 m floor ringed by walls, a raised centre platform with two ramps, corner
            // blocks to climb, a disc platform and a staircase.
            L.Box("Floor", (0, 0, -25), (4000, 4000, 50), "MI_Env_Floor", "Template");
            L.Wall("Wall_N", -2100, 2100, 2000, 2100, 400, "MI_Env_Wall", folder: "Template");
            L.Wall("Wall_S", -2100, 2100, -2100, -2000, 400, "MI_Env_Wall", folder: "Template");
            L.Wall("Wall_E", 2000, 2100, -2000, 2000, 400, "MI_Env_Wall", folder: "Template");
            L.Wall("Wall_W", -2100, -2000, -2000, 2000, 400, "MI_Env_Wall", folder: "Template");
            L.Box("Platform", (0, 0, 100), (900, 900, 200), "MI_Env_Dark", "Template");
            Rotate(L.Box("Ramp_E", (720, 0, 100), (620, 400, 30), "MI_Env_Dark", "Template"), 18f, 0f, 0f);
            Rotate(L.Box("Ramp_W", (-720, 0, 100), (620, 400, 30), "MI_Env_Dark", "Template"), 18f, 180f, 0f);
            foreach (var (n, x, y, h) in new[] { ("NE", 1500f, 1500f, 250f), ("NW", -1500f, 1500f, 150f), ("SE", 1500f, -1500f, 150f), ("SW", -1500f, -1500f, 250f) })
            {
                L.Box($"Block_{n}", (x, y, h / 2), (500, 500, h), "MI_Env_Dark", "Template");
            }
            L.Box("Disc", (900, -1100, 60), (600, 600, 120), "MI_Env_Wall", "Template", cylinder: true);
            for (int i = 0; i < 5; i++) L.Box($"Stair_{i}", (-900, 1000 + i * 60, 20 + i * 40), (400, 60, 40 + i * 80), "MI_Env_Wall", "Template");

            L.PlayerStart(new Vector3(-150, 0, 330), 0f);

            // Blue physics cubes, scattered on the platform and the floor.
            Material blue = BlueMaterial();
            var cubes = new[] { (330f, 240f, 200f, 90f, 15f), (-320f, -260f, 200f, 90f, -20f), (-430f, 180f, 200f, 90f, 40f), (1050f, 420f, 0f, 100f, 10f),
                (-1150f, -500f, 0f, 100f, -30f), (500f, 1250f, 0f, 100f, 25f), (-450f, -1200f, 0f, 100f, 5f), (1300f, -650f, 0f, 90f, 35f) };
            for (int i = 0; i < cubes.Length; i++)
            {
                var (x, y, baseZ, size, yaw) = cubes[i];
                GameObject c = L.Box($"PhysicsCube_{i}", (x, y, baseZ + size / 2 + 2), (size, size, size), "MI_Env_Floor", "Props/PhysicsCubes", yaw: yaw);
                c.isStatic = false;
                c.GetComponent<MeshRenderer>().sharedMaterial = blue;
                Rigidbody rb = c.AddComponent<Rigidbody>();
                rb.mass = 40f;
            }

            // Enemy patrols on the open floor; they mind their own business until the player opens fire.
            MetalBotArchetype shooter = B("shooter"), soldier = B("soldier");
            MetalBotArchetype[] heroes = { B("miner"), B("worker") };
            var groups = new (string name, (float x, float y) c, (MetalBotArchetype a, float dx, float dy)[] members)[]
            {
                ("East", (1300, 0), new[] { (shooter, 0f, -250f), (shooter, 150f, 120f), (shooter, -120f, 300f), (heroes[0], 60f, 30f) }),
                ("North", (0, 1300), new[] { (shooter, -250f, 0f), (soldier, 150f, 80f), (shooter, 300f, -60f) }),
                ("West", (-1300, 0), new[] { (shooter, 0f, 250f), (shooter, -150f, -150f), (heroes[1], 120f, -40f), (heroes[0], -60f, 60f) }),
                ("South", (0, -1300), new[] { (shooter, 220f, 0f), (shooter, -200f, 60f), (heroes[1], 0f, -150f) }),
            };
            foreach (var g in groups)
            {
                for (int i = 0; i < g.members.Length; i++)
                {
                    var (a, dx, dy) = g.members[i];
                    bool hero = a == heroes[0] || a == heroes[1];
                    MetalBot b = L.Bot($"{(hero ? "Hero" : "Enemy")}_{g.name}_{i}", a, (g.c.x + dx, g.c.y + dy, 0), (i * 97) % 360, null,
                        hero ? 450f : 650f, hero, hero ? MetalTeam.Neutral : (MetalTeam?)null, $"Robots/{g.name}");
                    b.patrolSpeedFraction = hero ? 0.45f : 0.6f;
                    b.docileUntilAttacked = true;
                }
            }
            L.BuildNavMesh();
            L.Save();
        }

        static Material BlueMaterial()
        {
            string path = MetalAssetPipeline.Materials + "/MI_Arena_PhysicsBlue.mat";
            var m = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (m == null)
            {
                m = new Material(Shader.Find("Universal Render Pipeline/Lit"));
                AssetDatabase.CreateAsset(m, path);
            }
            MetalAssetPipeline.SetupLit(m, new Color(0f, 0.11f, 0.85f).gamma, 0f, 0.55f, Color.black);
            EditorUtility.SetDirty(m);
            return m;
        }

        // ==================================================================================================
        // MZ_FlightTest: a 400 m floor; three parked Hero Stalkers to pilot, towers to weave round, a hovering Villain
        // squadron to shoot (and shoot the arms off) and a Shooter squad that fires back from the ground.
        // ==================================================================================================

        [MenuItem("Metalization/Build Level/Flight Test")]
        public static void BuildFlightMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildFlightTestImpl();
        }

        static partial void BuildFlightTestImpl()
        {
            var L = new MetalLevelWriter("MZ_FlightTest");
            MiddaySky(L);
            L.MainCamera(P(-600, 0, 400), Quaternion.identity);
            L.GameMode<MetalGameMode>(B("player"), resetOnDeath: false);
            L.Box("Floor", (0, 0, -25), (40000, 40000, 50), "MI_Env_Floor");
            // Launch apron: the player and three parked Hero Stalkers, 20 m apart, noses toward the range (+X).
            L.PlayerStart(new Vector3(0, 0, 120), 0f);
            L.Box("Apron", (1800, 0, 5), (2400, 6400, 10), "MI_Env_Wall", "Apron");
            float[] ys = { -2000, 0, 2000 };
            for (int i = 0; i < ys.Length; i++)
            {
                L.Box($"Pad_{i}", (1800, ys[i], 12), (1500, 1300, 6), "MI_Env_Hazard", "Apron");
                Stalker(L, $"Stalker_Hero_{i}", (1800, ys[i], 15), 0f, MetalTeam.Heroes, true, "Robots/Hangar");
            }
            // Towers to weave between on the way to the range.
            var towers = new[] { (6000f, -5000f, 7000f), (7500f, 3500f, 9000f), (11000f, -1500f, 6000f), (14000f, 5000f, 8000f), (16000f, -6000f, 10000f) };
            for (int i = 0; i < towers.Length; i++)
            {
                var (x, y, h) = towers[i];
                L.Box($"Tower_{i}", (x, y, h / 2), (1400, 1400, h), "MI_Env_Dark", "Towers", cylinder: true);
            }
            L.Box("Arch_Top", (9500, 0, 4200), (800, 6000, 600), "MI_Env_Wall", "Towers");
            L.Box("Arch_LegN", (9500, 2700, 2000), (800, 600, 4000), "MI_Env_Wall", "Towers");
            L.Box("Arch_LegS", (9500, -2700, 2000), (800, 600, 4000), "MI_Env_Wall", "Towers");
            // Target squadron: hovering Villain Stalkers spread across the range (no pilots yet).
            var targets = new[] { (12000f, 0f, 2500f, 180f), (13500f, -3000f, 3500f, 160f), (13500f, 3000f, 1800f, 200f), (16000f, -1000f, 4500f, 180f), (17000f, 2500f, 2800f, 210f) };
            for (int i = 0; i < targets.Length; i++)
            {
                var (x, y, z, yaw) = targets[i];
                Stalker(L, $"Stalker_Villain_{i}", (x, y, z), yaw, MetalTeam.Villains, false, "Robots/Targets");
            }
            // Ground fire: a Shooter squad by a bunker under the range.
            L.Box("Bunker", (12000, -6500, 150), (1200, 800, 300), "MI_Env_Wall", "Ground");
            var squad = new[] { (-700f, 0f), (0f, 700f), (700f, 100f) };
            for (int i = 0; i < squad.Length; i++)
            {
                L.Bot($"Shooter_{i}", B("shooter"), (12000 + squad[i].Item1, -6500 + 600 + squad[i].Item2, 0), 180f, null, 500f, folder: "Robots/Ground");
            }
            L.BuildNavMesh();
            L.Save();
        }
    }
}
