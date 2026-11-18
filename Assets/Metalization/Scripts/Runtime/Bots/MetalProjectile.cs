// Bolts, rockets and grenades, configured from the firing weapon's MetalWeaponData.
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// A physical shot spawned by MetalBot.FireOnce / ThrowGrenade and by capital ship turrets. Direct-hit shots deal
    /// point damage (so hit zones apply); explosive ones deal radial damage with falloff and can stun; thrown ones
    /// bounce and explode on a fuse or on touching a robot. Moves by sphere sweeps, so it never tunnels.
    /// </summary>
    public class MetalProjectile : MonoBehaviour
    {
        /// <summary>The robot that fired it (friendly fire and kill credit), or null.</summary>
        public MetalBot InstigatorBot { get; private set; }
        /// <summary>Whatever owns the shot (the shooter, or a capital ship for turret shots).</summary>
        public GameObject Owner { get; private set; }

        MetalController instigatorController;
        Vector3 velocity;
        float gravityScale;
        bool bounce;
        float radius = 0.06f;
        float damage;
        float explosionRadius;
        float stunDuration;
        float fuseTime;
        Color color = Color.white;
        float scale = 1f;
        bool detonated;
        string impactSound;
        float lifeLeft = 8f;
        Transform visual;

        const float Bounciness = 0.35f;
        const float Friction = 0.4f;

        /// <summary>Spawns and launches a projectile. Velocity in m/s.</summary>
        public static MetalProjectile Spawn(Vector3 position, Vector3 facing, MetalBot instigator, GameObject owner,
            MetalWeaponData weapon, float damage, Vector3 velocity)
        {
            if (weapon == null) return null;
            var go = new GameObject("Projectile " + weapon.name);
            go.transform.SetPositionAndRotation(position, Quaternion.LookRotation(facing.sqrMagnitude > 1e-6f ? facing : Vector3.forward));
            if (MetalPhysics.ProjectileLayer >= 0) go.layer = MetalPhysics.ProjectileLayer;
            MetalProjectile p = go.AddComponent<MetalProjectile>();
            p.InstigatorBot = instigator;
            p.instigatorController = instigator != null ? instigator.Controller : null;
            p.Owner = owner;
            p.Launch(weapon, damage, velocity);
            return p;
        }

        void Launch(MetalWeaponData weapon, float inDamage, Vector3 inVelocity)
        {
            damage = inDamage;
            explosionRadius = weapon.explosionRadius;
            stunDuration = weapon.stunDuration;
            color = weapon.tracerColor;
            impactSound = weapon.impactSound;
            scale = Mathf.Max(0.1f, weapon.projectileScale);
            radius = 0.06f * scale;
            bool thrown = weapon.fireMode == MetalFireMode.Thrown;
            if (thrown && weapon.propModel != null)
            {
                GameObject prop = Instantiate(weapon.propModel, transform, false);
                foreach (Collider c in prop.GetComponentsInChildren<Collider>()) Destroy(c);
                prop.transform.localScale = Vector3.one * 1.5f;
                visual = prop.transform;
                fuseTime = 1.6f;
            }
            else
            {
                // Glowing slug: a sphere stretched along the flight direction.
                GameObject slug = MetalMaterials.Shape(PrimitiveType.Sphere, transform, "Slug");
                slug.transform.localScale = (explosionRadius > 0f ? new Vector3(0.16f, 0.16f, 0.35f) : new Vector3(0.07f, 0.07f, 0.3f)) * scale;
                MetalMaterials.SetGlow(slug.GetComponent<Renderer>(), color, 8f);
                visual = slug.transform;
            }
            gravityScale = weapon.projectileGravityScale;
            bounce = thrown;
            velocity = inVelocity;
        }

        /// <summary>Overrides a thrown weapon's fuse, e.g. to let a long lob land before it goes off.</summary>
        public void SetFuse(float seconds) => fuseTime = seconds;

        public Vector3 Velocity => velocity;

        void Update()
        {
            float dt = Time.deltaTime;
            if (dt <= 0f || detonated) return;
            lifeLeft -= dt;
            if (lifeLeft <= 0f)
            {
                Destroy(gameObject);
                return;
            }
            if (fuseTime > 0f)
            {
                fuseTime -= dt;
                if (fuseTime <= 0f)
                {
                    Detonate(null);
                    return;
                }
            }
            velocity += Physics.gravity * gravityScale * dt;
            Move(dt, 3);
            if (!detonated && velocity.sqrMagnitude > 0.01f && !bounce)
            {
                transform.rotation = Quaternion.LookRotation(velocity);
            }
            else if (bounce && visual != null && velocity.sqrMagnitude > 0.25f)
            {
                visual.Rotate(Vector3.right, 540f * dt, Space.Self);
            }
        }

        static readonly RaycastHit[] Hits = new RaycastHit[16];

        void Move(float timeLeft, int iterations)
        {
            for (int it = 0; it < iterations && timeLeft > 1e-5f && !detonated; it++)
            {
                Vector3 delta = velocity * timeLeft;
                float dist = delta.magnitude;
                if (dist < 1e-5f) return;
                Vector3 dir = delta / dist;
                int mask = Physics.DefaultRaycastLayers;
                if (MetalPhysics.ProjectileLayer >= 0) mask &= ~(1 << MetalPhysics.ProjectileLayer);
                if (MetalPhysics.TriggerLayer >= 0) mask &= ~(1 << MetalPhysics.TriggerLayer);
                int n = Physics.SphereCastNonAlloc(transform.position, radius, dir, Hits, dist, mask, QueryTriggerInteraction.Ignore);
                int best = -1;
                float bestDist = float.MaxValue;
                for (int i = 0; i < n; i++)
                {
                    Collider c = Hits[i].collider;
                    // Ignore the shooter (and whatever owns the shot) so a fresh shot doesn't hit its own gun.
                    if (InstigatorBot != null && c.transform.IsChildOf(InstigatorBot.transform)) continue;
                    if (Owner != null && c.transform.IsChildOf(Owner.transform)) continue;
                    if (Hits[i].distance <= 0f && Hits[i].point == Vector3.zero) continue; // started overlapping
                    if (Hits[i].distance < bestDist)
                    {
                        bestDist = Hits[i].distance;
                        best = i;
                    }
                }
                if (best < 0)
                {
                    transform.position += delta;
                    return;
                }
                RaycastHit hit = Hits[best];
                transform.position += dir * Mathf.Max(0f, hit.distance - 0.005f);
                if (!OnHit(hit)) return;
                // Bounced: carry on for the rest of the frame along the new velocity.
                timeLeft *= 1f - Mathf.Clamp01(hit.distance / dist);
            }
        }

        /// <returns>True if the projectile bounced and keeps moving.</returns>
        bool OnHit(RaycastHit hit)
        {
            bool isBot = MetalPhysics.FindBot(hit.collider) != null;
            // Grenades bounce off the world and only go off early when they hit a robot.
            if (fuseTime > 0f && !isBot)
            {
                MetalEnergyField field = hit.collider.GetComponentInParent<MetalEnergyField>();
                if (field != null && velocity.sqrMagnitude > 1.5f * 1.5f) field.AddRipple(hit.point);
                Vector3 n = hit.normal;
                Vector3 normalPart = Vector3.Dot(velocity, n) * n;
                Vector3 tangent = velocity - normalPart;
                velocity = tangent * (1f - Friction) - normalPart * Bounciness;
                if (velocity.sqrMagnitude < 0.3f * 0.3f && n.y > 0.7f)
                {
                    velocity = Vector3.zero; // came to rest on the ground
                    return false;
                }
                transform.position += n * 0.01f;
                return true;
            }
            Detonate(hit);
            return false;
        }

        void Detonate(RaycastHit? directHit)
        {
            if (detonated) return;
            detonated = true;
            Vector3 location = directHit.HasValue ? directHit.Value.point : transform.position;
            MetalEnergyField hitField = directHit.HasValue ? directHit.Value.collider.GetComponentInParent<MetalEnergyField>() : null;
            if (hitField != null)
            {
                // Blow up just in front of the field: a blast centred on its surface would reach robots behind it.
                location += directHit.Value.normal * 0.15f;
            }
            if (explosionRadius > 0f)
            {
                if (hitField != null) hitField.AddRipple(directHit.Value.point);
                // Full damage inside the inner 30% of the radius, falling to 25% at the edge. The shooter can be caught
                // by their own blast (MetalBot halves self-damage).
                MetalPhysics.ApplyRadialDamage(damage, damage * 0.25f, location, explosionRadius * 0.3f, explosionRadius, 1f,
                    gameObject, instigatorController, null, Owner);
                if (stunDuration > 0f)
                {
                    foreach (MetalBot bot in MetalPhysics.OverlapBots(location, explosionRadius))
                    {
                        // The pulse goes round cover, but an energy field in between soaks it up.
                        bool shielded = MetalPhysics.Raycast(location, bot.transform.position, out RaycastHit block, bot)
                            && block.collider.GetComponentInParent<MetalEnergyField>() != null;
                        if (!shielded) bot.Stun(stunDuration);
                    }
                    // An EMP is a clean pulse of light; anything else is a fireball.
                    MetalFX.SpawnBurst(location, color, explosionRadius * 0.8f, 0.45f);
                }
                else
                {
                    MetalFX.SpawnExplosion(location, color, explosionRadius * 0.8f, 0.45f);
                }
                MetalAudio.PlayAt(stunDuration > 0f ? "emp_blast" : "explosion", location, 1f, 0.08f);
                // Explosions are heard a long way off.
                if (InstigatorBot != null && InstigatorBot.IsPlayerControlled) MetalBotAI.ReportNoise(InstigatorBot, location, 18f);
            }
            else
            {
                if (directHit.HasValue)
                {
                    RaycastHit h = directHit.Value;
                    MetalPhysics.ApplyDamage(h.collider, new MetalDamageEvent
                    {
                        amount = damage,
                        isPoint = true,
                        hitPoint = h.point,
                        shotDirection = velocity.normalized,
                        hitCollider = h.collider,
                        instigator = instigatorController,
                        causer = gameObject,
                        causerOwner = Owner,
                    });
                }
                Vector3 normal = directHit.HasValue ? directHit.Value.normal : -velocity.normalized;
                MetalFX.SpawnImpact(location, normal, color, 0.18f * scale);
                MetalAudio.PlayAt(impactSound, location, 0.5f, 0.1f);
            }
            Destroy(gameObject);
        }
    }
}
