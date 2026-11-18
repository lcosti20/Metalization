// Builds level scenes from code, the Unity counterpart of the helpers in the Unreal level scripts
// (ue_setup_*.py): boxes, floors, walls, pillars, lights, robots, pickups, zones, consoles, gates, energy fields,
// missions. Every helper takes the Unreal version's numbers (centimetres; X forward, Y right, Z up; yaw in
// degrees) and converts them, so the level scripts read like the originals and positions match the smoke tests.
using System.Collections.Generic;
using System.IO;
using Unity.AI.Navigation;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.SceneManagement;

namespace Metalization.Editor
{
    public class MetalLevelWriter
    {
        public const string ScenesFolder = MetalAssetPipeline.Root + "/Scenes";

        public Scene Scene { get; }
        public string Name { get; }
        readonly Dictionary<string, Transform> folders = new Dictionary<string, Transform>();
        static Mesh cylinderMesh;

        public MetalLevelWriter(string sceneName)
        {
            Name = sceneName;
            Scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
            RenderSettings.skybox = AssetDatabase.GetBuiltinExtraResource<Material>("Default-Skybox.mat");
        }

        // --- Conversions --------------------------------------------------------------------------------------

        public static Vector3 P(float x, float y, float z) => UE.Vec(x, y, z);
        /// <summary>An Unreal size (x, y, z) in cm as a Unity scale in metres.</summary>
        public static Vector3 Size(float x, float y, float z) => new Vector3(y, z, x) * UE.CmToM;

        public static Color Rgb(float r, float g, float b) => new Color(r, g, b);
        public static Color Linear(float r, float g, float b) => new Color(r, g, b).gamma;

        public Transform Folder(string path)
        {
            if (string.IsNullOrEmpty(path)) return null;
            if (folders.TryGetValue(path, out Transform t)) return t;
            Transform parent = null;
            int slash = path.LastIndexOf('/');
            string leaf = path;
            if (slash >= 0)
            {
                parent = Folder(path.Substring(0, slash));
                leaf = path.Substring(slash + 1);
            }
            var go = new GameObject(leaf);
            go.transform.SetParent(parent, false);
            folders[path] = go.transform;
            return go.transform;
        }

        public GameObject Spawn(string label, Vector3 position, float yaw = 0f, string folder = null)
        {
            var go = new GameObject(label);
            go.transform.SetParent(Folder(folder), false);
            go.transform.SetPositionAndRotation(position, Quaternion.Euler(0f, yaw, 0f));
            return go;
        }

        /// <summary>Marks an object with a stable id for checkpoint snapshots.</summary>
        static void Place(GameObject go)
        {
            MetalPlaced p = go.GetComponent<MetalPlaced>() ?? go.AddComponent<MetalPlaced>();
            p.id = go.name;
        }

        public static Material Env(string unrealName)
        {
            if (unrealName.StartsWith("MI_") && !unrealName.StartsWith("MI_Env_"))
            {
                return AssetDatabase.LoadAssetAtPath<Material>($"{MetalAssetPipeline.Materials}/{unrealName}.mat");
            }
            string n = unrealName.StartsWith("MI_Env_") ? unrealName.Substring(7) : unrealName;
            return AssetDatabase.LoadAssetAtPath<Material>($"{MetalAssetPipeline.Materials}/MI_Env_{n}.mat");
        }

        // --- Geometry -----------------------------------------------------------------------------------------

        /// <summary>A unit cube whose UVs tile the grid texture once per 2.56 m of the given size on every face.</summary>
        static Mesh BoxMesh(Vector3 size)
        {
            const float Tile = 2.56f;
            var verts = new List<Vector3>();
            var norms = new List<Vector3>();
            var uvs = new List<Vector2>();
            var tris = new List<int>();
            void Face(Vector3 n, Vector3 u, Vector3 v, float su, float sv)
            {
                int b = verts.Count;
                Vector3 c = n * 0.5f;
                verts.Add(c - u * 0.5f - v * 0.5f);
                verts.Add(c + u * 0.5f - v * 0.5f);
                verts.Add(c + u * 0.5f + v * 0.5f);
                verts.Add(c - u * 0.5f + v * 0.5f);
                for (int i = 0; i < 4; i++) norms.Add(n);
                uvs.Add(new Vector2(0f, 0f));
                uvs.Add(new Vector2(su / Tile, 0f));
                uvs.Add(new Vector2(su / Tile, sv / Tile));
                uvs.Add(new Vector2(0f, sv / Tile));
                tris.AddRange(new[] { b, b + 2, b + 1, b, b + 3, b + 2 });
            }
            Face(Vector3.up, Vector3.right, Vector3.forward, size.x, size.z);
            Face(Vector3.down, Vector3.right, Vector3.back, size.x, size.z);
            Face(Vector3.forward, Vector3.left, Vector3.up, size.x, size.y);
            Face(Vector3.back, Vector3.right, Vector3.up, size.x, size.y);
            Face(Vector3.right, Vector3.forward, Vector3.up, size.z, size.y);
            Face(Vector3.left, Vector3.back, Vector3.up, size.z, size.y);
            var mesh = new Mesh { name = $"Box {size.x:0.##}x{size.y:0.##}x{size.z:0.##}" };
            mesh.SetVertices(verts);
            mesh.SetNormals(norms);
            mesh.SetUVs(0, uvs);
            mesh.SetTriangles(tris, 0);
            mesh.RecalculateTangents();
            mesh.RecalculateBounds();
            return mesh;
        }

        static Mesh CylinderMesh()
        {
            if (cylinderMesh == null)
            {
                GameObject tmp = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
                cylinderMesh = tmp.GetComponent<MeshFilter>().sharedMesh;
                Object.DestroyImmediate(tmp);
            }
            return cylinderMesh;
        }

        /// <summary>A box of the given Unreal size (cm), centred at center, yawed. Unreal's basic cube and cylinder.</summary>
        public GameObject Box(string label, Vector3 ueCenter, Vector3 ueSize, string material = "MI_Env_Floor", string folder = "Geometry",
            bool cylinder = false, float yaw = 0f, bool invisibleWall = false)
        {
            GameObject go = Spawn(label, P(ueCenter.x, ueCenter.y, ueCenter.z), yaw, folder);
            Vector3 s = Size(ueSize.x, ueSize.y, ueSize.z);
            go.isStatic = true;
            if (cylinder)
            {
                // Unity's cylinder is 2 m tall and 1 m across.
                go.transform.localScale = new Vector3(s.x, s.y * 0.5f, s.z);
                go.AddComponent<MeshFilter>().sharedMesh = CylinderMesh();
                go.AddComponent<MeshRenderer>().sharedMaterial = Env(material);
                MeshCollider mc = go.AddComponent<MeshCollider>();
                mc.sharedMesh = CylinderMesh();
                mc.convex = true;
                return go;
            }
            go.transform.localScale = s;
            if (invisibleWall)
            {
                // Blocks robots (and the navmesh), but not shots or sight.
                go.layer = LayerMask.NameToLayer(MetalPhysics.BlockerLayerName);
                go.AddComponent<BoxCollider>();
                return go;
            }
            go.AddComponent<MeshFilter>().sharedMesh = BoxMesh(s);
            go.AddComponent<MeshRenderer>().sharedMaterial = Env(material);
            go.AddComponent<BoxCollider>();
            return go;
        }

        public GameObject Box(string label, (float x, float y, float z) c, (float x, float y, float z) s, string material = "MI_Env_Floor", string folder = "Geometry",
            bool cylinder = false, float yaw = 0f, bool invisibleWall = false)
            => Box(label, new Vector3(c.x, c.y, c.z), new Vector3(s.x, s.y, s.z), material, folder, cylinder, yaw, invisibleWall);

        /// <summary>Sets an object's rotation from an Unreal rotator (pitch, yaw, roll).</summary>
        public static GameObject Rotate(GameObject go, float pitch, float yaw, float roll)
        {
            go.transform.rotation = UE.Rot(pitch, yaw, roll);
            return go;
        }

        public GameObject Floor(string label, float x0, float x1, float y0, float y1, float top = 0f, string material = "MI_Env_Floor", string folder = "Geometry")
            => Box(label, ((x0 + x1) / 2, (y0 + y1) / 2, top - 25), (x1 - x0, y1 - y0, 50), material, folder);

        public GameObject Wall(string label, float x0, float x1, float y0, float y1, float height, string material = "MI_Env_Wall", float baseZ = 0f, string folder = "Geometry")
            => Box(label, ((x0 + x1) / 2, (y0 + y1) / 2, baseZ + height / 2), (x1 - x0, y1 - y0, height), material, folder);

        public GameObject Crate(string label, float x, float y, (float x, float y, float z)? size = null, float baseZ = 0f, string material = "MI_Env_Wall", string folder = "Cover")
        {
            (float x, float y, float z) s = size ?? (150f, 150f, 120f);
            return Box(label, (x, y, baseZ + s.z / 2), s, material, folder);
        }

        public GameObject Pillar(string label, float x, float y, float height = 500f, float radius = 60f, string material = "MI_Env_Dark", float baseZ = 0f)
            => Box(label, (x, y, baseZ + height / 2), (radius * 2, radius * 2, height), material, "Geometry", cylinder: true);

        // --- Lighting -------------------------------------------------------------------------------------------

        /// <summary>A point light (Unreal intensity and attenuation radius in cm). rgb is 0-1 sRGB.</summary>
        public Light PointLight(string label, Vector3 ueLoc, Color rgb, float intensity = 8000f, float radius = 1600f)
        {
            GameObject go = Spawn(label, P(ueLoc.x, ueLoc.y, ueLoc.z), 0f, "Lighting");
            Light l = go.AddComponent<Light>();
            l.type = LightType.Point;
            l.color = rgb;
            l.range = radius * UE.CmToM;
            l.intensity = Mathf.Clamp(intensity / 1200f, 0.5f, 10f);
            l.shadows = LightShadows.None;
            return l;
        }

        public Light PointLight(string label, (float x, float y, float z) loc, (float r, float g, float b) rgb, float intensity = 8000f, float radius = 1600f)
            => PointLight(label, new Vector3(loc.x, loc.y, loc.z), new Color(rgb.r, rgb.g, rgb.b), intensity, radius);

        /// <summary>The sun (an Unreal rotator), ambient sky and fog, and the post-processing volume.</summary>
        public Light Sun(float pitch, float yaw, Color color, float intensity = 1.3f)
        {
            GameObject go = Spawn("Sun", Vector3.up * 20f, 0f, "Lighting");
            go.transform.rotation = UE.Rot(pitch, yaw, 0f);
            Light l = go.AddComponent<Light>();
            l.type = LightType.Directional;
            l.color = color;
            l.intensity = intensity;
            l.shadows = LightShadows.Soft;
            RenderSettings.sun = l;
            return l;
        }

        public void Atmosphere(Color skyTint, Color equator, Color ground, float fogDensity, Color fogColor, float ambientIntensity = 1f)
        {
            RenderSettings.ambientMode = AmbientMode.Trilight;
            RenderSettings.ambientSkyColor = skyTint * ambientIntensity;
            RenderSettings.ambientEquatorColor = equator * ambientIntensity;
            RenderSettings.ambientGroundColor = ground * ambientIntensity;
            RenderSettings.fog = fogDensity > 0f;
            RenderSettings.fogMode = FogMode.ExponentialSquared;
            RenderSettings.fogDensity = fogDensity;
            RenderSettings.fogColor = fogColor;
        }

        /// <summary>A global post-processing volume (bloom for the glows, ACES, vignette).</summary>
        public Volume PostProcess(float exposure = 0f, float saturation = 0f, float vignette = 0.25f, float bloom = 0.8f)
        {
            string path = $"{ScenesFolder}/{Name}_Profile.asset";
            var profile = AssetDatabase.LoadAssetAtPath<VolumeProfile>(path);
            if (profile == null)
            {
                profile = ScriptableObject.CreateInstance<VolumeProfile>();
                AssetDatabase.CreateAsset(profile, path);
            }
            foreach (VolumeComponent c in new List<VolumeComponent>(profile.components)) Object.DestroyImmediate(c, true);
            profile.components.Clear();
            T Add<T>() where T : VolumeComponent
            {
                T c = profile.Add<T>(true);
                c.name = typeof(T).Name;
                AssetDatabase.AddObjectToAsset(c, profile);
                return c;
            }
            Bloom b = Add<Bloom>();
            b.intensity.Override(bloom);
            b.threshold.Override(1f);
            b.scatter.Override(0.65f);
            Tonemapping t = Add<Tonemapping>();
            t.mode.Override(TonemappingMode.ACES);
            Vignette v = Add<Vignette>();
            v.intensity.Override(vignette);
            ColorAdjustments ca = Add<ColorAdjustments>();
            ca.postExposure.Override(exposure);
            ca.saturation.Override(saturation);
            EditorUtility.SetDirty(profile);
            GameObject go = Spawn("PostProcess", Vector3.zero, 0f, "Lighting");
            Volume vol = go.AddComponent<Volume>();
            vol.isGlobal = true;
            vol.sharedProfile = profile;
            return vol;
        }

        public Camera MainCamera(Vector3 position, Quaternion rotation)
        {
            var go = new GameObject("Main Camera") { tag = "MainCamera" };
            go.transform.SetPositionAndRotation(position, rotation);
            Camera c = go.AddComponent<Camera>();
            c.nearClipPlane = 0.05f;
            c.farClipPlane = 5000f;
            go.AddComponent<AudioListener>();
            UniversalAdditionalCameraData data = go.AddComponent<UniversalAdditionalCameraData>();
            data.renderPostProcessing = true;
            data.antialiasing = AntialiasingMode.SubpixelMorphologicalAntiAliasing;
            return c;
        }

        // --- Gameplay -------------------------------------------------------------------------------------------

        public T GameMode<T>(MetalBotArchetype player, bool actionCamera = false, bool minimalHUD = false, bool resetOnDeath = true) where T : MetalGameMode
        {
            var go = new GameObject("Game Mode");
            T gm = go.AddComponent<T>();
            gm.playerArchetype = player;
            gm.actionCamera = actionCamera;
            gm.minimalHUD = minimalHUD;
            gm.resetLevelOnDeath = resetOnDeath;
            return gm;
        }

        public GameObject PlayerStart(Vector3 ueLoc, float yaw, string label = "PlayerStart")
        {
            GameObject go = Spawn(label, P(ueLoc.x, ueLoc.y, ueLoc.z) - Vector3.up * 0.9f, yaw, "Mission");
            go.AddComponent<MetalPlayerStart>();
            return go;
        }

        public MetalBot Bot(string label, MetalBotArchetype archetype, (float x, float y, float z) loc, float yaw, string[] tags = null, float patrol = 600f,
            bool passive = false, MetalTeam? team = null, string folder = "Robots", bool flyer = false)
        {
            float half = archetype != null ? archetype.capsuleHalfHeight : 0.7f;
            GameObject go = Spawn(label, P(loc.x, loc.y, loc.z) + Vector3.up * (half + 0.05f), yaw, folder);
            go.layer = LayerMask.NameToLayer(MetalPhysics.BotLayerName);
            MetalBot bot = flyer ? go.AddComponent<MetalStalker>() : go.AddComponent<MetalBot>();
            bot.archetype = archetype;
            bot.patrolRadius = patrol * UE.CmToM;
            bot.passive = passive;
            if (team.HasValue)
            {
                bot.overrideTeam = true;
                bot.teamOverride = team.Value;
            }
            bot.tags = new List<string>(tags ?? new string[0]);
            Place(go);
            return bot;
        }

        public MetalPickup Pickup(string label, MetalPickupType type, (float x, float y, float z) loc, float amount, MetalWeaponData weapon = null)
        {
            GameObject go = Spawn(label, P(loc.x, loc.y, loc.z + 70f), 0f, "Pickups");
            go.AddComponent<SphereCollider>();
            MetalPickup p = go.AddComponent<MetalPickup>();
            p.type = type;
            p.amount = amount;
            p.weapon = weapon;
            Place(go);
            return p;
        }

        public MetalTriggerZone Zone(string label, (float x, float y, float z) center, (float x, float y, float z) extent, bool checkpoint = false,
            (float r, float g, float b)? rgb = null, MetalDialogueLine[] dialogue = null, bool marker = true)
        {
            GameObject go = Spawn(label, P(center.x, center.y, center.z), 0f, "Mission");
            BoxCollider box = go.AddComponent<BoxCollider>();
            box.size = Size(extent.x * 2, extent.y * 2, extent.z * 2);
            box.isTrigger = true;
            go.layer = LayerMask.NameToLayer(MetalPhysics.TriggerLayerName);
            MetalTriggerZone z = go.AddComponent<MetalTriggerZone>();
            z.isCheckpoint = checkpoint;
            (float r, float g, float b) c = rgb ?? (0.1f, 0.8f, 1f);
            z.markerColor = new Color(c.r, c.g, c.b);
            z.showMarker = marker;
            z.dialogue = new List<MetalDialogueLine>(dialogue ?? new MetalDialogueLine[0]);
            Place(go);
            return z;
        }

        public MetalEnergyField EnergyField(string label, (float x, float y, float z) center, (float x, float y, float z) size, string folder = "Gate",
            (float r, float g, float b)? rgb = null)
        {
            GameObject go = GameObject.CreatePrimitive(PrimitiveType.Cube);
            go.name = label;
            go.transform.SetParent(Folder(folder), false);
            go.transform.position = P(center.x, center.y, center.z);
            go.transform.localScale = Size(size.x, size.y, size.z);
            go.GetComponent<MeshRenderer>().sharedMaterial = AssetDatabase.LoadAssetAtPath<Material>(MetalAssetPipeline.Materials + "/M_EnergyField.mat");
            MetalEnergyField f = go.AddComponent<MetalEnergyField>();
            (float r, float g, float b) c = rgb ?? (1f, 0.3f, 0.1f);
            f.fieldColor = new Color(c.r, c.g, c.b) * 3f;
            return f;
        }

        public MetalGate Gate(string label, (float x, float y, float z) loc, float yaw = 0f, string folder = "Gate")
        {
            GameObject go = Spawn(label, P(loc.x, loc.y, loc.z), yaw, folder);
            return go.AddComponent<MetalGate>();
        }

        public MetalControlConsole Console(string label, (float x, float y, float z) loc, float yaw, string folder = "Gate")
        {
            GameObject go = Spawn(label, P(loc.x, loc.y, loc.z), yaw, folder);
            MetalControlConsole c = go.AddComponent<MetalControlConsole>();
            Place(go);
            return c;
        }

        public MetalGravLift GravLift(string label, (float x, float y, float z) loc, float yaw, float height, float radius = 140f, (float x, float y, float z)? exit = null,
            float exitSpeed = 550f, float liftSpeed = 650f, string folder = "Traversal")
        {
            GameObject go = Spawn(label, P(loc.x, loc.y, loc.z), yaw, folder);
            go.SetActive(false);
            MetalGravLift l = go.AddComponent<MetalGravLift>();
            l.liftHeight = height * UE.CmToM;
            l.radius = radius * UE.CmToM;
            (float x, float y, float z) e = exit ?? (1f, 0f, 0f);
            l.exitDirection = UE.Dir(e.x, e.y, e.z);
            l.exitSpeed = exitSpeed * UE.CmToM;
            l.liftSpeed = liftSpeed * UE.CmToM;
            go.SetActive(true);
            return l;
        }

        public MetalBarrage Barrage(string label, (float x, float y, float z) center, (float x, float y, float z) extent, float yaw = 0f)
        {
            GameObject go = Spawn(label, P(center.x, center.y, center.z), yaw, "Mission");
            BoxCollider box = go.AddComponent<BoxCollider>();
            box.size = Size(extent.x * 2, extent.y * 2, extent.z * 2);
            box.isTrigger = true;
            go.layer = LayerMask.NameToLayer(MetalPhysics.TriggerLayerName);
            return go.AddComponent<MetalBarrage>();
        }

        public static MetalDialogueLine Line(string speaker, string text, float duration = 4.5f) => new MetalDialogueLine(speaker, text, duration);

        public static MetalObjective Objective(string text, MetalObjectiveType type, string tag = null, Component target = null, params MetalDialogueLine[] dialogue)
        {
            return new MetalObjective
            {
                description = text,
                type = type,
                targetTag = tag ?? "",
                targetObject = target != null ? target.gameObject : null,
                startDialogue = new List<MetalDialogueLine>(dialogue),
            };
        }

        public MetalMission Mission(string label, string missionName)
        {
            GameObject go = Spawn(label, Vector3.zero, 0f, "Mission");
            MetalMission m = go.AddComponent<MetalMission>();
            m.missionName = missionName;
            return m;
        }

        /// <summary>A non-positional music or ambience loop that plays for the whole level.</summary>
        public MetalAmbientSound Ambient(string sound, float volume = 1f)
        {
            var go = new GameObject("Ambient " + sound);
            go.transform.SetParent(Folder("Audio"), false);
            MetalAmbientSound a = go.AddComponent<MetalAmbientSound>();
            a.sound = sound;
            a.volume = volume;
            return a;
        }

        // --- Navigation and saving ------------------------------------------------------------------------------

        /// <summary>Bakes the navmesh from every static collider (robots, shots and triggers excluded) and saves it.</summary>
        public NavMeshSurface BuildNavMesh(float agentRadius = 0.35f, float agentHeight = 1.4f, float voxelSize = 0.1f)
        {
            var go = new GameObject("Navigation");
            NavMeshSurface surface = go.AddComponent<NavMeshSurface>();
            surface.collectObjects = CollectObjects.All;
            surface.useGeometry = UnityEngine.AI.NavMeshCollectGeometry.PhysicsColliders;
            int mask = ~0;
            foreach (string layer in new[] { MetalPhysics.BotLayerName, MetalPhysics.ProjectileLayerName, MetalPhysics.TriggerLayerName })
            {
                int l = LayerMask.NameToLayer(layer);
                if (l >= 0) mask &= ~(1 << l);
            }
            surface.layerMask = mask;
            surface.overrideVoxelSize = true;
            surface.voxelSize = voxelSize;
            surface.BuildNavMesh();
            if (surface.navMeshData != null)
            {
                string path = $"{ScenesFolder}/{Name}_NavMesh.asset";
                AssetDatabase.DeleteAsset(path);
                AssetDatabase.CreateAsset(surface.navMeshData, path);
            }
            return surface;
        }

        public string Save()
        {
            string path = $"{ScenesFolder}/{Name}.unity";
            EditorSceneManager.MarkSceneDirty(Scene);
            EditorSceneManager.SaveScene(Scene, path);
            Debug.Log($"[MZSetup] level saved: {path}");
            return path;
        }
    }
}
