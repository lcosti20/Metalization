// Anti-gravity lift: a glowing column that carries walking robots up to a landing.
using System.Collections.Generic;
using Unity.AI.Navigation;
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// Walk into the beam and it lifts you, slowing near the top and tossing you out onto the landing along
    /// exitDirection. AI robots ride it too: a NavMeshLink takes their paths from behind the column to the landing,
    /// and the lift only boards AI robots whose path leads through it.
    /// </summary>
    [DefaultExecutionOrder(-40)]
    public class MetalGravLift : MonoBehaviour
    {
        public static readonly List<MetalGravLift> All = new List<MetalGravLift>();

        const int RingCount = 4;

        [Tooltip("Height of the column above the pad, metres. Riders are released at the top.")]
        [Min(1f)] public float liftHeight = 9f;
        [Tooltip("Radius of the column, metres.")]
        [Min(0.5f)] public float radius = 1.4f;
        [Tooltip("Rising speed, m/s.")]
        public float liftSpeed = 6.5f;
        [Tooltip("Horizontal direction riders are pushed at the top (lift space). Zero = straight up and back down.")]
        public Vector3 exitDirection = Vector3.forward;
        [Tooltip("Speed of the push at the top, m/s.")]
        public float exitSpeed = 5.5f;
        [Tooltip("How far past the column edge, along exitDirection, the AI nav link lands (m).")]
        public float navLandingDistance = 2f;
        public Color color = new Color(0.25f, 0.85f, 1f);

        readonly HashSet<MetalBot> riders = new HashSet<MetalBot>();
        readonly List<Transform> rings = new List<Transform>();
        readonly List<Renderer> ringRenderers = new List<Renderer>();
        float ringPhase;
        Vector3 linkStartWorld, linkEndWorld;
        bool hasLink;

        void Awake()
        {
            float d = radius * 2f;
            GameObject pad = MetalMaterials.Shape(PrimitiveType.Cylinder, transform, "Pad");
            pad.transform.localPosition = new Vector3(0f, 0.04f, 0f);
            pad.transform.localScale = new Vector3(d * 1.15f, 0.04f, d * 1.15f);
            MetalMaterials.SetGlow(pad.GetComponent<Renderer>(), color, 4f);
            GameObject beam = MetalMaterials.Shape(PrimitiveType.Cylinder, transform, "Beam");
            beam.transform.localPosition = new Vector3(0f, liftHeight * 0.5f, 0f);
            beam.transform.localScale = new Vector3(d * 0.8f, liftHeight * 0.5f, d * 0.8f);
            MetalMaterials.SetGlow(beam.GetComponent<Renderer>(), color, 0.12f);
            for (int i = 0; i < RingCount; i++)
            {
                GameObject ring = MetalMaterials.Shape(PrimitiveType.Cylinder, transform, "Ring" + i);
                ring.transform.localScale = new Vector3(d, 0.02f, d);
                Renderer r = ring.GetComponent<Renderer>();
                MetalMaterials.SetGlow(r, color, 1.5f);
                rings.Add(ring.transform);
                ringRenderers.Add(r);
            }
            var lightGo = new GameObject("Light");
            lightGo.transform.SetParent(transform, false);
            lightGo.transform.localPosition = new Vector3(0f, 0.6f, 0f);
            Light l = lightGo.AddComponent<Light>();
            l.type = LightType.Point;
            l.range = 9f;
            l.intensity = 4f;
            l.color = color;
            l.shadows = LightShadows.None;
            BuildNavigation();
        }

        void OnEnable() => All.Add(this);
        void OnDisable() => All.Remove(this);

        void Start()
        {
            MetalAudio.PlayLoop(transform, "lift_hum_loop", 0.45f);
        }

        /// <summary>
        /// No navmesh inside the column (AI never strolls through it by accident), and a one-way link from just behind
        /// the column to the landing past it.
        /// </summary>
        void BuildNavigation()
        {
            NavMeshModifierVolume blocker = gameObject.AddComponent<NavMeshModifierVolume>();
            blocker.center = new Vector3(0f, liftHeight * 0.5f, 0f);
            blocker.size = new Vector3(radius * 2f, liftHeight + 0.8f, radius * 2f);
            blocker.area = 1; // Not Walkable
            Vector3 exit = MetalMath.Flat(exitDirection);
            if (exit.sqrMagnitude < 1e-4f) return;
            exit.Normalize();
            Vector3 end = exit * (radius + navLandingDistance) + Vector3.up * (liftHeight - 0.8f);
            // Trace down at the landing spot for its floor height.
            Vector3 above = transform.TransformPoint(exit * (radius + navLandingDistance) + Vector3.up * (liftHeight + 1f));
            if (Physics.Raycast(above, Vector3.down, out RaycastHit hit, 6f, MetalPhysics.ShotMask & ~MetalPhysics.BotMask, QueryTriggerInteraction.Ignore))
            {
                end.y = transform.InverseTransformPoint(hit.point).y;
            }
            NavMeshLink link = gameObject.AddComponent<NavMeshLink>();
            link.startPoint = -exit * (radius + 0.6f) + Vector3.up * 0.1f;
            link.endPoint = end;
            link.bidirectional = false;
            link.width = radius;
            link.costModifier = 1f;
            linkStartWorld = transform.TransformPoint(link.startPoint);
            linkEndWorld = transform.TransformPoint(link.endPoint);
            hasLink = true;
        }

        /// <summary>True if the path segment from 'from' to 'to' is this lift's nav link.</summary>
        public bool IsLinkSegment(Vector3 from, Vector3 to)
        {
            return hasLink && (from - linkStartWorld).sqrMagnitude < 2.5f * 2.5f && (to - linkEndWorld).sqrMagnitude < 2.5f * 2.5f;
        }

        public static bool IsAnyLiftSegment(Vector3 from, Vector3 to)
        {
            foreach (MetalGravLift lift in All)
            {
                if (lift.IsLinkSegment(from, to)) return true;
            }
            return false;
        }

        public bool IsCarrying(MetalBot bot) => riders.Contains(bot);

        bool Inside(MetalBot bot)
        {
            Vector3 local = transform.InverseTransformPoint(bot.transform.position);
            float feet = local.y - bot.Movement.FullHalfHeight;
            return new Vector2(local.x, local.z).magnitude <= radius + bot.Capsule.radius * 0.5f && feet > -0.5f && feet < liftHeight + 0.4f;
        }

        void Release(MetalBot bot, bool atTop)
        {
            riders.Remove(bot);
            MetalCharacterMovement move = bot.Movement;
            move.LiftRiding = false;
            if (move.IsFlying) move.SetMovementMode(MetalMoveMode.Falling);
            if (atTop)
            {
                // Toss the rider onto the landing and give back a mid-air jump for anything just out of reach.
                Vector3 outDir = MetalMath.Flat(transform.TransformDirection(exitDirection)).normalized;
                move.Velocity = outDir * exitSpeed + Vector3.up * 3.2f;
                move.ResetJumpCount();
                MetalAudio.PlayAt("lift_exit", bot.transform.position, 0.7f);
            }
        }

        void Update()
        {
            float dt = Time.deltaTime;
            Vector3 center = transform.position;
            float topY = center.y + liftHeight;
            // Rings drift up the column and fade in at the bottom, out at the top.
            ringPhase = Mathf.Repeat(ringPhase + dt * 0.35f, 1f);
            for (int i = 0; i < rings.Count; i++)
            {
                float t = Mathf.Repeat(ringPhase + (float)i / rings.Count, 1f);
                rings[i].localPosition = new Vector3(0f, t * liftHeight, 0f);
                MetalMaterials.SetGlowColor(ringRenderers[i], color, 1.5f * Mathf.Sin(t * Mathf.PI));
            }
            riders.RemoveWhere(b => b == null || !b.IsAlive);
            foreach (MetalBot rider in new List<MetalBot>(riders))
            {
                if (!Inside(rider)) Release(rider, false);
            }

            foreach (MetalBot bot in MetalBot.All)
            {
                // Flyers have their own engines; dead and powered-down robots stand (or fall) where they are.
                if (bot is MetalStalker || !bot.IsAlive || bot.IsDormant || bot.Movement == null || !Inside(bot)) continue;
                MetalCharacterMovement move = bot.Movement;
                float feet = bot.transform.position.y - move.FullHalfHeight;
                bool riding = riders.Contains(bot);
                if (feet >= topY - 0.25f)
                {
                    if (riding) Release(bot, true);
                    continue;
                }
                // AI robots only board when their path leads through this lift: one brushing the beam while strafing,
                // or dropping down through it from a ledge, is going somewhere else.
                if (!riding && bot.Controller is MetalBotAI ai && !ai.IsPathingThroughLift(this)) continue;
                if (!riding)
                {
                    riders.Add(bot);
                    bot.SetSneaking(false);
                    move.SetMovementMode(MetalMoveMode.Flying);
                    move.LiftRiding = true;
                    MetalAudio.PlayAt("lift_enter", bot.transform.position, 0.7f);
                }
                // Rise, slowing over the last couple of metres; pull gently towards the axis and damp sideways drift.
                Vector3 v = move.Velocity;
                float target = liftSpeed * Mathf.Clamp((topY - feet) / 2.5f, 0.35f, 1f);
                v.y = MetalMath.InterpTo(v.y, target, dt, 5f);
                Vector3 toAxis = MetalMath.Flat(center - bot.transform.position);
                v += toAxis * 2.5f * dt;
                float damp = Mathf.Clamp01(1f - 1.5f * dt);
                v.x *= damp;
                v.z *= damp;
                move.Velocity = v;
            }
        }

        void OnDrawGizmos()
        {
            Gizmos.color = color;
            Gizmos.matrix = transform.localToWorldMatrix;
            Gizmos.DrawWireCube(new Vector3(0f, liftHeight * 0.5f, 0f), new Vector3(radius * 2f, liftHeight, radius * 2f));
            Gizmos.DrawLine(new Vector3(0f, liftHeight, 0f), new Vector3(0f, liftHeight, 0f) + exitDirection.normalized * (radius + navLandingDistance));
        }
    }
}
