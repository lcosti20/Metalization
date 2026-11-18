// A destructible part of a capital ship (shield generator, engines, comms, life support).
using UnityEngine;

namespace Metalization
{
    /// <summary>A destructible part of a capital ship. Stores health; the ship decides what a hit does.</summary>
    public class MetalShipSubsystem : MonoBehaviour, IMetalDamageable
    {
        public MetalShipSubsystemType type = MetalShipSubsystemType.ShieldGenerator;
        public float maxHealth = 1000f;
        public MetalCapitalShip Ship { get; set; }
        public float Health { get; private set; } = 1000f;
        public bool IsDestroyed => Health <= 0f;

        public string Label => type == MetalShipSubsystemType.ShieldGenerator ? "SHIELD" : type == MetalShipSubsystemType.Engines ? "ENGINES"
            : type == MetalShipSubsystemType.Comms ? "COMMS" : "LIFE SUPPORT";

        /// <summary>World centre of the part (bounds of its renderers).</summary>
        public Vector3 Center
        {
            get
            {
                Renderer[] rs = GetComponentsInChildren<Renderer>();
                if (rs.Length == 0) return transform.position;
                Bounds b = rs[0].bounds;
                foreach (Renderer r in rs) b.Encapsulate(r.bounds);
                return b.center;
            }
        }

        public float Radius
        {
            get
            {
                Renderer[] rs = GetComponentsInChildren<Renderer>();
                if (rs.Length == 0) return 5f;
                Bounds b = rs[0].bounds;
                foreach (Renderer r in rs) b.Encapsulate(r.bounds);
                return b.extents.magnitude;
            }
        }

        public void ResetHealth(float newMax)
        {
            maxHealth = Mathf.Max(1f, newMax);
            Health = maxHealth;
        }

        /// <returns>True if this hit destroyed it.</returns>
        public bool ApplyDamage(float amount)
        {
            if (IsDestroyed) return false;
            Health = Mathf.Max(0f, Health - amount);
            return IsDestroyed;
        }

        public float TakeDamage(MetalDamageEvent e) => Ship != null ? Ship.ApplySubsystemDamage(this, e) : 0f;
    }
}
