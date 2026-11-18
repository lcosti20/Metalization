// Checkpoint snapshots: the state of a level when the player reached a checkpoint, kept across the scene reload
// that follows a death so the level can be put back exactly as it was.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    public class MetalBotSnapshot
    {
        public Vector3 position;
        public float yaw;
        public float health;
        public bool dormant;
    }

    public class MetalDispensedBotSnapshot
    {
        public string dispenser;
        public int slot = -1;
        public MetalBotArchetype archetype;
        public MetalBotSnapshot state;
    }

    /// <summary>Everything about a level that a checkpoint restores.</summary>
    public class MetalLevelSnapshot
    {
        public string sceneName;
        public Vector3 playerPosition;
        public float playerYaw;
        public List<MetalWeaponSlot> weapons = new List<MetalWeaponSlot>();
        public int weaponIndex;
        public int grenades;
        public int objectiveIndex = -1;
        public List<int> completedObjectives = new List<int>();
        public int kills;
        /// <summary>Placed objects that still existed; any other placed object is removed on restore.</summary>
        public HashSet<string> liveActors = new HashSet<string>();
        public Dictionary<string, MetalBotSnapshot> bots = new Dictionary<string, MetalBotSnapshot>();
        public List<MetalDispensedBotSnapshot> dispensedBots = new List<MetalDispensedBotSnapshot>();
        public HashSet<string> activatedConsoles = new HashSet<string>();
        public HashSet<string> triggeredZones = new HashSet<string>();
        public HashSet<string> playedDialogueZones = new HashSet<string>();
    }

    /// <summary>
    /// Holds the last checkpoint snapshot across scene loads. On a death the game mode marks a retry and reloads the
    /// scene; the new game mode takes the snapshot back and restores it in two passes around Start. Any other load
    /// (restart, level select, title) clears it.
    /// </summary>
    public static class MetalCheckpoints
    {
        static MetalLevelSnapshot snapshot;
        static bool retryPending;
        static string retryScene;
        static int retryDeaths;
        static float retryElapsed;

        /// <summary>Records the level as it is now as the new checkpoint.</summary>
        public static void Capture(MetalGameMode gm, MetalBot playerBody, Vector3 position, float yaw)
        {
            var s = new MetalLevelSnapshot
            {
                sceneName = gm.gameObject.scene.name,
                playerPosition = position,
                playerYaw = yaw,
                objectiveIndex = gm.Mission != null ? gm.Mission.CurrentObjectiveIndex : -1,
                completedObjectives = gm.Mission != null ? gm.Mission.GetCompletedInGroup() : new List<int>(),
                kills = gm.Kills,
            };
            if (playerBody != null)
            {
                foreach (MetalWeaponSlot w in playerBody.Weapons) s.weapons.Add(w.Clone());
                s.weaponIndex = Mathf.Max(0, playerBody.CurrentWeaponIndex);
                s.grenades = playerBody.Grenades;
            }
            foreach (MetalPlaced placed in MetalPlaced.All)
            {
                if (placed == null) continue;
                string key = placed.Key;
                MetalBot bot = placed.GetComponent<MetalBot>();
                if (bot != null)
                {
                    // A wreck still lying there counts as gone.
                    if (!bot.IsAlive || bot.IsHiddenAfterDeath) continue;
                    s.bots[key] = new MetalBotSnapshot
                    {
                        position = bot.transform.position,
                        yaw = bot.transform.eulerAngles.y,
                        health = bot.Health,
                        dormant = bot.IsDormant,
                    };
                }
                MetalControlConsole console = placed.GetComponent<MetalControlConsole>();
                if (console != null && console.IsActivated) s.activatedConsoles.Add(key);
                MetalRobotDispenser dispenser = placed.GetComponent<MetalRobotDispenser>();
                if (dispenser != null)
                {
                    dispenser.ForEachBuiltRobot((robot, slot) => s.dispensedBots.Add(new MetalDispensedBotSnapshot
                    {
                        dispenser = key,
                        slot = slot,
                        archetype = robot.archetype,
                        state = new MetalBotSnapshot
                        {
                            position = robot.transform.position,
                            yaw = robot.transform.eulerAngles.y,
                            health = robot.Health,
                            dormant = robot.IsDormant,
                        },
                    }));
                }
                MetalTriggerZone zone = placed.GetComponent<MetalTriggerZone>();
                if (zone != null)
                {
                    if (zone.IsCheckpointUsed) s.triggeredZones.Add(key);
                    if (zone.HasPlayedDialogue) s.playedDialogueZones.Add(key);
                }
                s.liveActors.Add(key);
            }
            snapshot = s;
            Debug.Log($"[Checkpoint] snapshot of {s.sceneName}: objective {s.objectiveIndex}, {s.bots.Count} robots standing (+{s.dispensedBots.Count} built), {s.liveActors.Count} placed objects");
        }

        /// <summary>Marks that the next load of this scene is a retry after a death.</summary>
        public static void PrepareRetry(string sceneName, int deaths, float elapsed)
        {
            retryPending = true;
            retryScene = sceneName;
            retryDeaths = deaths;
            retryElapsed = elapsed;
        }

        /// <summary>Called by the new game mode as the scene loads. Clears everything unless a retry of this scene is pending.</summary>
        public static MetalLevelSnapshot TakeRetry(string sceneName, ref int deaths, ref float elapsed)
        {
            bool retry = retryPending && sceneName == retryScene;
            retryPending = false;
            if (!retry)
            {
                // A fresh visit: the old checkpoint no longer applies.
                snapshot = null;
                return null;
            }
            deaths = retryDeaths;
            elapsed = retryElapsed;
            // The snapshot stays: dying again before the next checkpoint goes back to the same one.
            return snapshot != null && snapshot.sceneName == sceneName ? snapshot : null;
        }

        /// <summary>First pass, before any Start: removes placed objects that were gone, moves robots back, marks used zones.</summary>
        public static void ApplyBeforeStart(MetalLevelSnapshot s)
        {
            var removed = new List<GameObject>();
            foreach (MetalPlaced placed in Object.FindObjectsByType<MetalPlaced>(FindObjectsInactive.Exclude))
            {
                string key = placed.Key;
                if (!s.liveActors.Contains(key))
                {
                    removed.Add(placed.gameObject);
                    continue;
                }
                if (s.bots.TryGetValue(key, out MetalBotSnapshot bot))
                {
                    placed.transform.SetPositionAndRotation(bot.position, Quaternion.Euler(0f, bot.yaw, 0f));
                }
                MetalTriggerZone zone = placed.GetComponent<MetalTriggerZone>();
                if (zone != null) zone.RestoreState(s.triggeredZones.Contains(key), s.playedDialogueZones.Contains(key));
            }
            foreach (GameObject go in removed)
            {
                go.SetActive(false);
                Object.Destroy(go);
            }
        }

        /// <summary>Second pass, after every Start: robot health, consoles and their gates, dispensed robots, the loadout.</summary>
        public static void ApplyAfterStart(MetalLevelSnapshot s, MetalBot player)
        {
            var dispensers = new Dictionary<string, MetalRobotDispenser>();
            foreach (MetalPlaced placed in MetalPlaced.All)
            {
                if (placed == null) continue;
                string key = placed.Key;
                MetalBot bot = placed.GetComponent<MetalBot>();
                if (bot != null && s.bots.TryGetValue(key, out MetalBotSnapshot entry))
                {
                    bot.RestoreHealth(entry.health);
                    if (bot.IsDormant != entry.dormant) bot.SetDormant(entry.dormant);
                }
                MetalControlConsole console = placed.GetComponent<MetalControlConsole>();
                if (console != null && s.activatedConsoles.Contains(key)) console.RestoreActivated();
                MetalRobotDispenser dispenser = placed.GetComponent<MetalRobotDispenser>();
                if (dispenser != null) dispensers[key] = dispenser;
            }
            foreach (MetalDispensedBotSnapshot e in s.dispensedBots)
            {
                if (!dispensers.TryGetValue(e.dispenser, out MetalRobotDispenser d)) continue;
                MetalBot robot = d.RestoreBuiltRobot(e.archetype, e.slot, e.state.position, e.state.yaw);
                if (robot == null) continue;
                robot.RestoreHealth(e.state.health);
                if (e.state.dormant) robot.SetDormant(true);
            }
            if (player != null)
            {
                if (s.weapons.Count > 0) player.SetWeapons(s.weapons, s.weaponIndex);
                player.Grenades = s.grenades;
            }
            Debug.Log($"[Checkpoint] restored {s.sceneName} at objective {s.objectiveIndex}");
        }
    }
}
