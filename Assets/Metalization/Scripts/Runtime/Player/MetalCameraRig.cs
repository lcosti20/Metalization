// Third-person camera for whatever robot the player drives: Unreal's spring arm (boom length, socket offset,
// location lag, collision probe) driven by the robot's camera settings, which its action camera blends.
using UnityEngine;

namespace Metalization
{
    [DefaultExecutionOrder(200)]
    public class MetalCameraRig : MonoBehaviour
    {
        [Tooltip("Radius of the sphere swept from the robot to the camera to keep it out of walls, metres.")]
        public float probeRadius = 0.12f;

        public MetalPlayerController Owner { get; set; }
        public Camera Camera { get; private set; }

        Vector3 laggedOrigin;
        bool snap = true;

        void Awake()
        {
            Camera = Camera.main;
            if (Camera == null)
            {
                var go = new GameObject("Main Camera") { tag = "MainCamera" };
                Camera = go.AddComponent<Camera>();
                go.AddComponent<AudioListener>();
            }
            Camera.nearClipPlane = 0.05f;
            Camera.farClipPlane = Mathf.Max(Camera.farClipPlane, 5000f);
        }

        /// <summary>Skip the lag on the next frame (spawn, body switch, teleport).</summary>
        public void SnapToTarget() => snap = true;

        void LateUpdate()
        {
            MetalBot bot = Owner != null ? Owner.Pawn : null;
            if (bot == null || Camera == null) return;
            float dt = Time.deltaTime;
            Quaternion rot = Owner.ControlRotation;
            // The boom hangs off the capsule centre.
            Vector3 origin = bot.transform.position;
            if (snap || !bot.CameraLagEnabled || dt <= 0f)
            {
                laggedOrigin = origin;
                snap = false;
            }
            else
            {
                laggedOrigin = Vector3.Lerp(laggedOrigin, origin, Mathf.Clamp01(dt * bot.CameraLagSpeed));
                if (bot.CameraLagMaxDistance > 0f && (laggedOrigin - origin).magnitude > bot.CameraLagMaxDistance)
                {
                    laggedOrigin = origin + (laggedOrigin - origin).normalized * bot.CameraLagMaxDistance;
                }
            }
            Vector3 desired = laggedOrigin - rot * Vector3.forward * bot.CameraArmLength + rot * bot.CameraSocketOffset;
            // Keep the camera out of walls: sweep from the robot to where the camera wants to be.
            Vector3 delta = desired - laggedOrigin;
            float dist = delta.magnitude;
            Vector3 final = desired;
            if (dist > 0.01f)
            {
                RaycastHit[] hits = Physics.SphereCastAll(laggedOrigin, probeRadius, delta / dist, dist, MetalPhysics.ShotMask & ~MetalPhysics.BotMask,
                    QueryTriggerInteraction.Ignore);
                float best = dist;
                foreach (RaycastHit h in hits)
                {
                    if (h.collider.transform.IsChildOf(bot.transform)) continue;
                    if (h.collider.GetComponentInParent<MetalEnergyField>() != null) continue;
                    if (h.distance > 0f && h.distance < best) best = h.distance;
                }
                final = laggedOrigin + delta / dist * best;
            }
            Camera.transform.SetPositionAndRotation(final, rot);
            Camera.fieldOfView = HorizontalToVerticalFov(bot.CameraFOV, Camera.aspect);
        }

        /// <summary>Unreal FOVs are horizontal; Unity's camera takes a vertical one.</summary>
        public static float HorizontalToVerticalFov(float horizontalDegrees, float aspect)
        {
            float h = horizontalDegrees * Mathf.Deg2Rad;
            return 2f * Mathf.Atan(Mathf.Tan(h * 0.5f) / Mathf.Max(0.01f, aspect)) * Mathf.Rad2Deg;
        }
    }
}
