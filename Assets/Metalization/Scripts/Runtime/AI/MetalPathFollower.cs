// Navmesh path following for AI-driven walkers: the job of Unreal's PathFollowingComponent. Paths come from
// Unity's NavMesh (built from NavMeshSurface components in each level); the robot is steered along the
// corners through MetalBot.AddMovementInput, so it moves with its normal movement rules.
using UnityEngine;
using UnityEngine.AI;

namespace Metalization
{
    public enum MetalPathStatus
    {
        Idle,
        Moving,
    }

    public class MetalPathFollower
    {
        readonly MetalBot bot;
        readonly NavMeshPath path = new NavMeshPath();
        Vector3[] corners = new Vector3[0];
        int index;
        float acceptance;
        Transform goalActor;
        Vector3 goal;
        float nextRepathTime;

        public MetalPathStatus Status { get; private set; } = MetalPathStatus.Idle;
        /// <summary>Corner the robot is heading for, or -1.</summary>
        public int CurrentIndex => Status == MetalPathStatus.Moving ? index : -1;
        public Vector3[] Corners => corners;
        public bool CanStrafe { get; private set; }
        /// <summary>Next corner the robot steers towards (the "move focus").</summary>
        public Vector3? NextCorner => Status == MetalPathStatus.Moving && index < corners.Length ? corners[index] : (Vector3?)null;

        public MetalPathFollower(MetalBot owner)
        {
            bot = owner;
        }

        public Vector3 Feet => bot.transform.position - Vector3.up * (bot.Movement != null ? bot.Movement.FullHalfHeight : 0.7f);

        /// <summary>Paths from the robot's feet to destination. False (and idle) if there is no path.</summary>
        public bool MoveToLocation(Vector3 destination, float acceptanceRadius, bool canStrafe = false)
        {
            goalActor = null;
            goal = destination;
            acceptance = acceptanceRadius;
            CanStrafe = canStrafe;
            return Repath();
        }

        /// <summary>Follows an actor, re-pathing to it twice a second.</summary>
        public bool MoveToActor(Transform target, float acceptanceRadius, bool canStrafe = true)
        {
            if (target == null) return false;
            goalActor = target;
            goal = target.position;
            acceptance = acceptanceRadius;
            CanStrafe = canStrafe;
            return Repath();
        }

        bool Repath()
        {
            if (goalActor != null) goal = goalActor.position;
            nextRepathTime = Time.time + 0.5f;
            if (!MetalNav.Project(Feet, 2f, out Vector3 from) || !MetalNav.Project(goal, 4f, out Vector3 to))
            {
                Stop();
                return false;
            }
            if (!NavMesh.CalculatePath(from, to, NavMesh.AllAreas, path) || path.status == NavMeshPathStatus.PathInvalid || path.corners.Length == 0)
            {
                Stop();
                return false;
            }
            corners = path.corners;
            index = corners.Length > 1 ? 1 : 0;
            Status = MetalPathStatus.Moving;
            return true;
        }

        public void Stop()
        {
            Status = MetalPathStatus.Idle;
            corners = new Vector3[0];
            goalActor = null;
        }

        /// <summary>Steers the robot along the path. Pauses (keeping the path) while a lift carries it.</summary>
        public void Tick()
        {
            if (Status != MetalPathStatus.Moving) return;
            if (bot.Movement.IsFlying) return; // a gravlift is carrying it; resume at the top
            if (goalActor != null && Time.time >= nextRepathTime && bot.Movement.IsMovingOnGround)
            {
                if (goalActor == null || !Repath()) return;
            }
            Vector3 feet = Feet;
            Vector3 final = corners[corners.Length - 1];
            Vector3 toFinal = final - feet;
            if (MetalMath.Flat(toFinal).magnitude <= Mathf.Max(acceptance, 0.15f) && Mathf.Abs(toFinal.y) < 1.5f)
            {
                Stop();
                return;
            }
            // Skip corners already reached.
            while (index < corners.Length - 1 && MetalMath.Flat(corners[index] - feet).magnitude < 0.35f
                   && Mathf.Abs(corners[index].y - feet.y) < 1.2f)
            {
                index++;
            }
            Vector3 dir = MetalMath.Flat(corners[index] - feet);
            if (dir.sqrMagnitude < 1e-4f)
            {
                if (index < corners.Length - 1) index++;
                return;
            }
            bot.AddMovementInput(dir.normalized, 1f);
        }

        /// <summary>True if the segment towards corner i rises or drops more than a step: a jump link.</summary>
        public bool IsJumpSegment(int i)
        {
            if (i <= 0 || i >= corners.Length) return false;
            float rise = corners[i].y - corners[i - 1].y;
            return rise > 0.6f || rise < -0.9f;
        }

        public bool PathHasJumps()
        {
            for (int i = 1; i < corners.Length; i++)
            {
                if (IsJumpSegment(i)) return true;
            }
            return false;
        }
    }

    /// <summary>Navmesh queries.</summary>
    public static class MetalNav
    {
        public static bool Project(Vector3 point, float maxDistance, out Vector3 onMesh)
        {
            if (NavMesh.SamplePosition(point, out NavMeshHit hit, maxDistance, NavMesh.AllAreas))
            {
                onMesh = hit.position;
                return true;
            }
            onMesh = point;
            return false;
        }

        /// <summary>A random navmesh point within radius of center that a complete path from 'from' reaches.</summary>
        public static bool RandomReachablePoint(Vector3 from, Vector3 center, float radius, out Vector3 point)
        {
            point = center;
            if (!Project(from, 2f, out Vector3 start)) return false;
            var p = new NavMeshPath();
            for (int i = 0; i < 8; i++)
            {
                Vector2 r = Random.insideUnitCircle * radius;
                Vector3 candidate = center + new Vector3(r.x, 0f, r.y);
                if (!Project(candidate, Mathf.Max(1f, radius * 0.5f), out Vector3 onMesh)) continue;
                if (NavMesh.CalculatePath(start, onMesh, NavMesh.AllAreas, p) && p.status == NavMeshPathStatus.PathComplete)
                {
                    point = onMesh;
                    return true;
                }
            }
            return false;
        }

        /// <summary>True if the navmesh has a complete path between the two points.</summary>
        public static bool CanReach(Vector3 from, Vector3 to, out NavMeshPath path)
        {
            path = new NavMeshPath();
            if (!Project(from, 2f, out Vector3 a) || !Project(to, 3f, out Vector3 b)) return false;
            return NavMesh.CalculatePath(a, b, NavMesh.AllAreas, path) && path.status == NavMeshPathStatus.PathComplete;
        }
    }
}
