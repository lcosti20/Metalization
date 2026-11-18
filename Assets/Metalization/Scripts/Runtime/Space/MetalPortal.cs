// Linked see-through portals between a capital ship's hull and its interior hangar mouth. Each end shows what
// the other end sees (a second camera rendering into a texture, sampled in screen space so it lines up) and
// moves robots that fly through its opening to the other end, keeping their speed and view direction.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    [DefaultExecutionOrder(500)]
    public class MetalPortal : MonoBehaviour
    {
        /// <summary>Robots are tracked this far (m) in front of and behind the plane.</summary>
        const float TrackDepth = 30f;

        [Tooltip("Only render the far view when the camera is this close (m).")]
        public float captureDistance = 2500f;
        [Tooltip("Render target size as a fraction of the screen.")]
        public float resolutionScale = 0.5f;

        MetalPortal linked;
        /// <summary>+1: the room this end belongs to is in front of it (+Z); -1: behind it.</summary>
        float roomSide = 1f;
        Vector2 size = new Vector2(80f, 30f);
        Renderer surface;
        Camera captureCamera;
        RenderTexture target;
        readonly Dictionary<MetalBot, float> lastSide = new Dictionary<MetalBot, float>();
        readonly List<GameObject> hidden = new List<GameObject>();

        public MetalPortal Linked => linked;
        public Vector2 Size => size;

        /// <summary>Links this end to other. roomSide: which side of this end its own room is on.</summary>
        public void Link(MetalPortal other, float side, Vector2 openingSize)
        {
            linked = other;
            roomSide = side >= 0f ? 1f : -1f;
            size = openingSize;
            // A quad across the opening, facing this end's side.
            GameObject quad = GameObject.CreatePrimitive(PrimitiveType.Quad);
            quad.name = "Window";
            Destroy(quad.GetComponent<Collider>());
            quad.transform.SetParent(transform, false);
            // The quad faces -Z; turn it so it faces the room.
            quad.transform.localRotation = roomSide > 0f ? Quaternion.Euler(0f, 180f, 0f) : Quaternion.identity;
            quad.transform.localScale = new Vector3(size.x, size.y, 1f);
            surface = quad.GetComponent<Renderer>();
            surface.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            surface.receiveShadows = false;
            Material mat = MetalGameAssets.Get() != null ? MetalGameAssets.Get().portalMaterial : null;
            if (mat != null) surface.material = new Material(mat);
            var camGo = new GameObject("Portal Capture");
            camGo.transform.SetParent(transform, false);
            captureCamera = camGo.AddComponent<Camera>();
            captureCamera.enabled = false;
            captureCamera.depth = -10f;
        }

        /// <summary>Not drawn by this end's capture (e.g. the ship hull around the far end's camera).</summary>
        public void HideFromCapture(GameObject go)
        {
            if (go != null) hidden.Add(go);
        }

        /// <summary>Signed distance in front of the plane on this end's own side (positive = in its room).</summary>
        public float GetSideDistance(Vector3 world) => Vector3.Dot(world - transform.position, transform.forward) * roomSide;

        bool IsWithinOpening(Vector3 world, float margin)
        {
            Vector3 local = transform.InverseTransformPoint(world);
            return Mathf.Abs(local.x) <= size.x * 0.5f + margin && Mathf.Abs(local.y) <= size.y * 0.5f + margin;
        }

        /// <summary>Maps a world pose at this end to the same pose relative to the linked end.</summary>
        void MapToLinked(Vector3 pos, Quaternion rot, out Vector3 outPos, out Quaternion outRot)
        {
            Vector3 localPos = transform.InverseTransformPoint(pos);
            Quaternion localRot = Quaternion.Inverse(transform.rotation) * rot;
            outPos = linked.transform.TransformPoint(localPos);
            outRot = linked.transform.rotation * localRot;
        }

        void LateUpdate()
        {
            if (linked == null) return;
            UpdateTransit();
            UpdateWindow();
        }

        void EnsureTarget()
        {
            int w = Mathf.Max(64, Mathf.RoundToInt(Screen.width * resolutionScale));
            int h = Mathf.Max(64, Mathf.RoundToInt(Screen.height * resolutionScale));
            if (target != null && target.width == w && target.height == h) return;
            if (target != null) target.Release();
            target = new RenderTexture(w, h, 24, RenderTextureFormat.DefaultHDR) { name = name + " View" };
            captureCamera.targetTexture = target;
            if (surface != null && surface.material != null) surface.material.SetTexture("_BaseMap", target);
        }

        /// <summary>
        /// Renders the far side from where the viewer's camera would be if it stood at the far end, clipped at the far
        /// end's plane so nothing between the virtual camera and the opening (our own hull) gets in the way.
        /// </summary>
        void UpdateWindow()
        {
            Camera view = MetalPlayerController.Instance != null && MetalPlayerController.Instance.CameraRig != null
                ? MetalPlayerController.Instance.CameraRig.Camera : Camera.main;
            if (view == null || surface == null) return;
            Vector3 viewPos = view.transform.position;
            bool active = GetSideDistance(viewPos) > 0f && (viewPos - transform.position).sqrMagnitude < captureDistance * captureDistance
                && surface.isVisible;
            captureCamera.enabled = active;
            if (!active) return;
            EnsureTarget();
            MapToLinked(viewPos, view.transform.rotation, out Vector3 p, out Quaternion r);
            captureCamera.transform.SetPositionAndRotation(p, r);
            captureCamera.fieldOfView = view.fieldOfView;
            captureCamera.aspect = view.aspect;
            captureCamera.nearClipPlane = view.nearClipPlane;
            captureCamera.farClipPlane = view.farClipPlane;
            captureCamera.ResetProjectionMatrix();
            // Oblique near plane on the far end's opening, facing away from its room.
            Transform far = linked.transform;
            Vector3 normal = far.forward * -linked.roomSide;
            Vector4 plane = CameraSpacePlane(captureCamera, far.position, normal);
            captureCamera.projectionMatrix = captureCamera.CalculateObliqueMatrix(plane);
            foreach (GameObject go in hidden)
            {
                if (go != null) go.SetActive(go.activeSelf); // kept for parity; the oblique plane hides the hull
            }
        }

        static Vector4 CameraSpacePlane(Camera cam, Vector3 pos, Vector3 normal)
        {
            Matrix4x4 m = cam.worldToCameraMatrix;
            Vector3 cpos = m.MultiplyPoint(pos + normal * 0.05f);
            Vector3 cnormal = m.MultiplyVector(normal).normalized;
            return new Vector4(cnormal.x, cnormal.y, cnormal.z, -Vector3.Dot(cpos, cnormal));
        }

        void UpdateTransit()
        {
            float reach = Mathf.Max(size.x, size.y) + TrackDepth;
            Vector3 center = transform.position;
            var seen = new HashSet<MetalBot>();
            foreach (MetalBot bot in new List<MetalBot>(MetalBot.All))
            {
                if (bot == null || (bot.transform.position - center).sqrMagnitude > reach * reach) continue;
                seen.Add(bot);
                float side = GetSideDistance(bot.transform.position);
                float last = lastSide.TryGetValue(bot, out float l) ? l : -1f;
                lastSide[bot] = side;
                // A crossing is a sign change from our side to the far side between two frames, inside the opening.
                // Robots seen for the first time count as already on the far side, so nothing teleports just for
                // appearing near the portal.
                if (last > 0f && side <= 0f && IsWithinOpening(bot.transform.position, 1f)) SendThrough(bot);
            }
            foreach (MetalBot b in new List<MetalBot>(lastSide.Keys))
            {
                if (b == null || !seen.Contains(b)) lastSide.Remove(b);
            }
            // Shots don't carry across (yet): they fizzle at the threshold.
            foreach (MetalProjectile p in FindObjectsByType<MetalProjectile>())
            {
                Vector3 pos = p.transform.position;
                if ((pos - center).sqrMagnitude > reach * reach) continue;
                float side = GetSideDistance(pos);
                float prev = GetSideDistance(pos - p.Velocity * Time.deltaTime);
                if (prev > 0f && side <= 0f && IsWithinOpening(pos, 1f)) Destroy(p.gameObject);
            }
        }

        void SendThrough(MetalBot bot)
        {
            // Rotation from this end's frame to the far end's; applied to velocity and the view direction.
            Quaternion turn = linked.transform.rotation * Quaternion.Inverse(transform.rotation);
            MapToLinked(bot.transform.position, bot.transform.rotation, out Vector3 arrival, out Quaternion arrivalRot);
            Vector3 velocity = bot.Velocity;
            float controlYaw = bot.Controller != null ? bot.Controller.ControlYaw : 0f;
            float controlPitch = bot.Controller != null ? bot.Controller.ControlPitch : 0f;
            // Robots stay upright: only the yaw of the turn applies to the body.
            bot.TeleportTo(arrival, arrivalRot.eulerAngles.y);
            bot.Movement.Velocity = turn * velocity;
            if (bot.Controller != null)
            {
                Vector3 look = turn * MetalMath.Forward(controlPitch, controlYaw);
                bot.Controller.ControlYaw = MetalMath.Yaw(look);
                bot.Controller.ControlPitch = MetalMath.Pitch(look);
                if (bot.Controller is MetalPlayerController pc) pc.CameraRig.SnapToTarget();
            }
            // It arrives on the far end's own side; record that so it doesn't bounce straight back.
            linked.lastSide[bot] = linked.GetSideDistance(bot.transform.position);
            Debug.Log($"[Metal] {bot.name} went through {name} to {bot.transform.position}");
        }

        void OnDestroy()
        {
            if (target != null) target.Release();
        }
    }
}
