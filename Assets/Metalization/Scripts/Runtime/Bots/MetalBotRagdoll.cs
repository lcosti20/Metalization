// Death ragdoll for the Metalization robots (Unreal: physics asset + SetAllBodiesSimulatePhysics).
//
// The robots ship without physics assets, so the bodies are built on the spot: every main rig bone (pelvis, head,
// thighs, shins, upper arms, forearms, flyer arms) gets a box fitted around the vertices skinned to it, and the
// in-between bones (spine, hands, feet, weapon bones) ride along on their nearest body. The boxes are joined
// parent-to-child with CharacterJoints, the procedural animator lets go, and the whole wreck gets Unreal's nudge.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    public class MetalBotRagdoll : MonoBehaviour
    {
        // Bone-space boxes per mesh and bone name; robots of the same model share them.
        static readonly Dictionary<Mesh, Dictionary<string, Bounds>> BoundsCache = new Dictionary<Mesh, Dictionary<string, Bounds>>();

        readonly List<Rigidbody> bodies = new List<Rigidbody>();
        Rigidbody root;

        /// <summary>The ragdoll's root body (the pelvis), or null.</summary>
        public Rigidbody Root => root;

        /// <summary>Where the wreck is now (the pelvis body), falling back to the robot's position.</summary>
        public Vector3 Center => root != null ? root.worldCenterOfMass : transform.position;

        /// <summary>
        /// Turns the robot's skeleton into a ragdoll moving at velocity plus the death nudge (world space). Returns
        /// null when the model can't be ragdolled (no skinned mesh, or its mesh isn't readable).
        /// </summary>
        public static MetalBotRagdoll Create(MetalBot bot, Vector3 velocity)
        {
            if (bot == null || bot.BodyInstance == null || bot.archetype == null) return null;
            SkinnedMeshRenderer skin = bot.BodyInstance.GetComponentInChildren<SkinnedMeshRenderer>(true);
            if (skin == null || skin.sharedMesh == null || !skin.sharedMesh.isReadable) return null;

            // Boxes are fitted to the whole rig (they're cached per mesh); shot-off limbs just get no body.
            List<Transform> allBones = PickBodyBones(bot, true);
            List<Transform> bodyBones = PickBodyBones(bot, false);
            if (bodyBones.Count == 0) return null;
            Dictionary<string, Bounds> boxes = GetBoneBoxes(bot, skin, allBones);

            var ragdoll = bot.gameObject.AddComponent<MetalBotRagdoll>();
            ragdoll.Build(bot, bodyBones, boxes, velocity);
            return ragdoll;
        }

        /// <summary>The rig's main bones that exist (and, unless includeHidden, aren't shot off), parents first.</summary>
        static List<Transform> PickBodyBones(MetalBot bot, bool includeHidden)
        {
            MetalBotRig rig = bot.Rig;
            var names = new[]
            {
                rig.pelvis, rig.head, rig.thighL, rig.thighR, rig.kneeL, rig.kneeR,
                rig.upperArmL, rig.upperArmR, rig.forearmL, rig.forearmR,
                rig.flyerArmUpperL, rig.flyerArmUpperR, rig.flyerArmLowerL, rig.flyerArmLowerR,
            };
            var set = new HashSet<Transform>();
            foreach (string n in names)
            {
                Transform t = string.IsNullOrEmpty(n) ? null : bot.GetBone(n);
                if (t == null || (!includeHidden && IsHidden(bot, t))) continue;
                set.Add(t);
            }
            // Hierarchy order (parents before children) so each joint finds its parent body already made.
            var ordered = new List<Transform>();
            foreach (Transform t in bot.BodyInstance.GetComponentsInChildren<Transform>(true))
            {
                if (set.Contains(t)) ordered.Add(t);
            }
            return ordered;
        }

        static bool IsHidden(MetalBot bot, Transform bone)
        {
            for (Transform t = bone; t != null && t != bot.MeshRoot; t = t.parent)
            {
                if (bot.Animator != null && bot.Animator.IsBoneHidden(t)) return true;
            }
            return false;
        }

        static Transform BodyFor(Transform bone, HashSet<Transform> bodySet, Transform fallback)
        {
            for (Transform t = bone; t != null; t = t.parent)
            {
                if (bodySet.Contains(t)) return t;
            }
            return fallback;
        }

        /// <summary>
        /// Fits a bone-space box around the vertices whose strongest weight belongs to each body bone (or to a bone
        /// riding on it), in the bind pose.
        /// </summary>
        static Dictionary<string, Bounds> GetBoneBoxes(MetalBot bot, SkinnedMeshRenderer skin, List<Transform> bodyBones)
        {
            Mesh mesh = skin.sharedMesh;
            if (BoundsCache.TryGetValue(mesh, out Dictionary<string, Bounds> cached)) return cached;

            var result = new Dictionary<string, Bounds>();
            var bodySet = new HashSet<Transform>(bodyBones);
            Transform[] skinBones = skin.bones;
            // Mesh space -> MeshRoot space (constant: nothing between the renderer and the MeshRoot is animated).
            Matrix4x4 meshToRoot = bot.MeshRoot.worldToLocalMatrix * skin.transform.localToWorldMatrix;
            var rootToBone = new Dictionary<Transform, Matrix4x4>();
            foreach (Transform b in bodyBones)
            {
                if (bot.TryGetBindMatrix(b.name, out Matrix4x4 bind)) rootToBone[b] = bind.inverse;
            }

            Vector3[] vertices = mesh.vertices;
            BoneWeight[] weights = mesh.boneWeights;
            var owner = new Transform[skinBones.Length];
            for (int i = 0; i < skinBones.Length; i++) owner[i] = BodyFor(skinBones[i], bodySet, bodyBones[0]);

            var boxes = new Dictionary<Transform, Bounds>();
            for (int v = 0; v < vertices.Length && v < weights.Length; v++)
            {
                int bi = weights[v].boneIndex0;
                if (bi < 0 || bi >= owner.Length || owner[bi] == null) continue;
                Transform body = owner[bi];
                if (!rootToBone.TryGetValue(body, out Matrix4x4 toBone)) continue;
                Vector3 p = toBone.MultiplyPoint3x4(meshToRoot.MultiplyPoint3x4(vertices[v]));
                if (boxes.TryGetValue(body, out Bounds b)) { b.Encapsulate(p); boxes[body] = b; }
                else boxes[body] = new Bounds(p, Vector3.zero);
            }
            foreach (var kv in boxes) result[kv.Key.name] = kv.Value;
            BoundsCache[mesh] = result;
            return result;
        }

        void Build(MetalBot bot, List<Transform> bodyBones, Dictionary<string, Bounds> boxes, Vector3 velocity)
        {
            int layer = LayerMask.NameToLayer("Ignore Raycast"); // wrecks don't stop shots, sight lines or the camera
            var colliders = new List<Collider>();
            var bodyOf = new Dictionary<Transform, Rigidbody>();
            float scale = Mathf.Max(0.1f, bot.MeshScale);

            foreach (Transform bone in bodyBones)
            {
                if (!boxes.TryGetValue(bone.name, out Bounds box)) continue;
                // Bone-space units are model units; keep tiny parts from becoming slivers.
                Vector3 size = Vector3.Max(box.size, Vector3.one * 0.06f);
                Vector3 lossy = bone.lossyScale;
                Vector3 worldSize = Vector3.Scale(size, new Vector3(Mathf.Abs(lossy.x), Mathf.Abs(lossy.y), Mathf.Abs(lossy.z)));
                if (worldSize.x < 1e-3f || worldSize.y < 1e-3f || worldSize.z < 1e-3f) continue;

                if (layer >= 0) bone.gameObject.layer = layer;
                var col = bone.gameObject.AddComponent<BoxCollider>();
                col.center = box.center;
                col.size = size;
                colliders.Add(col);

                var rb = bone.gameObject.AddComponent<Rigidbody>();
                // Narrow mass range: big ratios between jointed bodies make PhysX joints jitter.
                rb.mass = Mathf.Clamp(worldSize.x * worldSize.y * worldSize.z * 400f, 3f, 20f);
                rb.linearDamping = 0.05f;
                rb.angularDamping = 0.3f;
                rb.interpolation = RigidbodyInterpolation.Interpolate;
                rb.collisionDetectionMode = CollisionDetectionMode.ContinuousSpeculative;
                rb.solverIterations = 12;

                Rigidbody parent = null;
                for (Transform t = bone.parent; t != null && parent == null; t = t.parent) bodyOf.TryGetValue(t, out parent);
                if (parent != null)
                {
                    var joint = bone.gameObject.AddComponent<CharacterJoint>();
                    joint.connectedBody = parent;
                    joint.autoConfigureConnectedAnchor = true;
                    joint.anchor = Vector3.zero;
                    // Twist around the bone's length (its longest box axis), swing on the other two.
                    Vector3 axis = LongestAxis(size);
                    joint.axis = axis;
                    joint.swingAxis = Mathf.Abs(axis.x) > 0.5f ? Vector3.up : Vector3.right;
                    joint.lowTwistLimit = new SoftJointLimit { limit = -40f };
                    joint.highTwistLimit = new SoftJointLimit { limit = 40f };
                    joint.swing1Limit = new SoftJointLimit { limit = 55f };
                    joint.swing2Limit = new SoftJointLimit { limit = 35f };
                    joint.enableProjection = true;
                    joint.enablePreprocessing = false;
                }
                else if (root == null)
                {
                    root = rb;
                }
                bodyOf[bone] = rb;
                bodies.Add(rb);
            }

            // Overlapping parts of the same robot would shove each other apart; only the world stops them.
            for (int i = 0; i < colliders.Count; i++)
            {
                for (int j = i + 1; j < colliders.Count; j++) Physics.IgnoreCollision(colliders[i], colliders[j]);
            }

            // The procedural animator would keep snapping the bones back to the bind pose.
            if (bot.Animator != null) bot.Animator.enabled = false;

            // Unreal: AddImpulse(-Forward * 150 + Up * 100 cm/s, velocity change) on every body.
            Vector3 nudge = -bot.transform.forward * 1.5f + Vector3.up * 1f;
            foreach (Rigidbody rb in bodies) rb.linearVelocity = velocity + nudge;
        }

        static Vector3 LongestAxis(Vector3 size)
        {
            if (size.x >= size.y && size.x >= size.z) return Vector3.right;
            return size.y >= size.z ? Vector3.up : Vector3.forward;
        }
    }
}
