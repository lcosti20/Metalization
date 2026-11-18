// Enemy (and allied) robot brain: a small, readable state machine.
//
// Patrol -> Investigate -> Combat (strafe at preferred range, burst fire) -> Retreat, plus stunned and
// disabled-weapon (melee charge) handling and squad alerts. Navigation uses the scene navmesh.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>States of the walking-robot AI.</summary>
    public enum MetalAIState
    {
        /// <summary>Wander near home at patrol speed, watching for enemies.</summary>
        Patrol,
        /// <summary>Walk to the last known enemy position and look around before going back to patrol.</summary>
        Investigate,
        /// <summary>Burst-fire at the target while strafing at the preferred range (or charge in for melee).</summary>
        Combat,
        /// <summary>Run away from the target once, at low health, then rejoin the fight.</summary>
        Retreat,
        Dead,
    }

    /// <summary>
    /// Default controller for walking robots. Senses enemies five times a second by sight cone and line of sight,
    /// runs the state machine every frame, and drives the robot only through its public API plus navmesh moves.
    /// </summary>
    public class MetalBotAI : MetalController
    {
        /// <summary>Every walker AI in the scene (noise reports and squad alerts).</summary>
        public static readonly List<MetalBotAI> All = new List<MetalBotAI>();

        const float SenseInterval = 0.2f;
        /// <summary>Enemies closer than this (m) are noticed even outside the sight cone.</summary>
        const float ProximityAwareness = 5f;
        /// <summary>Distance at which a disarmed robot starts swinging, m.</summary>
        const float MeleeReach = 1.7f;
        /// <summary>Minimum launch speed for AI jumps across jump links, m/s, and the strongest launch allowed.</summary>
        const float LinkJumpZ = 7f, LinkMaxJumpZ = 9f, LinkClimbClearance = 0.5f;

        MetalPathFollower path;
        MetalBot target;
        MetalAIState state = MetalAIState.Patrol;
        Vector3 home;
        Vector3 lastKnownLocation;
        float nextSenseTime;
        float nextMoveTime;
        float lastSeenTime = -100f;
        float stateStartTime;
        float burstToggleTime;
        bool burstFiring;
        float nextSecondaryTime;
        bool targetVisible;
        bool fieldBlocksShot;
        bool targetReachable;
        bool reachKnown;
        float nextReachCheckTime;
        bool hasRetreated;
        float nextBarkTime;
        float suspicion;
        float alertTime = -100f;
        MetalBot lastSeenSuspect;
        bool lastSeenStanding;
        Transform gameplayFocusActor;
        Vector3? gameplayFocusPoint;
        int jumpLinkSegment = -1;
        Vector3[] jumpLinkPath;
        float jumpLinkEndY;
        bool jumpLinkRises;

        public MetalAIState State => state;
        /// <summary>0..1 how close this robot is to spotting the player it can see.</summary>
        public float Suspicion => suspicion;
        /// <summary>When this robot last went from unaware to fighting the player, for the HUD's "!".</summary>
        public float AlertTime => alertTime;
        public MetalBot Target => target;
        public MetalPathStatus MoveStatus => path != null ? path.Status : MetalPathStatus.Idle;

        /// <summary>Sends the robot somewhere (dispensers send new robots to their posts; the dev autopilot uses it too).</summary>
        public bool DevMoveTo(Vector3 location, float acceptance) => path != null && MoveToLocation(location, acceptance);

        /// <summary>True if the robot's current path takes it up this gravlift (so the lift should carry it).</summary>
        public bool IsPathingThroughLift(MetalGravLift lift)
        {
            int i = path != null ? path.CurrentIndex : -1;
            if (i <= 0) return false;
            Vector3[] c = path.Corners;
            return lift.IsLinkSegment(c[i - 1], c[i]);
        }

        void OnEnable() => All.Add(this);
        void OnDisable() => All.Remove(this);

        protected override void OnPossess(MetalBot bot)
        {
            path = new MetalPathFollower(bot);
            home = bot.transform.position;
            lastKnownLocation = home;
            state = MetalAIState.Investigate; // force SetState to apply
            SetState(MetalAIState.Patrol);
            nextMoveTime = Time.time + Random.Range(0.5f, 2f);
        }

        protected override void OnUnPossess(MetalBot bot)
        {
            path?.Stop();
        }

        /// <summary>Moves the patrol centre (a rebuilt robot walks back to the post of the one it replaces).</summary>
        public void SetHome(Vector3 newHome)
        {
            home = newHome;
            lastKnownLocation = newHome;
        }

        void SetFocus(MetalBot actor)
        {
            gameplayFocusActor = actor != null ? actor.transform : null;
            gameplayFocusPoint = null;
        }

        void SetFocusPoint(Vector3 point)
        {
            gameplayFocusActor = null;
            gameplayFocusPoint = point;
        }

        void ClearFocus()
        {
            gameplayFocusActor = null;
            gameplayFocusPoint = null;
        }

        void StopMovement() => path?.Stop();

        bool MoveToLocation(Vector3 location, float acceptance, bool strafe = false)
        {
            // Walkers only fly while a gravlift carries them: a new request now would replan from mid-air.
            if (Pawn == null || Pawn.Movement.IsFlying) return false;
            return path.MoveToLocation(location, acceptance, strafe);
        }

        bool MoveToActor(MetalBot actor, float acceptance)
        {
            if (Pawn == null || actor == null || Pawn.Movement.IsFlying) return false;
            return path.MoveToActor(actor.transform, acceptance, true);
        }

        void SetState(MetalAIState newState)
        {
            if (state == newState) return;
            state = newState;
            stateStartTime = Time.time;
            nextMoveTime = 0f;
            if (newState == MetalAIState.Patrol) ClearFocus(); // done looking at whatever caught its attention
            MetalBot bot = Pawn;
            if (bot != null)
            {
                bot.StopFire();
                bot.StopSecondaryFire();
                float speed = bot.archetype != null ? bot.archetype.walkSpeed : 5f;
                bot.Movement.maxWalkSpeed = newState == MetalAIState.Patrol ? speed * bot.patrolSpeedFraction : speed;
            }
            burstFiring = false;
        }

        /// <summary>The robot took damage: a hostile, living attacker becomes the target and the squad is alerted.</summary>
        public override void NotifyDamaged(MetalBot attacker)
        {
            MetalBot bot = Pawn;
            if (bot != null && attacker != null && bot.IsHostileTo(attacker) && attacker.IsAlive)
            {
                lastKnownLocation = attacker.transform.position;
                if (state != MetalAIState.Combat || target == null) EnterCombat(attacker, true);
            }
        }

        public override void NotifyPawnDied()
        {
            StopMovement();
            ClearFocus();
            SetState(MetalAIState.Dead);
        }

        /// <summary>Squad alert from an ally that has spotted enemy. Ignored if already fighting.</summary>
        public void Alert(MetalBot enemy)
        {
            if (state != MetalAIState.Combat && state != MetalAIState.Dead && enemy != null)
            {
                lastKnownLocation = enemy.transform.position;
                EnterCombat(enemy, false);
            }
        }

        void EnterCombat(MetalBot enemy, bool alertSquad)
        {
            MetalBot bot = Pawn;
            if (bot == null || bot.passive) return;
            bot.docileUntilAttacked = false;
            if (state != MetalAIState.Combat && enemy != null && enemy.IsPlayerControlled)
            {
                // Caught the player: the HUD shows a "!" over this robot for a moment.
                alertTime = Time.time;
                MetalAudio.PlayAt("alert", bot.GetEyesLocation(), 0.8f, 0f);
            }
            if (state != MetalAIState.Combat && state != MetalAIState.Retreat && enemy != null && alertSquad)
            {
                // Only the robot that spotted the enemy calls out.
                bot.Speak(MetalBark.Alert);
                nextBarkTime = Time.time + Random.Range(3f, 7f);
            }
            suspicion = 0f;
            target = enemy;
            fieldBlocksShot = enemy != null && HasLineOfSight(enemy) && IsShotBlockedByField(enemy);
            targetReachable = false;
            reachKnown = false;
            lastSeenTime = Time.time;
            SetState(MetalAIState.Combat);
            SetFocus(enemy);

            if (alertSquad && bot.archetype != null)
            {
                float radiusSq = bot.archetype.squadAlertRadius * bot.archetype.squadAlertRadius;
                foreach (MetalBotAI other in All.ToArray())
                {
                    MetalBot ally = other.Pawn;
                    if (ally != null && ally != bot && ally.IsAlive && ally.Team == bot.Team
                        && (ally.transform.position - bot.transform.position).sqrMagnitude < radiusSq)
                    {
                        other.Alert(enemy);
                    }
                }
            }
        }

        /// <summary>Nothing blocks the view from this robot's eyes to other's centre. Energy fields are see-through.</summary>
        bool HasLineOfSight(MetalBot other)
        {
            MetalBot bot = Pawn;
            var ignore = new List<Component> { bot, other };
            ignore.AddRange(MetalEnergyField.All);
            return !MetalPhysics.Raycast(bot.GetEyesLocation(), other.transform.position, out _, ignore.ToArray());
        }

        /// <summary>An energy field sits between this robot's eyes and other, so shots would only hit it.</summary>
        bool IsShotBlockedByField(MetalBot other)
        {
            MetalBot bot = Pawn;
            return MetalPhysics.Raycast(bot.GetEyesLocation(), other.transform.position, out RaycastHit hit, bot, other)
                && hit.collider.GetComponentInParent<MetalEnergyField>() != null;
        }

        bool CanReach(MetalBot other)
        {
            return MetalNav.CanReach(path.Feet, other.transform.position, out _);
        }

        void Sense()
        {
            MetalBot bot = Pawn;
            if (bot == null || bot.archetype == null || bot.passive || bot.docileUntilAttacked) return;

            // Keep tracking the current target.
            if (target != null)
            {
                if (!target.IsAlive || !bot.IsHostileTo(target))
                {
                    target = null;
                    ClearFocus();
                    targetVisible = false;
                    fieldBlocksShot = false;
                    reachKnown = false;
                    SetState(MetalAIState.Investigate);
                }
                else
                {
                    targetVisible = HasLineOfSight(target);
                    fieldBlocksShot = targetVisible && IsShotBlockedByField(target);
                    if (targetVisible)
                    {
                        lastSeenTime = Time.time;
                        lastKnownLocation = target.transform.position;
                    }
                    if (!fieldBlocksShot)
                    {
                        reachKnown = false;
                    }
                    else if (!reachKnown || Time.time >= nextReachCheckTime)
                    {
                        nextReachCheckTime = Time.time + 1f;
                        targetReachable = CanReach(target);
                        reachKnown = true;
                    }
                }
            }
            else if (state == MetalAIState.Combat)
            {
                target = null;
            }

            // Look for a closer or first enemy.
            Vector3 eyes = bot.GetEyesLocation();
            Vector3 forward = bot.transform.forward;
            float cosHalf = Mathf.Cos(bot.archetype.sightHalfAngle * Mathf.Deg2Rad);
            MetalBot best = null;
            float bestDistance = bot.archetype.sightRange;
            float bestReal = 0f;
            bool bestInCone = false;
            foreach (MetalBot other in MetalBot.All)
            {
                if (other == null || !other.IsAlive || !bot.IsHostileTo(other)) continue;
                Vector3 to = other.transform.position - eyes;
                float real = to.magnitude;
                // Powered-down bodies count as further away, so active threats win. A sneaking robot is only seen at
                // about 60% of the usual range and isn't felt behind the back.
                float distance = real * (other.IsDormant ? 2f : 1f) * (other.IsSneaking ? 1.7f : 1f);
                float proximity = other.IsSneaking ? ProximityAwareness * 0.35f : ProximityAwareness;
                bool inCone = Vector3.Dot(to / Mathf.Max(real, 0.01f), forward) > cosHalf;
                if (distance < bestDistance && (inCone || real < proximity) && HasLineOfSight(other))
                {
                    best = other;
                    bestDistance = distance;
                    bestReal = real;
                    bestInCone = inCone;
                }
            }

            // The player isn't spotted at a glance: suspicion builds while they stay in view. That includes the
            // player's own body left crouched as a console while they hack. Other robots are fought on sight.
            bool disguisedBody = best != null && best.IsDormant && best.IsSneaking;
            bool suspect = best != null && (best.IsPlayerControlled || disguisedBody) && state != MetalAIState.Combat;
            // Ducking into the console disguise right in front of a robot fools nobody (the hack crouch is exempt).
            bool duckedInView = suspect && bestInCone && best.IsSneaking && !best.IsHackCrouching && lastSeenSuspect == best && lastSeenStanding;
            lastSeenSuspect = suspect ? best : null;
            lastSeenStanding = suspect && !best.IsSneaking;
            if (duckedInView)
            {
                lastKnownLocation = best.transform.position;
                EnterCombat(best, true);
                return;
            }
            if (suspect)
            {
                BuildSuspicion(best, bestReal);
                return;
            }
            if (suspicion > 0f)
            {
                suspicion = Mathf.Max(0f, suspicion - SenseInterval * 0.25f);
                if (suspicion <= 0f && state == MetalAIState.Patrol) ClearFocus();
            }
            // Only switch targets when not already fighting, or when the current target is a powered-down body and a
            // live threat is in view.
            bool currentHarmless = target != null && target.IsDormant && best != null && !best.IsDormant;
            if (best != null && (state != MetalAIState.Combat || target == null || currentHarmless))
            {
                EnterCombat(best, target == null);
            }
        }

        void BuildSuspicion(MetalBot suspect, float distance)
        {
            MetalBot bot = Pawn;
            // Seconds of continuous sight before attacking: under half a second up close, ~3.5 s at the edge of sight.
            // A robot of our own side (hijacked) or a player crouched into a console passes at a glance unless it does
            // something stupid; a console that slides around is spotted faster.
            const float DisguiseFactor = 4f, SlidingDisguiseFactor = 2f;
            float t = Mathf.InverseLerp(3f, bot.archetype.sightRange, distance);
            float seconds = Mathf.Lerp(0.4f, 3.5f, t);
            bool ourOwnHijacked = suspect.IsHijacked && suspect.GetBaseTeam() == bot.Team;
            if (suspect.IsSneaking)
            {
                bool sliding = MetalMath.Flat(suspect.Velocity).magnitude > 0.4f;
                seconds *= sliding ? SlidingDisguiseFactor : DisguiseFactor;
            }
            else if (ourOwnHijacked)
            {
                seconds *= DisguiseFactor;
            }
            if (suspect.IsSprinting) seconds *= 0.6f;
            if (Time.time - suspect.LastFireTime < 1.5f) seconds *= 0.25f;
            float before = suspicion;
            suspicion = Mathf.Min(1f, suspicion + SenseInterval / Mathf.Max(seconds, 0.05f));
            lastKnownLocation = suspect.transform.position;
            if (suspicion >= 1f)
            {
                EnterCombat(suspect, true);
                return;
            }
            if (before <= 0f) MetalAudio.PlayAt("suspicion", bot.GetEyesLocation(), 0.6f, 0f);
            // Something's there: stop and stare, and past halfway walk over to get a better look.
            SetFocus(suspect);
            if (state == MetalAIState.Patrol)
            {
                StopMovement();
                if (suspicion > 0.5f) SetState(MetalAIState.Investigate);
            }
        }

        void HearNoise(Vector3 location)
        {
            MetalBot bot = Pawn;
            if (bot == null || !bot.IsAlive || bot.passive || bot.docileUntilAttacked || bot.IsDormant
                || state == MetalAIState.Combat || state == MetalAIState.Retreat || state == MetalAIState.Dead)
            {
                return;
            }
            lastKnownLocation = location;
            suspicion = Mathf.Max(suspicion, 0.35f);
            SetFocusPoint(location);
            if (state == MetalAIState.Investigate)
            {
                // Already checking something out: head for the new noise and keep looking a while longer.
                stateStartTime = Time.time;
                MoveToLocation(location, 1f);
            }
            else
            {
                SetState(MetalAIState.Investigate);
            }
        }

        /// <summary>
        /// A sound the player made (gunshot, footstep, explosion). Every enemy AI within radius (m) that isn't
        /// already fighting turns towards it and walks over to investigate.
        /// </summary>
        public static void ReportNoise(MetalBot instigator, Vector3 location, float radius)
        {
            if (instigator == null || radius <= 0f) return;
            float radiusSq = radius * radius;
            foreach (MetalBotAI ai in All.ToArray())
            {
                MetalBot listener = ai.Pawn;
                if (listener != null && listener.IsHostileTo(instigator) && (listener.transform.position - location).sqrMagnitude < radiusSq)
                {
                    ai.HearNoise(location);
                }
            }
        }

        bool MoveToRandomPointNear(Vector3 center, float radius, bool strafe, bool allowClimb = false)
        {
            MetalBot bot = Pawn;
            if (bot == null) return false;
            // Strafing is a sidestep, not a trip: prefer points the robot can walk to over ones it could only reach by
            // a jump or a lift. The last tries look around the robot itself. If nothing is walkable and the target is
            // above (it's down in a pit), take a jump: it climbs out.
            Vector3 fallback = Vector3.zero;
            bool hasFallback = false;
            int tries = strafe ? (allowClimb ? 4 : 6) : 1;
            for (int t = 0; t < tries; t++)
            {
                bool nearSelf = t >= 4;
                if (!MetalNav.RandomReachablePoint(path.Feet, nearSelf ? path.Feet : center, nearSelf ? 4f : radius, out Vector3 point))
                {
                    break;
                }
                if (strafe)
                {
                    if (!MetalNav.CanReach(path.Feet, point, out UnityEngine.AI.NavMeshPath p)) continue;
                    if (HasJump(p.corners))
                    {
                        fallback = point;
                        hasFallback = allowClimb;
                        continue;
                    }
                }
                return MoveToLocation(point, 0.5f, strafe);
            }
            return hasFallback && MoveToLocation(fallback, 0.5f, strafe);
        }

        static bool HasJump(Vector3[] corners)
        {
            for (int i = 1; i < corners.Length; i++)
            {
                float rise = corners[i].y - corners[i - 1].y;
                if (rise > 0.6f || rise < -0.9f) return true;
            }
            return false;
        }

        void Update()
        {
            MetalBot bot = Pawn;
            if (bot == null || !bot.IsAlive || state == MetalAIState.Dead) return;
            float now = Time.time;
            if (nextBarkTime <= 0f)
            {
                // Stagger the first lines, so a squad that spawned together doesn't speak in chorus.
                nextBarkTime = now + Random.Range(2f, 14f);
            }
            // EMP-stunned or switched-off robots stand still and hold fire, keeping their state for when they wake.
            if (bot.IsStunned || bot.IsDormant)
            {
                StopMovement();
                bot.StopFire();
                bot.StopSecondaryFire();
                if (bot.IsStunned && now >= nextBarkTime)
                {
                    bot.Speak(MetalBark.Confused);
                    nextBarkTime = now + Random.Range(2.5f, 4f);
                }
                return;
            }
            // Unprompted lines: muttering on patrol, orders in a fight while the enemy is in view.
            if (now >= nextBarkTime)
            {
                if (state == MetalAIState.Patrol && suspicion <= 0f)
                {
                    bot.Speak(MetalBark.Chatter);
                    nextBarkTime = now + Random.Range(7f, 16f);
                }
                else if (state == MetalAIState.Combat && targetVisible)
                {
                    bot.Speak(MetalBark.Orders);
                    nextBarkTime = now + Random.Range(4f, 9f);
                }
                else
                {
                    nextBarkTime = now + 1f;
                }
            }
            if (now >= nextSenseTime)
            {
                nextSenseTime = now + SenseInterval;
                Sense();
            }
            switch (state)
            {
                case MetalAIState.Patrol: TickPatrol(now); break;
                case MetalAIState.Investigate: TickInvestigate(now); break;
                case MetalAIState.Combat: TickCombat(now); break;
                case MetalAIState.Retreat: TickRetreat(now); break;
            }
            path.Tick();
            TickJumpLinks();
            TickFocus();
        }

        /// <summary>Turns the control rotation to the gameplay focus, else along the path.</summary>
        void TickFocus()
        {
            if (gameplayFocusActor != null)
            {
                MetalBot focusBot = gameplayFocusActor.GetComponent<MetalBot>();
                SetFocalPoint(focusBot != null ? focusBot.transform.position : gameplayFocusActor.position);
            }
            else if (gameplayFocusPoint.HasValue)
            {
                SetFocalPoint(gameplayFocusPoint.Value);
            }
            else if (path.NextCorner.HasValue)
            {
                Vector3 to = MetalMath.Flat(path.NextCorner.Value - Pawn.transform.position);
                if (to.sqrMagnitude > 0.01f) ControlYaw = MetalMath.Yaw(to);
                ControlPitch = 0f;
            }
        }

        /// <summary>
        /// Jumps across jump links (ledge drops, hops onto ledges and over low obstacles) when the path reaches one, and
        /// uses a mid-air jump if the robot has one and is still short of the landing.
        /// </summary>
        void TickJumpLinks()
        {
            MetalBot bot = Pawn;
            MetalCharacterMovement move = bot.Movement;
            int index = path.CurrentIndex;
            if (index < 0)
            {
                jumpLinkPath = null;
                return;
            }
            Vector3[] corners = path.Corners;
            if (move.IsFalling)
            {
                if (corners == jumpLinkPath && index == jumpLinkSegment && jumpLinkRises && move.Velocity.y <= 0f
                    && move.JumpCurrentCount < move.jumpMaxCount && path.Feet.y < jumpLinkEndY + 0.1f)
                {
                    bot.Jump();
                }
                return;
            }
            if (!move.IsMovingOnGround || (corners == jumpLinkPath && index == jumpLinkSegment)) return;
            if (!path.IsJumpSegment(index)) return;
            // A gravlift's link: just walk into the beam and let it carry the robot up.
            if (MetalGravLift.IsAnyLiftSegment(corners[index - 1], corners[index])) return;
            Vector3 feet = path.Feet;
            Vector3 from = corners[index - 1];
            // Only once the robot has reached the edge where the link starts.
            if (MetalMath.Flat(from - feet).magnitude > 0.6f) return;
            Vector3 to = corners[index];
            jumpLinkPath = corners;
            jumpLinkSegment = index;
            jumpLinkEndY = to.y;
            float rise = to.y - feet.y;
            jumpLinkRises = rise > 0f;
            float gravity = -move.GravityY;
            // At least the player's jump, and enough to top a taller climb with some clearance.
            float climbZ = gravity > 0f ? Mathf.Sqrt(2f * gravity * (Mathf.Max(rise, 0f) + LinkClimbClearance)) : 0f;
            float launchZ = rise < -1.5f ? 2.5f : Mathf.Clamp(climbZ, Mathf.Max(move.jumpVelocity, LinkJumpZ), LinkMaxJumpZ);
            float disc = launchZ * launchZ - 2f * gravity * rise;
            if (gravity <= 0f || disc < 0f)
            {
                bot.Jump();
                return;
            }
            float airtime = (launchZ + Mathf.Sqrt(disc)) / gravity;
            float speed = Mathf.Min(MetalMath.Flat(to - feet).magnitude / airtime, move.maxWalkSpeed * 1.5f);
            bot.LaunchCharacter(MetalMath.Flat(to - feet).normalized * speed + Vector3.up * launchZ, true, true);
        }

        void TickPatrol(float now)
        {
            MetalBot bot = Pawn;
            bot.SetAITarget(Vector3.zero, false);
            if (bot.patrolRadius <= 0f || now < nextMoveTime) return;
            if (path.Status == MetalPathStatus.Idle)
            {
                MoveToRandomPointNear(home, bot.patrolRadius, false);
                nextMoveTime = now + Random.Range(3f, 6f);
            }
        }

        void TickInvestigate(float now)
        {
            if (now - stateStartTime < 0.1f)
            {
                MoveToLocation(lastKnownLocation, 1f);
                SetFocusPoint(lastKnownLocation);
            }
            // Look around at the last known position for a few seconds, then go back to patrolling.
            if (path.Status == MetalPathStatus.Idle && now - stateStartTime > 6f) SetState(MetalAIState.Patrol);
        }

        void TickCombat(float now)
        {
            MetalBot bot = Pawn;
            MetalBot enemy = target;
            if (enemy == null)
            {
                SetState(MetalAIState.Investigate);
                return;
            }
            MetalBotArchetype a = bot.archetype;
            float distance = Vector3.Distance(bot.transform.position, enemy.transform.position);
            if (a.retreatHealthFraction > 0f && !hasRetreated && bot.Health < bot.MaxHealth * a.retreatHealthFraction)
            {
                hasRetreated = true;
                SetState(MetalAIState.Retreat);
                return;
            }
            // Aim at the enemy's chest; the robot applies the archetype's aim error.
            bot.SetAITarget(enemy.transform.position + Vector3.up * (enemy.Movement.CurrentHalfHeight * 0.3f), true);

            if (bot.IsWeaponDisabled)
            {
                // Lost the gun arm: charge and swing.
                if (now >= nextMoveTime)
                {
                    MoveToActor(enemy, MeleeReach * 0.5f);
                    nextMoveTime = now + 0.5f;
                }
                if (distance < MeleeReach + bot.Capsule.radius) bot.StartFire();
                else
                {
                    bot.StopFire();
                    bot.StopSecondaryFire();
                }
                return;
            }

            // Lost sight: hold fire, give it a moment, then go to where the enemy was seen. After 12 s, give up.
            if (!targetVisible)
            {
                bot.StopFire();
                bot.StopSecondaryFire();
                if (now - lastSeenTime > 2.5f && now >= nextMoveTime)
                {
                    MoveToLocation(lastKnownLocation, 1.5f, true);
                    nextMoveTime = now + 2f;
                }
                if (now - lastSeenTime > 12f)
                {
                    target = null;
                    ClearFocus();
                    SetState(MetalAIState.Investigate);
                }
                return;
            }

            // In sight but behind an energy field: walk round to the target if the navmesh has a way, otherwise watch.
            if (fieldBlocksShot)
            {
                bot.StopFire();
                bot.StopSecondaryFire();
                burstFiring = false;
                if (now >= nextMoveTime)
                {
                    nextMoveTime = now + 1f;
                    if (targetReachable) MoveToActor(enemy, a.preferredRange * 0.5f);
                    else StopMovement();
                }
                return;
            }

            // Burst fire.
            if (now >= burstToggleTime)
            {
                burstFiring = !burstFiring;
                burstToggleTime = now + (burstFiring ? a.burstDuration : a.burstPause) * Random.Range(0.7f, 1.3f);
            }
            if (burstFiring) bot.StartFire();
            else bot.StopFire();

            // Built-in secondary weapon (Brute rockets): one shot whenever it's ready and the target is far enough away
            // that the blast won't catch the shooter, with a random extra wait.
            if (bot.HasSecondaryWeapon && now >= nextSecondaryTime && bot.IsSecondaryReady
                && distance > a.secondaryWeapon.explosionRadius * 1.3f + bot.Capsule.radius)
            {
                bot.StartSecondaryFire();
                nextSecondaryTime = now + Random.Range(0.5f, 2f);
            }
            else
            {
                bot.StopSecondaryFire();
            }

            // Reposition: strafe around the target at the preferred range.
            if (now >= nextMoveTime)
            {
                nextMoveTime = now + Random.Range(1.8f, 3.5f);
                Vector3 away = MetalMath.Flat(bot.transform.position - enemy.transform.position).normalized;
                Vector3 side = Vector3.Cross(away, Vector3.up) * (Random.value < 0.5f ? 1f : -1f);
                Vector3 desired = enemy.transform.position + Vector3.ClampMagnitude(away * a.preferredRange + side * a.preferredRange * 0.5f, a.preferredRange);
                MoveToRandomPointNear(desired, 2.5f, true, enemy.transform.position.y > bot.transform.position.y + 1f);
            }
        }

        void TickRetreat(float now)
        {
            MetalBot bot = Pawn;
            bot.StopFire();
            bot.StopSecondaryFire();
            // On entering the state, run to a random spot about 9 m directly away from the enemy.
            if (now - stateStartTime < 0.1f)
            {
                Vector3 from = target != null ? target.transform.position : lastKnownLocation;
                Vector3 away = MetalMath.Flat(bot.transform.position - from).normalized;
                MoveToRandomPointNear(bot.transform.position + away * 9f, 3f, false);
            }
            // After 4 s of falling back, turn and fight again (or go looking if the enemy is gone).
            if (now - stateStartTime > 4f)
            {
                if (target != null) EnterCombat(target, false);
                else SetState(MetalAIState.Investigate);
            }
        }
    }
}
