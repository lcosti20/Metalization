// Where the player spawns in a level (Unreal's PlayerStart).
using UnityEngine;

namespace Metalization
{
    /// <summary>Where the player spawns when no checkpoint has been reached.</summary>
    public class MetalPlayerStart : MonoBehaviour
    {
        void OnDrawGizmos()
        {
            Gizmos.color = Color.cyan;
            Gizmos.DrawWireSphere(transform.position, 0.4f);
            Gizmos.DrawLine(transform.position, transform.position + transform.forward);
        }
    }
}
