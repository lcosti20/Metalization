// Damage events and physics queries shared by weapons, explosions and level actors (Unreal's ApplyDamage /
// ApplyPointDamage / ApplyRadialDamageWithFalloff and the Visibility trace channel).
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>Everything that can take damage implements this (robots, ship subsystems, dispensers...).</summary>
    public interface IMetalDamageable
    {
        /// <returns>Damage actually applied.</returns>
        float TakeDamage(MetalDamageEvent e);
    }

    /// <summary>One hit. Point damage carries the ray so hit zones can be worked out.</summary>
    public class MetalDamageEvent
    {
        public float amount;
        /// <summary>Point damage (a shot) rather than radial or melee.</summary>
        public bool isPoint;
        public Vector3 hitPoint;
        public Vector3 shotDirection;
        /// <summary>Where the trace started, if it was a trace (projectile hits have none).</summary>
        public Vector3? traceStart;
        public Collider hitCollider;
        /// <summary>Controller responsible; credited with the kill.</summary>
        public MetalController instigator;
        /// <summary>The shooter robot, a projectile, or a level actor.</summary>
        public GameObject causer;
        /// <summary>Whatever owns the causer (a capital ship for its turrets' shots).</summary>
        public GameObject causerOwner;
    }

    /// <summary>Damage helpers and the layers used for shots.</summary>
    public static class MetalPhysics
    {
        public const string BotLayerName = "Bot";
        public const string ProjectileLayerName = "Projectile";
        public const string TriggerLayerName = "MetalTrigger";
        /// <summary>Invisible walls that keep robots out (and shape the navmesh) but let shots and sight through.</summary>
        public const string BlockerLayerName = "MetalBlocker";

        static int botLayer = -2, projectileLayer = -2, triggerLayer = -2, blockerLayer = -2;

        public static int BlockerLayer => blockerLayer == -2 ? (blockerLayer = LayerMask.NameToLayer(BlockerLayerName)) : blockerLayer;

        public static int BotLayer => botLayer == -2 ? (botLayer = LayerMask.NameToLayer(BotLayerName)) : botLayer;
        public static int ProjectileLayer => projectileLayer == -2 ? (projectileLayer = LayerMask.NameToLayer(ProjectileLayerName)) : projectileLayer;
        public static int TriggerLayer => triggerLayer == -2 ? (triggerLayer = LayerMask.NameToLayer(TriggerLayerName)) : triggerLayer;

        /// <summary>The "Visibility" channel: everything solid except projectiles and trigger volumes.</summary>
        public static int ShotMask
        {
            get
            {
                int mask = Physics.DefaultRaycastLayers;
                if (ProjectileLayer >= 0) mask &= ~(1 << ProjectileLayer);
                if (TriggerLayer >= 0) mask &= ~(1 << TriggerLayer);
                if (BlockerLayer >= 0) mask &= ~(1 << BlockerLayer);
                return mask;
            }
        }

        /// <summary>Only robots.</summary>
        public static int BotMask => BotLayer >= 0 ? 1 << BotLayer : Physics.DefaultRaycastLayers;

        static readonly RaycastHit[] Hits = new RaycastHit[64];

        /// <summary>Closest blocking hit from start to end along the visibility channel, ignoring the given roots.</summary>
        public static bool Raycast(Vector3 start, Vector3 end, out RaycastHit hit, params Component[] ignore)
        {
            return Raycast(start, end, ShotMask, out hit, ignore);
        }

        public static bool Raycast(Vector3 start, Vector3 end, int mask, out RaycastHit hit, params Component[] ignore)
        {
            hit = default;
            Vector3 delta = end - start;
            float dist = delta.magnitude;
            if (dist < 1e-4f) return false;
            int n = Physics.RaycastNonAlloc(start, delta / dist, Hits, dist, mask, QueryTriggerInteraction.Ignore);
            float best = float.MaxValue;
            bool found = false;
            for (int i = 0; i < n; i++)
            {
                if (Hits[i].distance < best && !IsIgnored(Hits[i].collider, ignore))
                {
                    best = Hits[i].distance;
                    hit = Hits[i];
                    found = true;
                }
            }
            return found;
        }

        static bool IsIgnored(Collider c, Component[] ignore)
        {
            if (ignore == null) return false;
            foreach (Component ig in ignore)
            {
                if (ig != null && c.transform.IsChildOf(ig.transform)) return true;
            }
            return false;
        }

        /// <summary>The damageable owning a collider, if any.</summary>
        public static IMetalDamageable FindDamageable(Collider c)
        {
            return c != null ? c.GetComponentInParent<IMetalDamageable>() : null;
        }

        public static MetalBot FindBot(Collider c)
        {
            return c != null ? c.GetComponentInParent<MetalBot>() : null;
        }

        /// <summary>Applies a damage event to whatever owns the collider.</summary>
        public static float ApplyDamage(Collider target, MetalDamageEvent e)
        {
            IMetalDamageable d = FindDamageable(target);
            return d != null ? d.TakeDamage(e) : 0f;
        }

        /// <summary>
        /// Radial damage: full damage inside innerRadius, falling to minDamage at outerRadius (exponent falloff).
        /// Targets behind solid geometry (on the visibility channel) are spared.
        /// </summary>
        public static void ApplyRadialDamage(float baseDamage, float minDamage, Vector3 origin, float innerRadius, float outerRadius,
            float falloff, GameObject causer, MetalController instigator, ICollection<GameObject> ignore = null, GameObject causerOwner = null)
        {
            var done = new HashSet<IMetalDamageable>();
            foreach (Collider c in Physics.OverlapSphere(origin, outerRadius, Physics.DefaultRaycastLayers, QueryTriggerInteraction.Ignore))
            {
                IMetalDamageable d = FindDamageable(c);
                if (d == null || done.Contains(d)) continue;
                var comp = (Component)d;
                if (ignore != null && ignore.Contains(comp.gameObject)) continue;
                Vector3 closest = c.ClosestPoint(origin);
                // Line of sight from the blast to the target, ignoring the target itself.
                if (Raycast(origin, closest, out RaycastHit block, comp) && (block.point - closest).sqrMagnitude > 0.01f)
                {
                    continue;
                }
                done.Add(d);
                float dist = (closest - origin).magnitude;
                float damage = baseDamage;
                if (dist > innerRadius)
                {
                    float t = Mathf.Clamp01((dist - innerRadius) / Mathf.Max(0.001f, outerRadius - innerRadius));
                    damage = Mathf.Lerp(baseDamage, minDamage, Mathf.Pow(t, falloff));
                }
                d.TakeDamage(new MetalDamageEvent
                {
                    amount = damage,
                    isPoint = false,
                    hitPoint = closest,
                    shotDirection = (closest - origin).normalized,
                    instigator = instigator,
                    causer = causer,
                    causerOwner = causerOwner,
                });
            }
        }

        /// <summary>Every robot within radius of center.</summary>
        public static List<MetalBot> OverlapBots(Vector3 center, float radius)
        {
            var result = new List<MetalBot>();
            foreach (Collider c in Physics.OverlapSphere(center, radius, BotMask, QueryTriggerInteraction.Ignore))
            {
                MetalBot b = FindBot(c);
                if (b != null && !result.Contains(b)) result.Add(b);
            }
            return result;
        }
    }
}
