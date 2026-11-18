// A sliding blast door opened by a control console.
using UnityEngine;
using UnityEngine.AI;

namespace Metalization
{
    /// <summary>A 6 m wide, 4.5 m tall slab that slides up out of the way when its console is operated.</summary>
    public class MetalGate : MonoBehaviour
    {
        [Tooltip("How far the door moves when open, in the gate's space (m).")]
        public Vector3 openOffset = new Vector3(0f, 4.8f, 0f);
        public float openTime = 2.5f;
        [Tooltip("Door size (width, height, thickness), metres.")]
        public Vector3 doorSize = new Vector3(6f, 4.5f, 0.4f);

        Transform door;
        Vector3 closedLocation;
        bool opening;
        float openAlpha;

        public bool IsOpen => openAlpha >= 1f;

        void Awake()
        {
            GameObject d = GameObject.CreatePrimitive(PrimitiveType.Cube);
            d.name = "Door";
            d.transform.SetParent(transform, false);
            d.transform.localScale = doorSize;
            d.transform.localPosition = new Vector3(0f, doorSize.y * 0.5f, 0f);
            Material mat = MetalGameAssets.Environment("Hazard") ?? MetalGameAssets.Environment("Metal");
            if (mat != null) d.GetComponent<Renderer>().sharedMaterial = mat;
            // The closed door blocks paths; the hole re-opens as it slides away.
            NavMeshObstacle obstacle = d.AddComponent<NavMeshObstacle>();
            obstacle.carving = true;
            obstacle.shape = NavMeshObstacleShape.Box;
            obstacle.size = Vector3.one;
            door = d.transform;
            closedLocation = door.localPosition;
        }

        public void Open()
        {
            if (!opening) MetalAudio.PlayAt("gate_open", transform.position, 1f, 0f);
            opening = true;
        }

        public void OpenInstantly()
        {
            opening = true;
            openAlpha = 1f;
            door.localPosition = closedLocation + openOffset;
        }

        void Update()
        {
            if (opening && openAlpha < 1f)
            {
                openAlpha = Mathf.Min(1f, openAlpha + Time.deltaTime / Mathf.Max(0.1f, openTime));
                door.localPosition = closedLocation + openOffset * MetalMath.EaseInOut(0f, 1f, openAlpha, 2f);
            }
        }
    }
}
