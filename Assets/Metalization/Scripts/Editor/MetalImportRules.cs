// Import settings for everything the asset pipeline writes into the project: the Blender FBX exports under
// Models/ and the synthesized sounds under Audio/. Applied automatically on every (re)import.
using System.IO;
using UnityEditor;
using UnityEngine;

namespace Metalization.Editor
{
    public class MetalImportRules : AssetPostprocessor
    {
        public const string ModelsFolder = "Assets/Metalization/Models";
        public const string AudioFolder = "Assets/Metalization/Audio";
        public const string ModelMaterialsFolder = "Assets/Metalization/Materials/Models";

        void OnPreprocessModel()
        {
            if (!assetPath.StartsWith(ModelsFolder)) return;
            var importer = (ModelImporter)assetImporter;
            // Blender exports metres with -Z forward / Y up. Baking the axis conversion still leaves Blender's
            // +Y (a character's back, a ship's left) on Unity's +Z, so OnPostprocessModel turns every model round.
            importer.bakeAxisConversion = true;
            importer.useFileScale = true;
            importer.globalScale = 1f;
            importer.importCameras = false;
            importer.importLights = false;
            importer.importBlendShapes = false;
            importer.importVisibility = false;
            importer.importAnimation = false;
            importer.animationType = ModelImporterAnimationType.Generic;
            importer.avatarSetup = ModelImporterAvatarSetup.NoAvatar;
            importer.optimizeGameObjects = false;
            // Characters: the death ragdoll fits its boxes to the skinned vertices.
            importer.isReadable = assetPath.Contains("/Ships/") || assetPath.Contains("/Characters/");
            importer.addCollider = false;
            importer.importNormals = ModelImporterNormals.Import;
            // The graybox materials are built by MetalAssetPipeline from the manifest: map every FBX material slot
            // onto the generated material of the same name.
            importer.materialImportMode = ModelImporterMaterialImportMode.ImportStandard;
            importer.materialLocation = ModelImporterMaterialLocation.InPrefab;
            foreach (string guid in AssetDatabase.FindAssets("t:Material", new[] { ModelMaterialsFolder }))
            {
                string path = AssetDatabase.GUIDToAssetPath(guid);
                var mat = AssetDatabase.LoadAssetAtPath<Material>(path);
                if (mat != null) importer.AddRemap(new AssetImporter.SourceAssetIdentifier(typeof(Material), mat.name), mat);
            }
        }

        // Bump whenever the postprocessing changes so Unity reimports the models.
        public override uint GetVersion() => 2;

        /// <summary>Half a turn about Y: imported Blender models face -Z, the game expects them to face +Z.</summary>
        static readonly Quaternion FaceForward = Quaternion.Euler(0f, 180f, 0f);

        void OnPostprocessModel(GameObject root)
        {
            if (!assetPath.StartsWith(ModelsFolder)) return;
            // Turn every top-level node (mesh, armature or collision hull) about the model origin. Bones below an
            // armature keep their local transforms, and skinning follows the bones, so skinned meshes turn with them.
            foreach (Transform child in root.transform)
            {
                child.localPosition = FaceForward * child.localPosition;
                child.localRotation = FaceForward * child.localRotation;
            }
            if (!assetPath.Contains("/Ships/")) return;
            // Ship and hangar pieces carry UCX_ collision hulls: turn them into convex colliders and hide them. Pieces
            // without any get a mesh collider on the render mesh.
            bool anyHull = false;
            foreach (MeshFilter mf in root.GetComponentsInChildren<MeshFilter>(true))
            {
                if (!mf.name.StartsWith("UCX_")) continue;
                anyHull = true;
                MeshCollider c = mf.gameObject.AddComponent<MeshCollider>();
                c.sharedMesh = mf.sharedMesh;
                c.convex = true;
                Object.DestroyImmediate(mf.GetComponent<MeshRenderer>());
                Object.DestroyImmediate(mf);
            }
            if (!anyHull)
            {
                foreach (MeshFilter mf in root.GetComponentsInChildren<MeshFilter>(true))
                {
                    MeshCollider c = mf.gameObject.AddComponent<MeshCollider>();
                    c.sharedMesh = mf.sharedMesh;
                }
            }
        }

        void OnPreprocessAudio()
        {
            if (!assetPath.StartsWith(AudioFolder)) return;
            var importer = (AudioImporter)assetImporter;
            bool music = Path.GetFileNameWithoutExtension(assetPath).StartsWith("music_") || assetPath.EndsWith(".mp3");
            bool loop = Path.GetFileNameWithoutExtension(assetPath).EndsWith("_loop");
            AudioImporterSampleSettings s = importer.defaultSampleSettings;
            s.loadType = music ? AudioClipLoadType.Streaming : (loop ? AudioClipLoadType.CompressedInMemory : AudioClipLoadType.DecompressOnLoad);
            s.compressionFormat = music ? AudioCompressionFormat.Vorbis : AudioCompressionFormat.ADPCM;
            s.quality = 0.7f;
            importer.defaultSampleSettings = s;
            // One-shots positioned in 3D play mono.
            importer.forceToMono = !music && !loop;
            importer.loadInBackground = music;
        }
    }
}
