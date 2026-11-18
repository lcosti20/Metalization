// A box volume the player walks into: ReachZone objectives, checkpoints and radio dialogue.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// Trigger volume with an optional glowing floor marker. Fires its dialogue once when the player's robot enters,
    /// and as a checkpoint saves the player's own body there (and repairs it). Missions ask it whether the player is
    /// inside (ReachZone objectives).
    /// </summary>
    [RequireComponent(typeof(BoxCollider))]
    public class MetalTriggerZone : MonoBehaviour
    {
        public static readonly List<MetalTriggerZone> All = new List<MetalTriggerZone>();

        [Tooltip("Saves a checkpoint (and repairs the player) the first time the player's own body enters.")]
        public bool isCheckpoint;
        public Color markerColor = new Color(0.1f, 0.8f, 1f);
        public bool showMarker = true;
        [Tooltip("Played once, the first time the player enters.")]
        public List<MetalDialogueLine> dialogue = new List<MetalDialogueLine>();

        BoxCollider box;
        Transform marker;
        bool triggered;
        bool dialoguePlayed;

        public bool IsCheckpointUsed => triggered;
        public bool HasPlayedDialogue => dialoguePlayed;
        public BoxCollider Box => box != null ? box : (box = GetComponent<BoxCollider>());

        void Awake()
        {
            box = GetComponent<BoxCollider>();
            box.isTrigger = true;
            if (MetalPhysics.TriggerLayer >= 0) gameObject.layer = MetalPhysics.TriggerLayer;
            Rigidbody rb = GetComponent<Rigidbody>();
            if (rb == null) rb = gameObject.AddComponent<Rigidbody>();
            rb.isKinematic = true;
            rb.useGravity = false;
            BuildMarker();
        }

        void OnEnable() => All.Add(this);
        void OnDisable() => All.Remove(this);

        void BuildMarker()
        {
            if (!showMarker) return;
            // Thin glowing disc on the floor of the volume.
            Vector3 size = box.size;
            GameObject disc = MetalMaterials.Shape(PrimitiveType.Cylinder, transform, "Marker");
            marker = disc.transform;
            marker.localPosition = box.center + new Vector3(0f, -size.y * 0.5f + 0.01f, 0f);
            marker.localScale = new Vector3(size.x, 0.01f, size.z);
            MetalMaterials.SetGlow(disc.GetComponent<Renderer>(), markerColor, 1.5f);
        }

        /// <summary>True if the robot's capsule overlaps the box.</summary>
        public bool Contains(MetalBot bot)
        {
            if (bot == null || Box == null) return false;
            Vector3 local = transform.InverseTransformPoint(bot.transform.position) - box.center;
            Vector3 half = box.size * 0.5f;
            Vector3 scale = transform.lossyScale;
            float r = bot.Capsule.radius / Mathf.Max(0.001f, Mathf.Min(scale.x, scale.z));
            float h = bot.Movement.CurrentHalfHeight / Mathf.Max(0.001f, scale.y);
            return Mathf.Abs(local.x) <= half.x + r && Mathf.Abs(local.z) <= half.z + r && Mathf.Abs(local.y) <= half.y + h;
        }

        /// <summary>Checkpoint restore: marks the zone used and/or its dialogue played without firing them.</summary>
        public void RestoreState(bool checkpointUsed, bool dialogueDone)
        {
            triggered = checkpointUsed;
            dialoguePlayed = dialogueDone;
            if (isCheckpoint && triggered && marker != null) marker.gameObject.SetActive(false);
        }

        void OnTriggerEnter(Collider other)
        {
            MetalBot bot = other.GetComponentInParent<MetalBot>();
            if (bot == null || !(bot.Controller is MetalPlayerController pc)) return;
            if (!dialoguePlayed && dialogue.Count > 0)
            {
                dialoguePlayed = true;
                MetalDialogue.Get().PlayDialogue(dialogue);
            }
            // Hijacked robots don't count: respawning puts the player's own body at the checkpoint.
            if (!isCheckpoint || triggered || pc.PlayerBody != bot) return;
            triggered = true;
            Vector3 floor = transform.TransformPoint(box.center - new Vector3(0f, box.size.y * 0.5f, 0f));
            MetalGameMode.Current?.SetCheckpoint(floor, bot.transform.eulerAngles.y, bot);
            bot.Heal(bot.MaxHealth);
            if (marker != null) marker.gameObject.SetActive(false);
        }

        void OnDrawGizmos()
        {
            BoxCollider b = GetComponent<BoxCollider>();
            if (b == null) return;
            Gizmos.matrix = transform.localToWorldMatrix;
            Gizmos.color = isCheckpoint ? new Color(0.2f, 1f, 0.3f, 0.4f) : new Color(0.1f, 0.8f, 1f, 0.3f);
            Gizmos.DrawWireCube(b.center, b.size);
        }
    }
}
