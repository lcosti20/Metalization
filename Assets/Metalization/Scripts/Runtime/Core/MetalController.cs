// Possession, ported from Unreal's Controller/Pawn split: a controller (player input, walker AI or pilot AI)
// drives exactly one robot at a time through the robot's public API, and any controller can drive any robot.
using UnityEngine;

namespace Metalization
{
    /// <summary>Base for everything that can drive a MetalBot.</summary>
    public abstract class MetalController : MonoBehaviour
    {
        /// <summary>The robot this controller drives, or null.</summary>
        public MetalBot Pawn { get; private set; }

        /// <summary>True for the player's controller.</summary>
        public virtual bool IsPlayer => false;

        /// <summary>Control rotation pitch in degrees (positive looks up).</summary>
        public float ControlPitch { get; set; }

        /// <summary>Control rotation yaw in degrees (0 = +Z).</summary>
        public float ControlYaw { get; set; }

        /// <summary>The control rotation as a quaternion.</summary>
        public Quaternion ControlRotation => Quaternion.Euler(-ControlPitch, ControlYaw, 0f);

        /// <summary>Takes control of bot, releasing whatever it drove before and whoever drove bot.</summary>
        public void Possess(MetalBot bot)
        {
            if (bot == Pawn) return;
            if (Pawn != null) UnPossess();
            if (bot == null) return;
            if (bot.Controller != null && bot.Controller != this) bot.Controller.UnPossess();
            Pawn = bot;
            bot.SetController(this);
            ControlYaw = bot.transform.eulerAngles.y;
            ControlPitch = 0f;
            OnPossess(bot);
        }

        /// <summary>Lets go of the current robot.</summary>
        public void UnPossess()
        {
            MetalBot old = Pawn;
            if (old == null) return;
            Pawn = null;
            if (old.Controller == this) old.SetController(null);
            OnUnPossess(old);
        }

        protected virtual void OnPossess(MetalBot bot) { }
        protected virtual void OnUnPossess(MetalBot bot) { }

        /// <summary>The robot was damaged by attacker (may be null).</summary>
        public virtual void NotifyDamaged(MetalBot attacker) { }

        /// <summary>The robot died.</summary>
        public virtual void NotifyPawnDied() { }

        /// <summary>Where this controller "sees" from: the camera for players, the robot's eyes for AI.</summary>
        public virtual void GetViewPoint(out Vector3 location, out Quaternion rotation)
        {
            location = Pawn != null ? Pawn.GetEyesLocation() : transform.position;
            rotation = ControlRotation;
        }

        /// <summary>Turns the control rotation towards a world point (AI focus).</summary>
        public void SetFocalPoint(Vector3 point)
        {
            if (Pawn == null) return;
            Vector3 dir = point - Pawn.GetEyesLocation();
            if (dir.sqrMagnitude < 1e-4f) return;
            ControlYaw = MetalMath.Yaw(dir);
            ControlPitch = MetalMath.Pitch(dir);
        }
    }
}
