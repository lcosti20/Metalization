// Player input and the Hack Tether.
//
// Possession is an explicit ownership transfer: the player's body is left powered down where it stood (and
// can still be shot), the target robot's AI is destroyed, and this controller possesses the robot. When the
// link ends - by choice, timeout, or the host being destroyed - control returns to the player's body and the
// host reboots back onto its own side.
//
// Hijacking an enemy first opens the hack puzzle (MetalHackPuzzle), a fake Blueprint the player rewires while
// the world keeps running. A correct compile starts the hijack; a wrong one makes the target put up a firewall.
// Friendly robots built for it (parked Stalkers) take an open-ended link instead.
using System;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Metalization
{
    /// <summary>
    /// The player's controller: builds the input actions in code, routes them to whichever robot is possessed, runs
    /// the Hack Tether, finds nearby consoles, drives the camera, and owns the pause menu.
    /// </summary>
    [DefaultExecutionOrder(-100)]
    public class MetalPlayerController : MetalController
    {
        public static MetalPlayerController Instance { get; private set; }

        [Tooltip("Seconds a released hijack victim stays stunned (\"rebooting\") before its AI takes over again.")]
        public float rebootTime = 2f;
        [Tooltip("Enemies must be rewired in the hack puzzle before a hijack. Off: the tether hijacks instantly.")]
        public bool requireHackPuzzle = true;
        [Tooltip("Seconds an enemy can't be hacked after a failed compile.")]
        public float firewallTime = 4f;
        [Tooltip("Max distance for the Hack Tether, metres.")]
        public float tetherRange = 26f;
        [Tooltip("Degrees of look per pixel of mouse movement.")]
        public float mouseSensitivity = 0.15f;
        [Tooltip("Degrees per second at full right-stick deflection.")]
        public float stickLookSpeed = 300f;

        static readonly Color TetherColor = new Color(0.2f, 0.9f, 1f);

        public override bool IsPlayer => true;

        public MetalBot PlayerBody { get; private set; }
        public MetalBot Host { get; private set; }
        public MetalBot HackCandidate { get; private set; }
        public MetalBot FirewalledCandidate { get; private set; }
        public MetalHackPuzzle HackPuzzle { get; private set; }
        public MetalControlConsole NearbyConsole { get; private set; }
        public MetalPauseMenu PauseMenu { get; private set; }
        public MetalCameraRig CameraRig { get; private set; }
        public float PossessionDuration { get; private set; }
        public bool IsAllyLink { get; private set; }
        public float LastHitConfirmTime { get; private set; } = -100f;
        public float LastDamageTime { get; private set; } = -100f;
        /// <summary>True when the last input came from a gamepad (the HUD shows pad prompts).</summary>
        public bool UsingGamepad { get; private set; }

        int shotsFired, shotsHit;
        float possessionEndTime;
        bool switchingBodies;
        bool applySpawnPitch;
        bool sneakHeld, sneakToggle, hackCrouchActive, standAfterHack;
        MetalBot hackTarget;
        MetalFX tetherBeam;
        InputActionMap map;
        InputAction move, lookMouse, lookStick, jump, fire, aim, reload, grenade, tether, interact, sprint, sneak, sneakToggleAction,
            confirm, pause, cycleWeapon, nextWeapon, ascend, dodgeLeft, dodgeRight;
        readonly InputAction[] weaponKeys = new InputAction[6];

        public float Accuracy => shotsFired > 0 ? (float)shotsHit / shotsFired : -1f;
        public float PossessionTimeRemaining => Host != null ? Mathf.Max(0f, possessionEndTime - Time.time) : 0f;

        // ==================================================================================================
        // Setup
        // ==================================================================================================

        void Awake()
        {
            Instance = this;
            CameraRig = GetComponent<MetalCameraRig>();
            if (CameraRig == null) CameraRig = gameObject.AddComponent<MetalCameraRig>();
            CameraRig.Owner = this;
            CreateInputActions();
        }

        void OnEnable()
        {
            map?.Enable();
            MetalBot.AnyBotDied += OnBotDied;
        }

        void OnDisable()
        {
            map?.Disable();
            MetalBot.AnyBotDied -= OnBotDied;
        }

        void OnDestroy()
        {
            if (Instance == this) Instance = null;
            map?.Dispose();
        }

        void Start()
        {
            MetalAudio.ApplyVolumes();
            SetGameInput(true);
        }

        /// <summary>
        /// Builds every input action in code (no input assets), for keyboard/mouse and gamepad. Walking and flying share
        /// the actions; the handlers read the right ones for the possessed robot.
        /// </summary>
        void CreateInputActions()
        {
            map = new InputActionMap("Metalization");
            move = map.AddAction("Move", InputActionType.Value, expectedControlLayout: "Vector2");
            move.AddCompositeBinding("2DVector").With("Up", "<Keyboard>/w").With("Down", "<Keyboard>/s").With("Left", "<Keyboard>/a").With("Right", "<Keyboard>/d");
            move.AddBinding("<Gamepad>/leftStick");
            lookMouse = map.AddAction("LookMouse", InputActionType.PassThrough, "<Mouse>/delta");
            lookStick = map.AddAction("LookStick", InputActionType.Value, "<Gamepad>/rightStick");
            InputAction Button(string name, params string[] paths)
            {
                InputAction a = map.AddAction(name, InputActionType.Button);
                foreach (string p in paths) a.AddBinding(p);
                return a;
            }
            jump = Button("Jump", "<Keyboard>/space", "<Gamepad>/buttonSouth");
            fire = Button("Fire", "<Mouse>/leftButton", "<Gamepad>/rightTrigger");
            aim = Button("Aim", "<Mouse>/rightButton", "<Gamepad>/leftTrigger");
            reload = Button("Reload", "<Keyboard>/r", "<Gamepad>/buttonWest");
            grenade = Button("Grenade", "<Keyboard>/g", "<Gamepad>/rightShoulder");
            tether = Button("Tether", "<Keyboard>/f", "<Gamepad>/leftShoulder");
            interact = Button("Interact", "<Keyboard>/e", "<Gamepad>/buttonEast");
            sprint = Button("Sprint", "<Keyboard>/leftShift", "<Gamepad>/leftStickPress");
            sneak = Button("Sneak", "<Keyboard>/leftCtrl");
            sneakToggleAction = Button("SneakToggle", "<Keyboard>/c", "<Gamepad>/rightStickPress");
            confirm = Button("Confirm", "<Keyboard>/enter", "<Keyboard>/numpadEnter");
            pause = Button("Pause", "<Keyboard>/escape", "<Keyboard>/p", "<Gamepad>/start");
            cycleWeapon = map.AddAction("CycleWeapon", InputActionType.PassThrough, "<Mouse>/scroll/y");
            nextWeapon = Button("NextWeapon", "<Gamepad>/buttonNorth");
            for (int i = 0; i < 6; i++) weaponKeys[i] = Button("Weapon" + (i + 1), "<Keyboard>/" + (i + 1));
            // Flying (Stalkers): Space/Ctrl climb and dive instead of jumping, Shift boosts, Q/E dodge-roll.
            ascend = map.AddAction("Ascend", InputActionType.Value, expectedControlLayout: "Axis");
            ascend.AddCompositeBinding("1DAxis").With("Positive", "<Keyboard>/space").With("Negative", "<Keyboard>/leftCtrl");
            ascend.AddCompositeBinding("1DAxis").With("Positive", "<Gamepad>/buttonSouth").With("Negative", "<Gamepad>/buttonEast");
            dodgeLeft = Button("DodgeLeft", "<Keyboard>/q", "<Gamepad>/dpad/left");
            dodgeRight = Button("DodgeRight", "<Keyboard>/e", "<Gamepad>/dpad/right");
            map.Enable();
        }

        /// <summary>Locks the cursor for play, or frees it for a window (menu, hack puzzle).</summary>
        public void SetGameInput(bool game)
        {
            Cursor.lockState = game ? CursorLockMode.Locked : CursorLockMode.None;
            Cursor.visible = !game;
        }

        MetalBot Bot => Pawn;
        MetalStalker Flyer => Pawn as MetalStalker;

        bool CanAct()
        {
            MetalBot bot = Bot;
            return bot != null && bot.IsAlive && !bot.IsDormant && !bot.IsStunned;
        }

        protected override void OnPossess(MetalBot bot)
        {
            if (!switchingBodies)
            {
                // A fresh spawn (start of level or respawn) is always the player's own body.
                PlayerBody = bot;
                Host = null;
                sneakToggle = false;
                hackCrouchActive = false;
                applySpawnPitch = true;
            }
            CameraRig.SnapToTarget();
        }

        public override void GetViewPoint(out Vector3 location, out Quaternion rotation)
        {
            Camera cam = CameraRig.Camera;
            if (cam != null)
            {
                location = cam.transform.position;
                rotation = cam.transform.rotation;
                return;
            }
            base.GetViewPoint(out location, out rotation);
        }

        public void NotifyHitConfirmed() => LastHitConfirmTime = Time.time;
        public override void NotifyDamaged(MetalBot attacker) => LastDamageTime = Time.time;

        /// <summary>Counts one trigger pull for the end-of-mission accuracy.</summary>
        public void NotifyShotFired(bool hit)
        {
            shotsFired++;
            if (hit) shotsHit++;
        }

        // ==================================================================================================
        // Input
        // ==================================================================================================

        void Update()
        {
            ReadInput();
            TickTether();
        }

        void ReadInput()
        {
            if (Gamepad.current != null && Gamepad.current.wasUpdatedThisFrame && (Gamepad.current.leftStick.ReadValue().sqrMagnitude > 0.04f
                || Gamepad.current.rightStick.ReadValue().sqrMagnitude > 0.04f || AnyPadButton()))
            {
                UsingGamepad = true;
            }
            if ((Mouse.current != null && Mouse.current.delta.ReadValue().sqrMagnitude > 4f) || (Keyboard.current != null && Keyboard.current.anyKey.wasPressedThisFrame))
            {
                UsingGamepad = false;
            }
            // Windows (pause menu, hack puzzle) take the input while they are open.
            if (PauseMenu != null) return;
            if (pause.WasPressedThisFrame())
            {
                OpenPauseMenu();
                return;
            }
            if (HackPuzzle != null) return;

            MetalBot bot = Bot;
            MetalStalker flyer = Flyer;
            float dt = Time.deltaTime;

            // Look: scopes slow it down so small corrections are possible at high zoom.
            float sens = bot != null ? bot.LookSensitivityScale : 1f;
            Vector2 look = lookMouse.ReadValue<Vector2>() * mouseSensitivity + lookStick.ReadValue<Vector2>() * stickLookSpeed * dt;
            look *= sens;
            ControlYaw += look.x;
            ControlPitch = Mathf.Clamp(ControlPitch + look.y, flyer != null ? -85f : -70f, flyer != null ? 85f : 60f);

            if (bot == null) return;
            Vector2 axis = move.ReadValue<Vector2>();
            if (CanAct() && axis.sqrMagnitude > 0.0001f)
            {
                if (flyer != null)
                {
                    // Forward follows the full aim (pitch up and push forward to climb); strafing stays level.
                    flyer.AddMovementInput(ControlRotation * Vector3.forward, axis.y);
                    flyer.AddMovementInput(Quaternion.Euler(0f, ControlYaw, 0f) * Vector3.right, axis.x);
                }
                else
                {
                    Quaternion yaw = Quaternion.Euler(0f, ControlYaw, 0f);
                    bot.AddMovementInput(yaw * Vector3.forward, axis.y);
                    bot.AddMovementInput(yaw * Vector3.right, axis.x);
                }
            }

            if (flyer != null)
            {
                if (CanAct()) flyer.AddVerticalInput(ascend.ReadValue<float>());
                if (sprint.WasPressedThisFrame() && CanAct()) flyer.SetBoostHeld(true);
                if (sprint.WasReleasedThisFrame()) flyer.SetBoostHeld(false);
                if (dodgeLeft.WasPressedThisFrame() && CanAct()) flyer.Dodge(-1f);
                if (dodgeRight.WasPressedThisFrame() && CanAct()) flyer.Dodge(1f);
            }
            else
            {
                if (sprint.WasPressedThisFrame())
                {
                    bot.SetWantsSprint(true);
                    StandUpForAction();
                }
                if (sprint.WasReleasedThisFrame()) bot.SetWantsSprint(false);
                if (sneak.WasPressedThisFrame() && bot.CanSneak && CanAct())
                {
                    sneakHeld = true;
                    ApplySneakInput();
                }
                if (sneak.WasReleasedThisFrame() && sneakHeld)
                {
                    sneakHeld = false;
                    ApplySneakInput();
                }
                if (sneakToggleAction.WasPressedThisFrame() && bot.CanSneak && CanAct())
                {
                    // Crouched without Ctrl held, C always stands up; otherwise it flips the toggle.
                    sneakToggle = bot.IsSneaking && !sneakHeld ? false : !sneakToggle;
                    ApplySneakInput();
                }
                if (jump.WasPressedThisFrame() && CanAct() && StandUpForAction()) bot.Jump();
                if (interact.WasPressedThisFrame()) InteractPressed();
                if (grenade.WasPressedThisFrame() && CanAct() && StandUpForAction()) bot.ThrowGrenade();
                if (reload.WasPressedThisFrame() && CanAct()) bot.Reload();
                float scroll = cycleWeapon.ReadValue<float>();
                if (CanAct() && Mathf.Abs(scroll) > 0.01f) bot.CycleWeapon(scroll > 0f ? 1 : -1);
                if (nextWeapon.WasPressedThisFrame() && CanAct()) bot.CycleWeapon(1);
                for (int i = 0; i < weaponKeys.Length; i++)
                {
                    if (weaponKeys[i].WasPressedThisFrame() && CanAct()) bot.EquipWeapon(i);
                }
            }
            if (fire.WasPressedThisFrame() && CanAct() && StandUpForAction()) bot.StartFire();
            if (fire.WasReleasedThisFrame()) bot.StopFire();
            if (aim.WasPressedThisFrame()) AimPressed();
            if (aim.WasReleasedThisFrame()) AimReleased();
            if (tether.WasPressedThisFrame()) TetherPressed();
            if (confirm.WasPressedThisFrame()) ConfirmPressed();
        }

        static bool AnyPadButton()
        {
            Gamepad p = Gamepad.current;
            return p.buttonSouth.wasPressedThisFrame || p.buttonNorth.wasPressedThisFrame || p.buttonEast.wasPressedThisFrame
                || p.buttonWest.wasPressedThisFrame || p.startButton.wasPressedThisFrame || p.rightTrigger.wasPressedThisFrame;
        }

        void ApplySneakInput()
        {
            MetalBot bot = Bot;
            if (bot != null && bot.CanSneak && Flyer == null) bot.SetSneaking(sneakHeld || sneakToggle || bot.IsHackCrouching);
        }

        void LatchHeldSneak()
        {
            if (sneakHeld)
            {
                sneakHeld = false;
                sneakToggle = true;
            }
        }

        /// <summary>
        /// Called before an action a crouched robot can't do. With toggle sneak the press stands the robot up and the action
        /// goes ahead; while Ctrl is held, or while crouched for a hack, the robot stays down and the action is refused.
        /// </summary>
        bool StandUpForAction()
        {
            MetalBot bot = Bot;
            if (bot == null || !bot.IsSneaking) return true;
            if (sneakHeld || bot.IsHackCrouching) return false;
            sneakToggle = false;
            bot.SetSneaking(false);
            // Under a low ceiling the robot can't stand, and stays a console.
            return !bot.IsSneaking;
        }

        void BeginHackCrouch()
        {
            MetalBot body = PlayerBody;
            if (hackCrouchActive || body == null || !body.CanSneak || !body.IsAlive) return;
            hackCrouchActive = true;
            standAfterHack = !body.IsSneaking;
            LatchHeldSneak();
            body.SetHackCrouch(true);
            body.SetSneaking(true);
        }

        void EndHackCrouch()
        {
            if (!hackCrouchActive) return;
            hackCrouchActive = false;
            MetalBot body = PlayerBody;
            if (body == null) return;
            body.SetHackCrouch(false);
            if (standAfterHack && !sneakHeld && !sneakToggle && body.IsAlive && !body.IsDormant) body.SetSneaking(false);
        }

        /// <summary>Press the aim button: aims, or fires the robot's secondary weapon if it has one.</summary>
        public void AimPressed()
        {
            MetalBot bot = Bot;
            if (bot != null && bot.HasSecondaryWeapon)
            {
                if (CanAct() && StandUpForAction()) bot.StartSecondaryFire();
            }
            else if (bot != null && StandUpForAction())
            {
                bot.SetAiming(true);
            }
        }

        public void AimReleased()
        {
            MetalBot bot = Bot;
            if (bot == null) return;
            bot.SetAiming(false);
            bot.StopSecondaryFire();
        }

        /// <summary>Uses the nearby console, if any, and shows its result.</summary>
        public void InteractPressed()
        {
            MetalControlConsole console = NearbyConsole;
            MetalBot bot = Bot;
            if (console != null && bot != null && CanAct())
            {
                string result = console.TryActivate(bot);
                MetalGameMode.Current?.ShowMessage("", result, 3.5f);
            }
        }

        void ConfirmPressed()
        {
            MetalGameMode gm = MetalGameMode.Current;
            if (gm != null && gm.IsMissionComplete) gm.RestartMission();
        }

        // ==================================================================================================
        // Pause menu
        // ==================================================================================================

        /// <summary>Pauses the game and shows the pause menu.</summary>
        public void OpenPauseMenu()
        {
            if (PauseMenu != null) return;
            if (HackPuzzle != null) CloseHackPuzzle();
            // Button releases won't arrive while the menu has the input: let go of everything now.
            MetalBot bot = Bot;
            if (bot != null)
            {
                bot.StopFire();
                bot.SetAiming(false);
                bot.StopSecondaryFire();
                bot.SetWantsSprint(false);
            }
            LatchHeldSneak();
            if (Flyer != null) Flyer.SetBoostHeld(false);
            PauseMenu = gameObject.AddComponent<MetalPauseMenu>();
            PauseMenu.InitPause(this);
            MetalGameMode.SetPaused(true);
            SetGameInput(false);
        }

        public void ClosePauseMenu()
        {
            if (PauseMenu == null) return;
            Destroy(PauseMenu);
            PauseMenu = null;
            MetalGameMode.SetPaused(false);
            SetGameInput(true);
        }

        // ==================================================================================================
        // Hack Tether
        // ==================================================================================================

        /// <summary>
        /// What the Hack Tether would grab: the hackable robot in range closest to the centre of a 7-degree cone
        /// around the view, with a clear line from the player's eyes. Only from the player's own body.
        /// </summary>
        MetalBot FindHackCandidate(bool firewalled = false)
        {
            MetalBot body = PlayerBody;
            if (body == null || Pawn != body || !CanAct()) return null;
            GetViewPoint(out Vector3 viewLoc, out Quaternion viewRot);
            Vector3 viewDir = viewRot * Vector3.forward;
            MetalBot best = null;
            float bestDot = Mathf.Cos(7f * Mathf.Deg2Rad);
            foreach (MetalBot candidate in MetalBot.All)
            {
                bool wanted = firewalled ? candidate.IsFirewalled && candidate.CanBeHackedBy(body, true) : candidate.CanBeHackedBy(body);
                if (!wanted || Vector3.Distance(candidate.transform.position, body.transform.position) > tetherRange) continue;
                float dot = Vector3.Dot((candidate.transform.position - viewLoc).normalized, viewDir);
                if (dot <= bestDot) continue;
                // The tether reaches through energy fields, which stop shots.
                var ignore = new System.Collections.Generic.List<Component> { body, candidate };
                ignore.AddRange(MetalEnergyField.All);
                if (!MetalPhysics.Raycast(body.GetEyesLocation(), candidate.transform.position, out _, ignore.ToArray()))
                {
                    best = candidate;
                    bestDot = dot;
                }
            }
            return best;
        }

        /// <summary>Testing: why a robot is or isn't a Hack Tether candidate right now.</summary>
        public string DescribeHackCheck(MetalBot candidate)
        {
            MetalBot body = PlayerBody;
            if (candidate == null || body == null) return "no candidate or body";
            GetViewPoint(out Vector3 viewLoc, out Quaternion viewRot);
            float angle = Vector3.Angle(candidate.transform.position - viewLoc, viewRot * Vector3.forward);
            var ignore = new System.Collections.Generic.List<Component> { body, candidate };
            ignore.AddRange(MetalEnergyField.All);
            bool blocked = MetalPhysics.Raycast(body.GetEyesLocation(), candidate.transform.position, out RaycastHit hit, ignore.ToArray());
            return $"canAct={CanAct()} pawnIsBody={Pawn == body} hackable={candidate.CanBeHackedBy(body)} "
                + $"dist={Vector3.Distance(candidate.transform.position, body.transform.position):F1} angle={angle:F1} "
                + $"blocked={(blocked ? hit.collider.name : "no")}";
        }

        /// <summary>Hijack the robot under the crosshair, or disconnect from the current host.</summary>
        public void TetherPressed()
        {
            if (HackPuzzle != null) return;
            MetalBot target = Host != null ? null : FindHackCandidate();
            // The target the HUD showed last frame counts too: the prompt on screen should be what the button does.
            if (target == null && HackCandidate != null && PlayerBody != null && Pawn == PlayerBody && HackCandidate.CanBeHackedBy(PlayerBody)
                && Vector3.Distance(HackCandidate.transform.position, PlayerBody.transform.position) <= tetherRange)
            {
                target = HackCandidate;
            }
            MetalGameMode gm = MetalGameMode.Current;
            if (Host != null)
            {
                EndPossession();
            }
            else if (target != null)
            {
                // Enemies have to be rewired first; friendly links (parked flyers) connect straight away.
                if (requireHackPuzzle && PlayerBody != null && target.Team != PlayerBody.Team) OpenHackPuzzle(target);
                else BeginPossession(target);
            }
            else if (FindHackCandidate(true) is MetalBot walled)
            {
                gm?.ShowMessage("", $"FIREWALL: {walled.DisplayName} is blocking the Hack Tether ({Mathf.CeilToInt(walled.FirewallTimeRemaining)}s).", 1.5f);
                MetalAudio.Play2D("tether_fail", 0.6f);
            }
            else if (gm != null && Pawn == PlayerBody)
            {
                gm.ShowMessage("", "Hack Tether: no robot in range.", 1.5f);
                MetalAudio.Play2D("tether_fail", 0.6f);
            }
        }

        void OpenHackPuzzle(MetalBot target)
        {
            MetalBot body = PlayerBody;
            if (body == null || target == null || HackPuzzle != null) return;
            body.StopFire();
            body.SetAiming(false);
            body.StopSecondaryFire();
            body.SetWantsSprint(false);
            // Hacking is done tucked into the console disguise.
            BeginHackCrouch();
            // "BP_<Unit>Controller", from the robot's name with anything that isn't a letter or digit dropped.
            var unit = new System.Text.StringBuilder();
            foreach (char c in target.DisplayName)
            {
                if (char.IsLetterOrDigit(c)) unit.Append(c);
            }
            string blueprintName = $"BP_{(unit.Length == 0 ? "Unit" : unit.ToString())}Controller";
            hackTarget = target;
            HackPuzzle = gameObject.AddComponent<MetalHackPuzzle>();
            HackPuzzle.Init(blueprintName, OnHackPuzzleFinished);
            SetGameInput(false);
            // A faint beam while the upload runs; BeginPossession swaps it for the full one.
            tetherBeam = MetalFX.SpawnBeam(body.GetMuzzleLocation(), target.transform.position, TetherColor * 0.5f, 0.006f, 0f);
            MetalAudio.Play2D("radio_blip", 0.8f);
            Debug.Log($"[Hack] puzzle opened on {target.name} ({blueprintName})");
        }

        void CloseHackPuzzle()
        {
            if (HackPuzzle == null) return;
            Destroy(HackPuzzle);
            HackPuzzle = null;
            hackTarget = null;
            if (tetherBeam != null && Host == null)
            {
                Destroy(tetherBeam.gameObject);
                tetherBeam = null;
            }
            SetGameInput(true);
        }

        void OnHackPuzzleFinished(MetalHackResult result)
        {
            MetalBot target = hackTarget;
            MetalBot body = PlayerBody;
            string blueprintName = HackPuzzle != null ? HackPuzzle.BlueprintName : "";
            string wiring = HackPuzzle != null ? HackPuzzle.DescribeLinks() : "";
            CloseHackPuzzle();
            MetalGameMode gm = MetalGameMode.Current;
            switch (result)
            {
                case MetalHackResult.Success:
                    // Re-checked: the target may have been killed or hacked away meanwhile.
                    if (target != null && body != null && target.CanBeHackedBy(body))
                    {
                        Debug.Log($"[Hack] compiled OK: {blueprintName}");
                        MetalAudio.Play2D("console_granted", 0.6f);
                        BeginPossession(target);
                    }
                    break;
                case MetalHackResult.Failed:
                    Debug.Log($"[Hack] compile failed: {blueprintName}, firewall up for {firewallTime:0}s (wiring: {wiring})");
                    if (target != null)
                    {
                        target.RaiseFirewall(firewallTime);
                        MetalFX.SpawnBurst(target.transform.position, new Color(1f, 0.2f, 0.1f), 0.9f, 0.4f);
                    }
                    MetalAudio.Play2D("console_denied", 0.8f);
                    gm?.ShowMessage("", $"COMPILE ERROR in {blueprintName}: firewall up for {Mathf.RoundToInt(firewallTime)}s.", 2.5f);
                    break;
                case MetalHackResult.Cancelled:
                    MetalAudio.Play2D("tether_release", 0.6f);
                    break;
            }
        }

        /// <summary>
        /// Hands the player's control over to target: destroys its AI, powers the body down, possesses target and starts
        /// the hijack timer (or an open-ended link for an ally).
        /// </summary>
        public void BeginPossession(MetalBot target)
        {
            MetalBot body = PlayerBody;
            if (body == null || target == null) return;
            // The target's AI is destroyed rather than kept: when the link ends the host either gets a fresh one (enemy)
            // or idles unpossessed (ally), and an idle ally must have no controller to be linkable again.
            if (target.Controller != null)
            {
                MetalController oldBrain = target.Controller;
                oldBrain.UnPossess();
                Destroy(oldBrain);
            }
            bool ally = target.Team == body.Team;
            body.StopFire();
            body.SetAiming(false);
            body.StopSecondaryFire();
            // The body waits out the link crouched as a console (a dormant robot can't change stance, so do it first).
            BeginHackCrouch();
            body.SetDormant(true);
            target.SetDormant(false);
            if (!ally) target.SetHijacked(true);
            target.Movement.StopMovementImmediately();

            switchingBodies = true;
            Possess(target);
            switchingBodies = false;
            ControlYaw = target.transform.eulerAngles.y;
            ControlPitch = 0f;

            Host = target;
            IsAllyLink = ally;
            PossessionDuration = ally ? 0f : target.PossessionDuration;
            possessionEndTime = Time.time + PossessionDuration;
            Debug.Log($"[Metal] Hack Tether: linked to {target.name} for {PossessionDuration:0}s (ally={ally})");
            if (!ally)
            {
                // A friendly link can take the pilot far from their body, so only hijacks draw a beam.
                tetherBeam = MetalFX.SpawnBeam(body.GetMuzzleLocation(), target.transform.position, TetherColor, 0.015f, 0f);
            }
            MetalFX.SpawnBurst(target.transform.position, TetherColor, 0.9f, 0.4f);
            MetalAudio.Play2D("tether_link", 0.9f);
            MetalGameMode.Current?.ShowMessage("", ally ? $"LINKED: {target.DisplayName}.  [F] to disconnect"
                : $"HACK TETHER LINKED: {target.DisplayName}.  [F] to disconnect", 3f);
        }

        /// <summary>
        /// Returns control to the player's body. A hijacked enemy still alive goes back to its own side with a new AI after
        /// a short reboot stun; an ally link goes dormant.
        /// </summary>
        public void EndPossession()
        {
            MetalBot oldHost = Host;
            MetalBot body = PlayerBody;
            bool wasAlly = IsAllyLink;
            Host = null;
            IsAllyLink = false;
            if (tetherBeam != null)
            {
                Destroy(tetherBeam.gameObject);
                tetherBeam = null;
            }
            MetalAudio.Play2D("tether_release", 0.8f);
            if (oldHost != null)
            {
                oldHost.StopFire();
                oldHost.SetAiming(false);
                oldHost.StopSecondaryFire();
            }
            if (body != null && body.IsAlive)
            {
                switchingBodies = true;
                Possess(body);
                switchingBodies = false;
                body.SetDormant(false);
                ControlYaw = body.transform.eulerAngles.y;
                ControlPitch = 0f;
            }
            else if (oldHost != null && Pawn == oldHost)
            {
                UnPossess();
            }
            if (oldHost != null && oldHost.IsAlive)
            {
                if (wasAlly)
                {
                    // Back to idle: a Stalker coasts to a hover, ready to be linked again.
                    if (oldHost.Controller == this) UnPossess();
                    oldHost.SetDormant(true);
                }
                else
                {
                    // Letting go hands the robot back, intact: it reboots for a moment, then returns to its own side.
                    oldHost.SetHijacked(false);
                    oldHost.SetDormant(false);
                    oldHost.SpawnDefaultAI();
                    oldHost.Stun(rebootTime);
                }
            }
        }

        void OnBotDied(MetalBot bot, MetalController killer)
        {
            if (bot == null) return;
            if (bot == Host) EndPossession();
            if (bot == PlayerBody)
            {
                CloseHackPuzzle();
                if (Host != null) EndPossession();
                MetalGameMode.Current?.PlayerDied(this, bot);
            }
        }

        void TickTether()
        {
            if (Host != null)
            {
                if (tetherBeam != null && PlayerBody != null) tetherBeam.SetBeamEndpoints(PlayerBody.GetMuzzleLocation(), Host.transform.position);
                if (!IsAllyLink && Time.time >= possessionEndTime) EndPossession();
            }
            if (applySpawnPitch && Pawn != null)
            {
                // Start looking slightly down at the robot rather than level with the horizon.
                applySpawnPitch = false;
                ControlPitch = -12f;
            }
            // While the puzzle is up, the hack drops if the target is lost: killed, hacked, firewalled, or out of reach.
            if (HackPuzzle != null)
            {
                MetalBot target = hackTarget;
                MetalBot body = PlayerBody;
                if (target == null || body == null || Pawn != body || !target.CanBeHackedBy(body)
                    || Vector3.Distance(target.transform.position, body.transform.position) > tetherRange * 1.25f)
                {
                    CloseHackPuzzle();
                    MetalAudio.Play2D("tether_fail", 0.6f);
                    MetalGameMode.Current?.ShowMessage("", "Hack Tether: lost the target.", 1.5f);
                }
                else if (tetherBeam != null)
                {
                    tetherBeam.SetBeamEndpoints(body.GetMuzzleLocation(), target.transform.position);
                }
            }
            // The hack is over: put the body back how it was.
            if (hackCrouchActive && HackPuzzle == null && Host == null) EndHackCrouch();

            HackCandidate = HackPuzzle != null ? null : FindHackCandidate();
            FirewalledCandidate = HackPuzzle != null || HackCandidate != null ? null : FindHackCandidate(true);

            // Nearest unused console in reach; drives the HUD prompt and InteractPressed.
            NearbyConsole = null;
            if (Pawn != null)
            {
                float best = float.MaxValue;
                foreach (MetalControlConsole c in MetalControlConsole.All)
                {
                    float d = Vector3.Distance(c.transform.position, Pawn.transform.position);
                    if (d < c.interactRadius && d < best && !c.IsActivated)
                    {
                        NearbyConsole = c;
                        best = d;
                    }
                }
            }
        }

        // --- Dev autopilot hooks ----------------------------------------------------------------------------

        /// <summary>Dev autopilot: the open hack puzzle (rewire it with DevRewire, then Compile).</summary>
        public MetalHackPuzzle DevPuzzle => HackPuzzle;

        /// <summary>Dev autopilot: points the control rotation at a world point.</summary>
        public void DevLookAt(Vector3 point)
        {
            Camera cam = CameraRig.Camera;
            Vector3 from = cam != null ? cam.transform.position : (Pawn != null ? Pawn.GetEyesLocation() : transform.position);
            Vector3 d = point - from;
            if (d.sqrMagnitude < 1e-4f) return;
            ControlYaw = MetalMath.Yaw(d);
            ControlPitch = MetalMath.Pitch(d);
        }
    }
}
