// Mission flow, checkpoints, respawning and on-screen messages.
using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Metalization
{
    /// <summary>One line of on-screen text shown by the HUD: a system notice or a banner.</summary>
    public class MetalMessage
    {
        public string text;
        public float duration;
    }

    /// <summary>
    /// Game mode for the on-foot levels, and the base of MetalSpaceBattleGameMode. One per scene: it spawns the
    /// player (body + controller + HUD), starts the level's MetalMission, keeps the message/banner state the HUD
    /// draws, and handles checkpoints, death, respawn and mission completion.
    /// </summary>
    [DefaultExecutionOrder(-1000)]
    public class MetalGameMode : MonoBehaviour
    {
        /// <summary>The running scene's game mode.</summary>
        public static MetalGameMode Current { get; private set; }

        /// <summary>Robots below this height have fallen out of the world.</summary>
        public static float KillY = -200f;

        [Tooltip("The player's robot.")]
        public MetalBotArchetype playerArchetype;
        [Tooltip("Action-adventure controls: running freely the robot faces where it runs; aiming or firing switches to the over-the-shoulder strafe.")]
        public bool actionCamera;
        [Tooltip("Hide the health/ammo panels until the player is in combat.")]
        public bool minimalHUD;
        [Tooltip("Dying puts the whole level back the way it was at the last checkpoint by reloading it.")]
        public bool resetLevelOnDeath = true;
        [Tooltip("Spawn the player at start (off for menus).")]
        public bool spawnPlayer = true;
        public float killY = -200f;

        public MetalMission Mission { get; private set; }
        public MetalPlayerController PlayerController { get; private set; }
        public bool IsMissionComplete { get; private set; }
        public int Kills { get; protected set; }
        public int Deaths { get; protected set; }
        public bool IsRestoredFromCheckpoint { get; private set; }

        MetalMessage notice = new MetalMessage();
        float noticeTimeLeft;
        MetalMessage banner = new MetalMessage();
        float bannerTimeLeft;
        bool hasCheckpoint;
        Vector3 checkpointPosition;
        float checkpointYaw;
        List<MetalWeaponSlot> savedWeapons = new List<MetalWeaponSlot>();
        int savedWeaponIndex;
        int savedGrenades;
        float missionStartTime;
        float missionEndTime;
        float carriedMissionTime;
        MetalLevelSnapshot restoreSnapshot;

        public MetalMessage Notice => noticeTimeLeft > 0f ? notice : null;
        public MetalMessage Banner => bannerTimeLeft > 0f ? banner : null;

        /// <summary>True for the game mode of a scene loaded additively into another level (a ship interior): it does nothing.</summary>
        protected bool IsAdditiveCopy { get; private set; }

        protected virtual void Awake()
        {
            if (Current != null && Current != this && gameObject.scene != SceneManager.GetActiveScene())
            {
                // A level loaded into another one (a capital ship's interior) is played under the host's game mode.
                IsAdditiveCopy = true;
                foreach (GameObject root in gameObject.scene.GetRootGameObjects())
                {
                    foreach (Camera c in root.GetComponentsInChildren<Camera>()) c.gameObject.SetActive(false);
                    foreach (AudioListener l in root.GetComponentsInChildren<AudioListener>()) l.enabled = false;
                }
                Destroy(gameObject);
                return;
            }
            Current = this;
            KillY = killY;
            MetalGameMode.SetPaused(false);
            // A retry after a death puts the level back the way it was at the last checkpoint.
            int deaths = 0;
            MetalLevelSnapshot snapshot = MetalCheckpoints.TakeRetry(SceneManager.GetActiveScene().name, ref deaths, ref carriedMissionTime);
            Deaths = deaths;
            if (snapshot != null)
            {
                IsRestoredFromCheckpoint = true;
                restoreSnapshot = snapshot;
                hasCheckpoint = true;
                checkpointPosition = snapshot.playerPosition;
                checkpointYaw = snapshot.playerYaw;
                Kills = snapshot.kills;
                MetalCheckpoints.ApplyBeforeStart(snapshot);
            }
            if (spawnPlayer) SpawnPlayer();
        }

        protected virtual void OnDestroy()
        {
            if (IsAdditiveCopy) return;
            if (Current == this) Current = null;
            MetalBot.AnyBotDied -= OnBotDied;
        }

        protected virtual void Start()
        {
            if (IsAdditiveCopy) return;
            missionStartTime = Time.time - carriedMissionTime;
            MetalBot.AnyBotDied += OnBotDied;
            Mission = FindAnyObjectByType<MetalMission>();
            if (Mission != null && IsRestoredFromCheckpoint && restoreSnapshot.objectiveIndex >= 0)
            {
                Mission.RestoreObjective(this, restoreSnapshot.objectiveIndex, restoreSnapshot.completedObjectives);
                ShowBanner("CHECKPOINT", 2f);
            }
            else if (Mission != null)
            {
                Mission.StartMission(this);
            }
            if (IsRestoredFromCheckpoint) StartCoroutine(ApplyAfterStart());
            MetalDevAutopilot.SpawnIfRequested();
        }

        IEnumerator ApplyAfterStart()
        {
            // After every Start (which resets health, screens and loadouts).
            yield return null;
            MetalCheckpoints.ApplyAfterStart(restoreSnapshot, PlayerController != null ? PlayerController.Pawn : null);
        }

        // ==================================================================================================
        // Player
        // ==================================================================================================

        /// <summary>Spawns a robot of the given archetype, as a walker or a Stalker, optionally with its AI.</summary>
        public static MetalBot SpawnBot(MetalBotArchetype archetype, Vector3 position, float yaw, bool autoAI, bool flyer = false, string objectName = null)
        {
            var go = new GameObject(objectName ?? ("Bot " + (archetype != null ? archetype.displayName : "?")));
            go.SetActive(false);
            go.transform.SetPositionAndRotation(position, Quaternion.Euler(0f, yaw, 0f));
            MetalBot bot = flyer ? go.AddComponent<MetalStalker>() : go.AddComponent<MetalBot>();
            bot.archetype = archetype;
            bot.autoPossessAI = autoAI;
            if (bot is MetalStalker s) s.startParked = false;
            go.SetActive(true);
            return bot;
        }

        Transform FindPlayerStart()
        {
            MetalPlayerStart start = FindAnyObjectByType<MetalPlayerStart>();
            return start != null ? start.transform : null;
        }

        /// <summary>Creates the player's controller (with camera rig and HUD) and their robot at the checkpoint or start.</summary>
        protected void SpawnPlayer()
        {
            var pcGo = new GameObject("Player Controller");
            PlayerController = pcGo.AddComponent<MetalPlayerController>();
            pcGo.AddComponent<MetalHUD>();
            RestartPlayer();
        }

        /// <summary>Where the player (re)spawns when there is no checkpoint: the level's player start by default.</summary>
        protected virtual bool ChooseSpawnPoint(out Vector3 position, out float yaw)
        {
            Transform start = FindPlayerStart();
            position = start != null ? start.position : Vector3.up;
            yaw = start != null ? start.eulerAngles.y : 0f;
            return start != null;
        }

        MetalBot RestartPlayer()
        {
            Vector3 pos;
            float yaw;
            if (hasCheckpoint)
            {
                pos = checkpointPosition;
                yaw = checkpointYaw;
            }
            else
            {
                ChooseSpawnPoint(out pos, out yaw);
            }
            MetalBotArchetype arch = playerArchetype != null ? playerArchetype : (MetalGameAssets.Get() != null ? MetalGameAssets.Get().playerArchetype : null);
            float half = arch != null ? arch.capsuleHalfHeight : 0.7f;
            MetalBot body = SpawnBot(arch, pos + Vector3.up * (half + 0.05f), yaw, false, false, "Player");
            PlayerController.Possess(body);
            PlayerController.ControlYaw = yaw;
            return body;
        }

        // ==================================================================================================
        // Messages
        // ==================================================================================================

        /// <summary>Dialogue (with a speaker) is queued; system notices (no speaker) replace the current notice.</summary>
        public void ShowMessage(string speaker, string text, float duration)
        {
            if (string.IsNullOrEmpty(text)) return;
            if (string.IsNullOrEmpty(speaker))
            {
                notice = new MetalMessage { text = text, duration = duration };
                noticeTimeLeft = duration;
                return;
            }
            MetalDialogue.Get().PlayLine(new MetalDialogueLine(speaker, text, duration));
        }

        /// <summary>Big centred text ("CHECKPOINT", "MISSION COMPLETE"). Replaces any current banner.</summary>
        public void ShowBanner(string text, float duration)
        {
            banner = new MetalMessage { text = text, duration = duration };
            bannerTimeLeft = duration;
        }

        protected virtual void Update()
        {
            noticeTimeLeft -= Time.deltaTime;
            bannerTimeLeft -= Time.deltaTime;
        }

        public string ObjectiveText => Mission != null ? Mission.CurrentObjectiveText : "";

        /// <summary>Seconds since the level started, frozen once the mission is complete.</summary>
        public float MissionTime => (IsMissionComplete ? missionEndTime : Time.time) - missionStartTime;

        // ==================================================================================================
        // Checkpoints, death and completion
        // ==================================================================================================

        /// <summary>Makes this the respawn point, shows the CHECKPOINT banner and snapshots the level.</summary>
        public void SetCheckpoint(Vector3 position, float yaw, MetalBot playerBody)
        {
            hasCheckpoint = true;
            checkpointPosition = position;
            checkpointYaw = yaw;
            if (resetLevelOnDeath) MetalCheckpoints.Capture(this, playerBody, position, yaw);
            ShowBanner("CHECKPOINT", 2f);
            MetalAudio.Play2D("checkpoint", 0.8f);
        }

        void OnBotDied(MetalBot bot, MetalController killer)
        {
            if (bot != null && bot.Team == MetalTeam.Villains && killer != null && killer.IsPlayer) Kills++;
        }

        /// <summary>
        /// The player's own body died. Three seconds later the level either reloads back to the last checkpoint snapshot
        /// or the player respawns in the running level with the loadout they died with.
        /// </summary>
        public virtual void PlayerDied(MetalPlayerController controller, MetalBot body)
        {
            if (body != null)
            {
                savedWeapons = new List<MetalWeaponSlot>();
                foreach (MetalWeaponSlot s in body.Weapons) savedWeapons.Add(s.Clone());
                savedWeaponIndex = Mathf.Max(0, body.CurrentWeaponIndex);
                savedGrenades = body.Grenades;
            }
            Deaths++;
            ShowBanner("SCRAPPED!", 3f);
            MetalAudio.Play2D("player_down", 0.9f);
            // After the mission is won a reload would throw away the results screen, so just respawn.
            if (resetLevelOnDeath && !IsMissionComplete) StartCoroutine(After(3f, ReloadFromCheckpoint));
            else StartCoroutine(After(3f, () => Respawn(controller)));
        }

        protected static IEnumerator After(float seconds, System.Action action)
        {
            yield return new WaitForSeconds(seconds);
            action();
        }

        void ReloadFromCheckpoint()
        {
            MetalCheckpoints.PrepareRetry(SceneManager.GetActiveScene().name, Deaths, MissionTime);
            Debug.Log($"[Checkpoint] player scrapped ({Deaths} deaths): reloading to the {(hasCheckpoint ? "last checkpoint" : "level start")}");
            RestartMission();
        }

        /// <summary>Destroys the old body (and any host still possessed) and spawns a fresh one with the saved loadout.</summary>
        protected void Respawn(MetalPlayerController controller)
        {
            if (controller == null) return;
            MetalBot oldBody = controller.Pawn;
            controller.UnPossess();
            if (oldBody != null) Destroy(oldBody.gameObject);
            if (controller.PlayerBody != null && controller.PlayerBody != oldBody) Destroy(controller.PlayerBody.gameObject);
            MetalBot newBody = RestartPlayer();
            StartCoroutine(GiveLoadoutNextFrame(newBody));
        }

        IEnumerator GiveLoadoutNextFrame(MetalBot body)
        {
            yield return null; // after the body's Start hands out its archetype loadout
            if (body == null) yield break;
            if (savedWeapons.Count > 0)
            {
                body.SetWeapons(savedWeapons, savedWeaponIndex);
                body.AddAmmoFraction(0.5f);
            }
            body.Grenades = Mathf.Max(savedGrenades, 2);
        }

        /// <summary>Ends the mission with the stats screen; only the first call counts.</summary>
        public void CompleteMission(string endBanner = null)
        {
            if (IsMissionComplete) return;
            IsMissionComplete = true;
            missionEndTime = Time.time;
            MetalAudio.Play2D("mission_complete", 1f);
            ShowBanner(string.IsNullOrEmpty(endBanner) ? "MISSION COMPLETE" : endBanner, 1000000f);
        }

        /// <summary>Reloads the current level.</summary>
        public void RestartMission() => LoadScene(SceneManager.GetActiveScene().name);

        /// <summary>Opens the title screen (MZ_Title), or reloads this level if there is none.</summary>
        public static void ReturnToTitle()
        {
            if (Application.CanStreamedLevelBeLoaded("MZ_Title")) LoadScene("MZ_Title");
            else LoadScene(SceneManager.GetActiveScene().name);
        }

        public static void LoadScene(string sceneName)
        {
            SetPaused(false);
            SceneManager.LoadScene(sceneName);
        }

        /// <summary>Pauses or resumes the game world (menus keep running on unscaled time).</summary>
        public static void SetPaused(bool paused)
        {
            IsPaused = paused;
            Time.timeScale = paused ? 0f : 1f;
            AudioListener.pause = paused;
        }

        public static bool IsPaused { get; private set; }
    }
}
