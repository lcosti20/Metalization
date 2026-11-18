// Sound effects for the whole game, looked up by name.
//
// The sounds are original WAVs synthesized by Tools/AssetPipeline/generate_sfx.py and imported into
// Assets/Metalization/Audio; the pipeline lists them in the MetalSoundLibrary (Data/DA_Sounds). Gameplay code
// never references a clip directly, it just asks for a name ("laser_fire"). A missing library or name is
// silent, so the game still runs before the pipeline has been run.
//
// Three volume settings (the sliders in the pause and title menus) scale every sound by its group: Music
// (music and background loops), Voices (the robots' beep-boop "speech") and Effects (everything else).
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>The player volume settings.</summary>
    public enum MetalVolume
    {
        Music,
        Effects,
        Voices,
    }

    /// <summary>Static helpers to play library sounds.</summary>
    public static class MetalAudio
    {
        /// <summary>Full volume within this distance (m).</summary>
        public const float MinDistance = 4f;
        /// <summary>Silent beyond this distance (m).</summary>
        public const float MaxDistance = 45f;

        static readonly HashSet<MetalSoundHandle> Active = new HashSet<MetalSoundHandle>();
        static readonly Dictionary<string, int> VariantCounts = new Dictionary<string, int>();

        static MetalSoundLibrary Library => MetalGameAssets.Get() != null ? MetalGameAssets.Get().sounds : null;

        internal static void Register(MetalSoundHandle h) => Active.Add(h);
        internal static void Unregister(MetalSoundHandle h) => Active.Remove(h);

        static MetalSoundLibrary.Entry Find(string name)
        {
            MetalSoundLibrary lib = Library;
            if (lib == null || string.IsNullOrEmpty(name)) return null;
            MetalSoundLibrary.Entry e = lib.Find(name);
            return e != null && e.clip != null ? e : null;
        }

        static float Pitch(float jitter) => jitter > 0f ? UnityEngine.Random.Range(1f - jitter, 1f + jitter) : 1f;

        static MetalSoundHandle Create(MetalSoundLibrary.Entry e, Vector3 position, Transform parent, bool spatial,
            bool loop, float volume, float pitch, string label)
        {
            var go = new GameObject("Sound " + label);
            if (parent != null)
            {
                go.transform.SetParent(parent, false);
            }
            else
            {
                go.transform.position = position;
            }
            AudioSource src = go.AddComponent<AudioSource>();
            src.clip = e.clip;
            src.loop = loop;
            src.pitch = pitch;
            src.playOnAwake = false;
            src.dopplerLevel = 0f;
            if (spatial)
            {
                src.spatialBlend = 1f;
                src.rolloffMode = AudioRolloffMode.Custom;
                src.minDistance = MinDistance;
                src.maxDistance = MaxDistance;
                // Natural-sounding falloff: full volume inside MinDistance, quickly quieter, silent at MaxDistance.
                var curve = new AnimationCurve(
                    new Keyframe(0f, 1f), new Keyframe(MinDistance / MaxDistance, 1f),
                    new Keyframe(0.3f, 0.35f), new Keyframe(0.6f, 0.1f), new Keyframe(1f, 0f));
                src.SetCustomCurve(AudioSourceCurveType.CustomRolloff, curve);
            }
            else
            {
                src.spatialBlend = 0f;
            }
            MetalSoundHandle h = go.AddComponent<MetalSoundHandle>();
            h.Init(src, e.group, volume * e.volume);
            return h;
        }

        /// <summary>Plays a sound in the world, fading with distance. Unknown names are ignored.</summary>
        public static void PlayAt(string name, Vector3 location, float volume = 1f, float pitchJitter = 0.05f)
        {
            MetalSoundLibrary.Entry e = Find(name);
            if (e == null) return;
            MetalSoundHandle h = Create(e, location, null, true, false, volume, Pitch(pitchJitter), name);
            h.AutoDestroy = true;
            h.Source.Play();
        }

        /// <summary>Plays a non-positional sound (UI, radio, stingers).</summary>
        public static void Play2D(string name, float volume = 1f, float pitchJitter = 0f)
        {
            MetalSoundLibrary.Entry e = Find(name);
            if (e == null) return;
            MetalSoundHandle h = Create(e, Vector3.zero, null, false, false, volume, Pitch(pitchJitter), name);
            h.AutoDestroy = true;
            h.Source.ignoreListenerPause = true;
            h.Source.Play();
        }

        /// <summary>Starts a looping positional sound attached to a transform (lift hum). Destroyed with its parent.</summary>
        public static MetalSoundHandle PlayLoop(Transform attachTo, string name, float volume = 1f)
        {
            if (attachTo == null) return null;
            MetalSoundLibrary.Entry e = Find(name);
            if (e == null) return null;
            MetalSoundHandle h = Create(e, Vector3.zero, attachTo, true, true, volume, 1f, name);
            h.Source.time = UnityEngine.Random.Range(0f, e.clip.length * 0.9f);
            h.Source.Play();
            return h;
        }

        /// <summary>
        /// One-shot positional sound that follows a transform and can be cut short (robot voices). It destroys
        /// itself when it ends.
        /// </summary>
        public static MetalSoundHandle PlayAttached(Transform attachTo, string name, float volume = 1f, float pitch = 1f)
        {
            if (attachTo == null) return null;
            MetalSoundLibrary.Entry e = Find(name);
            if (e == null) return null;
            MetalSoundHandle h = Create(e, Vector3.zero, attachTo, true, false, volume, pitch, name);
            h.AutoDestroy = true;
            h.Source.Play();
            return h;
        }

        /// <summary>A non-positional loop created stopped, so the caller can FadeIn from any point (radio whistle).</summary>
        public static MetalSoundHandle CreateLoop2D(string name, float volume = 1f)
        {
            MetalSoundLibrary.Entry e = Find(name);
            if (e == null) return null;
            return Create(e, Vector3.zero, null, false, true, volume, 1f, name);
        }

        /// <summary>A looping non-positional sound playing straight away (music, ambience).</summary>
        public static MetalSoundHandle PlayLoop2D(string name, float volume = 1f)
        {
            MetalSoundHandle h = CreateLoop2D(name, volume);
            if (h != null) h.Source.Play();
            return h;
        }

        /// <summary>Random numbered variant of a sound ("voice_pain" → "voice_pain4"), or null if there is none.</summary>
        public static string PickVariant(string baseName)
        {
            if (!VariantCounts.TryGetValue(baseName, out int count))
            {
                if (Library == null) return null;
                while (HasSound(baseName + (count + 1))) count++;
                VariantCounts[baseName] = count;
            }
            return count > 0 ? baseName + UnityEngine.Random.Range(1, count + 1) : null;
        }

        public static bool HasSound(string name) => Find(name) != null;

        static string Key(MetalVolume g) => g == MetalVolume.Music ? "MusicVolume" : g == MetalVolume.Voices ? "VoicesVolume" : "EffectsVolume";

        /// <summary>The player's volume setting for a group, 0..1 (default 1).</summary>
        public static float GetVolume(MetalVolume group) => Mathf.Clamp01(PlayerPrefs.GetFloat(Key(group), 1f));

        /// <summary>Changes a volume setting, saves it and applies it to sounds already playing.</summary>
        public static void SetVolume(MetalVolume group, float volume)
        {
            PlayerPrefs.SetFloat(Key(group), Mathf.Clamp01(volume));
            PlayerPrefs.Save();
            ApplyVolumes();
        }

        public static void ApplyVolumes()
        {
            foreach (MetalSoundHandle h in Active)
            {
                if (h != null) h.Apply();
            }
        }
    }
}
