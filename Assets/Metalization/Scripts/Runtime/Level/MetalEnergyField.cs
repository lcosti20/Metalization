// See-through energy field that stops shots but not sight lines or the Hack Tether.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// A glowing, semi-transparent slab (a scaled cube with the EnergyField shader). It blocks robots, hitscan shots,
    /// projectiles and blast damage. Sight and the Hack Tether see through it (those traces ignore every field). Every
    /// shot that lands on it starts a ring wave at the impact point (up to four at once, oldest replaced).
    /// </summary>
    [RequireComponent(typeof(BoxCollider))]
    public class MetalEnergyField : MonoBehaviour, IMetalDamageable
    {
        public static readonly List<MetalEnergyField> All = new List<MetalEnergyField>();

        const int MaxRipples = 4;
        static readonly int ColorId = Shader.PropertyToID("_BaseColor");
        static readonly int RippleLifeId = Shader.PropertyToID("_RippleLife");
        static readonly int[] HitIds = { Shader.PropertyToID("_Hit0"), Shader.PropertyToID("_Hit1"), Shader.PropertyToID("_Hit2"), Shader.PropertyToID("_Hit3") };

        public Color fieldColor = new Color(1f, 0.3f, 0.1f);
        [Tooltip("Seconds a ripple takes to spread and fade.")]
        public float rippleLife = 0.9f;

        readonly Vector3[] rippleCenters = new Vector3[MaxRipples];
        readonly float[] rippleStarts = { -1f, -1f, -1f, -1f };
        MeshRenderer meshRenderer;
        MaterialPropertyBlock block;
        bool anyActive;

        void Awake()
        {
            meshRenderer = GetComponent<MeshRenderer>();
            if (meshRenderer != null)
            {
                Material mat = MetalGameAssets.Get() != null ? MetalGameAssets.Get().energyFieldMaterial : null;
                if (mat != null) meshRenderer.sharedMaterial = mat;
                meshRenderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            }
            block = new MaterialPropertyBlock();
            PushRipples();
        }

        void OnEnable() => All.Add(this);
        void OnDisable() => All.Remove(this);

        /// <summary>Starts a ring wave on the field surface at location (world space).</summary>
        public void AddRipple(Vector3 location)
        {
            float now = Time.time;
            // A free (or finished) slot, else the oldest ripple makes way.
            int slot = 0;
            for (int i = 0; i < MaxRipples; i++)
            {
                if (rippleStarts[i] < 0f || now - rippleStarts[i] >= rippleLife)
                {
                    slot = i;
                    break;
                }
                if (rippleStarts[i] < rippleStarts[slot]) slot = i;
            }
            rippleCenters[slot] = location;
            rippleStarts[slot] = now;
            anyActive = true;
            PushRipples();
        }

        void Update()
        {
            if (anyActive) PushRipples();
        }

        void PushRipples()
        {
            float now = Time.time;
            bool active = false;
            if (meshRenderer != null) meshRenderer.GetPropertyBlock(block);
            for (int i = 0; i < MaxRipples; i++)
            {
                float age = rippleStarts[i] < 0f ? -1f : now - rippleStarts[i];
                if (age >= rippleLife)
                {
                    rippleStarts[i] = -1f;
                    age = -1f;
                }
                active |= age >= 0f;
                Vector3 c = rippleCenters[i];
                block.SetVector(HitIds[i], new Vector4(c.x, c.y, c.z, age));
            }
            block.SetColor(ColorId, fieldColor);
            block.SetFloat(RippleLifeId, rippleLife);
            if (meshRenderer != null) meshRenderer.SetPropertyBlock(block);
            anyActive = active;
        }

        /// <summary>Shots that land on the field ripple it instead of hurting anything.</summary>
        public float TakeDamage(MetalDamageEvent e)
        {
            AddRipple(e.isPoint ? e.hitPoint : GetComponent<Collider>().ClosestPoint(e.hitPoint));
            return 0f;
        }
    }
}
