// AI pilot for Stalkers in the space battles: picks a target by role, routes through portals between open
// space and the capital ships' hangars, circles its target at a standoff range and fires in bursts.
using UnityEngine;

namespace Metalization
{
    /// <summary>What an AI pilot goes after.</summary>
    public enum MetalPilotRole
    {
        /// <summary>Defends its own ship, then hunts enemy flyers anywhere in range.</summary>
        Fighter,
        /// <summary>Attacks the enemy capital ship's subsystems, then its hull.</summary>
        Raider,
    }

    public class MetalFlyerAI : MetalController
    {
        public MetalPilotRole pilotRole = MetalPilotRole.Fighter;
        [Tooltip("Enemy flyers this close (m) are engaged.")]
        public float engageRange = 3000f;
        [Tooltip("Enemy flyers this close to the home ship (m) are hunted first by fighters.")]
        public float defendRadius = 800f;
        [Tooltip("Standoff distance (m) when strafing a ship subsystem.")]
        public float strafeRange = 220f;
        [Tooltip("Fires when the target is closer than this (m)...")]
        public float fireRange = 450f;
        [Tooltip("...and within this cone (degrees) of the guns.")]
        public float fireCone = 8f;

        /// <summary>Interiors are self-contained rooms this close (m) to their ship's interior location.</summary>
        const float InteriorRadius = 600f;
        const float ThinkInterval = 0.4f;
        /// <summary>Portal routing: line up this far (m) in front of the mouth, then fly straight through.</summary>
        const float PortalApproach = 60f;
        const float PortalLineUpRadius = 15f;
        /// <summary>How far ahead (seconds of travel) to sweep for obstacles.</summary>
        const float LookAheadTime = 1.2f;

        MetalBot targetFlyer;
        MetalShipSubsystem targetPart;
        MetalCapitalShip targetShipHull;
        MetalBot lastAttacker;
        MetalPortal routePortal;
        bool routeLinedUp;
        float nextThinkTime;
        float nextDodgeTime;
        float burstToggleTime;
        bool burstFiring;
        float orbitPhase;
        float orbitDirection = 1f;

        MetalStalker Me => Pawn as MetalStalker;

        protected override void OnPossess(MetalBot bot)
        {
            orbitPhase = Random.Range(0f, Mathf.PI * 2f);
            orbitDirection = Random.value < 0.5f ? 1f : -1f;
            nextThinkTime = 0f;
        }

        /// <summary>One line for debug output: role and what it's doing.</summary>
        public string DescribeState()
        {
            string role = pilotRole == MetalPilotRole.Fighter ? "fighter" : "raider";
            if (routePortal != null) return $"{role} via {routePortal.name}";
            if (targetFlyer != null) return $"{role} -> {targetFlyer.name}";
            if (targetPart != null) return $"{role} -> {targetPart.name}";
            if (targetShipHull != null) return $"{role} -> {targetShipHull.name} hull";
            return role + " idle";
        }

        public override void NotifyDamaged(MetalBot attacker)
        {
            MetalStalker me = Me;
            if (me == null || attacker == null || !me.IsHostileTo(attacker)) return;
            lastAttacker = attacker;
            // Break the attacker's aim with a roll, now and then.
            if (Time.time >= nextDodgeTime)
            {
                nextDodgeTime = Time.time + Random.Range(2.5f, 4.5f);
                me.Dodge(Random.value < 0.5f ? 1f : -1f);
            }
            // Fighters turn on whoever is shooting them.
            if (pilotRole == MetalPilotRole.Fighter && attacker is MetalStalker)
            {
                targetFlyer = attacker;
                targetPart = null;
                targetShipHull = null;
            }
        }

        static MetalCapitalShip SpaceOf(Vector3 location)
        {
            foreach (MetalCapitalShip ship in MetalCapitalShip.All)
            {
                if ((location - ship.InteriorLocation).sqrMagnitude < InteriorRadius * InteriorRadius) return ship;
            }
            return null;
        }

        MetalBot FindEnemyFlyer(Vector3 near, float range)
        {
            MetalStalker me = Me;
            MetalBot best = null;
            float bestDistance = range;
            foreach (MetalBot b in MetalBot.All)
            {
                // Flying (piloted) enemy Stalkers; parked ones are just hardware.
                if (!(b is MetalStalker s) || !s.IsAlive || s.IsDormant || s.IsParked || s.Controller == null || !me.IsHostileTo(s)) continue;
                float d = Vector3.Distance(s.transform.position, near);
                if (d < bestDistance)
                {
                    best = s;
                    bestDistance = d;
                }
            }
            return best;
        }

        /// <summary>Shields first (they protect the hull), then engines and comms, then life support, then the hull.</summary>
        bool FindRaidTarget(out MetalShipSubsystem part, out MetalCapitalShip hull)
        {
            part = null;
            hull = null;
            MetalStalker me = Me;
            foreach (MetalCapitalShip ship in MetalCapitalShip.All)
            {
                if (ship.Team == me.Team || ship.IsShipDestroyed) continue;
                MetalShipSubsystemType[] order =
                {
                    MetalShipSubsystemType.ShieldGenerator, MetalShipSubsystemType.Engines,
                    MetalShipSubsystemType.Comms, MetalShipSubsystemType.LifeSupport,
                };
                foreach (MetalShipSubsystemType type in order)
                {
                    MetalShipSubsystem closest = null;
                    foreach (MetalShipSubsystem s in ship.Subsystems)
                    {
                        if (s.type == type && !s.IsDestroyed && (closest == null
                            || (s.Center - me.transform.position).sqrMagnitude < (closest.Center - me.transform.position).sqrMagnitude))
                        {
                            closest = s;
                        }
                    }
                    if (closest != null)
                    {
                        part = closest;
                        return true;
                    }
                }
                hull = ship;
                return true;
            }
            return false;
        }

        void ChooseTarget()
        {
            MetalStalker me = Me;
            MetalCapitalShip home = null;
            foreach (MetalCapitalShip ship in MetalCapitalShip.All)
            {
                if (ship.Team == me.Team) home = ship;
            }
            // Keep a live flyer target while it's in range; otherwise re-pick by role.
            if (targetFlyer != null && targetFlyer.IsAlive && !targetFlyer.IsDormant
                && Vector3.Distance(targetFlyer.transform.position, me.transform.position) < engageRange)
            {
                return;
            }
            targetFlyer = null;
            if (pilotRole == MetalPilotRole.Raider && ((targetPart != null && !targetPart.IsDestroyed) || (targetShipHull != null && !targetShipHull.IsShipDestroyed)))
            {
                return;
            }
            targetPart = null;
            targetShipHull = null;
            if (pilotRole == MetalPilotRole.Fighter)
            {
                // Defend the ship first, then hunt anywhere in range.
                MetalBot raider = home != null ? FindEnemyFlyer(home.transform.position, defendRadius) : null;
                targetFlyer = raider != null ? raider : FindEnemyFlyer(me.transform.position, engageRange);
                if (targetFlyer == null) FindRaidTarget(out targetPart, out targetShipHull);
            }
            else
            {
                if (!FindRaidTarget(out targetPart, out targetShipHull)) targetFlyer = FindEnemyFlyer(me.transform.position, engageRange);
            }
        }

        Vector3 RouteTo(Vector3 goal, out bool direct)
        {
            Vector3 here = Me.transform.position;
            MetalCapitalShip mySpace = SpaceOf(here);
            MetalCapitalShip goalSpace = SpaceOf(goal);
            direct = mySpace == goalSpace;
            if (direct)
            {
                routePortal = null;
                routeLinedUp = false;
                return goal;
            }
            // Inside an interior: leave by its hangar mouth. Out in space: enter the goal's interior by its hull mouth.
            MetalPortal portal = mySpace != null ? mySpace.InnerPortal : (goalSpace != null ? goalSpace.OuterPortal : null);
            if (portal == null)
            {
                direct = true;
                return goal;
            }
            if (routePortal != portal)
            {
                routePortal = portal;
                routeLinedUp = false;
            }
            // Line up out in front of the mouth on our side, then fly straight through.
            Vector3 forward = portal.transform.forward;
            Vector3 ownSide = forward * Mathf.Sign(portal.GetSideDistance(portal.transform.position + forward));
            Vector3 lineUp = portal.transform.position + ownSide * PortalApproach;
            if (!routeLinedUp && Vector3.Distance(here, lineUp) < PortalLineUpRadius) routeLinedUp = true;
            // Once lined up, aim 30 m past the mouth so the pilot crosses the portal plane at speed.
            return routeLinedUp ? portal.transform.position - ownSide * 30f : lineUp;
        }

        Vector3 AvoidObstacles(Vector3 direction)
        {
            MetalStalker me = Me;
            float speed = Mathf.Max(15f, me.Velocity.magnitude);
            Vector3 start = me.transform.position;
            float radius = me.Capsule.radius * 1.5f;
            float dist = speed * LookAheadTime;
            RaycastHit[] hits = Physics.SphereCastAll(start, radius, direction, dist, MetalPhysics.ShotMask & ~MetalPhysics.BotMask, QueryTriggerInteraction.Ignore);
            float best = float.MaxValue;
            RaycastHit? hit = null;
            foreach (RaycastHit h in hits)
            {
                if (h.collider.transform.IsChildOf(me.transform)) continue;
                if (targetFlyer != null && h.collider.transform.IsChildOf(targetFlyer.transform)) continue;
                if (h.collider.GetComponentInParent<MetalPortal>() != null) continue;
                if (h.distance < best && h.distance > 0f)
                {
                    best = h.distance;
                    hit = h;
                }
            }
            if (!hit.HasValue) return direction;
            // Slide off the surface, biased upward so pilots climb over hulls instead of hugging them. Nearer
            // obstacles bend the path harder.
            Vector3 n = hit.Value.normal;
            Vector3 slide = Vector3.ProjectOnPlane(direction, n).normalized;
            float urgency = 1f - best / dist;
            return (direction * (1f - urgency) + (slide + n * 0.8f + Vector3.up * 0.4f) * urgency).normalized;
        }

        void Steer(Vector3 movePoint, Vector3 lookPoint, float dt, bool boost)
        {
            MetalStalker me = Me;
            Vector3 toMove = movePoint - me.transform.position;
            float distance = toMove.magnitude;
            Vector3 dir = AvoidObstacles(distance > 0.01f ? toMove / distance : me.transform.forward);
            // Ease off on arrival so pilots settle into an orbit instead of overshooting it.
            float throttle = Mathf.Clamp(distance / 30f, 0.15f, 1f);
            me.AddMovementInput(dir, throttle);
            me.SetBoostHeld(boost && distance > 400f && Vector3.Dot(me.GetAimForward(), toMove / Mathf.Max(distance, 0.01f)) > 0.8f);
            // Turn the nose (control rotation drives the body's yaw and pitch) toward what we want to look at.
            Vector3 toLook = lookPoint - me.transform.position;
            if (toLook.sqrMagnitude > 1e-4f)
            {
                float k = Mathf.Clamp01(dt * 5f);
                ControlYaw = Mathf.LerpAngle(ControlYaw, MetalMath.Yaw(toLook), k);
                ControlPitch = Mathf.LerpAngle(ControlPitch, MetalMath.Pitch(toLook), k);
            }
        }

        void UpdateFiring(Vector3 aimPoint, float now)
        {
            MetalStalker me = Me;
            Vector3 toAim = aimPoint - me.transform.position;
            bool linedUp = toAim.magnitude < fireRange && Vector3.Dot(me.GetAimForward(), toAim.normalized) > Mathf.Cos(fireCone * Mathf.Deg2Rad);
            if (linedUp)
            {
                // Hold fire if something else (a hull, a friend) is in the way.
                if (MetalPhysics.Raycast(me.transform.position, aimPoint, out RaycastHit hit, me))
                {
                    bool onTarget = (targetFlyer != null && hit.collider.transform.IsChildOf(targetFlyer.transform))
                        || (targetPart != null && hit.collider.transform.IsChildOf(targetPart.transform))
                        || (targetShipHull != null && hit.collider.transform.IsChildOf(targetShipHull.transform));
                    if (!onTarget) linedUp = false;
                }
            }
            if (!linedUp)
            {
                me.StopFire();
                burstFiring = false;
                return;
            }
            // Bursts and pauses, like the ground troops.
            if (now >= burstToggleTime)
            {
                burstFiring = !burstFiring;
                float burst = me.archetype != null ? me.archetype.burstDuration : 1.2f;
                float pause = me.archetype != null ? me.archetype.burstPause : 1f;
                burstToggleTime = now + (burstFiring ? burst : pause) * Random.Range(0.8f, 1.2f);
            }
            if (burstFiring) me.StartFire();
            else me.StopFire();
        }

        void Update()
        {
            MetalStalker me = Me;
            if (me == null || !me.IsAlive || me.IsDormant || me.IsStunned)
            {
                if (me != null)
                {
                    me.StopFire();
                    me.SetBoostHeld(false);
                }
                return;
            }
            float dt = Time.deltaTime;
            float now = Time.time;
            if (now >= nextThinkTime)
            {
                nextThinkTime = now + ThinkInterval * Random.Range(0.8f, 1.2f);
                ChooseTarget();
            }
            Vector3 here = me.transform.position;
            Vector3 goal;
            Vector3 aimPoint;
            float standoff;
            if (targetFlyer != null)
            {
                // Lead the target by the slug's flight time.
                MetalWeaponSlot slot = me.CurrentWeapon;
                float shotSpeed = slot != null && slot.data != null ? Mathf.Max(10f, slot.data.projectileSpeed) : 300f;
                Vector3 t = targetFlyer.transform.position;
                aimPoint = t + targetFlyer.Velocity * (Vector3.Distance(here, t) / shotSpeed);
                goal = t;
                standoff = me.archetype != null ? Mathf.Max(30f, me.archetype.preferredRange) : 40f;
            }
            else if (targetPart != null || targetShipHull != null)
            {
                aimPoint = targetPart != null ? targetPart.Center : targetShipHull.transform.position;
                goal = aimPoint;
                standoff = targetPart != null ? strafeRange : strafeRange * 2f;
            }
            else
            {
                me.StopFire();
                me.SetAITarget(Vector3.zero, false);
                Steer(here, here + me.GetAimForward() * 100f, dt, false);
                return;
            }

            Vector3 waypoint = RouteTo(goal, out bool direct);
            if (!direct)
            {
                // On the way through a portal: no shooting, nose on the path.
                me.StopFire();
                me.SetAITarget(Vector3.zero, false);
                Steer(waypoint, waypoint, dt, true);
                return;
            }

            // Circle the target at the standoff range, guns on it. The orbit point stays on our side of the target at
            // a shallow 25-35 degree elevation, swaying left/right and up/down to stay a moving target.
            orbitPhase += dt * 0.35f * orbitDirection;
            Vector3 away = MetalMath.Flat(here - goal).normalized;
            if (away.sqrMagnitude < 1e-4f) away = -MetalMath.Flat(me.transform.forward).normalized;
            Vector3 sideways = Quaternion.AngleAxis(35f * Mathf.Sin(orbitPhase), Vector3.up) * away;
            float elevation = (30f + 5f * Mathf.Cos(orbitPhase * 0.7f)) * Mathf.Deg2Rad;
            Vector3 orbit = sideways * Mathf.Cos(elevation) + Vector3.up * Mathf.Sin(elevation);
            // Far away, head straight for the target; only start orbiting once within 2.5x the standoff range.
            Vector3 movePoint = Vector3.Distance(here, goal) > standoff * 2.5f ? goal : goal + orbit * standoff;
            me.SetAITarget(aimPoint, true);
            Steer(movePoint, aimPoint, dt, true);
            UpdateFiring(aimPoint, now);
        }
    }
}
