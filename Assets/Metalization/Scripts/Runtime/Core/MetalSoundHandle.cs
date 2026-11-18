// A sound playing through MetalAudio that its caller can stop, fade or re-pitch.
using UnityEngine;

namespace Metalization
{
    /// <summary>A sound playing through MetalAudio that the caller can stop, fade or re-pitch.</summary>
    public class MetalSoundHandle : MonoBehaviour
    {
        public AudioSource Source { get; private set; }
        public MetalVolume Group { get; private set; }
        /// <summary>Volume before the group setting is applied.</summary>
        public float BaseVolume { get; set; }
        /// <summary>Destroy the object when a one-shot ends.</summary>
        public bool AutoDestroy { get; set; }

        float fadeTarget = 1f;
        float fadeRate;
        float fade = 1f;
        bool stopAfterFade;

        internal void Init(AudioSource source, MetalVolume group, float baseVolume)
        {
            Source = source;
            Group = group;
            BaseVolume = baseVolume;
            MetalAudio.Register(this);
            Apply();
        }

        public bool IsPlaying => Source != null && Source.isPlaying;

        public void Apply()
        {
            if (Source != null) Source.volume = BaseVolume * fade * MetalAudio.GetVolume(Group);
        }

        public void Stop()
        {
            if (Source != null) Source.Stop();
            if (AutoDestroy) Destroy(gameObject);
        }

        /// <summary>Starts (from startTime seconds in) and fades in over duration.</summary>
        public void FadeIn(float duration, float targetVolume = 1f, float startTime = 0f)
        {
            if (Source == null || Source.clip == null) return;
            fade = 0f;
            fadeTarget = targetVolume;
            fadeRate = duration > 0f ? targetVolume / duration : 1000f;
            stopAfterFade = false;
            Source.time = Mathf.Clamp(startTime, 0f, Mathf.Max(0f, Source.clip.length - 0.01f));
            Source.Play();
            Apply();
        }

        public void FadeOut(float duration)
        {
            fadeTarget = 0f;
            fadeRate = duration > 0f ? Mathf.Max(fade, 0.01f) / duration : 1000f;
            stopAfterFade = true;
        }

        void Update()
        {
            if (!Mathf.Approximately(fade, fadeTarget))
            {
                fade = Mathf.MoveTowards(fade, fadeTarget, fadeRate * Time.unscaledDeltaTime);
                Apply();
                if (stopAfterFade && fade <= 0f && Source != null) Source.Stop();
            }
            if (AutoDestroy && Source != null && !Source.isPlaying && Source.timeSamples == 0 && !Source.loop)
            {
                Destroy(gameObject);
            }
        }

        void OnDestroy()
        {
            MetalAudio.Unregister(this);
        }
    }
}
