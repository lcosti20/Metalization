// Repairs, ammo, grenades, weapons and weapon upgrades lying in the level.
using UnityEngine;

namespace Metalization
{
    public enum MetalPickupType
    {
        /// <summary>Repairs amount health.</summary>
        Health,
        /// <summary>Refills amount (0-1) of every weapon's reserve.</summary>
        Ammo,
        /// <summary>Adds amount grenades.</summary>
        Grenades,
        /// <summary>Grants weapon, or upgrades it a level if already owned.</summary>
        Weapon,
    }

    /// <summary>A spinning, bobbing item the player's own body collects by walking into it.</summary>
    [RequireComponent(typeof(SphereCollider))]
    public class MetalPickup : MonoBehaviour
    {
        public MetalPickupType type = MetalPickupType.Health;
        public MetalWeaponData weapon;
        public float amount = 50f;

        Transform visual;
        Vector3 visualBase;
        float age;

        public Color GetColor()
        {
            switch (type)
            {
                case MetalPickupType.Health: return new Color(0.1f, 1f, 0.3f);
                case MetalPickupType.Ammo: return new Color(1f, 0.75f, 0.1f);
                case MetalPickupType.Grenades: return new Color(0.4f, 0.6f, 1f);
                default: return new Color(1f, 0.3f, 0.9f);
            }
        }

        void Awake()
        {
            SphereCollider trigger = GetComponent<SphereCollider>();
            trigger.isTrigger = true;
            trigger.radius = 0.7f;
            if (MetalPhysics.TriggerLayer >= 0) gameObject.layer = MetalPhysics.TriggerLayer;
            Rigidbody rb = GetComponent<Rigidbody>();
            if (rb == null) rb = gameObject.AddComponent<Rigidbody>();
            rb.isKinematic = true;
            rb.useGravity = false;
            BuildVisual();
            var lightGo = new GameObject("Light");
            lightGo.transform.SetParent(transform, false);
            Light l = lightGo.AddComponent<Light>();
            l.type = LightType.Point;
            l.range = 2.5f;
            l.intensity = 1.5f;
            l.color = GetColor();
            l.shadows = LightShadows.None;
        }

        GameObject ModelPrefab()
        {
            MetalGameAssets a = MetalGameAssets.Get();
            if (a == null) return null;
            return type == MetalPickupType.Health ? a.wrenchModel : type == MetalPickupType.Ammo ? a.ammoCrateModel : null;
        }

        /// <summary>
        /// Weapons show their prop model, repairs a wrench and ammo an ammo crate, and grenades a glowing sphere. A
        /// missing model falls back to a glowing cube.
        /// </summary>
        void BuildVisual()
        {
            GameObject prefab = type == MetalPickupType.Weapon && weapon != null ? weapon.propModel : ModelPrefab();
            GameObject go;
            if (prefab != null)
            {
                go = Instantiate(prefab, transform, false);
                foreach (Collider c in go.GetComponentsInChildren<Collider>()) Destroy(c);
                float scale = type == MetalPickupType.Weapon ? 1.6f : (type == MetalPickupType.Health ? 1.7f : 1.2f);
                go.transform.localScale = Vector3.one * scale;
                // The wrench is modelled lying flat: stand it on edge, tilted, so its outline shows as it spins.
                if (type == MetalPickupType.Health) go.transform.localRotation = UE.Rot(0f, 0f, 70f);
            }
            else
            {
                go = MetalMaterials.Shape(type == MetalPickupType.Grenades ? PrimitiveType.Sphere : PrimitiveType.Cube, transform, "Shape");
                go.transform.localScale = Vector3.one * 0.35f;
                MetalMaterials.SetGlow(go.GetComponent<Renderer>(), GetColor(), 3f);
            }
            var pivot = new GameObject("Visual").transform;
            pivot.SetParent(transform, false);
            go.transform.SetParent(pivot, false);
            visual = pivot;
            visualBase = visual.localPosition;
        }

        void Update()
        {
            age += Time.deltaTime;
            visual.localPosition = visualBase + new Vector3(0f, 0.1f * Mathf.Sin(age * 2.5f), 0f);
            visual.Rotate(0f, 90f * Time.deltaTime, 0f, Space.Self);
        }

        void OnTriggerEnter(Collider other)
        {
            MetalBot bot = other.GetComponentInParent<MetalBot>();
            // Only the player's own body collects items.
            if (bot == null || !(bot.Controller is MetalPlayerController pc) || pc.PlayerBody != bot || !bot.IsAlive) return;
            bool used = false;
            string message = "";
            switch (type)
            {
                case MetalPickupType.Health:
                    used = bot.Health < bot.MaxHealth;
                    bot.Heal(amount);
                    message = "Repair kit";
                    break;
                case MetalPickupType.Ammo:
                    used = bot.AddAmmoFraction(amount);
                    message = "Ammo";
                    break;
                case MetalPickupType.Grenades:
                    used = true;
                    bot.Grenades += Mathf.RoundToInt(amount);
                    message = $"+{Mathf.RoundToInt(amount)} grenades";
                    break;
                case MetalPickupType.Weapon:
                    if (weapon != null)
                    {
                        bool owned = false;
                        foreach (MetalWeaponSlot s in bot.Weapons) owned |= s.data == weapon;
                        used = bot.GiveWeapon(weapon, true);
                        message = owned ? $"{weapon.displayName} UPGRADED" : $"New weapon: {weapon.displayName}";
                    }
                    break;
            }
            if (!used) return;
            MetalGameMode.Current?.ShowMessage("", message, 2f);
            MetalFX.SpawnBurst(transform.position, GetColor(), 0.4f, 0.25f);
            MetalAudio.Play2D("pickup", 0.8f);
            Destroy(gameObject);
        }
    }
}
