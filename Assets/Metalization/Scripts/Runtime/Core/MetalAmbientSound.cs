// A level's music or ambience: a non-positional library loop that plays while the scene runs (Unreal's
// AmbientSound actors), on the volume group the library gives it.
using UnityEngine;

namespace Metalization
{
    public class MetalAmbientSound : MonoBehaviour
    {
        [Tooltip("Sound library name of the loop.")]
        public string sound = "music_background_loop";
        public float volume = 1f;

        MetalSoundHandle handle;

        void Start()
        {
            handle = MetalAudio.PlayLoop2D(sound, volume);
            if (handle != null) handle.transform.SetParent(transform, false);
        }

        void OnDestroy()
        {
            if (handle != null) Destroy(handle.gameObject);
        }
    }
}
