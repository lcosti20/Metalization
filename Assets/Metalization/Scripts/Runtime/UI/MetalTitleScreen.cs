// Title screen (MZ_Title): a slow camera orbit over the level behind the menu in title mode.
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// Placed in the title scene with the camera. Orbits the camera around the point it initially looks at (at its
    /// placed distance and height) and shows the menu: Start Mission, Level Select, Controls, Sound, Credits, Quit.
    /// </summary>
    public class MetalTitleScreen : MonoBehaviour
    {
        public Camera titleCamera;
        [Tooltip("Radians per second.")]
        public float orbitSpeed = 0.06f;

        Vector3 focus;
        float orbitRadius, orbitHeight, orbitAngle;

        public MetalPauseMenu Menu { get; private set; }

        void Start()
        {
            MetalGameMode.SetPaused(false);
            MetalAudio.ApplyVolumes();
            if (titleCamera == null) titleCamera = Camera.main;
            if (titleCamera != null)
            {
                // Orbit around the point the placed camera looks at, at the placed distance and height.
                Vector3 start = titleCamera.transform.position;
                Vector3 ground = start + titleCamera.transform.forward * 20f;
                ground.y = 1f;
                focus = ground;
                Vector3 offset = start - focus;
                orbitRadius = new Vector2(offset.x, offset.z).magnitude;
                orbitHeight = offset.y;
                orbitAngle = Mathf.Atan2(offset.z, offset.x);
            }
            Cursor.lockState = CursorLockMode.None;
            Cursor.visible = true;
            Menu = gameObject.AddComponent<MetalPauseMenu>();
            Menu.InitTitle();
            MetalDevAutopilot.SpawnIfRequested();
        }

        void Update()
        {
            if (titleCamera == null) return;
            orbitAngle += Time.unscaledDeltaTime * orbitSpeed;
            Vector3 location = focus + new Vector3(Mathf.Cos(orbitAngle) * orbitRadius, orbitHeight, Mathf.Sin(orbitAngle) * orbitRadius);
            titleCamera.transform.SetPositionAndRotation(location, Quaternion.LookRotation(focus - location));
        }
    }
}
