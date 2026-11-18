// Data-driven weapon definition. One asset per weapon; upgrade levels live inside the asset.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>How a weapon delivers its damage.</summary>
    public enum MetalFireMode
    {
        /// <summary>Instant trace (laser, shotgun pellets).</summary>
        Hitscan,
        /// <summary>Travelling projectile (repeater bolts, rockets).</summary>
        Projectile,
        /// <summary>Lobbed with gravity (grenades).</summary>
        Thrown,
        /// <summary>Short-range swing (robots whose weapon arm was destroyed).</summary>
        Melee,
    }

    /// <summary>A weapon model skinned to one holder's skeleton.</summary>
    [Serializable]
    public class MetalHeldModel
    {
        public string archetypeId = "";
        public GameObject model;
    }

    /// <summary>Stats for one upgrade level of a weapon.</summary>
    [Serializable]
    public class MetalWeaponLevel
    {
        [Tooltip("Weapon model per holder skeleton, keyed by archetype id.")]
        public List<MetalHeldModel> heldModels = new List<MetalHeldModel>();
        [Tooltip("Damage per hit (per pellet for shotguns).")]
        public float damage = 10f;
        [Tooltip("Seconds between shots.")]
        public float fireInterval = 0.2f;
        public int clipSize = 20;
        public int maxReserveAmmo = 200;

        public GameObject GetHeldModel(string archetypeId)
        {
            foreach (MetalHeldModel h in heldModels)
            {
                if (h.archetypeId == archetypeId) return h.model;
            }
            return null;
        }
    }

    /// <summary>
    /// One weapon (Laser, Repeater, Shotgun, grenades, ship cannons...). Shared fields apply at every upgrade
    /// level; per-level stats and models live in levels.
    /// </summary>
    [CreateAssetMenu(menuName = "Metalization/Weapon Data", fileName = "WD_")]
    public class MetalWeaponData : ScriptableObject
    {
        [Header("Weapon")]
        public string displayName = "";
        public MetalFireMode fireMode = MetalFireMode.Hitscan;
        [Tooltip("Hold to keep firing.")]
        public bool automatic = true;
        public bool infiniteAmmo;
        public float reloadTime = 1.2f;
        [Tooltip("Shots per trigger pull (shotgun pellets).")]
        public int pellets = 1;
        [Tooltip("Cone half-angle in degrees.")]
        public float spreadDegrees = 1f;
        [Tooltip("Spread when fired without aiming. Negative = same as spreadDegrees.")]
        public float hipSpreadDegrees = -1f;
        [Tooltip("Maximum reach of a shot, metres.")]
        public float range = 60f;

        [Header("Projectile")]
        [Tooltip("Launch speed of projectiles and thrown grenades, m/s.")]
        public float projectileSpeed = 50f;
        [Tooltip("0 flies straight, 1 falls like a thrown object.")]
        public float projectileGravityScale;
        [Tooltip("0 for direct-hit only, metres.")]
        public float explosionRadius;
        [Tooltip("Seconds an explosion disables robot electronics (EMP grenade).")]
        public float stunDuration;

        [Header("Visual")]
        [Tooltip("Distance in front of the weapon bone where shots appear, metres.")]
        public float muzzleOffset = 0.45f;
        public float projectileScale = 1f;
        public Color tracerColor = new Color(1f, 0.4f, 0.1f);
        [Tooltip("Model used for pickups and thrown grenades (pivot at the grip).")]
        public GameObject propModel;

        [Header("Aim")]
        public float aimFOV = 60f;
        [Tooltip("Camera distance while aiming, as a fraction of the robot's normal distance.")]
        public float aimArmLengthScale = 0.55f;
        [Tooltip("Draws a scope overlay and hides the robot's own body while aiming (sniper).")]
        public bool scopeOverlay;
        public float aimSensitivity = 1f;
        [Tooltip("AI holding this weapon shows a laser sight while it has a target.")]
        public bool laserSight;

        [Header("Stealth")]
        [Tooltip("Enemies within this distance (m) hear a shot and come to investigate.")]
        public float noiseRadius = 22f;

        [Header("Audio (sound library names)")]
        public string fireSound = "";
        public string reloadSound = "reload";
        public string impactSound = "impact";

        [Header("Levels (index 0 is the base weapon)")]
        public List<MetalWeaponLevel> levels = new List<MetalWeaponLevel>();

        static readonly MetalWeaponLevel Fallback = new MetalWeaponLevel();

        /// <summary>Stats for an upgrade level; out-of-range values are clamped.</summary>
        public MetalWeaponLevel GetLevel(int index)
        {
            if (levels.Count == 0) return Fallback;
            return levels[Mathf.Clamp(index, 0, levels.Count - 1)];
        }

        public int MaxLevel => Mathf.Max(0, levels.Count - 1);
    }
}
