// Artillery that chases the player through an area.
using System.Collections;
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// While the player is inside the box, shells fall behind them along the way they are heading (a red warning flash
    /// and an incoming whistle, then the blast), so stopping is what gets them hit.
    /// </summary>
    [RequireComponent(typeof(BoxCollider))]
    public class MetalBarrage : MonoBehaviour
    {
        [Tooltip("Seconds between shells.")]
        public float interval = 1.1f;
        [Tooltip("How far behind the player shells land, metres.")]
        public float trailDistance = 4.5f;
        [Tooltip("Random scatter around the aim point, metres.")]
        public float scatter = 2f;
        public float damage = 22f;
        [Tooltip("Radius of a blast, metres. Damage falls off towards the edge.")]
        public float blastRadius = 3.2f;
        [Tooltip("Seconds between the incoming whistle and the impact.")]
        public float flightTime = 0.6f;

        BoxCollider area;
        float nextShotTime;

        void Awake()
        {
            area = GetComponent<BoxCollider>();
            area.isTrigger = true;
            if (MetalPhysics.TriggerLayer >= 0) gameObject.layer = MetalPhysics.TriggerLayer;
        }

        bool Contains(Vector3 world, out Vector3 local)
        {
            local = transform.InverseTransformPoint(world) - area.center;
            Vector3 half = area.size * 0.5f;
            return Mathf.Abs(local.x) <= half.x && Mathf.Abs(local.y) <= half.y && Mathf.Abs(local.z) <= half.z;
        }

        void Update()
        {
            MetalBot player = MetalPlayerController.Instance != null ? MetalPlayerController.Instance.Pawn : null;
            float now = Time.time;
            if (player == null || !player.IsAlive || !Contains(player.transform.position, out _))
            {
                // Give the player a moment to react the first time they step in.
                nextShotTime = Mathf.Max(nextShotTime, now + 0.8f);
                return;
            }
            if (now < nextShotTime) return;
            nextShotTime = now + interval * Random.Range(0.8f, 1.2f);
            // Aim behind the player along the way they're heading, and keep the point inside the box.
            Vector3 heading = MetalMath.Flat(player.Velocity).normalized;
            if (heading.sqrMagnitude < 1e-4f) heading = transform.forward;
            Vector3 target = player.transform.position - heading * trailDistance
                + new Vector3(Random.Range(-1f, 1f), 0f, Random.Range(-1f, 1f)) * scatter;
            Contains(target, out Vector3 local);
            Vector3 half = area.size * 0.5f;
            local.x = Mathf.Clamp(local.x, -half.x, half.x);
            local.z = Mathf.Clamp(local.z, -half.z, half.z);
            target = transform.TransformPoint(local + area.center);
            // Down onto the ground.
            if (MetalPhysics.Raycast(target + Vector3.up * 4f, target - Vector3.up * 10f, out RaycastHit hit, player)) target = hit.point;
            StartCoroutine(Fire(target));
        }

        IEnumerator Fire(Vector3 location)
        {
            // A red warning flash and the incoming whistle, then the blast.
            MetalFX.SpawnBurst(location + Vector3.up * 0.1f, new Color(1f, 0.1f, 0.05f), 0.6f, flightTime);
            MetalAudio.PlayAt("grenade_throw", location + Vector3.up * 6f, 0.7f, 0.2f);
            yield return new WaitForSeconds(flightTime);
            MetalFX.SpawnExplosion(location + Vector3.up * 0.6f, new Color(1f, 0.5f, 0.15f), blastRadius * 0.8f, 0.45f);
            MetalAudio.PlayAt("explosion", location, 0.9f, 0.1f);
            // Credited to no one, so the player's own team takes it.
            MetalPhysics.ApplyRadialDamage(damage, damage * 0.3f, location, blastRadius * 0.4f, blastRadius, 1f, gameObject, null);
        }

        void OnDrawGizmos()
        {
            BoxCollider b = GetComponent<BoxCollider>();
            if (b == null) return;
            Gizmos.matrix = transform.localToWorldMatrix;
            Gizmos.color = new Color(1f, 0.3f, 0.1f, 0.4f);
            Gizmos.DrawWireCube(b.center, b.size);
        }
    }
}
