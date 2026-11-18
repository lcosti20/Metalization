// Marks an object placed by a level script so checkpoint snapshots can find it again after a reload.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>
    /// Marks an object placed in a level (by the level builder) so checkpoint snapshots can track it. The id is the
    /// same every time the scene loads.
    /// </summary>
    [DisallowMultipleComponent]
    public class MetalPlaced : MonoBehaviour
    {
        public string id = "";

        public static readonly List<MetalPlaced> All = new List<MetalPlaced>();

        void Awake() => All.Add(this);
        void OnDestroy() => All.Remove(this);

        public string Key => string.IsNullOrEmpty(id) ? gameObject.name : id;
    }
}
