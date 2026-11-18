// Space battle (ue_setup_space.py): the capital ship interior every ship loads as its own copy, the ship class data
// and the battle scene (two capital ships broadside to broadside across 1.6 km of asteroid field).
//
// Interior layout (cm, origin = floor at the middle of the hangar mouth, which faces +Y):
//     hangar      y 0 .. -10200  (SM_Hangar_Shell; pads at y = -5500; life-support core at x -4300, y -9300)
//     corridor    y -10200 .. -12200, 8 x 6 m, through the shell's back door
//     spawn room  y -12200 .. -14600, 24 x 8 m high, four player starts in line with the corridor
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using UnityEngine.Rendering;
using static Metalization.Editor.MetalLevelWriter;

namespace Metalization.Editor
{
    public static partial class MetalLevels
    {
        const string InteriorScene = "MZ_CapitalShip_Interior";

        [MenuItem("Metalization/Build Level/Space Battle")]
        public static void BuildSpaceMenu()
        {
            MetalAssetPipeline.LoadManifest();
            LoadData();
            BuildSpaceImpl();
            RegisterScenes();
        }

        static partial void BuildSpaceImpl()
        {
            BuildInterior();
            MetalCapitalShipData data = BuildShipData();
            BuildBattle(data);
        }

        /// <summary>Box spanning lo..hi (cm).</summary>
        static GameObject Span(MetalLevelWriter L, string label, (float x, float y, float z) lo, (float x, float y, float z) hi, string material, string folder)
            => L.Box(label, ((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2), (hi.x - lo.x, hi.y - lo.y, hi.z - lo.z), material, folder);

        /// <summary>No sky to adapt to, and the camera crosses between the two maps: same dark ambient and exposure in both.</summary>
        static void SpaceLook(MetalLevelWriter L)
        {
            RenderSettings.skybox = null;
            RenderSettings.ambientMode = AmbientMode.Flat;
            RenderSettings.ambientLight = new Color(0.12f, 0.13f, 0.17f);
            RenderSettings.fog = false;
            L.PostProcess(0f, 0f, 0.25f, 0.8f);
        }

        static Camera SpaceCamera(MetalLevelWriter L, Vector3 position, Quaternion rotation)
        {
            Camera c = L.MainCamera(position, rotation);
            c.clearFlags = CameraClearFlags.SolidColor;
            c.backgroundColor = Color.black;
            c.farClipPlane = 6000f;
            return c;
        }

        static void BuildInterior()
        {
            // Opened on its own it plays like any level (the hangar parks Hero Stalkers by itself).
            var L = new MetalLevelWriter(InteriorScene);
            SpaceLook(L);
            SpaceCamera(L, P(0, -13600, 300), UE.Rot(0f, 90f, 0f));
            L.GameMode<MetalGameMode>(B("player"), resetOnDeath: false);

            GameObject hangarGo = L.Spawn("Hangar", Vector3.zero, 0f, "Hangar");
            MetalHangar hangar = hangarGo.AddComponent<MetalHangar>();
            hangar.shellModel = MetalAssetPipeline.ShipPrefab("Hangar_Shell");
            hangar.padModel = MetalAssetPipeline.ShipPrefab("Hangar_Pad");
            hangar.robotArchetype = B("stalker");
            hangar.team = MetalTeam.Heroes;
            hangar.respawnDelay = 20f;
            Prefab(L, "Hangar_BlastDoor", "BlastDoor", P(0, -10200, 0), 90f, "Hangar");
            int n = 0;
            foreach (float y in new[] { -2500f, -7500f })
            {
                foreach (float x in new[] { -3000f, 0f, 3000f }) L.PointLight($"HangarLight_{n++}", (x, y, 3700), (1f, 0.92f, 0.82f), 15000f, 9000f);
            }

            // Corridor: 8 m wide, 6 m high, 20 m long, from the shell's back door to the spawn room.
            const float Y0 = -12200, Y1 = -10200;
            Span(L, "Corridor_Floor", (-500, Y0, -50), (500, Y1, 0), "MI_Env_Floor", "Corridor");
            Span(L, "Corridor_Ceiling", (-500, Y0, 600), (500, Y1, 700), "MI_Env_Wall", "Corridor");
            Span(L, "Corridor_Wall_W", (-500, Y0, 0), (-400, Y1, 600), "MI_Env_Wall", "Corridor");
            Span(L, "Corridor_Wall_E", (400, Y0, 0), (500, Y1, 600), "MI_Env_Wall", "Corridor");
            L.PointLight("CorridorLight_0", (0, -10700, 560), (0.75f, 0.9f, 1f), 1800f, 1500f);
            L.PointLight("CorridorLight_1", (0, -11700, 560), (0.75f, 0.9f, 1f), 1800f, 1500f);

            // Spawn room: 24 x 24 m, 8 m high; its north wall has the corridor opening.
            const float R0 = -14600, R1 = -12200, W = 1200, H = 800;
            Span(L, "Spawn_Floor", (-W - 100, R0 - 100, -50), (W + 100, R1, 0), "MI_Env_Floor", "SpawnRoom");
            Span(L, "Spawn_Ceiling", (-W - 100, R0 - 100, H), (W + 100, R1, H + 100), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Wall_S", (-W - 100, R0 - 100, 0), (W + 100, R0, H), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Wall_W", (-W - 100, R0, 0), (-W, R1, H), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Wall_E", (W, R0, 0), (W + 100, R1, H), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Wall_N_W", (-W, R1 - 100, 0), (-400, R1, H), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Wall_N_E", (400, R1 - 100, 0), (W, R1, H), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Wall_N_Top", (-400, R1 - 100, 600), (400, R1, H), "MI_Env_Wall", "SpawnRoom");
            Span(L, "Spawn_Stripe", (-400, R1 - 102, 600), (400, R1 - 98, 630), "MI_Env_Glow", "SpawnRoom");
            // Two by two in line with the corridor, so walking straight ahead leads to the hangar.
            n = 0;
            foreach (float y in new[] { -13300f, -13900f })
            {
                foreach (float x in new[] { -250f, 250f }) L.PlayerStart(new Vector3(x, y, 100), 90f, $"PlayerStart_{n++}");
            }
            L.PointLight("SpawnLight_0", (-600, -13400, 760), (1f, 0.92f, 0.82f), 12000f, 3000f);
            L.PointLight("SpawnLight_1", (600, -13400, 760), (1f, 0.92f, 0.82f), 12000f, 3000f);
            L.BuildNavMesh(voxelSize: 0.15f);
            // Each ship moves its copy of the interior 20 km up at runtime, so nothing in it may be statically batched
            // (batched meshes would stay drawn where they were built).
            foreach (GameObject root in L.Scene.GetRootGameObjects())
            {
                foreach (Transform t in root.GetComponentsInChildren<Transform>(true)) GameObjectUtility.SetStaticEditorFlags(t.gameObject, 0);
            }
            L.Save();
        }

        static MetalCapitalShipData BuildShipData()
        {
            MetalCapitalShipData d = MetalAssetPipeline.DataAsset<MetalCapitalShipData>("DA_Ship_Capital");
            d.displayName = "Capital Ship";
            d.hullModel = MetalAssetPipeline.ShipPrefab("CapShip_Hull");
            d.enginesModel = MetalAssetPipeline.ShipPrefab("CapShip_Engines");
            d.shieldGeneratorModel = MetalAssetPipeline.ShipPrefab("CapShip_ShieldGen");
            d.commsModel = MetalAssetPipeline.ShipPrefab("CapShip_Comms");
            d.turretBaseModel = MetalAssetPipeline.ShipPrefab("CapShip_TurretBase");
            d.turretGunModel = MetalAssetPipeline.ShipPrefab("CapShip_TurretGun");
            d.hullHealth = 20000f;
            d.shieldGeneratorHealth = 2500f;
            d.enginesHealth = 4000f;
            d.commsHealth = 2000f;
            d.lifeSupportHealth = 3000f;
            d.shieldedHullDamageScale = 0.25f;
            d.teamColors = new List<MetalTeamColors>
            {
                new MetalTeamColors { team = MetalTeam.Heroes, body = new Color(0.16f, 0.2f, 0.36f).gamma, accent = new Color(0.45f, 0.52f, 0.72f).gamma },
                new MetalTeamColors { team = MetalTeam.Villains, body = new Color(0.36f, 0.17f, 0.13f).gamma, accent = new Color(0.72f, 0.5f, 0.42f).gamma },
            };
            d.interiorScene = InteriorScene;
            d.hangarRobot = B("stalker");
            d.hangarRespawnDelay = 20f;
            d.hangarMouthSize = new Vector2(80f, 30f);
            d.turretWeapon = W("TurretFlak");
            d.turretRange = 1200f;
            d.turretTurnRate = 70f;
            d.turretAimError = 2.5f;
            EditorUtility.SetDirty(d);
            AssetDatabase.SaveAssets();
            return d;
        }

        static void BuildBattle(MetalCapitalShipData data)
        {
            var L = new MetalLevelWriter("MZ_SpaceBattle");
            SpaceLook(L);
            SpaceCamera(L, P(0, -60000, 3000), UE.Rot(-5f, 90f, 0f));
            L.GameMode<MetalSpaceBattleGameMode>(B("player"), resetOnDeath: false);

            // Broadside to broadside, hangar mouths (starboard) facing each other across 1.6 km. Interiors float 20 km up
            // and 10 km apart, beyond the cameras' far plane. The player is the Heroes' third pilot, so their squadron
            // has one AI pilot fewer.
            var ships = new[]
            {
                ("Ship_Heroes", MetalTeam.Heroes, -80000f, 0f, (0f, 0f, 2000000f), 2),
                ("Ship_Villains", MetalTeam.Villains, 80000f, 180f, (1000000f, 0f, 2000000f), 3),
            };
            foreach (var (label, team, y, yaw, interior, pilots) in ships)
            {
                GameObject go = L.Spawn(label, P(0, y, 0), yaw, "Ships");
                MetalCapitalShip ship = go.AddComponent<MetalCapitalShip>();
                ship.shipData = data;
                ship.team = team;
                ship.interiorLocation = P(interior.Item1, interior.Item2, interior.Item3);
                ship.aiPilots = pilots;
            }

            // High sun along the ships' length; a blue fill lifts the shadows.
            Light sun = L.Sun(-50f, 20f, new Color(1f, 0.957f, 0.894f), 2.2f);
            sun.shadowNormalBias = 1f;
            GameObject fillGo = L.Spawn("Fill", Vector3.up * 20f, 0f, "Lighting");
            fillGo.transform.rotation = UE.Rot(30f, -160f, 0f);
            Light fill = fillGo.AddComponent<Light>();
            fill.type = LightType.Directional;
            fill.color = new Color(150f / 255f, 175f / 255f, 1f);
            fill.intensity = 0.5f;
            fill.shadows = LightShadows.None;

            // Asteroid field around (not between) the ships, so the lanes between the hangars stay open.
            var rng = new System.Random(7);
            float Range(float a, float b) => a + (float)rng.NextDouble() * (b - a);
            for (int i = 0; i < 36; i++)
            {
                float x, y, z;
                do
                {
                    x = Range(-200000, 200000);
                    y = Range(-160000, 160000);
                    z = Range(-60000, 60000);
                } while (!(Mathf.Abs(x) > 50000 || Mathf.Abs(y) > 120000));
                float s = Range(1500, 9000);
                GameObject a = L.Box($"Asteroid_{i:00}", (x, y, z), (s, s * Range(0.6f, 1f), s * Range(0.5f, 0.9f)), "MI_Env_Dark", "Asteroids");
                Rotate(a, Range(-60, 60), Range(0, 360), Range(0, 360));
            }
            L.Save();
        }
    }
}
