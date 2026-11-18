// Development helper for automated smoke tests and screenshots. Options come from the command line (player
// builds) or the METALIZATION_ARGS environment variable (Play Mode in the editor). Positions use the Unreal
// version's coordinates (centimetres, X forward / Y right / Z up) so the smoke-test setups carry over; they are
// converted with UE.Vec, which is also how the level builders place everything.
//
//   -MetalTeleport=X,Y,Z,Yaw     move the player on start
//   -MetalCam=X,Y,Z,Pitch,Yaw    view from a fixed camera instead of the player ("player" = the game camera;
//                                separate several with ";" to use one camera per screenshot)
//   -MetalShots=2,5.5,9          take screenshots at these times (Screenshots/ next to the project or player)
//   -MetalAutopilot=<scenario>   run a scripted smoke test (see Scenarios below)
//   -MetalMenuLevel=Arena        Level Select entry the "menu" scenario picks
//   -MetalLureAt=X,Y,Z           where AlertNearest first puts the enemy
//   -MetalObjective=N            start the mission at objective N
//   -MetalQuitAt=12              exit (timed from each level load)
//   -MetalLevel=MZ_Arena         load this scene first (player builds start on the title screen)
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace Metalization
{
    [DefaultExecutionOrder(-90)]
    public class MetalDevAutopilot : MonoBehaviour
    {
        enum Step
        {
            AimEnemy, AimHackable, AimBoss, AimGateGuard, Fire, StopFire, Tether, GoToConsole, Interact, Run, Stop, Jump, TurnLeft, TurnRight,
            AimLinkable, Climb, StopClimb, Boost, StopBoost, DodgeLeft, DodgeRight, AimEnemyGun, AimEnemyShield, AimHomeMouth,
            GoNearEnemyMouth, AimEnemyMouth, AimEnemyLifeSupport, WreckEnemyShip,
            OpenPauseMenu, MenuLevelSelect, MenuBack, MenuPickLevel, MenuContinue,
            MenuSound, MenuCredits, MusicDown, MusicUp, VoicesDown, VoicesUp,
            Sprint, StopSprint, Sneak, StopSneak, Aim, StopAim, NextWeapon, AimMarksman, Grenade, AimBunker, ClearSquad1,
            SolveHack, FailHack, CancelHack, CountRobots, ScrapNearestEnemy, SelfDestruct, GoToCheckpoint, DropToLowHealth, HealFully,
            Reload, GoToPickup, ScrapCommandYard, ScrapBoss, ScrapYardPatrol, AimButton, StopAimButton, LureToLift, ReportHeights, AlertNearest,
            GiveSniper, ClearAllButBoss, FxGallery,
        }

        static (float t, Step s)[] S(params (float, Step)[] steps) => steps;

        /// <summary>Scripted smoke tests of the core loops (Tools/AssetPipeline/smoke_test.ps1 launches them).</summary>
        static readonly Dictionary<string, (float t, Step s)[]> Scenarios = new Dictionary<string, (float, Step)[]>
        {
            ["combat"] = S((1.5f, Step.AimEnemy), (1.6f, Step.Fire), (4.5f, Step.StopFire), (5.0f, Step.AimHackable), (5.6f, Step.Tether), (6.0f, Step.SolveHack),
                (7.0f, Step.AimEnemy), (7.1f, Step.Fire), (10.0f, Step.StopFire), (10.5f, Step.Tether)),
            ["gate"] = S((1.0f, Step.AimGateGuard), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (2.5f, Step.GoToConsole), (3.2f, Step.Interact), (7.5f, Step.Tether)),
            ["boss"] = S((1.0f, Step.AimBoss), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (2.5f, Step.AimEnemy), (2.6f, Step.Fire), (2.7f, Step.AimButton),
                (8.0f, Step.StopFire), (8.1f, Step.StopAimButton)),
            ["arena"] = S((1.0f, Step.Run), (2.0f, Step.Jump), (2.35f, Step.Jump), (3.6f, Step.TurnLeft), (4.4f, Step.Jump), (5.4f, Step.TurnRight), (6.4f, Step.Stop),
                (7.0f, Step.AimEnemy), (7.2f, Step.Fire), (8.5f, Step.StopFire)),
            ["flight"] = S((1.0f, Step.AimLinkable), (1.6f, Step.Tether), (2.4f, Step.Climb), (3.0f, Step.StopClimb), (3.0f, Step.Run), (4.4f, Step.Boost),
                (5.2f, Step.StopBoost), (6.0f, Step.Stop), (7.4f, Step.AimEnemy), (7.6f, Step.AimEnemyGun), (7.7f, Step.Fire), (9.4f, Step.AimEnemy),
                (11.0f, Step.StopFire), (11.2f, Step.DodgeLeft), (12.2f, Step.DodgeRight), (13.2f, Step.Tether)),
            ["hangar"] = S((0.5f, Step.Run), (10.5f, Step.Stop), (10.7f, Step.AimLinkable), (11.3f, Step.Tether), (12.2f, Step.Climb), (12.6f, Step.StopClimb),
                (12.7f, Step.AimHomeMouth), (13.4f, Step.Run), (15.6f, Step.Boost), (16.8f, Step.StopBoost), (17.6f, Step.Stop), (18.0f, Step.AimEnemyShield),
                (18.2f, Step.Fire), (22.0f, Step.StopFire), (22.2f, Step.AimHomeMouth), (22.8f, Step.Run), (29.5f, Step.Stop), (30.5f, Step.Tether)),
            ["battle"] = S((0.5f, Step.Run), (10.5f, Step.Stop), (10.7f, Step.AimLinkable), (11.3f, Step.Tether), (12.2f, Step.Climb), (12.6f, Step.StopClimb),
                (12.7f, Step.AimHomeMouth), (13.4f, Step.Run), (16.0f, Step.Stop), (16.5f, Step.GoNearEnemyMouth), (16.7f, Step.AimEnemyMouth), (16.9f, Step.Run),
                (23.5f, Step.Stop), (24.2f, Step.AimEnemyLifeSupport), (24.5f, Step.Fire), (30.5f, Step.StopFire), (31.0f, Step.WreckEnemyShip)),
            ["menu"] = S((0.8f, Step.OpenPauseMenu), (1.6f, Step.MenuLevelSelect), (2.4f, Step.MenuBack), (3.2f, Step.MenuLevelSelect), (3.6f, Step.MenuPickLevel),
                (4.4f, Step.MenuContinue)),
            ["sound"] = S((0.8f, Step.OpenPauseMenu), (1.4f, Step.MenuSound), (2.0f, Step.MusicDown), (2.8f, Step.MusicUp), (3.0f, Step.VoicesDown), (3.4f, Step.VoicesUp),
                (3.6f, Step.MenuBack), (4.0f, Step.MenuCredits), (5.0f, Step.MenuBack), (5.4f, Step.MenuContinue)),
            ["dogfight"] = S(),
            ["ships"] = S(),
            ["alpha"] = S((0.8f, Step.Run), (1.0f, Step.Sprint), (6.5f, Step.StopSprint), (7.5f, Step.Jump), (9.0f, Step.Stop)),
            ["swap"] = S((3.0f, Step.NextWeapon), (5.0f, Step.NextWeapon), (7.0f, Step.NextWeapon)),
            ["reload"] = S((2.0f, Step.Fire), (2.6f, Step.StopFire), (2.7f, Step.Reload), (5.0f, Step.Fire), (5.5f, Step.StopFire), (5.6f, Step.Reload)),
            ["draw"] = S((1.0f, Step.AimGateGuard), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (3.0f, Step.GiveSniper), (4.5f, Step.NextWeapon), (6.0f, Step.NextWeapon)),
            ["liftai"] = S((0.5f, Step.LureToLift), (3.0f, Step.ReportHeights), (6.0f, Step.ReportHeights), (9.0f, Step.ReportHeights), (12.0f, Step.ReportHeights)),
            ["drop"] = S((0.3f, Step.AlertNearest), (2.0f, Step.ReportHeights), (4.0f, Step.ReportHeights), (6.0f, Step.ReportHeights), (8.0f, Step.ReportHeights),
                (11.5f, Step.ReportHeights)),
            ["hop"] = S((0.3f, Step.AlertNearest), (1.5f, Step.ReportHeights), (3.5f, Step.ReportHeights), (6.0f, Step.ReportHeights), (8.0f, Step.ReportHeights)),
            ["ditch"] = S((0.3f, Step.AlertNearest), (1.5f, Step.ReportHeights), (3.5f, Step.ReportHeights), (6.0f, Step.ReportHeights), (8.0f, Step.ReportHeights)),
            ["lift"] = S((0.5f, Step.Run), (1.6f, Step.Stop)),
            ["sneak"] = S((0.5f, Step.Sneak), (1.0f, Step.Run), (14.0f, Step.Stop), (14.5f, Step.StopSneak)),
            ["sniper"] = S((1.0f, Step.AimMarksman), (1.5f, Step.Aim), (3.0f, Step.Fire), (3.1f, Step.StopFire), (5.0f, Step.Fire), (5.1f, Step.StopFire), (6.0f, Step.StopAim)),
            ["hack"] = S((1.0f, Step.AimGateGuard), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (2.5f, Step.GoToConsole), (3.2f, Step.Interact), (7.5f, Step.Tether)),
            ["field"] = S((1.0f, Step.AimGateGuard), (1.3f, Step.Fire), (3.0f, Step.StopFire), (6.5f, Step.AimGateGuard), (7.0f, Step.Tether), (7.6f, Step.SolveHack)),
            ["hackfail"] = S((1.0f, Step.AimGateGuard), (1.6f, Step.Tether), (2.2f, Step.FailHack), (2.8f, Step.Tether), (6.6f, Step.AimGateGuard), (7.0f, Step.Tether),
                (7.4f, Step.CancelHack), (7.6f, Step.AimGateGuard), (8.0f, Step.Tether), (8.6f, Step.SolveHack), (11.0f, Step.Tether)),
            ["bridge"] = S((0.5f, Step.Run), (0.82f, Step.Jump), (1.25f, Step.Jump), (2.6f, Step.Stop)),
            ["bunker"] = S((0.1f, Step.ClearSquad1), (0.6f, Step.AimBunker), (1.0f, Step.Grenade), (1.8f, Step.AimBunker), (2.2f, Step.Grenade), (3.0f, Step.AimBunker),
                (3.4f, Step.Grenade), (6.0f, Step.AimEnemy), (6.2f, Step.Fire), (11.0f, Step.StopFire)),
            ["jetpack"] = S((1.0f, Step.Jump), (1.3f, Step.Jump), (3.0f, Step.Jump), (3.3f, Step.Jump)),
            ["lowhealth"] = S((0.5f, Step.DropToLowHealth), (3.6f, Step.HealFully)),
            ["fxboss"] = S((0.1f, Step.ClearAllButBoss), (1.0f, Step.AimBoss), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (2.6f, Step.Fire), (2.7f, Step.AimButton),
                (8.0f, Step.StopFire), (8.1f, Step.StopAimButton)),
            ["fx"] = S((0.1f, Step.ClearAllButBoss), (1.0f, Step.FxGallery), (2.5f, Step.FxGallery), (4.0f, Step.FxGallery)),
            ["title"] = S(),
            ["checkpoint"] = S((0.3f, Step.ScrapNearestEnemy), (0.6f, Step.GoToCheckpoint), (1.0f, Step.CountRobots), (1.5f, Step.ScrapNearestEnemy), (2.0f, Step.CountRobots),
                (2.5f, Step.SelfDestruct)),
            ["checkpoint_restored"] = S((0.5f, Step.CountRobots)),
            ["dispenser"] = S((0.3f, Step.ScrapNearestEnemy), (0.5f, Step.CountRobots), (4.6f, Step.CountRobots), (5.0f, Step.GoToConsole), (5.5f, Step.Interact),
                (9.0f, Step.GoToConsole), (9.5f, Step.Interact), (10.0f, Step.CountRobots), (13.5f, Step.CountRobots)),
            ["yard"] = S((0.3f, Step.ScrapNearestEnemy), (0.5f, Step.CountRobots), (5.0f, Step.CountRobots), (5.5f, Step.GoToConsole), (6.0f, Step.Interact), (7.0f, Step.CountRobots)),
            ["command"] = S((1.0f, Step.AimGateGuard), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (2.5f, Step.GoToConsole), (3.2f, Step.Interact), (13.0f, Step.GoToConsole),
                (13.5f, Step.Interact), (14.0f, Step.CountRobots)),
            ["endgame"] = S((1.0f, Step.AimGateGuard), (1.6f, Step.Tether), (2.0f, Step.SolveHack), (2.5f, Step.GoToConsole), (3.2f, Step.Interact), (7.5f, Step.Tether),
                (8.0f, Step.ScrapCommandYard), (8.5f, Step.CountRobots), (11.5f, Step.GoToConsole), (12.0f, Step.Interact), (12.5f, Step.CountRobots), (13.0f, Step.SelfDestruct)),
            ["endgame_restored"] = S((0.5f, Step.CountRobots), (1.0f, Step.ScrapCommandYard), (1.5f, Step.CountRobots), (2.0f, Step.ScrapBoss), (2.5f, Step.CountRobots)),
            ["yardcheckpoint"] = S((0.3f, Step.ScrapYardPatrol), (0.5f, Step.CountRobots), (5.0f, Step.CountRobots), (5.2f, Step.GoToCheckpoint), (5.6f, Step.SelfDestruct)),
            ["yardcheckpoint_restored"] = S((0.5f, Step.CountRobots), (5.0f, Step.CountRobots)),
            ["pickups"] = S((0.5f, Step.Run), (1.4f, Step.Stop), (4.0f, Step.Fire), (4.15f, Step.StopFire), (4.3f, Step.Reload), (6.5f, Step.GoToPickup), (7.3f, Step.GoToPickup)),
        };

        // --- Options ----------------------------------------------------------------------------------------

        static string[] args;
        static bool levelJumped;

        static string[] Args
        {
            get
            {
                if (args != null) return args;
                var list = new List<string>(Environment.GetCommandLineArgs());
                string env = Environment.GetEnvironmentVariable("METALIZATION_ARGS");
                if (!string.IsNullOrEmpty(env)) list.AddRange(env.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries));
                args = list.ToArray();
                return args;
            }
        }

        static string GetArg(string prefix)
        {
            foreach (string a in Args)
            {
                if (a.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return a.Substring(prefix.Length);
            }
            return null;
        }

        static bool HasFlag(string flag)
        {
            foreach (string a in Args)
            {
                if (string.Equals(a, flag, StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        public static int GetIntArg(string prefix, int fallback)
        {
            string v = GetArg(prefix);
            return v != null && int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out int i) ? i : fallback;
        }

        static float[] Floats(string csv)
        {
            var list = new List<float>();
            foreach (string p in csv.Split(','))
            {
                if (float.TryParse(p.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out float f)) list.Add(f);
            }
            return list.ToArray();
        }

        /// <summary>
        /// Editor testing: replaces the options (e.g. "-MetalAutopilot=combat -MetalTeleport=2300,0,120,0") and starts an
        /// autopilot in the running scene. Call from Play Mode.
        /// </summary>
        public static void Run(string options)
        {
            args = new List<string>(Environment.GetCommandLineArgs()).Concat(options.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries)).ToArray();
            foreach (MetalDevAutopilot old in FindObjectsByType<MetalDevAutopilot>()) Destroy(old.gameObject);
            new GameObject("Metal Dev Autopilot").AddComponent<MetalDevAutopilot>();
        }

        /// <summary>Spawns the autopilot if any -Metal* testing option was given. Called by the game modes.</summary>
        public static void SpawnIfRequested()
        {
            // A player build starts on the title screen: -MetalLevel=<scene> jumps straight to the level under test (once).
            string level = GetArg("-MetalLevel=");
            if (!levelJumped && !string.IsNullOrEmpty(level) && SceneManager.GetActiveScene().name != level)
            {
                levelJumped = true;
                SceneManager.LoadScene(level);
                return;
            }
            levelJumped = true;
            if (FindAnyObjectByType<MetalDevAutopilot>() != null) return;
            if (HasFlag("-MetalAutopilot") || GetArg("-MetalAutopilot=") != null || GetArg("-MetalShots=") != null || GetArg("-MetalTeleport=") != null
                || GetArg("-MetalCam=") != null || GetArg("-MetalQuitAt=") != null)
            {
                new GameObject("Metal Dev Autopilot").AddComponent<MetalDevAutopilot>();
            }
        }

        // --- State ------------------------------------------------------------------------------------------

        readonly List<float> shotTimes = new List<float>();
        readonly List<(Vector3 pos, float pitch, float yaw, bool player)> cameras = new List<(Vector3, float, float, bool)>();
        float cameraSwitchTime = -1f;
        float quitAt = -1f;
        float startTime;
        float nextLogTime;
        bool autopilot;
        bool restored;
        bool running;
        bool started;
        bool climbing;
        string scenario = "combat";
        int nextStep;
        int shotIndex;
        static int shotNumber;
        MetalBot aimTarget;
        string aimBone;
        Transform aimPoint;
        MetalBot luredEnemy;
        MetalFX fxBeam;
        Camera fixedCamera;

        void Start()
        {
            startTime = Time.unscaledTime;
            string sc = GetArg("-MetalAutopilot=");
            autopilot = HasFlag("-MetalAutopilot") || sc != null;
            if (sc != null) scenario = sc;
            if (autopilot && !Scenarios.ContainsKey(scenario)) scenario = "combat";
            // After a death reloads the level from a checkpoint, run the scenario's follow-up (or nothing).
            if (autopilot && MetalGameMode.Current != null && MetalGameMode.Current.IsRestoredFromCheckpoint)
            {
                restored = true;
                scenario = Scenarios.ContainsKey(scenario + "_restored") ? scenario + "_restored" : "title";
            }
            string shots = GetArg("-MetalShots=");
            if (shots != null) shotTimes.AddRange(Floats(shots));
            string quit = GetArg("-MetalQuitAt=");
            if (quit != null && float.TryParse(quit, NumberStyles.Float, CultureInfo.InvariantCulture, out float q)) quitAt = q;
            string cams = GetArg("-MetalCam=");
            if (cams != null)
            {
                foreach (string entry in cams.Split(';'))
                {
                    float[] v = Floats(entry);
                    if (v.Length >= 5) cameras.Add((UE.Vec(v[0], v[1], v[2]), v[3], v[4], false));
                    else if (entry.Trim() == "player") cameras.Add((Vector3.zero, 0f, 0f, true));
                }
            }
            Debug.Log($"[Autopilot] active in {SceneManager.GetActiveScene().name} (scenario={(autopilot ? scenario : "none")} shots={shotTimes.Count} quit={quitAt:0.0})");
        }

        static MetalPlayerController PC => MetalPlayerController.Instance;

        void Begin()
        {
            started = true;
            startTime = Time.unscaledTime;
            string tp = GetArg("-MetalTeleport=");
            MetalBot me = PC != null ? PC.Pawn : null;
            if (!restored && tp != null && me != null)
            {
                float[] v = Floats(tp);
                if (v.Length >= 4)
                {
                    me.TeleportTo(UE.Vec(v[0], v[1], v[2]), v[3]);
                    PC.ControlYaw = v[3];
                    PC.ControlPitch = 0f;
                    PC.CameraRig.SnapToTarget();
                }
            }
            ViewFromCamera(0);
        }

        void ViewFromCamera(int index)
        {
            if (index < 0 || index >= cameras.Count) return;
            var c = cameras[index];
            if (c.player)
            {
                if (fixedCamera != null) fixedCamera.enabled = false;
                if (PC != null && PC.CameraRig.Camera != null) PC.CameraRig.Camera.enabled = true;
                return;
            }
            if (fixedCamera == null)
            {
                fixedCamera = new GameObject("Autopilot Camera").AddComponent<Camera>();
                fixedCamera.depth = 50f;
                fixedCamera.farClipPlane = 50000f;
            }
            fixedCamera.enabled = true;
            fixedCamera.transform.SetPositionAndRotation(c.pos, Quaternion.Euler(-c.pitch, c.yaw, 0f));
            fixedCamera.fieldOfView = MetalCameraRig.HorizontalToVerticalFov(90f, fixedCamera.aspect);
        }

        MetalBot FindNearest(bool hackableOnly, string tag)
        {
            MetalBot me = PC != null ? PC.Pawn : null;
            MetalBot best = null;
            float bestDist = float.MaxValue;
            if (me == null) return null;
            foreach (MetalBot b in MetalBot.All)
            {
                if (!b.IsAlive || !me.IsHostileTo(b) || (hackableOnly && !b.CanBeHackedBy(me)) || (!string.IsNullOrEmpty(tag) && !b.HasTag(tag))) continue;
                float d = Vector3.Distance(b.transform.position, me.transform.position);
                if (d < bestDist)
                {
                    best = b;
                    bestDist = d;
                }
            }
            return best;
        }

        MetalBot FindNearestLinkable()
        {
            MetalBot me = PC != null ? PC.PlayerBody : null;
            MetalBot best = null;
            float bestDist = float.MaxValue;
            if (me == null) return null;
            foreach (MetalBot b in MetalBot.All)
            {
                if (b.Team != me.Team || !b.CanBeHackedBy(me)) continue;
                float d = Vector3.Distance(b.transform.position, me.transform.position);
                if (d < bestDist)
                {
                    best = b;
                    bestDist = d;
                }
            }
            return best;
        }

        static void Scrap(MetalBot bot, MetalController instigator)
        {
            if (bot != null) bot.TakeDamage(new MetalDamageEvent { amount = 1e6f, hitPoint = bot.transform.position, instigator = instigator });
        }

        void RunStep(int index)
        {
            MetalPlayerController pc = PC;
            MetalBot me = pc != null ? pc.Pawn : null;
            if (me == null) return;
            Step action = Scenarios[scenario][index].s;
            aimBone = null;
            aimPoint = null;
            MetalCapitalShip enemyShip = null, homeShip = null;
            foreach (MetalCapitalShip s in MetalCapitalShip.All)
            {
                if (s.Team != me.Team) enemyShip = s;
                else homeShip = s;
            }
            Debug.Log($"[Autopilot] step {index}: {action}");
            switch (action)
            {
                case Step.AimEnemy: aimTarget = FindNearest(false, null); break;
                case Step.AimHackable: aimTarget = FindNearest(true, null); break;
                case Step.AimBoss: aimTarget = FindNearest(true, "Boss"); break;
                case Step.AimGateGuard: aimTarget = FindNearest(true, "GateGuard"); break;
                case Step.Fire: me.StartFire(); break;
                case Step.StopFire: me.StopFire(); aimTarget = null; break;
                case Step.Tether:
                    Debug.Log($"[Autopilot]    tether: aim target={(aimTarget != null ? aimTarget.name : "none")} candidate={(pc.HackCandidate != null ? pc.HackCandidate.name : "none")}");
                    if (aimTarget != null && pc.HackCandidate == null) Debug.Log("[Autopilot]    tether check: " + pc.DescribeHackCheck(aimTarget));
                    pc.TetherPressed();
                    aimTarget = null;
                    if (pc.HackPuzzle != null) Debug.Log($"[Autopilot]    hack puzzle open: {pc.HackPuzzle.BlueprintName} ({pc.HackPuzzle.DescribeLinks()})");
                    break;
                case Step.SolveHack:
                case Step.FailHack:
                    if (pc.HackPuzzle != null)
                    {
                        pc.HackPuzzle.DevRewire(action == Step.SolveHack);
                        Debug.Log($"[Autopilot]    rewired: {pc.HackPuzzle.DescribeLinks()} (solved={pc.HackPuzzle.IsSolved()})");
                        pc.HackPuzzle.Compile();
                    }
                    else Debug.LogWarning("[Autopilot]    no hack puzzle open");
                    break;
                case Step.CancelHack: if (pc.HackPuzzle != null) pc.HackPuzzle.Cancel(); break;
                case Step.Interact: pc.InteractPressed(); break;
                case Step.Run: running = true; break;
                case Step.Stop: running = false; break;
                case Step.Jump: me.Jump(); break;
                case Step.TurnLeft: pc.ControlYaw -= 90f; break;
                case Step.TurnRight: pc.ControlYaw += 90f; break;
                case Step.AimLinkable: aimTarget = FindNearestLinkable(); break;
                case Step.AimEnemyShield:
                    if (enemyShip != null)
                    {
                        foreach (MetalShipSubsystem p in enemyShip.Subsystems)
                        {
                            if (p.type == MetalShipSubsystemType.ShieldGenerator && !p.IsDestroyed)
                            {
                                aimPoint = p.transform;
                                break;
                            }
                        }
                    }
                    break;
                case Step.AimHomeMouth:
                    if (homeShip != null && homeShip.InnerPortal != null)
                    {
                        MetalPortal inner = homeShip.InnerPortal;
                        MetalPortal mouth = inner.GetSideDistance(me.transform.position) > 0f && Vector3.Distance(inner.transform.position, me.transform.position) < 500f
                            ? inner : homeShip.OuterPortal;
                        aimPoint = mouth != null ? mouth.transform : null;
                    }
                    break;
                case Step.Sprint: me.SetWantsSprint(true); break;
                case Step.StopSprint: me.SetWantsSprint(false); break;
                case Step.Sneak: me.SetSneaking(true); break;
                case Step.StopSneak: me.SetSneaking(false); break;
                case Step.Aim: me.SetAiming(true); break;
                case Step.StopAim: me.SetAiming(false); break;
                case Step.AimButton: pc.AimPressed(); break;
                case Step.StopAimButton: pc.AimReleased(); break;
                case Step.LureToLift:
                {
                    MetalGravLift lift = null;
                    foreach (MetalGravLift l in MetalGravLift.All)
                    {
                        if (lift == null || Vector3.Distance(l.transform.position, me.transform.position) < Vector3.Distance(lift.transform.position, me.transform.position)) lift = l;
                    }
                    MetalBot enemy = FindNearest(false, null);
                    if (lift != null && enemy != null && enemy.Controller is MetalBotAI ai)
                    {
                        // 7 m back from the pad, on the side away from the exit, so it has to walk in and ride up.
                        Vector3 back = -MetalMath.Flat(lift.transform.TransformDirection(lift.exitDirection)).normalized;
                        Vector3 spot = lift.transform.position + back * 7f + Vector3.up * (enemy.Movement.FullHalfHeight + 0.2f);
                        enemy.TeleportTo(spot, enemy.transform.eulerAngles.y);
                        luredEnemy = enemy;
                        ai.Alert(me);
                        Debug.Log($"[Autopilot]    lured {enemy.name} to {lift.name} at {spot}");
                    }
                    break;
                }
                case Step.AlertNearest:
                {
                    MetalBot enemy = luredEnemy != null ? luredEnemy : FindNearest(false, null);
                    if (enemy == null) break;
                    string lure = GetArg("-MetalLureAt=");
                    if (luredEnemy == null && lure != null)
                    {
                        float[] v = Floats(lure);
                        if (v.Length == 3)
                        {
                            Vector3 at = UE.Vec(v[0], v[1], v[2]);
                            enemy.TeleportTo(at, enemy.transform.eulerAngles.y);
                            // The player's allies there would just shoot it: send them back to the start camp.
                            int moved = 0;
                            foreach (MetalBot b in MetalBot.All)
                            {
                                if (b != me && b.IsAlive && !me.IsHostileTo(b) && Vector3.Distance(b.transform.position, at) < 30f)
                                {
                                    b.TeleportTo(UE.Vec(-400f, -1000f + 250f * moved++, 150f), b.transform.eulerAngles.y);
                                }
                            }
                        }
                    }
                    luredEnemy = enemy;
                    if (enemy.Controller is MetalBotAI ai2) ai2.Alert(me);
                    bool reach = MetalNav.CanReach(enemy.transform.position, me.transform.position, out UnityEngine.AI.NavMeshPath p);
                    Debug.Log($"[Autopilot]    alerted {enemy.name} at {enemy.transform.position}: path complete={reach} corners={p.corners.Length}");
                    break;
                }
                case Step.ReportHeights:
                    if (luredEnemy != null) Debug.Log($"[Autopilot]    {luredEnemy.name} at {luredEnemy.transform.position} (player at {me.transform.position})");
                    break;
                case Step.NextWeapon: me.CycleWeapon(1); break;
                case Step.AimMarksman: aimTarget = FindNearest(false, "Marksmen"); break;
                case Step.Grenade: me.ThrowGrenade(); break;
                case Step.AimBunker:
                    // Their heads show over the wall: aim there, as a player would.
                    aimTarget = FindNearest(false, "Bunker");
                    aimBone = aimTarget != null ? aimTarget.Rig.head : null;
                    break;
                case Step.CountRobots:
                {
                    int standing = 0;
                    foreach (MetalBot b in MetalBot.All) standing += b.IsAlive && me.IsHostileTo(b) ? 1 : 0;
                    string group = "";
                    MetalGameMode gm = MetalGameMode.Current;
                    if (gm != null && gm.Mission != null)
                    {
                        foreach (MetalObjectiveStatus st in gm.Mission.GetObjectiveList())
                        {
                            group += $" {(st.state == MetalObjectiveState.Done ? '+' : st.state == MetalObjectiveState.Locked ? '-' : '>')}{st.index}";
                        }
                    }
                    Debug.Log($"[Autopilot]    hostile robots standing: {standing} (objective {(gm != null && gm.Mission != null ? gm.Mission.CurrentObjectiveIndex : -1)} [{group} ], "
                        + $"mission {(gm != null && gm.IsMissionComplete ? "complete" : "running")}, at {me.transform.position})");
                    break;
                }
                case Step.ScrapCommandYard:
                case Step.ScrapBoss:
                case Step.ScrapYardPatrol:
                {
                    string tag = action == Step.ScrapBoss ? "Boss" : action == Step.ScrapYardPatrol ? "Yard" : "Command";
                    int scrapped = 0;
                    foreach (MetalBot b in new List<MetalBot>(MetalBot.All))
                    {
                        if (!b.HasTag(tag) || !b.IsAlive || b.IsHijacked) continue;
                        Scrap(b, pc);
                        if (++scrapped == 1 && action == Step.ScrapYardPatrol) break;
                    }
                    Debug.Log($"[Autopilot]    scrapped {scrapped} robots tagged {tag}");
                    break;
                }
                case Step.ScrapNearestEnemy:
                {
                    MetalBot enemy = FindNearest(false, null);
                    if (enemy != null)
                    {
                        Debug.Log($"[Autopilot]    scrapping {enemy.name}");
                        Scrap(enemy, pc);
                    }
                    break;
                }
                case Step.GoToCheckpoint:
                {
                    MetalTriggerZone best = null;
                    foreach (MetalTriggerZone z in MetalTriggerZone.All)
                    {
                        if (z.isCheckpoint && !z.IsCheckpointUsed && (best == null
                            || Vector3.Distance(z.transform.position, me.transform.position) < Vector3.Distance(best.transform.position, me.transform.position)))
                        {
                            best = z;
                        }
                    }
                    if (best != null) me.TeleportTo(best.transform.TransformPoint(best.Box.center), me.transform.eulerAngles.y);
                    break;
                }
                case Step.Reload: me.Reload(); break;
                case Step.GiveSniper:
                {
                    MetalWeaponData sniper = null;
                    foreach (MetalWeaponData w in Resources.FindObjectsOfTypeAll<MetalWeaponData>())
                    {
                        if (w.name == "WD_EnemySniper") sniper = w;
                    }
                    if (sniper != null) me.GiveWeapon(sniper, true);
                    break;
                }
                case Step.GoToPickup:
                {
                    MetalPickup best = null;
                    foreach (MetalPickup p in FindObjectsByType<MetalPickup>())
                    {
                        if (best == null || Vector3.Distance(p.transform.position, me.transform.position) < Vector3.Distance(best.transform.position, me.transform.position)) best = p;
                    }
                    if (best != null)
                    {
                        Debug.Log($"[Autopilot] going to pickup {best.name}");
                        me.TeleportTo(best.transform.position + Vector3.up * 0.3f, me.transform.eulerAngles.y);
                    }
                    break;
                }
                case Step.DropToLowHealth: me.RestoreHealth(me.MaxHealth * 0.2f); break;
                case Step.HealFully: me.Heal(me.MaxHealth); break;
                case Step.SelfDestruct: Scrap(me, null); break;
                case Step.ClearAllButBoss:
                    foreach (MetalBot b in new List<MetalBot>(MetalBot.All))
                    {
                        if (b != me && b.IsAlive && me.IsHostileTo(b) && !b.HasTag("Boss")) Scrap(b, null);
                    }
                    break;
                case Step.ClearSquad1:
                    foreach (MetalBot b in new List<MetalBot>(MetalBot.All))
                    {
                        if (b.HasTag("Squad1")) Scrap(b, null);
                    }
                    break;
                case Step.FxGallery:
                {
                    // Left to right across the view, 7 m out: muzzle flash, impact, explosion, burst, tracer and beam.
                    pc.GetViewPoint(out _, out Quaternion viewRot);
                    Quaternion flat = Quaternion.Euler(0f, viewRot.eulerAngles.y, 0f);
                    Vector3 fwd = flat * Vector3.forward;
                    Vector3 right = flat * Vector3.right;
                    Vector3 floor = me.transform.position + fwd * 7f;
                    if (MetalPhysics.Raycast(floor + Vector3.up * 3f, floor - Vector3.up * 6f, out RaycastHit hit)) floor = hit.point;
                    var laser = new Color(1f, 0.35f, 0.1f);
                    var cyan = new Color(0.1f, 0.8f, 1f);
                    MetalFX.SpawnMuzzleFlash(floor - right * 3.3f + Vector3.up * 1.2f, right, laser, 0.14f);
                    MetalFX.SpawnImpact(floor - right * 1.7f, Vector3.up, cyan, 0.1f);
                    MetalFX.SpawnExplosion(floor + right * 0.6f + Vector3.up * 2.3f, new Color(1f, 0.5f, 0.15f), 1.1f);
                    MetalFX.SpawnBurst(floor + right * 2.2f + Vector3.up * 0.8f, new Color(0.2f, 1f, 0.3f), 0.6f, 0.4f);
                    MetalFX.SpawnBeam(floor - right * 4f + Vector3.up * 2.6f, floor + right * 4f + Vector3.up * 2.6f, laser, 0.025f, 0.07f);
                    if (fxBeam == null) fxBeam = MetalFX.SpawnBeam(floor - right * 4f + Vector3.up * 3.3f, floor + right * 4f + Vector3.up * 3.6f, cyan, 0.015f, 0f);
                    break;
                }
                case Step.OpenPauseMenu: pc.OpenPauseMenu(); break;
                case Step.MenuLevelSelect: pc.PauseMenu?.PressButton("Level Select"); break;
                case Step.MenuBack: pc.PauseMenu?.PressButton("Back"); break;
                case Step.MenuSound: pc.PauseMenu?.PressButton("Sound"); break;
                case Step.MenuCredits: pc.PauseMenu?.PressButton("Credits"); break;
                case Step.MusicDown: pc.PauseMenu?.StepVolume(MetalVolume.Music, -0.3f); break;
                case Step.MusicUp: pc.PauseMenu?.StepVolume(MetalVolume.Music, 0.3f); break;
                case Step.VoicesDown: pc.PauseMenu?.StepVolume(MetalVolume.Voices, -0.3f); break;
                case Step.VoicesUp: pc.PauseMenu?.StepVolume(MetalVolume.Voices, 0.3f); break;
                case Step.MenuContinue:
                    if (pc.PauseMenu != null)
                    {
                        pc.PauseMenu.ShowMain();
                        pc.PauseMenu.PressButton("Continue");
                    }
                    break;
                case Step.MenuPickLevel:
                    if (pc.PauseMenu != null)
                    {
                        string level = GetArg("-MetalMenuLevel=") ?? "Arena";
                        // Already here (the scenario's second run): don't travel again.
                        if (pc.PauseMenu.HasButton(level + "  (current)")) Debug.Log($"[Autopilot] menu: already in {level}");
                        else pc.PauseMenu.PressButton(level);
                    }
                    break;
                case Step.GoNearEnemyMouth:
                    if (enemyShip != null && enemyShip.OuterPortal != null)
                    {
                        // 250 m straight out from the enemy's hull mouth, facing it.
                        Vector3 outDir = enemyShip.OuterPortal.transform.forward;
                        me.TeleportTo(enemyShip.OuterPortal.transform.position + outDir * 250f, MetalMath.Yaw(-outDir));
                        pc.ControlYaw = MetalMath.Yaw(-outDir);
                        pc.ControlPitch = MetalMath.Pitch(-outDir);
                    }
                    break;
                case Step.AimEnemyMouth: aimPoint = enemyShip != null && enemyShip.OuterPortal != null ? enemyShip.OuterPortal.transform : null; break;
                case Step.AimEnemyLifeSupport: aimPoint = enemyShip != null && enemyShip.Hangar != null ? enemyShip.Hangar.LifeSupport.transform : null; break;
                case Step.WreckEnemyShip:
                    if (enemyShip != null)
                    {
                        foreach (MetalShipSubsystem p in new List<MetalShipSubsystem>(enemyShip.Subsystems))
                        {
                            enemyShip.ApplySubsystemDamage(p, new MetalDamageEvent { amount = 1e7f, instigator = pc, causer = me.gameObject });
                        }
                        enemyShip.TakeDamage(new MetalDamageEvent { amount = 1e8f, instigator = pc, causer = me.gameObject });
                    }
                    break;
                case Step.AimEnemyGun:
                    aimTarget = FindNearest(false, null);
                    aimBone = aimTarget != null && aimTarget.archetype != null && aimTarget.archetype.muzzleBones.Count > 0 ? aimTarget.archetype.muzzleBones[0] : null;
                    break;
                case Step.Climb: climbing = true; break;
                case Step.StopClimb: climbing = false; break;
                case Step.Boost: if (me is MetalStalker f1) f1.SetBoostHeld(true); break;
                case Step.StopBoost: if (me is MetalStalker f2) f2.SetBoostHeld(false); break;
                case Step.DodgeLeft: if (me is MetalStalker f3) f3.Dodge(-1f); break;
                case Step.DodgeRight: if (me is MetalStalker f4) f4.Dodge(1f); break;
                case Step.GoToConsole:
                {
                    // The nearest console not used yet.
                    MetalControlConsole best = null;
                    foreach (MetalControlConsole c in MetalControlConsole.All)
                    {
                        if (!c.IsActivated && (best == null || (c.transform.position - me.transform.position).sqrMagnitude < (best.transform.position - me.transform.position).sqrMagnitude))
                        {
                            best = c;
                        }
                    }
                    if (best != null)
                    {
                        me.TeleportTo(best.transform.position + best.transform.forward * 1.2f + Vector3.up * (me.Movement.FullHalfHeight + 0.1f), best.transform.eulerAngles.y + 180f);
                    }
                    break;
                }
            }
        }

        void Update()
        {
            MetalPlayerController pc = PC;
            if (!started)
            {
                // The title screen has no player at all.
                if ((pc != null && pc.Pawn != null) || FindAnyObjectByType<MetalTitleScreen>() != null || Time.unscaledTime - startTime > 15f) Begin();
                return;
            }
            // Unscaled time: keeps running while the pause menu is up.
            float t = Time.unscaledTime - startTime;
            if (autopilot && Scenarios.TryGetValue(scenario, out var steps))
            {
                while (nextStep < steps.Length && t >= steps[nextStep].t) RunStep(nextStep++);
                if (pc != null)
                {
                    if (aimPoint != null)
                    {
                        MetalShipSubsystem sub = aimPoint.GetComponent<MetalShipSubsystem>();
                        pc.DevLookAt(sub != null ? sub.Center : aimPoint.position);
                    }
                    else if (aimTarget != null)
                    {
                        Transform bone = !string.IsNullOrEmpty(aimBone) ? aimTarget.GetBone(aimBone) : null;
                        pc.DevLookAt(bone != null ? bone.position : aimTarget.transform.position);
                    }
                    MetalBot pawn = pc.Pawn;
                    if (pawn != null && running) pawn.AddMovementInput(Quaternion.Euler(0f, pc.ControlYaw, 0f) * Vector3.forward);
                    if (pawn is MetalStalker flyer && climbing) flyer.AddVerticalInput(1f);
                }
            }
            if (shotIndex < shotTimes.Count && t >= shotTimes[shotIndex])
            {
                // Numbered for the whole process, so a scenario that changes level doesn't overwrite its first shots.
                string dir = Path.Combine(Application.isEditor ? Path.GetDirectoryName(Application.dataPath) : Application.persistentDataPath, "Screenshots");
                Directory.CreateDirectory(dir);
                string file = Path.Combine(dir, $"MetalShot_{shotNumber:00}.png");
                ScreenCapture.CaptureScreenshot(file);
                Debug.Log($"[Autopilot] screenshot {shotNumber++} at {t:0.0}s -> {file}");
                shotIndex++;
                cameraSwitchTime = t + 0.3f;
            }
            if (cameraSwitchTime > 0f && t >= cameraSwitchTime)
            {
                cameraSwitchTime = -1f;
                ViewFromCamera(shotIndex);
            }
            if (t >= nextLogTime)
            {
                nextLogTime = t + 1f;
                LogState(t);
            }
            if (quitAt > 0f && t >= quitAt)
            {
                quitAt = -1f;
#if UNITY_EDITOR
                UnityEditor.EditorApplication.isPlaying = false;
#else
                Application.Quit();
#endif
            }
        }

        void LogState(float t)
        {
            MetalBot me = PC != null ? PC.Pawn : null;
            if (me != null)
            {
                MetalWeaponSlot slot = me.CurrentWeapon;
                Debug.Log($"[Autopilot] t={t:0.0} pawn={me.name} hp={me.Health:0} loc={me.transform.position} hijacked={me.IsHijacked} clip={(slot != null ? slot.clip : -1)} "
                    + $"weapon={(slot != null && slot.data != null ? slot.data.displayName : "-")} sprint={me.IsSprinting} sneak={me.IsSneaking} swap={me.SwapProgress:0.00} scope={me.ScopeAlpha:0.00}");
                if (me is MetalStalker flyer)
                {
                    Debug.Log($"[Autopilot]    flight speed={flyer.Velocity.magnitude:0.0} boost={flyer.BoostFraction:0.00} boosting={flyer.IsBoosting} guns={flyer.WorkingMuzzleCount}");
                }
            }
            MetalSpaceBattleGameMode battle = MetalGameMode.Current as MetalSpaceBattleGameMode;
            foreach (MetalCapitalShip ship in MetalCapitalShip.All)
            {
                string parts = "";
                foreach (MetalShipSubsystem p in ship.Subsystems) parts += $" {p.name}={p.Health:0}";
                Debug.Log($"[Autopilot]    {ship.name} team={ship.Team} tickets={(battle != null ? battle.GetTickets(ship.Team) : -1)} hull={ship.HullHealth:0} shielded={ship.IsShielded}{parts}");
            }
            foreach (MetalBot b in MetalBot.All)
            {
                if (b.Controller is MetalBotAI ai)
                {
                    Debug.Log($"[Autopilot]    {b.name} hp={b.Health:0} state={ai.State} suspicion={ai.Suspicion:0.00} speed={MetalMath.Flat(b.Velocity).magnitude:0.0} loc={b.transform.position}");
                }
                else if (b.Controller is MetalFlyerAI pilot)
                {
                    Debug.Log($"[Autopilot]    {b.name} team={b.Team} hp={b.Health:0} speed={b.Velocity.magnitude:0} loc={b.transform.position} {pilot.DescribeState()}");
                }
            }
        }
    }
}
