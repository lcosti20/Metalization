// Designer-configured mission: an ordered list of objectives placed in the level.
using System;
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    /// <summary>What the player has to do to complete an objective.</summary>
    public enum MetalObjectiveType
    {
        /// <summary>Destroy every robot carrying targetTag.</summary>
        EliminateTagged,
        /// <summary>Get the player's current body into targetObject (a MetalTriggerZone).</summary>
        ReachZone,
        /// <summary>Activate targetObject (a MetalControlConsole).</summary>
        ActivateConsole,
        /// <summary>Take control of any robot carrying targetTag with the Hack Tether.</summary>
        HijackTagged,
    }

    /// <summary>One step of a mission.</summary>
    [Serializable]
    public class MetalObjective
    {
        public string description = "";
        public MetalObjectiveType type = MetalObjectiveType.ReachZone;
        [Tooltip("Robot tag for EliminateTagged and HijackTagged (HijackTagged with no tag accepts any robot).")]
        public string targetTag = "";
        [Tooltip("Trigger zone for ReachZone, console for ActivateConsole.")]
        public GameObject targetObject;
        public List<MetalDialogueLine> startDialogue = new List<MetalDialogueLine>();
        [Tooltip("Control tip shown while the objective is active, e.g. \"Hold [Shift] to sprint\".")]
        public string hint = "";
        public bool showMarker = true;
        public List<MetalDialogueLine> completeDialogue = new List<MetalDialogueLine>();
        [Tooltip("Active at the same time as the next objective (a group, completable in any order).")]
        public bool withNext;
        [Tooltip("Name other objectives in the group use in their requires.")]
        public string id = "";
        [Tooltip("Ids of objectives that must be complete before this one unlocks.")]
        public List<string> requires = new List<string>();
        [Tooltip("Saves a checkpoint where the player's own body stands (and repairs it) when this completes.")]
        public bool checkpointOnComplete;
    }

    public enum MetalObjectiveState
    {
        Active,
        Locked,
        Done,
    }

    public struct MetalObjectiveStatus
    {
        public int index;
        public MetalObjectiveState state;
    }

    /// <summary>
    /// Placed once per level. Holds the ordered objectives and their dialogue; the game mode starts it, and it polls the
    /// active objectives four times a second, advancing when a group is done and completing the mission after the last.
    /// </summary>
    public class MetalMission : MonoBehaviour
    {
        [Tooltip("Shown as a banner when the mission starts.")]
        public string missionName = "";
        public List<MetalObjective> objectives = new List<MetalObjective>();
        public List<MetalDialogueLine> completionDialogue = new List<MetalDialogueLine>();
        [Tooltip("Replaces the player's archetype loadout at the start of the mission. Empty keeps it.")]
        public List<MetalWeaponData> playerStartingWeapons = new List<MetalWeaponData>();
        [Tooltip("Grenades the player starts with; negative keeps the archetype's count.")]
        public int playerStartingGrenades = -1;

        MetalGameMode gameMode;
        int currentObjective = -1;
        bool[] done = new bool[0];
        bool[] announced = new bool[0];
        float nextCheckTime;
        bool loadoutApplied;
        float quietUntil = -1f;

        public int CurrentObjectiveIndex => currentObjective;
        bool Valid(int i) => i >= 0 && i < objectives.Count;

        /// <summary>Shows the mission banner and activates the first objective (or -MetalObjective=N from the command line).</summary>
        public void StartMission(MetalGameMode gm)
        {
            gameMode = gm;
            if (gm != null && !string.IsNullOrEmpty(missionName)) gm.ShowBanner(missionName, 4f);
            int first = MetalDevAutopilot.GetIntArg("-MetalObjective=", 0);
            ActivateObjective(first);
        }

        /// <summary>Resumes at a checkpoint snapshot's objective: no banner, no start dialogue, no starting loadout.</summary>
        public void RestoreObjective(MetalGameMode gm, int index, List<int> completed)
        {
            gameMode = gm;
            loadoutApplied = true;
            quietUntil = Time.time + 1f;
            currentObjective = index;
            done = new bool[objectives.Count];
            announced = new bool[objectives.Count];
            foreach (int i in completed)
            {
                if (Valid(i)) done[i] = true;
            }
            if (Valid(index))
            {
                for (int i = index; i <= GroupEnd(index); i++) announced[i] = done[i] || RequirementsMet(objectives[i]);
            }
        }

        int GroupEnd(int first)
        {
            int last = first;
            while (Valid(last + 1) && objectives[last].withNext) last++;
            return last;
        }

        bool RequirementsMet(MetalObjective o)
        {
            foreach (string required in o.requires)
            {
                for (int i = 0; i < objectives.Count; i++)
                {
                    // Objectives before the current group are done by definition.
                    if (objectives[i].id == required && i >= currentObjective && !(i < done.Length && done[i])) return false;
                }
            }
            return true;
        }

        bool IsActive(int i) => Valid(currentObjective) && i >= currentObjective && i <= GroupEnd(currentObjective) && !done[i] && RequirementsMet(objectives[i]);

        public string CurrentObjectiveText
        {
            get
            {
                if (Valid(currentObjective))
                {
                    for (int i = currentObjective; i <= GroupEnd(currentObjective); i++)
                    {
                        if (IsActive(i)) return objectives[i].description;
                    }
                }
                return "";
            }
        }

        /// <summary>Every objective of the current group with its state (empty when finished).</summary>
        public List<MetalObjectiveStatus> GetObjectiveList()
        {
            var list = new List<MetalObjectiveStatus>();
            if (!Valid(currentObjective)) return list;
            for (int i = currentObjective; i <= GroupEnd(currentObjective); i++)
            {
                list.Add(new MetalObjectiveStatus
                {
                    index = i,
                    state = done[i] ? MetalObjectiveState.Done : RequirementsMet(objectives[i]) ? MetalObjectiveState.Active : MetalObjectiveState.Locked,
                });
            }
            return list;
        }

        public List<int> GetCompletedInGroup()
        {
            var list = new List<int>();
            if (!Valid(currentObjective)) return list;
            for (int i = currentObjective; i <= GroupEnd(currentObjective); i++)
            {
                if (done[i]) list.Add(i);
            }
            return list;
        }

        /// <summary>Control tips of the active objectives.</summary>
        public List<string> GetCurrentHints()
        {
            var hints = new List<string>();
            if (!Valid(currentObjective)) return hints;
            for (int i = currentObjective; i <= GroupEnd(currentObjective); i++)
            {
                if (IsActive(i) && !string.IsNullOrEmpty(objectives[i].hint)) hints.Add(objectives[i].hint);
            }
            return hints;
        }

        /// <summary>Where the HUD's objective markers point: one per active objective with a marker.</summary>
        public void GetObjectiveMarkers(List<Vector3> locations)
        {
            if (!Valid(currentObjective)) return;
            MetalBot viewer = MetalPlayerController.Instance != null ? MetalPlayerController.Instance.Pawn : null;
            for (int i = currentObjective; i <= GroupEnd(currentObjective); i++)
            {
                MetalObjective o = objectives[i];
                if (!IsActive(i) || !o.showMarker) continue;
                switch (o.type)
                {
                    case MetalObjectiveType.ReachZone:
                    case MetalObjectiveType.ActivateConsole:
                        if (o.targetObject != null) locations.Add(o.targetObject.transform.position);
                        break;
                    case MetalObjectiveType.EliminateTagged:
                    case MetalObjectiveType.HijackTagged:
                        // The nearest robot still to deal with.
                        if (viewer == null || string.IsNullOrEmpty(o.targetTag)) break;
                        float best = float.MaxValue;
                        Vector3 loc = Vector3.zero;
                        foreach (MetalBot b in MetalBot.All)
                        {
                            if (!b.HasTag(o.targetTag) || !b.IsAlive || b.IsHijacked) continue;
                            float d = (b.transform.position - viewer.transform.position).sqrMagnitude;
                            if (d < best)
                            {
                                best = d;
                                loc = b.transform.position + Vector3.up * (b.Movement.FullHalfHeight + 0.4f);
                            }
                        }
                        if (best < float.MaxValue) locations.Add(loc);
                        break;
                }
            }
        }

        void PlayDialogue(List<MetalDialogueLine> lines) => MetalDialogue.Get().PlayDialogue(lines);

        void AnnounceUnlocked()
        {
            if (!Valid(currentObjective)) return;
            for (int i = currentObjective; i <= GroupEnd(currentObjective); i++)
            {
                if (!announced[i] && !done[i] && RequirementsMet(objectives[i]))
                {
                    announced[i] = true;
                    PlayDialogue(objectives[i].startDialogue);
                    Debug.Log($"[Metal] Objective {i}: {objectives[i].description}");
                }
            }
        }

        void ActivateObjective(int index)
        {
            currentObjective = index;
            if (done.Length != objectives.Count)
            {
                done = new bool[objectives.Count];
                announced = new bool[objectives.Count];
            }
            if (Valid(index))
            {
                AnnounceUnlocked();
            }
            else if (gameMode != null)
            {
                PlayDialogue(completionDialogue);
                gameMode.CompleteMission();
            }
        }

        bool IsObjectiveComplete(MetalObjective o)
        {
            // "Player body" here is whatever the player currently controls, so hijacked robots count for zones.
            MetalBot player = MetalPlayerController.Instance != null ? MetalPlayerController.Instance.Pawn : null;
            switch (o.type)
            {
                case MetalObjectiveType.EliminateTagged:
                    foreach (MetalBot b in MetalBot.All)
                    {
                        if (b.HasTag(o.targetTag) && b.IsAlive) return false;
                    }
                    return true;
                case MetalObjectiveType.ReachZone:
                    MetalTriggerZone zone = o.targetObject != null ? o.targetObject.GetComponent<MetalTriggerZone>() : null;
                    return zone != null && player != null && zone.Contains(player);
                case MetalObjectiveType.ActivateConsole:
                    MetalControlConsole console = o.targetObject != null ? o.targetObject.GetComponent<MetalControlConsole>() : null;
                    return console != null && console.IsActivated;
                case MetalObjectiveType.HijackTagged:
                    return player != null && player.IsHijacked && (string.IsNullOrEmpty(o.targetTag) || player.HasTag(o.targetTag));
            }
            return false;
        }

        void Update()
        {
            float now = Time.time;
            if (!loadoutApplied && (playerStartingWeapons.Count > 0 || playerStartingGrenades >= 0))
            {
                // Done on the first frame the player's robot has handed out its own archetype loadout.
                MetalBot player = MetalPlayerController.Instance != null ? MetalPlayerController.Instance.Pawn : null;
                if (player != null && player.Weapons.Count > 0)
                {
                    loadoutApplied = true;
                    if (playerStartingWeapons.Count > 0)
                    {
                        var loadout = new List<MetalWeaponSlot>();
                        foreach (MetalWeaponData w in playerStartingWeapons)
                        {
                            if (w != null) loadout.Add(new MetalWeaponSlot { data = w, clip = w.GetLevel(0).clipSize, reserve = w.GetLevel(0).maxReserveAmmo / 2 });
                        }
                        player.SetWeapons(loadout, 0);
                    }
                    if (playerStartingGrenades >= 0) player.Grenades = playerStartingGrenades;
                }
            }
            if (!Valid(currentObjective) || now < nextCheckTime) return;
            nextCheckTime = now + 0.25f;

            // Every unlocked objective of the group is checked, so they can be done in any order.
            bool quiet = now < quietUntil;
            int last = GroupEnd(currentObjective);
            for (int i = currentObjective; i <= last; i++)
            {
                if (!IsActive(i) || !IsObjectiveComplete(objectives[i])) continue;
                done[i] = true;
                Debug.Log($"[Metal] Objective {i} complete: {objectives[i].description}");
                if (quiet) continue;
                gameMode?.ShowMessage("", "Objective complete: " + objectives[i].description, 3f);
                MetalAudio.Play2D("objective_complete", 0.8f);
                PlayDialogue(objectives[i].completeDialogue);
                if (objectives[i].checkpointOnComplete)
                {
                    // Saved where the player's own body stands, even if they're driving a hijacked robot right now.
                    MetalBot body = MetalPlayerController.Instance != null ? MetalPlayerController.Instance.PlayerBody : null;
                    if (body != null && body.IsAlive && gameMode != null)
                    {
                        gameMode.SetCheckpoint(body.transform.position - Vector3.up * body.Movement.FullHalfHeight, body.transform.eulerAngles.y, body);
                        body.Heal(body.MaxHealth);
                    }
                }
            }
            bool groupDone = true;
            for (int i = currentObjective; i <= last; i++) groupDone &= done[i];
            if (groupDone)
            {
                ActivateObjective(last + 1);
            }
            else if (!quiet)
            {
                AnnounceUnlocked();
            }
            else
            {
                for (int i = currentObjective; i <= last; i++) announced[i] |= RequirementsMet(objectives[i]);
            }
        }
    }
}
