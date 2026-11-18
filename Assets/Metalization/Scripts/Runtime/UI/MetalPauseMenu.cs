// Pause and title menu, built in code: Continue / Restart Mission / Level Select / Controls / Sound / Credits /
// Title Screen / Quit, driven by mouse, keyboard or gamepad with one shared selection.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.SceneManagement;

namespace Metalization
{
    public class MetalPauseMenu : MonoBehaviour
    {
        static readonly Color Amber = new Color(1f, 0.65f, 0.15f);
        static readonly Color Idle = new Color(0.08f, 0.09f, 0.11f, 0.92f);
        static readonly Color Dim = new Color(1f, 1f, 1f, 0.85f);

        enum PanelId { Main, Levels, Controls, Sound, Credits }

        class Item
        {
            public string label;
            public Action action;
            public bool slider;
            public MetalVolume group;
            public Rect rect;
        }

        /// <summary>Title screen: Start Mission instead of Continue/Restart, nothing to go back to.</summary>
        public bool TitleMode { get; private set; }

        MetalPlayerController owner;
        PanelId panel = PanelId.Main;
        readonly List<Item> items = new List<Item>();
        int selected;
        int lastSelected = -1;
        float nextNavTime;
        int draggingSlider = -1;
        readonly int[] sliderSteps = new int[3];

        static readonly string[,] Bindings =
        {
            { "WASD / Left stick", "Move" },
            { "Mouse / Right stick", "Look" },
            { "Shift / L3", "Sprint (hold)" },
            { "Ctrl (hold) / C / R3", "Crouch into console disguise" },
            { "Space / A", "Jump, again in the air to double jump" },
            { "Left click / RT", "Shoot" },
            { "Right click / LT", "Aim (scope with the Sniper); fires a hacked Brute's rockets" },
            { "1-6, wheel / Y", "Swap weapon arm" },
            { "R / X", "Reload" },
            { "G / RB", "Throw EMP grenade" },
            { "F / LB", "Hack Tether: take over a robot / let go" },
            { "Ctrl+S / Y", "Hack window: Save & Compile (drag wires with the mouse, or left stick + A)" },
            { "E / B", "Use console" },
            { "Esc, P / Start", "Menu" },
        };

        static readonly (string text, int size, bool heading)[] CreditLines =
        {
            ("Music", 22, true),
            ("\"Game Minecraft Gaming Background Music\"", 18, false),
            ("by MaksymMalko via Pixabay", 15, false),
            ("pixabay.com/users/maksymmalko-46459014", 14, false),
            ("", 8, false),
            ("Sound Effects", 22, true),
            ("Original, synthesized for Metalization", 15, false),
            ("", 8, false),
            ("Made with Unity", 15, false),
        };

        public void InitPause(MetalPlayerController controller)
        {
            owner = controller;
            TitleMode = false;
            ShowPanel(PanelId.Main);
        }

        public void InitTitle()
        {
            TitleMode = true;
            ShowPanel(PanelId.Main);
        }

        static MetalLevelSettings Levels => MetalGameAssets.Get() != null ? MetalGameAssets.Get().levels : null;

        void ShowPanel(PanelId p)
        {
            panel = p;
            items.Clear();
            selected = 0;
            lastSelected = -1;
            switch (p)
            {
                case PanelId.Main: BuildMain(); break;
                case PanelId.Levels: BuildLevels(); break;
                case PanelId.Sound:
                    AddSlider("Music / Background", MetalVolume.Music);
                    AddSlider("Sound Effects", MetalVolume.Effects);
                    AddSlider("Beep-boops", MetalVolume.Voices);
                    Add("Back", () => ShowPanel(PanelId.Main));
                    break;
                default:
                    Add("Back", () => ShowPanel(PanelId.Main));
                    break;
            }
        }

        void Add(string label, Action action) => items.Add(new Item { label = label, action = action });

        void AddSlider(string label, MetalVolume group)
        {
            items.Add(new Item { label = label, slider = true, group = group });
            sliderSteps[(int)group] = Mathf.RoundToInt(MetalAudio.GetVolume(group) * 20f);
        }

        void BuildMain()
        {
            MetalLevelSettings levels = Levels;
            if (TitleMode)
            {
                // The first level in the list is the featured mission.
                string first = levels != null && levels.levels.Count > 0 ? levels.levels[0].sceneName : null;
                Add("Start Mission", () => { if (!string.IsNullOrEmpty(first)) MetalGameMode.LoadScene(first); });
            }
            else
            {
                Add("Continue", () => owner?.ClosePauseMenu());
                Add("Restart Mission", () => MetalGameMode.LoadScene(SceneManager.GetActiveScene().name));
            }
            Add("Level Select", () => ShowPanel(PanelId.Levels));
            Add("Controls", () => ShowPanel(PanelId.Controls));
            Add("Sound", () => ShowPanel(PanelId.Sound));
            Add("Credits", () => ShowPanel(PanelId.Credits));
            if (!TitleMode && Application.CanStreamedLevelBeLoaded("MZ_Title")) Add("Title Screen", () => MetalGameMode.LoadScene("MZ_Title"));
            Add("Quit", Quit);
        }

        void BuildLevels()
        {
            MetalLevelSettings levels = Levels;
            string current = SceneManager.GetActiveScene().name;
            if (levels != null)
            {
                foreach (MetalLevelSettings.Entry e in levels.levels)
                {
                    if (string.IsNullOrEmpty(e.sceneName)) continue;
                    string scene = e.sceneName;
                    Add(scene == current ? e.displayName + "  (current)" : e.displayName, () =>
                    {
                        Debug.Log($"[Metal] Level Select: opening {scene}");
                        MetalGameMode.LoadScene(scene);
                    });
                }
            }
            Add("Back", () => ShowPanel(PanelId.Main));
        }

        static void Quit()
        {
#if UNITY_EDITOR
            UnityEditor.EditorApplication.isPlaying = false;
#else
            Application.Quit();
#endif
        }

        /// <summary>Goes back one panel, or closes the pause menu.</summary>
        public void Back()
        {
            if (panel != PanelId.Main) ShowPanel(PanelId.Main);
            else if (!TitleMode) owner?.ClosePauseMenu();
        }

        /// <summary>Presses the button with this label on the visible panel (dev autopilot).</summary>
        public bool PressButton(string label)
        {
            foreach (Item i in items)
            {
                if (!i.slider && i.label == label)
                {
                    Press(i);
                    return true;
                }
            }
            Debug.LogWarning($"[Metal] Pause menu: no \"{label}\" button on this panel");
            return false;
        }

        public bool HasButton(string label) => items.Exists(i => !i.slider && i.label == label);

        /// <summary>Back to the first panel.</summary>
        public void ShowMain() => ShowPanel(PanelId.Main);

        /// <summary>Moves a volume slider by delta (dev autopilot).</summary>
        public void StepVolume(MetalVolume group, float delta) => SetVolume(group, MetalAudio.GetVolume(group) + delta);

        void Press(Item item)
        {
            MetalAudio.Play2D("ui_select", 0.7f);
            item.action?.Invoke();
        }

        void SetVolume(MetalVolume group, float value)
        {
            value = Mathf.Clamp01(value);
            if (Mathf.Approximately(MetalAudio.GetVolume(group), value)) return;
            MetalAudio.SetVolume(group, value);
            // A tick per 5% step, which also previews the new effects volume.
            int step = Mathf.RoundToInt(value * 20f);
            if (sliderSteps[(int)group] != step)
            {
                sliderSteps[(int)group] = step;
                MetalAudio.Play2D("ui_move", 0.5f);
            }
        }

        // --- Input ----------------------------------------------------------------------------------------

        void Update()
        {
            Keyboard kb = Keyboard.current;
            Gamepad pad = Gamepad.current;
            Mouse mouse = Mouse.current;
            if (items.Count == 0) return;
            bool back = (kb != null && (kb.escapeKey.wasPressedThisFrame || kb.pKey.wasPressedThisFrame))
                || (pad != null && (pad.startButton.wasPressedThisFrame || pad.buttonEast.wasPressedThisFrame));
            if (back)
            {
                Back();
                return;
            }
            // Up/down (keys, d-pad or stick, repeating while held).
            float vertical = 0f, horizontal = 0f;
            if (kb != null)
            {
                if (kb.upArrowKey.isPressed || kb.wKey.isPressed) vertical += 1f;
                if (kb.downArrowKey.isPressed || kb.sKey.isPressed) vertical -= 1f;
                if (kb.leftArrowKey.isPressed || kb.aKey.isPressed) horizontal -= 1f;
                if (kb.rightArrowKey.isPressed || kb.dKey.isPressed) horizontal += 1f;
            }
            if (pad != null)
            {
                Vector2 s = pad.leftStick.ReadValue() + pad.dpad.ReadValue();
                if (Mathf.Abs(s.y) > 0.5f) vertical += Mathf.Sign(s.y);
                if (Mathf.Abs(s.x) > 0.5f) horizontal += Mathf.Sign(s.x);
            }
            float now = Time.unscaledTime;
            if (Mathf.Abs(vertical) < 0.5f && Mathf.Abs(horizontal) < 0.5f)
            {
                nextNavTime = 0f;
            }
            else if (now >= nextNavTime)
            {
                nextNavTime = now + (nextNavTime == 0f ? 0.35f : 0.12f);
                if (Mathf.Abs(vertical) >= 0.5f)
                {
                    selected = (selected + (vertical > 0f ? -1 : 1) + items.Count) % items.Count;
                }
                else if (items[selected].slider)
                {
                    // Left/right move the slider directly; up/down leave it.
                    MetalVolume g = items[selected].group;
                    SetVolume(g, MetalAudio.GetVolume(g) + 0.05f * Mathf.Sign(horizontal));
                }
            }
            bool confirm = (kb != null && (kb.enterKey.wasPressedThisFrame || kb.numpadEnterKey.wasPressedThisFrame || kb.spaceKey.wasPressedThisFrame))
                || (pad != null && pad.buttonSouth.wasPressedThisFrame);
            if (confirm && !items[selected].slider)
            {
                Press(items[selected]);
                return;
            }
            // Mouse: hovering selects, clicking presses, dragging moves a slider.
            if (mouse != null)
            {
                Vector2 p = MetalDraw.FromScreen(mouse.position.ReadValue());
                for (int i = 0; i < items.Count; i++)
                {
                    if (items[i].rect.Contains(p) && mouse.delta.ReadValue().sqrMagnitude > 0.01f) selected = i;
                }
                if (mouse.leftButton.wasPressedThisFrame)
                {
                    for (int i = 0; i < items.Count; i++)
                    {
                        if (!items[i].rect.Contains(p)) continue;
                        selected = i;
                        if (items[i].slider) draggingSlider = i;
                        else
                        {
                            Press(items[i]);
                            return;
                        }
                    }
                }
                if (mouse.leftButton.wasReleasedThisFrame) draggingSlider = -1;
                if (draggingSlider >= 0 && draggingSlider < items.Count)
                {
                    Rect bar = SliderBar(items[draggingSlider].rect);
                    SetVolume(items[draggingSlider].group, Mathf.Round((p.x - bar.x) / bar.width * 20f) / 20f);
                }
            }
            if (selected != lastSelected)
            {
                if (lastSelected >= 0) MetalAudio.Play2D("ui_move", 0.5f);
                lastSelected = selected;
            }
        }

        static Rect SliderBar(Rect row) => new Rect(row.x + 16f + 210f, row.center.y - 3f, row.width - 32f - 210f - 70f, 6f);

        // --- Drawing --------------------------------------------------------------------------------------

        void OnGUI()
        {
            GUI.depth = 0;
            MetalDraw.Begin();
            float W = MetalDraw.Width, H = MetalDraw.Height;
            // Dim the paused game behind the menu (only lightly over the title backdrop).
            MetalDraw.Rect(0f, 0f, W, H, new Color(0f, 0f, 0f, TitleMode ? 0.3f : 0.6f));
            const float ColumnW = 520f;
            float x = (W - ColumnW) * 0.5f;
            string title = panel switch
            {
                PanelId.Levels => "LEVEL SELECT",
                PanelId.Controls => "CONTROLS",
                PanelId.Sound => "SOUND",
                PanelId.Credits => "CREDITS",
                _ => TitleMode ? "METALIZATION\nProduction Alpha" : "PAUSED",
            };
            // Measure the column first so it can be centred vertically.
            float contentH = 0f;
            string[] titleLines = title.Split('\n');
            contentH += titleLines.Length * MetalDraw.Measure("Ag", 44, true).y + 28f;
            if (panel == PanelId.Controls) contentH += Bindings.GetLength(0) * (MetalDraw.Measure("Ag", 17).y + 4f) + 20f;
            if (panel == PanelId.Credits)
            {
                foreach (var c in CreditLines) contentH += MetalDraw.Measure("Ag", c.size).y + (c.heading ? 12f : 4f);
                contentH += 20f;
            }
            contentH += items.Count * (56f + 12f);
            float y = Mathf.Max(20f, (H - contentH) * 0.5f);

            foreach (string line in titleLines)
            {
                MetalDraw.Text(new Vector2(W * 0.5f, y), line, 44, Amber, TextAnchor.UpperCenter, true);
                y += MetalDraw.Measure("Ag", 44, true).y;
            }
            y += 28f;
            if (panel == PanelId.Controls)
            {
                for (int i = 0; i < Bindings.GetLength(0); i++)
                {
                    MetalDraw.Text(new Vector2(W * 0.5f, y), $"{Bindings[i, 0]}   -   {Bindings[i, 1]}", 17, Dim, TextAnchor.UpperCenter);
                    y += MetalDraw.Measure("Ag", 17).y + 4f;
                }
                y += 20f;
            }
            if (panel == PanelId.Credits)
            {
                foreach (var c in CreditLines)
                {
                    y += c.heading ? 6f : 2f;
                    MetalDraw.Text(new Vector2(W * 0.5f, y), c.text, c.size, c.heading ? Amber : Dim, TextAnchor.UpperCenter, c.heading);
                    y += MetalDraw.Measure("Ag", c.size).y + (c.heading ? 6f : 2f);
                }
                y += 20f;
            }
            for (int i = 0; i < items.Count; i++)
            {
                Item item = items[i];
                y += 6f;
                item.rect = new Rect(x, y, ColumnW, 56f);
                bool sel = i == selected;
                if (item.slider)
                {
                    MetalDraw.Rect(item.rect, sel ? Amber.WithAlpha(0.35f) : Idle);
                    MetalDraw.Text(new Vector2(x + 16f, item.rect.center.y), item.label, 20, sel ? Color.white : Dim, TextAnchor.MiddleLeft);
                    float v = MetalAudio.GetVolume(item.group);
                    Rect bar = SliderBar(item.rect);
                    MetalDraw.Rect(bar, new Color(1f, 1f, 1f, 0.35f));
                    MetalDraw.Rect(bar.x, bar.y, bar.width * v, bar.height, Amber.WithAlpha(0.8f));
                    MetalDraw.Disc(new Vector2(bar.x + bar.width * v, bar.center.y), 10f, Amber);
                    MetalDraw.Text(new Vector2(item.rect.xMax - 16f, item.rect.center.y), $"{Mathf.RoundToInt(v * 100f)}%", 20, Dim, TextAnchor.MiddleRight);
                }
                else
                {
                    MetalDraw.Rect(item.rect, sel ? Amber : Idle);
                    MetalDraw.Text(item.rect.center, item.label, 26, sel ? new Color(0.05f, 0.05f, 0.05f) : Dim, TextAnchor.MiddleCenter);
                }
                y += 56f + 6f;
            }
        }
    }
}
