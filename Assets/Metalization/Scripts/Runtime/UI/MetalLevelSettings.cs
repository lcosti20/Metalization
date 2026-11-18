// The Level Select list shown by the pause and title menus (generated as Data/LevelSettings).
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>The Level Select list: a name to show and the scene it opens, in order.</summary>
    [CreateAssetMenu(menuName = "Metalization/Level Settings", fileName = "LevelSettings")]
    public class MetalLevelSettings : ScriptableObject
    {
        [Serializable]
        public class Entry
        {
            public string displayName;
            [Tooltip("Scene name (must be in the build profile's scene list).")]
            public string sceneName;
        }

        public List<Entry> levels = new List<Entry>();
    }
}
