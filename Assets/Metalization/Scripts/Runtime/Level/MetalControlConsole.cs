// Enemy-only control consoles that open gates and shut down robot dispensers.
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// A terminal the player operates with the interact button. Only robots of requiredFaction (by archetype, so a
    /// hijacked Villain keeps its clearance) are accepted, so the player has to borrow an enemy to use it. On success it
    /// opens its linked gates and shuts down its linked dispensers.
    /// </summary>
    public class MetalControlConsole : MonoBehaviour
    {
        public static readonly List<MetalControlConsole> All = new List<MetalControlConsole>();

        static readonly Color OpenColor = new Color(0.1f, 1f, 0.3f);

        [Tooltip("Faction (archetype team) allowed to operate it. Neutral = anyone.")]
        public MetalTeam requiredFaction = MetalTeam.Villains;
        public List<MetalGate> linkedGates = new List<MetalGate>();
        public List<MetalRobotDispenser> linkedDispensers = new List<MetalRobotDispenser>();
        public Color lockedColor = new Color(1f, 0.15f, 0.05f);
        public string grantedText = "ACCESS GRANTED - gate opening.";
        public string doneText = "The gate is already open.";
        [Tooltip("Metres within which the prompt shows and the console can be used.")]
        public float interactRadius = 2.2f;
        public string promptText = "[E] Operate gate console";

        Renderer screen;
        Light screenLight;

        public bool IsActivated { get; private set; }

        void Awake()
        {
            Material dark = MetalGameAssets.Environment("Dark");
            GameObject baseGo = GameObject.CreatePrimitive(PrimitiveType.Cube);
            baseGo.name = "Base";
            baseGo.transform.SetParent(transform, false);
            baseGo.transform.localPosition = new Vector3(0f, 0.55f, 0f);
            baseGo.transform.localScale = new Vector3(1f, 1.1f, 0.6f);
            if (dark != null) baseGo.GetComponent<Renderer>().sharedMaterial = dark;
            // Screen faces the console's +Z side, tilted back towards the operator.
            GameObject screenGo = MetalMaterials.Shape(PrimitiveType.Cube, transform, "Screen");
            screenGo.transform.localPosition = new Vector3(0f, 0.92f, 0.33f);
            screenGo.transform.localRotation = UE.Rot(25f, 0f, 0f);
            screenGo.transform.localScale = new Vector3(0.8f, 0.45f, 0.05f);
            screen = screenGo.GetComponent<Renderer>();
            var lightGo = new GameObject("Light");
            lightGo.transform.SetParent(transform, false);
            lightGo.transform.localPosition = new Vector3(0f, 1f, 0.7f);
            screenLight = lightGo.AddComponent<Light>();
            screenLight.type = LightType.Point;
            screenLight.range = 3.5f;
            screenLight.intensity = 2f;
            screenLight.shadows = LightShadows.None;
            SetScreenColor(lockedColor);
        }

        void OnEnable() => All.Add(this);
        void OnDisable() => All.Remove(this);

        void SetScreenColor(Color color)
        {
            MetalMaterials.SetGlow(screen, color, 4f);
            if (screenLight != null) screenLight.color = color;
        }

        /// <summary>Checkpoint restore: shows the console as used and opens its gates at once.</summary>
        public void RestoreActivated()
        {
            IsActivated = true;
            SetScreenColor(OpenColor);
            foreach (MetalGate g in linkedGates)
            {
                if (g != null) g.OpenInstantly();
            }
            foreach (MetalRobotDispenser d in linkedDispensers)
            {
                if (d != null) d.Disable(true);
            }
        }

        /// <summary>Operates the console as user; returns the message to show.</summary>
        public string TryActivate(MetalBot user)
        {
            if (IsActivated) return doneText;
            // The archetype's team, not the current one: a hijacked Villain fights for the Heroes but keeps its clearance.
            MetalTeam faction = user != null && user.archetype != null ? user.archetype.team : MetalTeam.Neutral;
            // The terminal chatters as it reads the credentials, then answers once it has finished.
            MetalAudio.PlayAt("console_beep", transform.position, 0.9f, 0f);
            if (requiredFaction != MetalTeam.Neutral && faction != requiredFaction)
            {
                StartCoroutine(PlayAfterBeep("console_denied", 0.8f));
                return "ACCESS DENIED - enemy personnel only. (Maybe borrow one?)";
            }
            IsActivated = true;
            StartCoroutine(PlayAfterBeep("console_granted", 0.9f));
            SetScreenColor(OpenColor);
            MetalFX.SpawnBurst(screen.transform.position, OpenColor, 0.5f, 0.3f);
            foreach (MetalGate g in linkedGates)
            {
                if (g != null) g.Open();
            }
            foreach (MetalRobotDispenser d in linkedDispensers)
            {
                if (d != null) d.Disable();
            }
            return grantedText;
        }

        IEnumerator PlayAfterBeep(string sound, float volume)
        {
            // console_beep lasts about 0.7 s.
            yield return new WaitForSeconds(0.7f);
            MetalAudio.PlayAt(sound, transform.position, volume);
        }
    }
}
