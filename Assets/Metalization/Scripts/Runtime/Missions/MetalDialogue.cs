// Radio dialogue: lines queue up and the HUD types each one out in a dialogue box, with a radio blip per line
// and a quiet heterodyne whistle under the speech.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>One line of radio dialogue.</summary>
    [Serializable]
    public class MetalDialogueLine
    {
        public string speaker = "";
        [TextArea] public string line = "";
        [Tooltip("Seconds the line stays on screen. The text types out over all but the last second.")]
        [Min(0.5f)] public float duration = 4f;

        public MetalDialogueLine() { }

        public MetalDialogueLine(string speaker, string line, float duration = 4f)
        {
            this.speaker = speaker;
            this.line = line;
            this.duration = duration;
        }
    }

    /// <summary>The scene's dialogue queue (Unreal's world subsystem). Created on first use.</summary>
    public class MetalDialogue : MonoBehaviour
    {
        const float WhistleVolume = 0.3f;
        const float WhistleFadeIn = 0.15f, WhistleFadeOut = 0.4f;
        const float WhistlePitchMin = 0.85f, WhistlePitchMax = 1.15f;

        static MetalDialogue instance;

        /// <summary>The dialogue queue of the running scene.</summary>
        public static MetalDialogue Get()
        {
            if (instance == null)
            {
                var go = new GameObject("Metal Dialogue");
                instance = go.AddComponent<MetalDialogue>();
            }
            return instance;
        }

        readonly List<MetalDialogueLine> queue = new List<MetalDialogueLine>();
        MetalSoundHandle whistle;
        bool whistleOn;

        public bool IsPlaying => queue.Count > 0;
        public MetalDialogueLine Current => queue.Count > 0 ? queue[0] : null;
        /// <summary>Seconds the front line has been shown.</summary>
        public float LineTime { get; private set; }
        /// <summary>Seconds the dialogue box has been open.</summary>
        public float OpenTime { get; private set; }

        /// <summary>Seconds a line spends being typed out (all but the last second).</summary>
        public static float GetTalkTime(MetalDialogueLine line) => Mathf.Max(Mathf.Max(0.5f, line.duration) - 1f, 0.25f);

        public bool IsTalking => queue.Count > 0 && LineTime < GetTalkTime(queue[0]);

        public void PlayDialogue(IEnumerable<MetalDialogueLine> lines)
        {
            if (lines == null) return;
            foreach (MetalDialogueLine l in lines) PlayLine(l);
        }

        public void PlayLine(MetalDialogueLine line)
        {
            if (line == null || string.IsNullOrEmpty(line.line)) return;
            // Nothing playing: this line opens the dialogue box, with a radio blip.
            if (queue.Count == 0)
            {
                LineTime = 0f;
                OpenTime = 0f;
                MetalAudio.Play2D("radio_blip", 0.6f);
                SetWhistle(true);
            }
            queue.Add(line);
            Debug.Log($"[Metal] Dialogue queued: {line.speaker}: {line.line}");
        }

        void Update()
        {
            if (queue.Count == 0) return;
            float dt = Time.deltaTime;
            LineTime += dt;
            OpenTime += dt;
            // The line has been spoken: the whistle stops for the pause before the next line.
            if (whistleOn && !IsTalking) SetWhistle(false);
            if (LineTime >= Mathf.Max(0.5f, queue[0].duration))
            {
                queue.RemoveAt(0);
                LineTime = 0f;
                if (queue.Count > 0)
                {
                    MetalAudio.Play2D("radio_blip", 0.4f);
                    SetWhistle(true);
                }
                else if (whistleOn)
                {
                    SetWhistle(false);
                }
            }
        }

        void SetWhistle(bool playing)
        {
            whistleOn = playing;
            if (!playing)
            {
                if (whistle != null) whistle.FadeOut(WhistleFadeOut);
                return;
            }
            if (whistle == null)
            {
                whistle = MetalAudio.CreateLoop2D("radio_whistle_loop", WhistleVolume);
                if (whistle == null) return;
                whistle.transform.SetParent(transform, false);
            }
            // A different stretch of the loop, slightly higher or lower, every time the box opens.
            whistle.Source.pitch = UnityEngine.Random.Range(WhistlePitchMin, WhistlePitchMax);
            float length = whistle.Source.clip != null ? whistle.Source.clip.length : 0f;
            whistle.FadeIn(WhistleFadeIn, 1f, length > 0f ? UnityEngine.Random.Range(0f, length) : 0f);
        }

        void OnDestroy()
        {
            if (instance == this) instance = null;
        }
    }
}
