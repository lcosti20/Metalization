// The editor half of the asset pipeline (the Unity counterpart of ue_import_assets.py + ue_setup_gameplay.py):
// builds materials, weapon and robot data assets, the sound library, ship-part prefabs and the shared asset
// registry from the Blender export (Assets/Metalization/Models/manifest.json) and the synthesized sounds, then
// the levels (MetalLevels). Run it from Metalization > Build All Assets, or headless:
//   Unity.exe -batchmode -projectPath . -executeMethod Metalization.Editor.MetalAssetPipeline.BuildAllBatch -quit
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace Metalization.Editor
{
    public static class MetalAssetPipeline
    {
        public const string Root = "Assets/Metalization";
        public const string Data = Root + "/Data";
        public const string Materials = Root + "/Materials";
        public const string Prefabs = Root + "/Prefabs";
        public const string Textures = Root + "/Textures";

        public static Dictionary<string, object> Manifest { get; private set; }

        [MenuItem("Metalization/Build All Assets", priority = 0)]
        public static void BuildAll()
        {
            BuildAssets();
            MetalLevels.BuildAll();
            Debug.Log("[MZSetup] DONE");
        }

        [MenuItem("Metalization/Build Assets Only (no levels)", priority = 1)]
        public static void BuildAssets()
        {
            LoadManifest();
            EnsureFolders();
            EnsureLayers();
            BuildCoreMaterials();
            BuildModelMaterials();
            RemapModels();
            Dictionary<string, MetalWeaponData> weapons = BuildWeapons();
            Dictionary<string, MetalBotArchetype> bots = BuildArchetypes(weapons);
            MetalSoundLibrary sounds = BuildSoundLibrary();
            BuildShipPrefabs();
            BuildGameAssets(sounds, bots);
            AssetDatabase.SaveAssets();
            Debug.Log($"[MZSetup] assets built: {weapons.Count} weapons, {bots.Count} robots, {sounds.sounds.Count} sounds");
        }

        /// <summary>Batch-mode entry point (exits with an error code on failure).</summary>
        public static void BuildAllBatch()
        {
            try
            {
                BuildAll();
            }
            catch (System.Exception e)
            {
                Debug.LogException(e);
                EditorApplication.Exit(1);
            }
        }

        public static void LoadManifest()
        {
            string path = Path.Combine(MetalImportRules.ModelsFolder, "manifest.json");
            Manifest = File.Exists(path) ? MetalJson.Parse(File.ReadAllText(path)) as Dictionary<string, object> : new Dictionary<string, object>();
        }

        static void EnsureFolders()
        {
            foreach (string f in new[] { Data, Materials, Materials + "/Models", Prefabs, Prefabs + "/Ships", Textures, Root + "/Resources", Root + "/Scenes" })
            {
                if (!AssetDatabase.IsValidFolder(f))
                {
                    string parent = Path.GetDirectoryName(f).Replace('\\', '/');
                    AssetDatabase.CreateFolder(parent, Path.GetFileName(f));
                }
            }
        }

        /// <summary>The layers gameplay code looks up by name: robots, projectiles and trigger volumes.</summary>
        public static void EnsureLayers()
        {
            var tagManager = new SerializedObject(AssetDatabase.LoadAllAssetsAtPath("ProjectSettings/TagManager.asset")[0]);
            SerializedProperty layers = tagManager.FindProperty("layers");
            string[] wanted = { MetalPhysics.BotLayerName, MetalPhysics.ProjectileLayerName, MetalPhysics.TriggerLayerName, MetalPhysics.BlockerLayerName };
            int slot = 8;
            foreach (string name in wanted)
            {
                bool exists = false;
                for (int i = 0; i < layers.arraySize; i++) exists |= layers.GetArrayElementAtIndex(i).stringValue == name;
                if (exists) continue;
                while (slot < layers.arraySize && !string.IsNullOrEmpty(layers.GetArrayElementAtIndex(slot).stringValue)) slot++;
                if (slot >= layers.arraySize) break;
                layers.GetArrayElementAtIndex(slot).stringValue = name;
            }
            tagManager.ApplyModifiedProperties();
            // Projectiles never collide with anything physically (they sweep), and triggers only with robots.
            int proj = LayerMask.NameToLayer(MetalPhysics.ProjectileLayerName);
            int trig = LayerMask.NameToLayer(MetalPhysics.TriggerLayerName);
            for (int i = 0; i < 32; i++)
            {
                if (proj >= 0) Physics.IgnoreLayerCollision(proj, i, true);
            }
        }

        // ==================================================================================================
        // Materials
        // ==================================================================================================

        static Material SaveMaterial(string path, Shader shader, System.Action<Material> setup)
        {
            var mat = AssetDatabase.LoadAssetAtPath<Material>(path);
            if (mat == null)
            {
                mat = new Material(shader);
                AssetDatabase.CreateAsset(mat, path);
            }
            else if (mat.shader != shader)
            {
                mat.shader = shader;
            }
            setup(mat);
            EditorUtility.SetDirty(mat);
            return mat;
        }

        static Shader Lit => Shader.Find("Universal Render Pipeline/Lit");

        /// <summary>URP Lit with emission switched on (black), so hit flashes and glows can drive it per renderer.</summary>
        public static void SetupLit(Material m, Color baseColor, float metallic, float smoothness, Color emission)
        {
            m.SetColor("_BaseColor", baseColor);
            m.SetFloat("_Metallic", metallic);
            m.SetFloat("_Smoothness", smoothness);
            m.EnableKeyword("_EMISSION");
            m.SetColor("_EmissionColor", emission);
            m.globalIlluminationFlags = MaterialGlobalIlluminationFlags.None;
        }

        /// <summary>The grid plates of the level geometry: a light/dark checker with thin seams, one repeat per 2.56 m.</summary>
        static Texture2D GridTexture()
        {
            string path = Textures + "/T_Grid.png";
            if (!File.Exists(path))
            {
                const int Size = 256;
                var tex = new Texture2D(Size, Size, TextureFormat.RGBA32, true);
                for (int y = 0; y < Size; y++)
                {
                    for (int x = 0; x < Size; x++)
                    {
                        bool checker = ((x / 128) + (y / 128)) % 2 == 0;
                        float v = checker ? 0.42f : 0.36f;
                        bool seam = x % 128 < 2 || y % 128 < 2 || x % 32 == 0 || y % 32 == 0;
                        if (seam) v = x % 128 < 2 || y % 128 < 2 ? 0.26f : v - 0.03f;
                        Color c = new Color(v, v, v, 1f).gamma;
                        tex.SetPixel(x, y, c);
                    }
                }
                File.WriteAllBytes(path, tex.EncodeToPNG());
                AssetDatabase.ImportAsset(path);
                var imp = (TextureImporter)AssetImporter.GetAtPath(path);
                imp.wrapMode = TextureWrapMode.Repeat;
                imp.anisoLevel = 8;
                imp.SaveAndReimport();
            }
            return AssetDatabase.LoadAssetAtPath<Texture2D>(path);
        }

        static void BuildCoreMaterials()
        {
            Shader glow = Shader.Find("Metalization/Glow");
            Shader particle = Shader.Find("Metalization/Particle");
            SaveMaterial(Materials + "/M_FX_Glow.mat", glow, m => m.SetColor("_BaseColor", Color.white));
            SaveMaterial(Materials + "/M_FX_Additive.mat", particle, m =>
            {
                m.SetColor("_BaseColor", new Color(2.5f, 2.5f, 2.5f, 1f));
                m.SetFloat("_Beam", 0f);
                m.SetFloat("_SrcBlend", (float)UnityEngine.Rendering.BlendMode.One);
                m.SetFloat("_DstBlend", (float)UnityEngine.Rendering.BlendMode.One);
            });
            SaveMaterial(Materials + "/M_FX_Beam.mat", particle, m =>
            {
                m.SetColor("_BaseColor", new Color(3f, 3f, 3f, 1f));
                m.SetFloat("_Beam", 1f);
                m.SetFloat("_SrcBlend", (float)UnityEngine.Rendering.BlendMode.One);
                m.SetFloat("_DstBlend", (float)UnityEngine.Rendering.BlendMode.One);
            });
            SaveMaterial(Materials + "/M_FX_Smoke.mat", particle, m =>
            {
                m.SetColor("_BaseColor", Color.white);
                m.SetFloat("_Beam", 0f);
                m.SetFloat("_SrcBlend", (float)UnityEngine.Rendering.BlendMode.SrcAlpha);
                m.SetFloat("_DstBlend", (float)UnityEngine.Rendering.BlendMode.OneMinusSrcAlpha);
            });
            SaveMaterial(Materials + "/M_EnergyField.mat", Shader.Find("Metalization/EnergyField"), m => m.SetColor("_BaseColor", new Color(1f, 0.3f, 0.1f) * 3f));
            SaveMaterial(Materials + "/M_Portal.mat", Shader.Find("Metalization/Portal"), m => { });

            // Level geometry: the grid plates tinted per use (colours from the Unreal MI_Env_* instances, linear).
            Texture2D grid = GridTexture();
            void Env(string name, Color linearTint, float metallic, float roughness, float glowAmount)
            {
                SaveMaterial($"{Materials}/MI_Env_{name}.mat", Lit, m =>
                {
                    Color tint = linearTint.gamma;
                    SetupLit(m, tint, metallic, 1f - roughness, glowAmount > 0f ? (linearTint * glowAmount).gamma : Color.black);
                    m.SetTexture("_BaseMap", grid);
                });
            }
            Env("Floor", new Color(0.5f, 0.42f, 0.34f), 0.15f, 0.85f, 0f);
            Env("Wall", new Color(0.45f, 0.33f, 0.24f), 0.35f, 0.7f, 0f);
            Env("Dark", new Color(0.22f, 0.19f, 0.17f), 0.2f, 0.8f, 0f);
            Env("Hazard", new Color(0.9f, 0.55f, 0.08f), 0.35f, 0.7f, 0f);
            Env("Glow", new Color(0.1f, 0.6f, 1f), 0.35f, 0.7f, 4f);
            Env("Metal", new Color(0.55f, 0.5f, 0.45f), 0.35f, 0.7f, 0f);

            // Production Alpha: the same grid metal in faction colours (Unreal's MI_Alpha_* instances of M_Env_Metal).
            void Alpha(string name, Color linearTint, float glow)
            {
                SaveMaterial($"{Materials}/MI_Alpha_{name}.mat", Lit, m =>
                {
                    SetupLit(m, linearTint.gamma, 0.35f, 0.3f, glow > 0f ? (linearTint * glow).gamma : Color.black);
                    m.SetTexture("_BaseMap", grid);
                });
            }
            Alpha("Rock", new Color(0.36f, 0.31f, 0.27f), 0f);
            Alpha("Ground", new Color(0.42f, 0.37f, 0.30f), 0f);
            Alpha("Concrete", new Color(0.5f, 0.5f, 0.52f), 0f);
            Alpha("Blue", new Color(0.12f, 0.28f, 0.7f), 0f);
            Alpha("Red", new Color(0.62f, 0.1f, 0.07f), 0f);
            Alpha("BlueGlow", new Color(0.15f, 0.5f, 1f), 5f);
            Alpha("RedGlow", new Color(1f, 0.12f, 0.05f), 5f);
            Alpha("Fence", new Color(1f, 0.25f, 0.1f), 3f);
        }

        /// <summary>One material per Blender material in the manifest: lit graybox colours, glowing strips, additive decals.</summary>
        static void BuildModelMaterials()
        {
            Dictionary<string, object> mats = Manifest.O("materials");
            if (mats == null) return;
            Shader glow = Shader.Find("Metalization/Glow");
            foreach (KeyValuePair<string, object> kv in mats)
            {
                string kind = kv.Value.S("kind") ?? "lit";
                float[] c = (kv.Value as Dictionary<string, object>).TryGetValue("color", out object col) ? col.Floats() : new float[0];
                // Blender colours are linear; Unity material colours are authored in sRGB.
                Color linear = c.Length >= 3 ? new Color(c[0], c[1], c[2], 1f) : Color.white;
                string path = $"{Materials}/Models/{kv.Key}.mat";
                if (kind == "decal")
                {
                    SaveMaterial(path, glow, m => m.SetColor("_BaseColor", (linear * 2f).gamma));
                }
                else if (kind == "light")
                {
                    SaveMaterial(path, Lit, m => SetupLit(m, Color.black, 0f, 0.5f, (linear * 2f).gamma));
                }
                else
                {
                    // Flat graybox colour reads better matte (Unreal's RoughnessMin 0.6).
                    SaveMaterial(path, Lit, m => SetupLit(m, linear.gamma, 0f, 0.4f, Color.black));
                }
            }
        }

        /// <summary>Reimports the FBX files whose material remaps are missing (first run, or new materials).</summary>
        static void RemapModels()
        {
            var materials = AssetDatabase.FindAssets("t:Material", new[] { Materials + "/Models" })
                .Select(g => AssetDatabase.LoadAssetAtPath<Material>(AssetDatabase.GUIDToAssetPath(g))).Where(m => m != null).ToList();
            AssetDatabase.StartAssetEditing();
            try
            {
                foreach (string guid in AssetDatabase.FindAssets("t:Model", new[] { MetalImportRules.ModelsFolder }))
                {
                    string path = AssetDatabase.GUIDToAssetPath(guid);
                    var importer = AssetImporter.GetAtPath(path) as ModelImporter;
                    if (importer == null) continue;
                    Dictionary<AssetImporter.SourceAssetIdentifier, Object> map = importer.GetExternalObjectMap();
                    bool missing = false;
                    foreach (Material m in materials)
                    {
                        var id = new AssetImporter.SourceAssetIdentifier(typeof(Material), m.name);
                        if (!map.TryGetValue(id, out Object o) || o != m) missing = true;
                    }
                    if (missing) importer.SaveAndReimport();
                }
            }
            finally
            {
                AssetDatabase.StopAssetEditing();
            }
        }

        // ==================================================================================================
        // Data assets
        // ==================================================================================================

        public static T DataAsset<T>(string name) where T : ScriptableObject
        {
            string path = $"{Data}/{name}.asset";
            var a = AssetDatabase.LoadAssetAtPath<T>(path);
            if (a == null)
            {
                a = ScriptableObject.CreateInstance<T>();
                AssetDatabase.CreateAsset(a, path);
            }
            return a;
        }

        /// <summary>Resets an existing data asset to its class defaults (keeping its name), so stale values don't linger.</summary>
        static void ResetToDefaults<T>(T asset) where T : ScriptableObject
        {
            T fresh = ScriptableObject.CreateInstance<T>();
            CopyKeepingName(fresh, asset);
            Object.DestroyImmediate(fresh);
        }

        public static void CopyKeepingName(Object from, Object to)
        {
            string name = to.name;
            EditorUtility.CopySerialized(from, to);
            to.name = name;
        }

        public static GameObject Model(string relative) => AssetDatabase.LoadAssetAtPath<GameObject>($"{MetalImportRules.ModelsFolder}/{relative}.fbx");

        static GameObject Held(string weaponId, string holder) => Model($"Weapons/{weaponId}__on__{holder}");

        static MetalWeaponLevel Level(Dictionary<string, GameObject> models, float damage, float interval, int clip = 0, int reserve = 0)
        {
            var lv = new MetalWeaponLevel { damage = damage, fireInterval = interval, clipSize = clip, maxReserveAmmo = reserve };
            foreach (var kv in models)
            {
                if (kv.Value != null) lv.heldModels.Add(new MetalHeldModel { archetypeId = kv.Key, model = kv.Value });
            }
            return lv;
        }

        static Dictionary<string, GameObject> M(params (string holder, GameObject model)[] entries) => entries.ToDictionary(e => e.holder, e => e.model);

        static Color Lin(float r, float g, float b) => new Color(r, g, b).gamma;

        /// <summary>Weapon definitions (values from the Unreal pipeline, converted to metres).</summary>
        static Dictionary<string, MetalWeaponData> BuildWeapons()
        {
            var w = new Dictionary<string, MetalWeaponData>();
            MetalWeaponData W(string key, System.Action<MetalWeaponData> setup)
            {
                MetalWeaponData d = DataAsset<MetalWeaponData>("WD_" + key);
                // Reset to defaults first, so values removed from this script don't linger.
                ResetToDefaults(d);
                setup(d);
                EditorUtility.SetDirty(d);
                w[key] = d;
                return d;
            }
            W("Laser", d =>
            {
                d.displayName = "Laser"; d.fireMode = MetalFireMode.Hitscan; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 0.6f; d.range = 60f; d.muzzleOffset = 0.4f; d.tracerColor = Lin(1f, 0.2f, 0.08f); d.propModel = Model("Props/laser_l1");
                d.levels = new List<MetalWeaponLevel>
                {
                    Level(M(("player", Held("laser_l1", "player"))), 8f, 0.15f),
                    Level(M(("player", Held("laser_l2", "player"))), 11f, 0.14f),
                    Level(M(("player", Held("laser_l3", "player"))), 15f, 0.12f),
                };
            });
            W("Repeater", d =>
            {
                d.displayName = "Repeater"; d.fireMode = MetalFireMode.Projectile; d.automatic = true; d.reloadTime = 1.3f;
                d.spreadDegrees = 1.2f; d.projectileSpeed = 65f; d.muzzleOffset = 0.45f; d.tracerColor = Lin(1f, 0.85f, 0.3f); d.propModel = Model("Props/repeater_l1");
                d.levels = new List<MetalWeaponLevel>
                {
                    Level(M(("player", Held("repeater_l1", "player")), ("shooter", Held("repeater_l1", "shooter"))), 13f, 0.1f, 30, 240),
                    Level(M(("player", Held("repeater_l3", "player"))), 19f, 0.085f, 40, 320),
                };
            });
            W("Shotgun", d =>
            {
                d.displayName = "Shotgun"; d.fireMode = MetalFireMode.Hitscan; d.automatic = false; d.reloadTime = 1.6f;
                d.pellets = 8; d.spreadDegrees = 6f; d.range = 25f; d.muzzleOffset = 0.5f; d.tracerColor = Lin(1f, 0.55f, 0.15f); d.propModel = Model("Props/shotgun_l1");
                d.levels = new List<MetalWeaponLevel>
                {
                    Level(M(("player", Held("shotgun_l1", "player")), ("shooter", Held("shotgun_l1", "shooter"))), 9f, 0.7f, 6, 36),
                    Level(M(("player", Held("shotgun_l3", "player"))), 12f, 0.55f, 8, 48),
                };
            });
            W("RocketLauncher", d =>
            {
                d.displayName = "Rocket Launcher"; d.fireMode = MetalFireMode.Projectile; d.automatic = false; d.reloadTime = 2f;
                d.spreadDegrees = 0.3f; d.projectileSpeed = 28f; d.explosionRadius = 3.8f; d.muzzleOffset = 0.6f;
                d.tracerColor = Lin(1f, 0.35f, 0.05f); d.propModel = Model("Props/rocketlauncher_l1");
                d.levels = new List<MetalWeaponLevel>
                {
                    Level(M(("player", Held("rocketlauncher_l1", "player"))), 120f, 0.9f, 3, 15),
                    Level(M(("player", Held("rocketlauncher_l3", "player"))), 160f, 0.75f, 4, 20),
                };
            });
            W("EnemyLaser", d =>
            {
                d.displayName = "Enemy Laser"; d.fireMode = MetalFireMode.Hitscan; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 1f; d.range = 50f; d.muzzleOffset = 0.4f; d.tracerColor = Lin(1f, 0.1f, 0.25f);
                d.levels = new List<MetalWeaponLevel> { Level(M(("shooter", Held("laser_enemy", "shooter"))), 3f, 0.22f) };
            });
            W("ArmBlasters", d =>
            {
                d.displayName = "Arm Blasters"; d.fireMode = MetalFireMode.Projectile; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 1.5f; d.projectileSpeed = 32f; d.muzzleOffset = 0f; d.tracerColor = Lin(0.8f, 0.2f, 1f);
                d.levels = new List<MetalWeaponLevel> { Level(M(), 7f, 0.2f) };
            });
            W("TwinMiniguns", d =>
            {
                d.displayName = "Twin Miniguns"; d.fireMode = MetalFireMode.Hitscan; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 3.5f; d.range = 60f; d.muzzleOffset = 0f; d.tracerColor = Lin(1f, 0.9f, 0.2f);
                d.levels = new List<MetalWeaponLevel> { Level(M(), 3.5f, 0.08f) };
            });
            // The Brute's shoulder rocket launcher: its secondary attack, slower and weaker than the pickup launcher.
            W("BruteRockets", d =>
            {
                d.displayName = "Shoulder Rockets"; d.fireMode = MetalFireMode.Projectile; d.automatic = false; d.infiniteAmmo = true;
                d.spreadDegrees = 0.8f; d.projectileSpeed = 24f; d.explosionRadius = 3.2f; d.muzzleOffset = 0.7f; d.noiseRadius = 35f;
                d.tracerColor = Lin(1f, 0.35f, 0.05f);
                d.levels = new List<MetalWeaponLevel> { Level(M(), 60f, 3.5f) };
            });
            // The Stalker's four arm guns fire in turn; slugs leave from the gun tips.
            W("StalkerCannons", d =>
            {
                d.displayName = "Arm Cannons"; d.fireMode = MetalFireMode.Projectile; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 0.4f; d.projectileSpeed = 300f; d.muzzleOffset = 2.9f; d.projectileScale = 4f; d.tracerColor = Lin(0.35f, 1f, 0.45f);
                d.levels = new List<MetalWeaponLevel> { Level(M(), 12f, 0.08f) };
            });
            W("TurretFlak", d =>
            {
                d.displayName = "Turret Flak"; d.fireMode = MetalFireMode.Projectile; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 1f; d.projectileSpeed = 180f; d.projectileScale = 6f; d.tracerColor = Lin(1f, 0.55f, 0.15f);
                d.levels = new List<MetalWeaponLevel> { Level(M(), 10f, 0.9f) };
            });
            W("Claw", d =>
            {
                d.displayName = "Bare Servos"; d.fireMode = MetalFireMode.Melee; d.automatic = true; d.infiniteAmmo = true;
                d.range = 1.3f; d.tracerColor = Lin(1f, 0.6f, 0.2f);
                d.levels = new List<MetalWeaponLevel> { Level(M(), 14f, 0.8f) };
            });
            W("EMPGrenade", d =>
            {
                d.displayName = "EMP Grenade"; d.fireMode = MetalFireMode.Thrown; d.projectileSpeed = 15f; d.projectileGravityScale = 1f;
                d.explosionRadius = 4.2f; d.stunDuration = 4f; d.tracerColor = Lin(0.3f, 0.6f, 1f); d.propModel = Model("Props/grenade_emp");
                d.levels = new List<MetalWeaponLevel> { Level(M(), 25f, 1f) };
            });
            // Marksman rifle: hitscan, huge damage, slow; only accurate down the scope, and loud.
            W("Sniper", d =>
            {
                d.displayName = "Sniper"; d.fireMode = MetalFireMode.Hitscan; d.automatic = false; d.reloadTime = 2f;
                d.spreadDegrees = 0f; d.hipSpreadDegrees = 5f; d.range = 220f; d.muzzleOffset = 0.8f; d.tracerColor = Lin(0.3f, 0.95f, 1f);
                d.aimFOV = 16f; d.aimArmLengthScale = 0f; d.scopeOverlay = true; d.aimSensitivity = 0.3f; d.noiseRadius = 45f; d.propModel = Model("Props/sniper");
                d.levels = new List<MetalWeaponLevel> { Level(M(("player", Held("sniper", "player"))), 150f, 1.1f, 4, 20) };
            });
            // The Red marksmen's version: weaker, slow, with a laser sight so the player can read where it aims.
            W("EnemySniper", d =>
            {
                d.displayName = "Marksman Rifle"; d.fireMode = MetalFireMode.Hitscan; d.automatic = true; d.infiniteAmmo = true;
                d.spreadDegrees = 0.4f; d.range = 90f; d.muzzleOffset = 0.8f; d.tracerColor = Lin(1f, 0.15f, 0.1f); d.laserSight = true;
                d.levels = new List<MetalWeaponLevel> { Level(M(("shooter", Held("sniper", "shooter"))), 28f, 2.6f) };
            });
            var sounds = new Dictionary<string, string>
            {
                ["Laser"] = "laser_fire", ["Repeater"] = "repeater_fire", ["Shotgun"] = "shotgun_fire", ["RocketLauncher"] = "rocket_fire",
                ["BruteRockets"] = "rocket_fire", ["EnemyLaser"] = "enemy_laser_fire", ["ArmBlasters"] = "blaster_fire", ["TwinMiniguns"] = "minigun_fire",
                ["StalkerCannons"] = "blaster_fire", ["TurretFlak"] = "blaster_fire", ["Claw"] = "robot_hit", ["EMPGrenade"] = "",
                ["Sniper"] = "sniper_fire", ["EnemySniper"] = "sniper_fire",
            };
            foreach (var kv in sounds) w[kv.Key].fireSound = kv.Value;
            return w;
        }

        static MetalHitZone Zone(string name, string bone, float radiusCm, float mult, float durability = 0f, bool disables = false, string muzzle = "")
        {
            return new MetalHitZone
            {
                zoneName = name, bone = bone, radius = radiusCm * UE.CmToM, damageMultiplier = mult, durability = durability,
                disablesWeaponWhenBroken = disables, disablesMuzzleBone = muzzle,
            };
        }

        static List<MetalTeamColors> Faction()
        {
            return new List<MetalTeamColors>
            {
                new MetalTeamColors { team = MetalTeam.Heroes, body = Lin(0.11f, 0.24f, 0.62f), accent = Lin(0.5f, 0.72f, 1f) },
                new MetalTeamColors { team = MetalTeam.Villains, body = Lin(0.6f, 0.11f, 0.07f), accent = Lin(1f, 0.5f, 0.22f) },
            };
        }

        /// <summary>One archetype per exported character, then the gameplay robots tuned by hand.</summary>
        static Dictionary<string, MetalBotArchetype> BuildArchetypes(Dictionary<string, MetalWeaponData> w)
        {
            var bots = new Dictionary<string, MetalBotArchetype>();
            Dictionary<string, object> chars = Manifest.O("characters") ?? new Dictionary<string, object>();
            string Pascal(string id) => string.Concat(id.Split('_').Where(p => p.Length > 0).Select(p => char.ToUpperInvariant(p[0]) + p.Substring(1)));
            foreach (var kv in chars)
            {
                string cid = kv.Key;
                float[] mn = kv.Value.A("bounds_min_m").Floats();
                float[] mx = kv.Value.A("bounds_max_m").Floats();
                float heightCm = mn.Length == 3 && mx.Length == 3 ? (mx[2] - mn[2]) * 100f : 120f;
                MetalBotArchetype a = DataAsset<MetalBotArchetype>("DA_Bot_" + Pascal(cid));
                ResetToDefaults(a);
                a.archetypeId = cid;
                a.displayName = kv.Value.S("display_name");
                a.team = kv.Value.S("faction") == "Heroes" ? MetalTeam.Heroes : MetalTeam.Villains;
                a.model = Model("Characters/" + cid);
                a.parts = new List<GameObject>();
                Dictionary<string, object> parts = kv.Value.O("parts");
                if (parts != null)
                {
                    foreach (string p in parts.Keys)
                    {
                        if (p == "hand" || p == "hand_1" || p == "arm_2") continue;
                        GameObject pm = Model($"Characters/{cid}__{p}");
                        if (pm != null) a.parts.Add(pm);
                    }
                }
                a.capsuleRadius = 0.34f;
                a.capsuleHalfHeight = Mathf.Max(30f, heightCm * 0.5f) * UE.CmToM;
                a.hackable = false;
                a.maxHealth = 100f;
                a.walkSpeed = 3.5f;
                // Voice pitch by size: 1.0 at 1.2 m tall, deeper for taller robots, higher for smaller ones.
                a.voicePitch = Mathf.Min(1.4f, Mathf.Max(0.5f, Mathf.Round(Mathf.Sqrt(120f / Mathf.Max(30f, heightCm)) * 100f) / 100f));
                bots[cid] = a;
            }

            if (bots.TryGetValue("player", out MetalBotArchetype player))
            {
                player.displayName = "Player";
                player.unarmedPart = Model("Characters/player__hand");
                player.capsuleRadius = 0.30f; player.capsuleHalfHeight = 0.66f; player.maxHealth = 150f; player.walkSpeed = 6f;
                player.jumpVelocity = 7f; player.maxJumps = 2; player.sprintSpeed = 10f; player.crouchSpeed = 2.6f; player.crouchHeightFraction = 0.65f;
                player.animatedWeaponSwap = true; player.weaponSwapDuration = 0.8f; player.hasJetpack = true;
                player.rig = new MetalBotRig
                {
                    strideLength = 2f, legSwingDegrees = 40f, kneeBendDegrees = 50f, bobHeight = 0.04f, armSwingDegrees = 20f,
                    crouchDrop = 0.22f, handL = "bip_hand_L", rackOffset = UE.MeshVec(0f, -24f, 70f),
                };
                player.hitZones = new List<MetalHitZone> { Zone("Head", "bip_head", 18f, 1.5f) };
                player.startingWeapons = new List<MetalWeaponData> { w["Laser"], w["Repeater"] };
                player.meleeWeapon = w["Claw"]; player.grenadeWeapon = w["EMPGrenade"]; player.startingGrenades = 3;
                player.cameraDistance = 2.6f; player.cameraOffset = UE.Vec(0f, 50f, 60f);
            }
            if (bots.TryGetValue("shooter", out MetalBotArchetype shooter))
            {
                shooter.displayName = "Shooter"; shooter.unarmedPart = Model("Characters/shooter__arm_2"); shooter.hackable = true;
                shooter.capsuleRadius = 0.34f; shooter.capsuleHalfHeight = 0.65f; shooter.maxHealth = 60f; shooter.walkSpeed = 4f; shooter.possessionDuration = 25f;
                shooter.rig = new MetalBotRig
                {
                    thighL = "bip_hip_L", thighR = "bip_hip_R", kneeL = "ankle_1_L", kneeR = "ankle_1_R",
                    upperArmRReady = UE.Rot(-10f, -80f, 0f), upperArmLReady = UE.Rot(0f, 0f, -75f),
                    strideLength = 1.2f, legSwingDegrees = 26f, kneeBendDegrees = -30f, bobHeight = 0.03f, armSwingDegrees = 0f, aimFromShoulder = true,
                };
                shooter.hitZones = new List<MetalHitZone> { Zone("Head", "bip_head", 20f, 2f), Zone("GunArm", "bip_lowerArm_R", 24f, 1f, 30f, true) };
                shooter.startingWeapons = new List<MetalWeaponData> { w["EnemyLaser"] }; shooter.meleeWeapon = w["Claw"]; shooter.teamColors = Faction();
                shooter.sightRange = 32f; shooter.preferredRange = 8.5f; shooter.aimErrorDegrees = 4.5f; shooter.burstDuration = 1.1f; shooter.burstPause = 1.1f;
                shooter.retreatHealthFraction = 0.3f; shooter.cameraDistance = 3.2f; shooter.cameraOffset = UE.Vec(0f, 60f, 70f);

                // Red marksman: a Shooter body with a long rifle that stays back and picks targets off from cover.
                MetalBotArchetype marksman = DataAsset<MetalBotArchetype>("DA_Bot_Marksman");
                CopyKeepingName(shooter, marksman);
                marksman.displayName = "Marksman"; marksman.maxHealth = 55f; marksman.walkSpeed = 3.3f;
                marksman.hitZones = new List<MetalHitZone> { Zone("Head", "bip_head", 20f, 2f), Zone("GunArm", "bip_lowerArm_R", 24f, 1f, 30f, true) };
                marksman.startingWeapons = new List<MetalWeaponData> { w["EnemySniper"] };
                marksman.sightRange = 70f; marksman.sightHalfAngle = 40f; marksman.preferredRange = 35f; marksman.aimErrorDegrees = 1.2f;
                marksman.burstDuration = 0.3f; marksman.burstPause = 2.4f; marksman.squadAlertRadius = 25f; marksman.retreatHealthFraction = 0.3f;
                bots["marksman"] = marksman;
            }
            if (bots.TryGetValue("soldier", out MetalBotArchetype soldier))
            {
                soldier.displayName = "Soldier"; soldier.hackable = true;
                soldier.capsuleRadius = 0.40f; soldier.capsuleHalfHeight = 0.68f; soldier.maxHealth = 130f; soldier.walkSpeed = 3.2f; soldier.possessionDuration = 20f;
                soldier.rig = new MetalBotRig
                {
                    upperArmRReady = UE.Rot(0f, 0f, 50f), upperArmLReady = UE.Rot(0f, 0f, -50f),
                    strideLength = 1.4f, legSwingDegrees = 22f, kneeBendDegrees = 30f, bobHeight = 0.04f, aimWithBothArms = true,
                };
                soldier.hitZones = new List<MetalHitZone> { Zone("Head", "bip_head", 22f, 2f) };
                soldier.startingWeapons = new List<MetalWeaponData> { w["ArmBlasters"] }; soldier.meleeWeapon = w["Claw"]; soldier.teamColors = Faction();
                soldier.muzzleBones = new List<string> { "bip_lowerArm_R", "bip_lowerArm_L" };
                soldier.sightRange = 35f; soldier.preferredRange = 11f; soldier.aimErrorDegrees = 3.5f; soldier.burstDuration = 1.5f; soldier.burstPause = 1.4f;
                soldier.cameraDistance = 3.8f; soldier.cameraOffset = UE.Vec(0f, 70f, 80f);
            }
            if (bots.TryGetValue("brute", out MetalBotArchetype brute))
            {
                brute.displayName = "Brute"; brute.hackable = true;
                brute.capsuleRadius = 0.75f; brute.capsuleHalfHeight = 1.27f; brute.maxHealth = 700f; brute.walkSpeed = 2.6f; brute.possessionDuration = 30f;
                brute.rig = new MetalBotRig
                {
                    thighL = "hip_L", thighR = "hip_R", strideLength = 2.3f, legSwingDegrees = 20f, kneeBendDegrees = 28f, bobHeight = 0.06f, aimWithBothArms = true,
                };
                brute.hitZones = new List<MetalHitZone> { Zone("Head", "bip_head", 35f, 1.6f) };
                brute.startingWeapons = new List<MetalWeaponData> { w["TwinMiniguns"] }; brute.meleeWeapon = w["Claw"];
                brute.muzzleBones = new List<string> { "minigun_R", "minigun_L" }; brute.teamColors = Faction();
                brute.secondaryWeapon = w["BruteRockets"]; brute.secondaryMuzzleBone = "rocketlauncher";
                brute.sightRange = 45f; brute.preferredRange = 14f; brute.aimErrorDegrees = 5.5f; brute.burstDuration = 1.6f; brute.burstPause = 2f;
                brute.squadAlertRadius = 30f; brute.cameraDistance = 6.2f; brute.cameraOffset = UE.Vec(0f, 120f, 160f);
            }
            if (bots.TryGetValue("stalker", out MetalBotArchetype stalker))
            {
                // Flying gunship. The capsule wraps only the 5 m body; the arms take hits through their own colliders and
                // each one carries a gun that can be shot off. Friendly Stalkers can be piloted without a time limit.
                stalker.displayName = "Stalker"; stalker.hackable = true; stalker.allyHackable = true; stalker.possessionDuration = 20f;
                stalker.capsuleRadius = 1.4f; stalker.capsuleHalfHeight = 1.7f; stalker.meshOffset = UE.Vec(86f, 0f, 57f);
                stalker.maxHealth = 400f; stalker.walkSpeed = 6f;
                stalker.flight = new MetalFlightTuning
                {
                    maxSpeed = 45f, acceleration = 60f, braking = 40f, grip = 3f, turnRate = 140f, maxBank = 35f, boostSpeed = 100f,
                    boostAcceleration = 140f, boostDuration = 3f, boostRechargeTime = 5f, dodgeSpeed = 45f, dodgeCost = 0.25f,
                };
                stalker.teamColors = new List<MetalTeamColors>
                {
                    new MetalTeamColors { team = MetalTeam.Heroes, body = Lin(0.1575f, 0.1883f, 0.45f), accent = Lin(0.4875f, 0.5152f, 0.75f) },
                    new MetalTeamColors { team = MetalTeam.Villains, body = Lin(0.45f, 0.2223f, 0.1575f), accent = Lin(0.75f, 0.5456f, 0.4875f) },
                };
                stalker.rig = new MetalBotRig
                {
                    pelvis = "body", head = "", aimPitchWeight = 0f, armSwingDegrees = 0f,
                    flyerArmUpperL = "arm_upper_0_L", flyerArmUpperR = "arm_upper_0_R", flyerArmLowerL = "arm_lower_0_L", flyerArmLowerR = "arm_lower_0_R",
                    xSpreadDegrees = 28f, boostSweepDegrees = 40f, parkedFoldDegrees = 55f,
                };
                stalker.hitZones = new List<MetalHitZone>
                {
                    Zone("GunUpperL", "arm_upper_0_L", 60f, 1f, 90f, false, "gun_upper_L"),
                    Zone("GunUpperR", "arm_upper_0_R", 60f, 1f, 90f, false, "gun_upper_R"),
                    Zone("GunLowerL", "arm_lower_0_L", 60f, 1f, 90f, false, "gun_lower_L"),
                    Zone("GunLowerR", "arm_lower_0_R", 60f, 1f, 90f, false, "gun_lower_R"),
                };
                stalker.startingWeapons = new List<MetalWeaponData> { w["StalkerCannons"] };
                stalker.muzzleBones = new List<string> { "gun_upper_L", "gun_lower_R", "gun_upper_R", "gun_lower_L" };
                stalker.sightRange = 80f; stalker.preferredRange = 40f; stalker.aimErrorDegrees = 1.5f;
                stalker.cameraDistance = 15f; stalker.cameraOffset = UE.Vec(0f, 0f, 330f);
            }
            if (bots.TryGetValue("guide", out MetalBotArchetype guide))
            {
                guide.displayName = "Guide";
                guide.capsuleHalfHeight = 0.48f;
            }
            foreach (MetalBotArchetype a in bots.Values) EditorUtility.SetDirty(a);
            return bots;
        }

        static readonly HashSet<string> MusicSounds = new HashSet<string> { "music_background_loop", "ambience_loop", "battle_loop" };

        static MetalSoundLibrary BuildSoundLibrary()
        {
            MetalSoundLibrary lib = DataAsset<MetalSoundLibrary>("DA_Sounds");
            lib.sounds = new List<MetalSoundLibrary.Entry>();
            var volumes = new Dictionary<string, float>
            {
                ["footstep"] = 0.6f, ["footstep_sprint"] = 0.7f, ["ambience_loop"] = 0.5f, ["battle_loop"] = 0.6f, ["music_background_loop"] = 0.8f,
                ["swap_servo"] = 0.6f, ["minigun_fire"] = 0.5f, ["enemy_laser_fire"] = 0.7f, ["ui_move"] = 0.6f, ["suspicion"] = 0.8f,
            };
            foreach (string guid in AssetDatabase.FindAssets("t:AudioClip", new[] { MetalImportRules.AudioFolder }))
            {
                string path = AssetDatabase.GUIDToAssetPath(guid);
                var clip = AssetDatabase.LoadAssetAtPath<AudioClip>(path);
                string name = Path.GetFileNameWithoutExtension(path);
                lib.sounds.Add(new MetalSoundLibrary.Entry
                {
                    name = name,
                    clip = clip,
                    volume = volumes.TryGetValue(name, out float v) ? v : 1f,
                    group = MusicSounds.Contains(name) ? MetalVolume.Music : name.StartsWith("voice_") ? MetalVolume.Voices : MetalVolume.Effects,
                });
            }
            lib.sounds.Sort((a, b) => string.CompareOrdinal(a.name, b.name));
            EditorUtility.SetDirty(lib);
            return lib;
        }

        // ==================================================================================================
        // Ship parts
        // ==================================================================================================

        /// <summary>
        /// One prefab per ship / hangar / dispenser part: a root in the Unreal-style actor frame, the model under it
        /// (turned 90 degrees so Blender's +X faces the root's +Z, as Unreal's +X), and a SOCKET_name child per socket.
        /// </summary>
        static void BuildShipPrefabs()
        {
            Dictionary<string, object> ships = Manifest.O("ships");
            if (ships == null) return;
            foreach (var kv in ships)
            {
                GameObject model = Model("Ships/" + kv.Key);
                if (model == null) continue;
                var root = new GameObject(kv.Key);
                GameObject inst = (GameObject)PrefabUtility.InstantiatePrefab(model);
                inst.name = "Model";
                inst.transform.SetParent(root.transform, false);
                inst.transform.localRotation = StaticModelRotation;
                Dictionary<string, object> sockets = kv.Value.O("sockets");
                if (sockets != null)
                {
                    foreach (var s in sockets)
                    {
                        float[] loc = s.Value.A("location").Floats();
                        float[] fwd = s.Value.A("forward").Floats();
                        float[] up = s.Value.A("up").Floats();
                        var socket = new GameObject("SOCKET_" + s.Key).transform;
                        socket.SetParent(root.transform, false);
                        socket.localPosition = UE.Vec(loc[0], loc[1], loc[2]);
                        Vector3 f = UE.Dir(fwd[0], fwd[1], fwd[2]);
                        Vector3 u = UE.Dir(up[0], up[1], up[2]);
                        socket.localRotation = Quaternion.LookRotation(f, u);
                    }
                }
                PrefabUtility.SaveAsPrefabAsset(root, $"{Prefabs}/Ships/SM_{kv.Key}.prefab");
                Object.DestroyImmediate(root);
            }
        }

        /// <summary>Turns a static Blender model so its +X (Unreal's forward) faces the parent's +Z.</summary>
        public static readonly Quaternion StaticModelRotation = Quaternion.Euler(0f, 90f, 0f);

        public static GameObject ShipPrefab(string part) => AssetDatabase.LoadAssetAtPath<GameObject>($"{Prefabs}/Ships/SM_{part}.prefab");

        // ==================================================================================================
        // Registry
        // ==================================================================================================

        static void BuildGameAssets(MetalSoundLibrary sounds, Dictionary<string, MetalBotArchetype> bots)
        {
            string path = Root + "/Resources/MetalGameAssets.asset";
            var a = AssetDatabase.LoadAssetAtPath<MetalGameAssets>(path);
            if (a == null)
            {
                a = ScriptableObject.CreateInstance<MetalGameAssets>();
                AssetDatabase.CreateAsset(a, path);
            }
            a.glowMaterial = AssetDatabase.LoadAssetAtPath<Material>(Materials + "/M_FX_Glow.mat");
            a.particleMaterial = AssetDatabase.LoadAssetAtPath<Material>(Materials + "/M_FX_Additive.mat");
            a.beamMaterial = AssetDatabase.LoadAssetAtPath<Material>(Materials + "/M_FX_Beam.mat");
            a.smokeMaterial = AssetDatabase.LoadAssetAtPath<Material>(Materials + "/M_FX_Smoke.mat");
            a.energyFieldMaterial = AssetDatabase.LoadAssetAtPath<Material>(Materials + "/M_EnergyField.mat");
            a.portalMaterial = AssetDatabase.LoadAssetAtPath<Material>(Materials + "/M_Portal.mat");
            a.environment = new List<MetalGameAssets.NamedMaterial>();
            foreach (string n in new[] { "Floor", "Wall", "Dark", "Hazard", "Glow", "Metal" })
            {
                a.environment.Add(new MetalGameAssets.NamedMaterial { name = n, material = AssetDatabase.LoadAssetAtPath<Material>($"{Materials}/MI_Env_{n}.mat") });
            }
            a.sounds = sounds;
            a.playerArchetype = bots.TryGetValue("player", out MetalBotArchetype p) ? p : null;
            a.wrenchModel = Model("Props/wrench");
            a.ammoCrateModel = Model("Props/ammo_crate");
            a.levels = MetalLevels.BuildLevelSettings();
            EditorUtility.SetDirty(a);
        }
    }
}
