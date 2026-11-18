// Combat effects (muzzle flashes, impacts, explosions, beams), built in code from Unity particle systems, a
// line renderer and a fading point light, so no effect assets need authoring. Every call site goes through
// these static helpers; replacing them with authored VFX later means changing only this file.
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// A short-lived effect: particle systems plus an optional point light that fades out. It destroys itself once
    /// the particles and the light are done, unless it is a persistent beam (lifetime &lt;= 0), which the caller
    /// re-aims with SetBeamEndpoints and destroys.
    /// </summary>
    public class MetalFX : MonoBehaviour
    {
        float lifetime;
        float age;
        float startLightIntensity;
        float beamWidth = 0.03f;
        Light fxLight;
        LineRenderer beam;
        LineRenderer halo;
        Color beamColor;
        ParticleSystem[] systems = new ParticleSystem[0];

        // --- Spawning -------------------------------------------------------------------------------------

        static MetalFX Create(string name, Vector3 location, Quaternion rotation, float lifetime)
        {
            var go = new GameObject(name);
            go.transform.SetPositionAndRotation(location, rotation);
            MetalFX fx = go.AddComponent<MetalFX>();
            fx.lifetime = lifetime;
            return fx;
        }

        void AddLight(Color color, float range, float intensity)
        {
            if (range <= 0f) return;
            var lightGo = new GameObject("Light");
            lightGo.transform.SetParent(transform, false);
            fxLight = lightGo.AddComponent<Light>();
            fxLight.type = LightType.Point;
            fxLight.color = color;
            fxLight.range = range;
            fxLight.shadows = LightShadows.None;
            fxLight.intensity = intensity;
            startLightIntensity = intensity;
        }

        /// <summary>Glowing line from start to end. lifetime &gt; 0: a tracer that thins and fades; &lt;= 0: persistent (laser sight, tether).</summary>
        public static MetalFX SpawnBeam(Vector3 start, Vector3 end, Color color, float width = 0.03f, float lifetime = 0.08f)
        {
            MetalFX fx = Create(lifetime > 0f ? "FX Tracer" : "FX Beam", start, Quaternion.identity, lifetime);
            fx.beamWidth = Mathf.Max(0.004f, width);
            fx.beamColor = color;
            fx.beam = fx.MakeLine("Core", 1f, Color.Lerp(color, Color.white, 0.5f) * 3f);
            fx.halo = fx.MakeLine("Halo", 3.5f, color * 0.8f);
            fx.SetBeamEndpoints(start, end);
            if (lifetime > 0f)
            {
                // A few sparks crackle along a tracer.
                ParticleSystem sparks = fx.AddSystem("Sparks", MetalGameAssets.Get()?.particleMaterial);
                var main = sparks.main;
                main.startLifetime = new ParticleSystem.MinMaxCurve(0.05f, 0.15f);
                main.startSpeed = new ParticleSystem.MinMaxCurve(0.2f, 1.2f);
                main.startSize = new ParticleSystem.MinMaxCurve(width * 0.8f, width * 1.6f);
                main.startColor = color * 2f;
                var shape = sparks.shape;
                shape.shapeType = ParticleSystemShapeType.SingleSidedEdge;
                shape.radius = Mathf.Max(0.01f, (end - start).magnitude * 0.5f);
                shape.rotation = new Vector3(0f, 90f, 0f);
                shape.position = Vector3.zero;
                sparks.transform.position = (start + end) * 0.5f;
                sparks.transform.rotation = Quaternion.LookRotation(Vector3.up, end - start);
                Burst(sparks, Mathf.Clamp((int)((end - start).magnitude / 2f), 2, 10));
                fx.PlayAll();
            }
            return fx;
        }

        LineRenderer MakeLine(string name, float widthScale, Color color)
        {
            var go = new GameObject(name);
            go.transform.SetParent(transform, false);
            LineRenderer lr = go.AddComponent<LineRenderer>();
            lr.useWorldSpace = true;
            lr.positionCount = 2;
            lr.numCapVertices = 2;
            lr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            lr.receiveShadows = false;
            MetalGameAssets assets = MetalGameAssets.Get();
            lr.sharedMaterial = assets != null ? (assets.beamMaterial != null ? assets.beamMaterial : assets.particleMaterial) : null;
            lr.textureMode = LineTextureMode.Stretch;
            lr.widthMultiplier = beamWidth * widthScale;
            lr.startColor = lr.endColor = color;
            return lr;
        }

        /// <summary>Swelling glow with drifting motes and a light (pickups, consoles, small sparks of light). Radius in metres.</summary>
        public static MetalFX SpawnBurst(Vector3 location, Color color, float radius, float lifetime = 0.3f)
        {
            MetalFX fx = Create("FX Burst", location, Quaternion.identity, Mathf.Max(lifetime, 0.01f));
            Material add = MetalGameAssets.Get()?.particleMaterial;
            ParticleSystem glow = fx.AddSystem("Glow", add);
            var main = glow.main;
            main.startLifetime = Mathf.Max(0.15f, lifetime);
            main.startSpeed = 0f;
            main.startSize = radius * 2f;
            main.startColor = color * 2f;
            SizeOverLife(glow, AnimationCurve.EaseInOut(0f, 0.4f, 1f, 1.2f));
            FadeOverLife(glow);
            Burst(glow, 1);

            ParticleSystem motes = fx.AddSystem("Motes", add);
            main = motes.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.3f, 0.7f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(radius * 0.5f, radius * 2f);
            main.startSize = new ParticleSystem.MinMaxCurve(radius * 0.05f, radius * 0.12f);
            main.startColor = color * 3f;
            var shape = motes.shape;
            shape.shapeType = ParticleSystemShapeType.Sphere;
            shape.radius = radius * 0.3f;
            FadeOverLife(motes);
            Burst(motes, 10);
            fx.AddLight(color, radius * 4f, 3f);
            fx.PlayAll();
            return fx;
        }

        /// <summary>Muzzle flash: a hot star, flame petals and sparks thrown along direction, with a quick light. Size in metres.</summary>
        public static MetalFX SpawnMuzzleFlash(Vector3 location, Vector3 direction, Color color, float size)
        {
            Quaternion rot = Quaternion.LookRotation(direction.sqrMagnitude > 1e-6f ? direction : Vector3.forward);
            MetalFX fx = Create("FX Muzzle", location, rot, 0.08f);
            Material add = MetalGameAssets.Get()?.particleMaterial;
            ParticleSystem star = fx.AddSystem("Star", add);
            var main = star.main;
            main.startLifetime = 0.06f;
            main.startSpeed = 0f;
            main.startSize = size * 2.4f;
            main.startColor = Color.Lerp(color, Color.white, 0.6f) * 4f;
            main.startRotation = new ParticleSystem.MinMaxCurve(0f, Mathf.PI * 2f);
            FadeOverLife(star);
            Burst(star, 1);

            ParticleSystem petals = fx.AddSystem("Petals", add);
            main = petals.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.04f, 0.09f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(size * 10f, size * 25f);
            main.startSize = new ParticleSystem.MinMaxCurve(size * 0.6f, size * 1.2f);
            main.startColor = color * 3f;
            var shape = petals.shape;
            shape.shapeType = ParticleSystemShapeType.Cone;
            shape.angle = 18f;
            shape.radius = size * 0.1f;
            FadeOverLife(petals);
            Burst(petals, 5);

            ParticleSystem sparks = fx.AddSystem("Sparks", add);
            main = sparks.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.08f, 0.2f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(size * 30f, size * 60f);
            main.startSize = new ParticleSystem.MinMaxCurve(size * 0.08f, size * 0.16f);
            main.startColor = color * 4f;
            main.gravityModifier = 0.5f;
            shape = sparks.shape;
            shape.shapeType = ParticleSystemShapeType.Cone;
            shape.angle = 25f;
            shape.radius = 0.01f;
            StretchSparks(sparks);
            Burst(sparks, 6);
            fx.AddLight(color, size * 8f, 4f);
            fx.PlayAll();
            return fx;
        }

        /// <summary>Bullet impact: a flash, sparks bouncing off the surface and a dust puff. Size in metres.</summary>
        public static MetalFX SpawnImpact(Vector3 location, Vector3 normal, Color color, float size)
        {
            Quaternion rot = Quaternion.LookRotation(normal.sqrMagnitude > 1e-6f ? normal : Vector3.up);
            MetalFX fx = Create("FX Impact", location, rot, 0.1f);
            Material add = MetalGameAssets.Get()?.particleMaterial;
            ParticleSystem flash = fx.AddSystem("Flash", add);
            var main = flash.main;
            main.startLifetime = 0.08f;
            main.startSpeed = 0f;
            main.startSize = size * 2.5f;
            main.startColor = Color.Lerp(color, Color.white, 0.4f) * 3f;
            FadeOverLife(flash);
            Burst(flash, 1);

            ParticleSystem sparks = fx.AddSystem("Sparks", add);
            main = sparks.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.15f, 0.4f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(size * 15f, size * 40f);
            main.startSize = new ParticleSystem.MinMaxCurve(size * 0.08f, size * 0.15f);
            main.startColor = color * 4f;
            main.gravityModifier = 1f;
            var shape = sparks.shape;
            shape.shapeType = ParticleSystemShapeType.Cone;
            shape.angle = 50f;
            shape.radius = 0.01f;
            StretchSparks(sparks);
            var collision = sparks.collision;
            collision.enabled = true;
            collision.type = ParticleSystemCollisionType.World;
            collision.bounce = 0.4f;
            collision.lifetimeLoss = 0.3f;
            Burst(sparks, 8);

            ParticleSystem dust = fx.AddSystem("Dust", MetalGameAssets.Get()?.smokeMaterial);
            main = dust.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.3f, 0.6f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(size * 2f, size * 6f);
            main.startSize = new ParticleSystem.MinMaxCurve(size * 1f, size * 2f);
            main.startColor = new Color(0.5f, 0.48f, 0.45f, 0.35f);
            shape = dust.shape;
            shape.shapeType = ParticleSystemShapeType.Cone;
            shape.angle = 40f;
            SizeOverLife(dust, AnimationCurve.Linear(0f, 0.6f, 1f, 1.6f));
            FadeOverLife(dust);
            Burst(dust, 3);
            fx.AddLight(color, size * 5f, 2.5f);
            fx.PlayAll();
            return fx;
        }

        /// <summary>Explosion: white-hot core, fireball, flying sparks, a rising smoke cloud and a bright light. Radius in metres.</summary>
        public static MetalFX SpawnExplosion(Vector3 location, Color color, float radius, float lifetime = 0.45f)
        {
            MetalFX fx = Create("FX Explosion", location, Quaternion.identity, Mathf.Max(lifetime, 0.01f));
            Material add = MetalGameAssets.Get()?.particleMaterial;
            ParticleSystem core = fx.AddSystem("Core", add);
            var main = core.main;
            main.startLifetime = 0.12f;
            main.startSpeed = 0f;
            main.startSize = radius * 1.6f;
            main.startColor = new Color(4f, 3.6f, 3f);
            FadeOverLife(core);
            Burst(core, 1);

            ParticleSystem fire = fx.AddSystem("Fireball", add);
            main = fire.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.25f, 0.5f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(radius * 1f, radius * 3f);
            main.startSize = new ParticleSystem.MinMaxCurve(radius * 0.6f, radius * 1.2f);
            main.startColor = color * 3f;
            main.startRotation = new ParticleSystem.MinMaxCurve(0f, Mathf.PI * 2f);
            var shape = fire.shape;
            shape.shapeType = ParticleSystemShapeType.Sphere;
            shape.radius = radius * 0.3f;
            SizeOverLife(fire, AnimationCurve.EaseInOut(0f, 0.5f, 1f, 1.3f));
            FadeOverLife(fire);
            Burst(fire, 12);

            ParticleSystem sparks = fx.AddSystem("Sparks", add);
            main = sparks.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(0.4f, 1f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(radius * 6f, radius * 14f);
            main.startSize = new ParticleSystem.MinMaxCurve(radius * 0.03f, radius * 0.07f);
            main.startColor = Color.Lerp(color, Color.white, 0.3f) * 4f;
            main.gravityModifier = 1f;
            shape = sparks.shape;
            shape.shapeType = ParticleSystemShapeType.Sphere;
            shape.radius = radius * 0.2f;
            StretchSparks(sparks);
            Burst(sparks, 24);

            ParticleSystem smoke = fx.AddSystem("Smoke", MetalGameAssets.Get()?.smokeMaterial);
            main = smoke.main;
            main.startLifetime = new ParticleSystem.MinMaxCurve(1.2f, 2.2f);
            main.startSpeed = new ParticleSystem.MinMaxCurve(radius * 0.3f, radius * 1f);
            main.startSize = new ParticleSystem.MinMaxCurve(radius * 0.9f, radius * 1.6f);
            main.startColor = new Color(0.2f, 0.19f, 0.18f, 0.55f);
            main.gravityModifier = -0.05f;
            main.startRotation = new ParticleSystem.MinMaxCurve(0f, Mathf.PI * 2f);
            shape = smoke.shape;
            shape.shapeType = ParticleSystemShapeType.Sphere;
            shape.radius = radius * 0.4f;
            SizeOverLife(smoke, AnimationCurve.Linear(0f, 0.6f, 1f, 1.8f));
            FadeOverLife(smoke);
            Burst(smoke, 8);
            fx.AddLight(color, radius * 4f, 6f);
            fx.PlayAll();
            return fx;
        }

        // --- Particle helpers -----------------------------------------------------------------------------

        ParticleSystem AddSystem(string name, Material material)
        {
            var go = new GameObject(name);
            go.transform.SetParent(transform, false);
            ParticleSystem ps = go.AddComponent<ParticleSystem>();
            ps.Stop(true, ParticleSystemStopBehavior.StopEmittingAndClear);
            var main = ps.main;
            main.playOnAwake = false;
            main.loop = false;
            main.duration = 0.1f;
            main.simulationSpace = ParticleSystemSimulationSpace.World;
            main.scalingMode = ParticleSystemScalingMode.Hierarchy;
            main.maxParticles = 64;
            var emission = ps.emission;
            emission.rateOverTime = 0f;
            var shape = ps.shape;
            shape.enabled = true;
            shape.shapeType = ParticleSystemShapeType.Sphere;
            shape.radius = 0.001f;
            ParticleSystemRenderer r = go.GetComponent<ParticleSystemRenderer>();
            r.sharedMaterial = material;
            r.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            r.receiveShadows = false;
            var list = new System.Collections.Generic.List<ParticleSystem>(systems) { ps };
            systems = list.ToArray();
            return ps;
        }

        static void Burst(ParticleSystem ps, int count)
        {
            var emission = ps.emission;
            emission.SetBursts(new[] { new ParticleSystem.Burst(0f, (short)count) });
        }

        static void FadeOverLife(ParticleSystem ps)
        {
            var col = ps.colorOverLifetime;
            col.enabled = true;
            var g = new Gradient();
            g.SetKeys(new[] { new GradientColorKey(Color.white, 0f), new GradientColorKey(Color.white, 1f) },
                new[] { new GradientAlphaKey(1f, 0f), new GradientAlphaKey(0.8f, 0.4f), new GradientAlphaKey(0f, 1f) });
            col.color = g;
        }

        static void SizeOverLife(ParticleSystem ps, AnimationCurve curve)
        {
            var size = ps.sizeOverLifetime;
            size.enabled = true;
            size.size = new ParticleSystem.MinMaxCurve(1f, curve);
        }

        static void StretchSparks(ParticleSystem ps)
        {
            ParticleSystemRenderer r = ps.GetComponent<ParticleSystemRenderer>();
            r.renderMode = ParticleSystemRenderMode.Stretch;
            r.velocityScale = 0.04f;
            r.lengthScale = 1.5f;
            FadeOverLife(ps);
        }

        void PlayAll()
        {
            foreach (ParticleSystem ps in systems) ps.Play(false);
        }

        // --- Beams ----------------------------------------------------------------------------------------

        /// <summary>Re-aims a beam.</summary>
        public void SetBeamEndpoints(Vector3 start, Vector3 end)
        {
            transform.position = (start + end) * 0.5f;
            if (beam != null)
            {
                beam.SetPosition(0, start);
                beam.SetPosition(1, end);
            }
            if (halo != null)
            {
                halo.SetPosition(0, start);
                halo.SetPosition(1, end);
            }
        }

        void Update()
        {
            if (lifetime <= 0f)
            {
                return; // persistent; the owner destroys it
            }
            age += Time.deltaTime;
            float alpha = Mathf.Clamp01(age / lifetime);
            if (fxLight != null)
            {
                // Squared so the light drops quickly, like the flash it comes from.
                fxLight.intensity = startLightIntensity * (1f - alpha) * (1f - alpha);
                fxLight.enabled = alpha < 1f;
            }
            if (beam != null)
            {
                // Tracers thin out and fade.
                float k = 1f - alpha;
                beam.widthMultiplier = beamWidth * k;
                halo.widthMultiplier = beamWidth * 3.5f * k;
                Color c = beamColor * k;
                halo.startColor = halo.endColor = c * 0.8f;
                beam.startColor = beam.endColor = Color.Lerp(beamColor, Color.white, 0.5f) * 3f * k;
            }
            if (alpha >= 1f && !AnyAlive())
            {
                Destroy(gameObject);
            }
        }

        bool AnyAlive()
        {
            foreach (ParticleSystem ps in systems)
            {
                if (ps != null && ps.IsAlive(true)) return true;
            }
            return false;
        }
    }
}
