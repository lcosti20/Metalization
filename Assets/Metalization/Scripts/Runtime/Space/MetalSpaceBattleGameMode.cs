// Space battle rules: reinforcement tickets per team, lost to deaths, destroyed subsystems and a failing life
// support; the side that runs out loses. Players spawn aboard their ship once its interior has loaded.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    public class MetalSpaceBattleGameMode : MetalGameMode
    {
        public MetalTeam playerTeam = MetalTeam.Heroes;
        [Tooltip("Spawn players anyway if the interiors haven't loaded after this many seconds.")]
        public float interiorLoadTimeout = 10f;
        public int startingTickets = 150;
        public int subsystemTicketPenalty = 20;
        public int lifeSupportTicketPenalty = 30;
        [Tooltip("Seconds between ticket losses once a ship's life support is down.")]
        public float lifeSupportBleedInterval = 4f;

        readonly Dictionary<MetalTeam, int> tickets = new Dictionary<MetalTeam, int>();
        readonly Dictionary<MetalTeam, float> nextBleedTime = new Dictionary<MetalTeam, float>();
        bool battleOver;
        bool playerPending;

        public MetalTeam Winner { get; private set; }

        protected override void Awake()
        {
            // Players wait for the ships' interiors before spawning.
            spawnPlayer = false;
            resetLevelOnDeath = false;
            base.Awake();
            playerPending = true;
        }

        protected override void Start()
        {
            base.Start();
            tickets[MetalTeam.Heroes] = startingTickets;
            tickets[MetalTeam.Villains] = startingTickets;
            MetalCapitalShip.AnyInteriorReady += OnInteriorReady;
            MetalCapitalShip.AnySubsystemDestroyed += OnSubsystemDestroyed;
            MetalCapitalShip.AnyShipDestroyed += OnShipDestroyed;
            MetalBot.AnyBotDied += OnCombatantDied;
        }

        protected override void OnDestroy()
        {
            base.OnDestroy();
            MetalCapitalShip.AnyInteriorReady -= OnInteriorReady;
            MetalCapitalShip.AnySubsystemDestroyed -= OnSubsystemDestroyed;
            MetalCapitalShip.AnyShipDestroyed -= OnShipDestroyed;
            MetalBot.AnyBotDied -= OnCombatantDied;
        }

        public int GetTickets(MetalTeam team) => tickets.TryGetValue(team, out int t) ? t : 0;

        public MetalCapitalShip GetShip(MetalTeam team)
        {
            foreach (MetalCapitalShip s in MetalCapitalShip.All)
            {
                if (s.Team == team) return s;
            }
            return null;
        }

        bool AreInteriorsReady()
        {
            foreach (MetalCapitalShip s in MetalCapitalShip.All)
            {
                if (s.shipData != null && !string.IsNullOrEmpty(s.shipData.interiorScene) && !s.IsInteriorReady) return false;
            }
            return true;
        }

        void OnInteriorReady(MetalCapitalShip ship)
        {
            if (AreInteriorsReady()) StartPendingPlayer();
        }

        void StartPendingPlayer()
        {
            if (!playerPending) return;
            playerPending = false;
            SpawnPlayer();
        }

        protected override bool ChooseSpawnPoint(out Vector3 position, out float yaw)
        {
            Transform start = ChooseStart();
            if (start == null) return base.ChooseSpawnPoint(out position, out yaw);
            position = start.position;
            yaw = start.eulerAngles.y;
            return true;
        }

        /// <summary>A random start aboard our ship, preferring one nobody is standing on.</summary>
        Transform ChooseStart()
        {
            MetalCapitalShip ship = GetShip(playerTeam);
            if (ship == null) return null;
            List<Transform> starts = ship.GetSpawnPoints();
            for (int i = 0; i < starts.Count; i++)
            {
                int j = Random.Range(i, starts.Count);
                (starts[i], starts[j]) = (starts[j], starts[i]);
            }
            foreach (Transform s in starts)
            {
                bool occupied = false;
                foreach (MetalBot b in MetalBot.All) occupied |= (b.transform.position - s.position).sqrMagnitude < 1.5f * 1.5f;
                if (!occupied) return s;
            }
            return starts.Count > 0 ? starts[0] : null;
        }

        void SpendTickets(MetalTeam team, int amount)
        {
            if (battleOver || !tickets.ContainsKey(team) || amount <= 0) return;
            tickets[team] = Mathf.Max(0, tickets[team] - amount);
            if (tickets[team] == 0) EndBattle(team == MetalTeam.Heroes ? MetalTeam.Villains : MetalTeam.Heroes);
        }

        void OnCombatantDied(MetalBot bot, MetalController killer)
        {
            if (bot == null) return;
            // A Stalker only costs a ticket if its pilot dies with it: an AI pilot. Players wake up in their body,
            // and empty parked ones are just hardware.
            MetalController pilot = bot.Controller;
            bool combatant = bot is MetalStalker ? pilot != null && !pilot.IsPlayer : pilot != null;
            if (combatant) SpendTickets(bot.GetBaseTeam(), 1);
        }

        void OnSubsystemDestroyed(MetalCapitalShip ship, MetalShipSubsystem s)
        {
            bool lifeSupport = s.type == MetalShipSubsystemType.LifeSupport;
            bool ours = ship.Team == playerTeam;
            string what = s.type == MetalShipSubsystemType.ShieldGenerator ? "shield generator" : s.type == MetalShipSubsystemType.Engines ? "engines"
                : s.type == MetalShipSubsystemType.Comms ? "comms array" : "life support";
            ShowMessage("", ours ? $"Our {what} is down!" : $"Enemy {what} destroyed!", 3f);
            if (lifeSupport) nextBleedTime[ship.Team] = Time.time + lifeSupportBleedInterval;
            SpendTickets(ship.Team, lifeSupport ? lifeSupportTicketPenalty : subsystemTicketPenalty);
        }

        void OnShipDestroyed(MetalCapitalShip ship) => SpendTickets(ship.Team, GetTickets(ship.Team));

        void EndBattle(MetalTeam winner)
        {
            if (battleOver) return;
            battleOver = true;
            Winner = winner;
            Debug.Log($"[Metal] Battle over: {winner} wins (tickets {GetTickets(MetalTeam.Heroes)} / {GetTickets(MetalTeam.Villains)})");
            CompleteMission(winner == playerTeam ? "VICTORY" : "DEFEAT");
        }

        public override void PlayerDied(MetalPlayerController controller, MetalBot body)
        {
            if (battleOver) return;
            base.PlayerDied(controller, body);
        }

        protected override void Update()
        {
            base.Update();
            float now = Time.time;
            foreach (MetalTeam team in new List<MetalTeam>(nextBleedTime.Keys))
            {
                if (now >= nextBleedTime[team])
                {
                    nextBleedTime[team] = now + lifeSupportBleedInterval;
                    SpendTickets(team, 1);
                }
            }
            if (playerPending && Time.timeSinceLevelLoad > interiorLoadTimeout)
            {
                Debug.LogError($"[Metal] Ship interiors still not loaded after {interiorLoadTimeout:0} s; spawning the player anyway.");
                StartPendingPlayer();
            }
        }
    }
}
