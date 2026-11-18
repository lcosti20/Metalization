// The hack puzzle: a joke "Blueprint editor" window the player rewires to finish a Hack Tether hijack.
//
// Hacking an enemy opens a window with a tab named BP_<Unit>Controller and a tiny graph:
//
//   [ Event: On Target ] -> [ Get Value: team ] -> [ Switch Team ]
//   [           target ] -> [ target     team ] -> [ team    red ] -> [ Call Ceasefire ]
//                                                  [        blue ] -> [ Call Shoot ]
//
// The nodes sit in a fixed layout and can't be moved. The player rewires pins, either by dragging a wire or by
// clicking one pin and then the pin to join it to (easier on a gamepad). The hack succeeds when the graph is the
// original with red and blue swapped. After the first edit a "Save & Compile" button appears; it (or Ctrl+S)
// ends the puzzle. The [X] cancels. The world keeps running meanwhile. Gamepads drive a virtual cursor with the
// left stick (A pick/drop a wire, X break links, Y compile, B cancel).
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Metalization
{
    /// <summary>How a hack puzzle ended.</summary>
    public enum MetalHackResult
    {
        /// <summary>Compiled with the right wiring: the hijack goes ahead.</summary>
        Success,
        /// <summary>Compiled with the wrong wiring: the target puts up a firewall.</summary>
        Failed,
        /// <summary>Closed with [X] (or Escape / B), or abandoned because the target was lost.</summary>
        Cancelled,
    }

    public enum MetalHackPinType
    {
        Exec,
        Object,
        Team,
    }

    public class MetalHackPuzzle : MonoBehaviour
    {
        struct Node
        {
            public string title;
            public Color header;
            public Vector2 position;
            public float width;
            public int rows;
        }

        struct Pin
        {
            public int node;
            public bool output;
            public MetalHackPinType type;
            public string label;
            public int row;
        }

        // Window layout, in window units (1 unit = 1 px at 1080p).
        const float WinW = 1120f, WinH = 660f, TabBarH = 38f, StatusH = 30f;
        const float HeaderH = 28f, RowH = 26f, BodyPad = 6f, PinInset = 14f, PinHitRadius = 13f, ClickSlop = 6f;

        const int NEvent = 0, NGet = 1, NSwitch = 2, NCeasefire = 3, NShoot = 4;
        const int PEventExec = 0, PEventTarget = 1, PGetTarget = 2, PGetTeam = 3, PSwitchExec = 4, PSwitchTeam = 5,
            PSwitchRed = 6, PSwitchBlue = 7, PCeasefire = 8, PShoot = 9;

        /// <summary>The winning wiring: the original graph with the red and blue outputs swapped.</summary>
        static readonly Vector2Int[] Solution =
        {
            new Vector2Int(PEventExec, PSwitchExec), new Vector2Int(PEventTarget, PGetTarget), new Vector2Int(PGetTeam, PSwitchTeam),
            new Vector2Int(PSwitchRed, PShoot), new Vector2Int(PSwitchBlue, PCeasefire),
        };

        static readonly Color Frame = new Color(0.22f, 0.22f, 0.24f);
        static readonly Color TabBar = new Color(0.02f, 0.02f, 0.022f);
        static readonly Color TabFill = new Color(0.075f, 0.075f, 0.08f);
        static readonly Color GraphFill = new Color(0.045f, 0.045f, 0.05f);
        static readonly Color GridMinor = new Color(0.065f, 0.065f, 0.07f);
        static readonly Color GridMajor = new Color(0.025f, 0.025f, 0.028f);
        static readonly Color StatusFill = new Color(0.03f, 0.03f, 0.033f);
        static readonly Color BlueprintBlue = new Color(0.05f, 0.45f, 1f);
        static readonly Color Amber = new Color(1f, 0.65f, 0.15f);
        static readonly Color CloseHover = new Color(0.8f, 0.12f, 0.08f);
        static readonly Color TextDim = new Color(0.62f, 0.62f, 0.66f);

        static Color PinColor(MetalHackPinType t) => t == MetalHackPinType.Object ? new Color(0.05f, 0.45f, 1f)
            : t == MetalHackPinType.Team ? new Color(0f, 0.72f, 0.5f) : new Color(0.92f, 0.92f, 0.92f);

        Node[] nodes;
        Pin[] pins;
        readonly List<Vector2Int> links = new List<Vector2Int>();
        Action<MetalHackResult> onFinished;
        Vector2 pointer = new Vector2(560f, 330f);
        Vector2 pressPointer;
        int dragPin = -1;
        Vector2Int detachedLink = new Vector2Int(-1, -1);
        bool wireSticky;
        bool gamepad;
        bool finished;

        public string BlueprintName { get; private set; } = "BP_UnitController";
        public bool IsDirty { get; private set; }

        /// <summary>Builds the graph in its fixed layout. Call once, right after adding the component.</summary>
        public void Init(string blueprintName, Action<MetalHackResult> finishedCallback)
        {
            BlueprintName = blueprintName;
            onFinished = finishedCallback;
            BuildGraph();
            pointer = new Vector2(WinW * 0.5f, WinH * 0.5f);
        }

        void BuildGraph()
        {
            // Header colours follow the real editor: events red, pure getters green, flow control grey, calls blue.
            nodes = new[]
            {
                new Node { title = "Event: On Target", header = new Color(0.55f, 0.06f, 0.05f), position = new Vector2(105f, 230f), width = 190f, rows = 2 },
                new Node { title = "Get Value: team", header = new Color(0.16f, 0.4f, 0.1f), position = new Vector2(345f, 300f), width = 180f, rows = 1 },
                new Node { title = "Switch Team", header = new Color(0.32f, 0.32f, 0.34f), position = new Vector2(585f, 230f), width = 170f, rows = 2 },
                new Node { title = "Call Ceasefire", header = new Color(0.06f, 0.25f, 0.55f), position = new Vector2(845f, 190f), width = 170f, rows = 1 },
                new Node { title = "Call Shoot", header = new Color(0.06f, 0.25f, 0.55f), position = new Vector2(845f, 330f), width = 150f, rows = 1 },
            };
            pins = new[]
            {
                new Pin { node = NEvent, output = true, type = MetalHackPinType.Exec, label = "", row = 0 },
                new Pin { node = NEvent, output = true, type = MetalHackPinType.Object, label = "target", row = 1 },
                new Pin { node = NGet, output = false, type = MetalHackPinType.Object, label = "target", row = 0 },
                new Pin { node = NGet, output = true, type = MetalHackPinType.Team, label = "team", row = 0 },
                new Pin { node = NSwitch, output = false, type = MetalHackPinType.Exec, label = "", row = 0 },
                new Pin { node = NSwitch, output = false, type = MetalHackPinType.Team, label = "team", row = 1 },
                new Pin { node = NSwitch, output = true, type = MetalHackPinType.Exec, label = "red", row = 0 },
                new Pin { node = NSwitch, output = true, type = MetalHackPinType.Exec, label = "blue", row = 1 },
                new Pin { node = NCeasefire, output = false, type = MetalHackPinType.Exec, label = "", row = 0 },
                new Pin { node = NShoot, output = false, type = MetalHackPinType.Exec, label = "", row = 0 },
            };
            // The robot's own logic: hold fire on red (its own side), shoot blue.
            links.Clear();
            links.Add(new Vector2Int(PEventExec, PSwitchExec));
            links.Add(new Vector2Int(PEventTarget, PGetTarget));
            links.Add(new Vector2Int(PGetTeam, PSwitchTeam));
            links.Add(new Vector2Int(PSwitchRed, PCeasefire));
            links.Add(new Vector2Int(PSwitchBlue, PShoot));
        }

        float NodeHeight(int node) => HeaderH + BodyPad + nodes[node].rows * RowH + BodyPad;

        Vector2 PinPosition(int pin)
        {
            Pin p = pins[pin];
            Node n = nodes[p.node];
            float y = n.position.y + HeaderH + BodyPad + p.row * RowH + RowH * 0.5f;
            return new Vector2(p.output ? n.position.x + n.width - PinInset : n.position.x + PinInset, y);
        }

        int PinAt(Vector2 point)
        {
            int best = -1;
            float bestDist = PinHitRadius;
            for (int i = 0; i < pins.Length; i++)
            {
                float d = Vector2.Distance(PinPosition(i), point);
                if (d <= bestDist)
                {
                    best = i;
                    bestDist = d;
                }
            }
            return best;
        }

        bool CanConnect(int a, int b) => a >= 0 && b >= 0 && a < pins.Length && b < pins.Length && pins[a].node != pins[b].node
            && pins[a].output != pins[b].output && pins[a].type == pins[b].type;

        void Connect(int a, int b)
        {
            int outPin = pins[a].output ? a : b;
            int inPin = pins[a].output ? b : a;
            // Inputs take one wire, and so do exec outputs; data outputs can feed many inputs.
            links.RemoveAll(l => l.y == inPin || (pins[outPin].type == MetalHackPinType.Exec && l.x == outPin));
            links.Add(new Vector2Int(outPin, inPin));
            IsDirty = true;
        }

        void BreakLinks(int pin)
        {
            if (links.RemoveAll(l => l.x == pin || l.y == pin) > 0)
            {
                IsDirty = true;
                MetalAudio.Play2D("ui_move", 0.5f);
            }
        }

        public bool IsSolved()
        {
            if (links.Count != Solution.Length) return false;
            foreach (Vector2Int s in Solution)
            {
                if (!links.Contains(s)) return false;
            }
            return true;
        }

        /// <summary>The wiring as text, for logs.</summary>
        public string DescribeLinks()
        {
            string PinName(int p) => nodes[pins[p].node].title + "." + (string.IsNullOrEmpty(pins[p].label) ? "exec" : pins[p].label);
            var parts = new List<string>();
            foreach (Vector2Int l in links) parts.Add(PinName(l.x) + " -> " + PinName(l.y));
            return string.Join(", ", parts);
        }

        /// <summary>Dev autopilot: rewires the graph as a player would (correctly or not), then leaves it for Compile().</summary>
        public void DevRewire(bool correct)
        {
            Connect(PSwitchRed, PShoot);
            if (correct) Connect(PSwitchBlue, PCeasefire);
        }

        /// <summary>Save &amp; Compile: Success if the wiring is right, otherwise Failed.</summary>
        public void Compile()
        {
            DropWire(-1);
            Finish(IsSolved() ? MetalHackResult.Success : MetalHackResult.Failed);
        }

        public void Cancel() => Finish(MetalHackResult.Cancelled);

        void Finish(MetalHackResult result)
        {
            if (finished) return;
            finished = true;
            dragPin = -1;
            wireSticky = false;
            Action<MetalHackResult> cb = onFinished;
            onFinished = null;
            cb?.Invoke(result);
        }

        // --- Layout ---------------------------------------------------------------------------------------

        /// <summary>Window scale and top-left corner in reference GUI units (centred, leaving a margin).</summary>
        void WindowTransform(out float scale, out Vector2 offset)
        {
            float w = MetalDraw.Width, h = MetalDraw.Height;
            scale = Mathf.Max(0.3f, Mathf.Min(Mathf.Min(w * 0.94f / WinW, h * 0.88f / WinH), 1f));
            offset = (new Vector2(w, h) - new Vector2(WinW, WinH) * scale) * 0.5f;
        }

        Rect TabRect()
        {
            float textW = MetalDraw.Measure(BlueprintName + "*", 15).x;
            return new Rect(8f, 6f, 36f + textW + 16f, TabBarH - 6f);
        }

        Rect SaveRect()
        {
            float left = TabRect().xMax + 12f;
            return new Rect(left, 8f, 160f, TabBarH - 13f);
        }

        static Rect CloseRect() => new Rect(WinW - 40f, 5f, 34f, TabBarH - 10f);
        static Rect GraphRect() => new Rect(0f, TabBarH, WinW, WinH - TabBarH - StatusH);

        // --- Input ----------------------------------------------------------------------------------------

        Vector2 MouseInWindow()
        {
            WindowTransform(out float scale, out Vector2 offset);
            Vector2 gui = MetalDraw.FromScreen(Mouse.current != null ? Mouse.current.position.ReadValue() : Vector2.zero);
            return (gui - offset) / scale;
        }

        void PointerMoved(Vector2 p) => pointer = new Vector2(Mathf.Clamp(p.x, 0f, WinW), Mathf.Clamp(p.y, 0f, WinH));

        void PointerDown(bool breakLinks)
        {
            if (finished) return;
            pressPointer = pointer;
            if (CloseRect().Contains(pointer))
            {
                Cancel();
                return;
            }
            if (IsDirty && SaveRect().Contains(pointer))
            {
                Compile();
                return;
            }
            Rect graph = GraphRect();
            int pin = graph.Contains(pointer) ? PinAt(pointer - graph.position) : -1;
            // Second click of click-to-connect: join the held wire to this pin, or let it go.
            if (wireSticky)
            {
                bool joins = CanConnect(dragPin, pin);
                bool ownPin = pin == dragPin || pin == detachedLink.y;
                DropWire(pin);
                if (joins || ownPin || pin < 0 || breakLinks) return;
            }
            if (pin < 0) return;
            if (breakLinks)
            {
                BreakLinks(pin);
                return;
            }
            PickUpWire(pin);
        }

        void PickUpWire(int pin)
        {
            detachedLink = new Vector2Int(-1, -1);
            dragPin = pin;
            wireSticky = false;
            if (!pins[pin].output)
            {
                // Grabbing a wired input picks that wire up by its end, so it can be moved to another pin.
                int index = links.FindIndex(l => l.y == pin);
                if (index >= 0)
                {
                    detachedLink = links[index];
                    dragPin = detachedLink.x;
                    links.RemoveAt(index);
                }
            }
            MetalAudio.Play2D("ui_move", 0.35f);
        }

        void DropWire(int target)
        {
            if (dragPin < 0) return;
            if (CanConnect(dragPin, target))
            {
                Connect(dragPin, target);
                MetalAudio.Play2D("ui_move", 0.6f);
            }
            else if (detachedLink.x >= 0)
            {
                // Let go over nothing: a picked-up wire snaps back rather than vanishing.
                links.Add(detachedLink);
            }
            detachedLink = new Vector2Int(-1, -1);
            dragPin = -1;
            wireSticky = false;
        }

        void PointerUp()
        {
            if (finished || dragPin < 0 || wireSticky) return;
            if (Vector2.Distance(pointer, pressPointer) <= ClickSlop)
            {
                // A click, not a drag: keep holding the wire, and the next click says where it goes.
                wireSticky = true;
                return;
            }
            Rect graph = GraphRect();
            DropWire(PinAt(pointer - graph.position));
        }

        void Update()
        {
            if (finished) return;
            Mouse mouse = Mouse.current;
            Keyboard kb = Keyboard.current;
            Gamepad pad = Gamepad.current;
            if (mouse != null)
            {
                if (mouse.delta.ReadValue().sqrMagnitude > 0.01f)
                {
                    gamepad = false;
                    PointerMoved(MouseInWindow());
                }
                if (mouse.rightButton.wasPressedThisFrame) DropWire(-1);
                if (mouse.leftButton.wasPressedThisFrame)
                {
                    gamepad = false;
                    PointerMoved(MouseInWindow());
                    bool alt = kb != null && (kb.leftAltKey.isPressed || kb.rightAltKey.isPressed);
                    PointerDown(alt);
                    if (finished) return;
                }
                if (mouse.leftButton.wasReleasedThisFrame)
                {
                    PointerMoved(MouseInWindow());
                    PointerUp();
                }
            }
            if (kb != null)
            {
                bool ctrl = kb.leftCtrlKey.isPressed || kb.rightCtrlKey.isPressed;
                if (kb.escapeKey.wasPressedThisFrame)
                {
                    gamepad = false;
                    // The first press lets go of a held wire; with nothing held it closes the window.
                    if (dragPin >= 0) DropWire(-1);
                    else Cancel();
                    return;
                }
                if (ctrl && kb.sKey.wasPressedThisFrame && IsDirty)
                {
                    Compile();
                    return;
                }
            }
            if (pad != null)
            {
                if (pad.buttonEast.wasPressedThisFrame || pad.startButton.wasPressedThisFrame)
                {
                    gamepad = true;
                    if (dragPin >= 0) DropWire(-1);
                    else Cancel();
                    return;
                }
                if (pad.buttonNorth.wasPressedThisFrame)
                {
                    gamepad = true;
                    if (IsDirty) Compile();
                    return;
                }
                if (pad.buttonSouth.wasPressedThisFrame)
                {
                    gamepad = true;
                    PointerDown(false);
                    if (finished) return;
                }
                if (pad.buttonSouth.wasReleasedThisFrame) PointerUp();
                if (pad.buttonWest.wasPressedThisFrame)
                {
                    gamepad = true;
                    PointerDown(true);
                    PointerUp();
                }
                // Virtual cursor: squared response, full tilt crosses the window in about a second.
                Vector2 stick = pad.leftStick.ReadValue();
                float tilt = stick.magnitude;
                if (tilt > 0.2f)
                {
                    gamepad = true;
                    float speed = 1100f * tilt * tilt;
                    PointerMoved(pointer + new Vector2(stick.x, -stick.y) / tilt * speed * Time.unscaledDeltaTime);
                }
            }
        }

        // --- Drawing --------------------------------------------------------------------------------------

        void OnGUI()
        {
            GUI.depth = 5;
            MetalDraw.Begin();
            // Light shade over the (still running) game.
            MetalDraw.Rect(0f, 0f, MetalDraw.Width, MetalDraw.Height, new Color(0f, 0f, 0f, 0.2f));
            WindowTransform(out float scale, out Vector2 offset);
            Matrix4x4 baseMatrix = GUI.matrix;
            GUI.matrix = baseMatrix * Matrix4x4.TRS(offset, Quaternion.identity, new Vector3(scale, scale, 1f));

            MetalDraw.Rect(6f, 10f, WinW, WinH, new Color(0f, 0f, 0f, 0.5f));
            MetalDraw.Rect(-1f, -1f, WinW + 2f, WinH + 2f, Frame);
            MetalDraw.Rect(0f, 0f, WinW, TabBarH, TabBar);

            // Tab: blueprint icon, name (with the editor's unsaved-changes star), blue top edge.
            Rect tab = TabRect();
            MetalDraw.Rect(tab, TabFill);
            MetalDraw.Rect(tab.x + 4f, tab.y, tab.width - 8f, 2f, BlueprintBlue);
            MetalDraw.Rect(tab.x + 12f, tab.y + 8f, 16f, 16f, BlueprintBlue);
            MetalDraw.Rect(tab.x + 16f, tab.y + 12f, 8f, 8f, new Color(0.85f, 0.93f, 1f));
            MetalDraw.Text(new Vector2(tab.x + 36f, tab.y + 5f), BlueprintName + (IsDirty ? "*" : ""), 15, Color.white);

            if (IsDirty)
            {
                Rect save = SaveRect();
                MetalDraw.Rect(save, save.Contains(pointer) ? new Color(1f, 0.78f, 0.35f) : Amber);
                MetalDraw.Text(save.center, gamepad ? "(Y) Save & Compile" : "Save & Compile", 15, new Color(0.04f, 0.03f, 0.02f), TextAnchor.MiddleCenter, true);
            }
            Rect close = CloseRect();
            if (close.Contains(pointer)) MetalDraw.Rect(close, CloseHover);
            MetalDraw.Text(close.center, "X", 18, new Color(0.9f, 0.9f, 0.9f), TextAnchor.MiddleCenter, true);

            // Graph area, clipped so wires that loop out of it don't cover the tab bar.
            Rect graphArea = GraphRect();
            GUI.BeginClip(graphArea);
            MetalDraw.Rect(0f, 0f, graphArea.width, graphArea.height, GraphFill);
            for (float x = 0f; x <= graphArea.width; x += 16f)
            {
                bool major = Mathf.Approximately(x % 128f, 0f);
                MetalDraw.Rect(x, 0f, 1f, graphArea.height, major ? GridMajor : GridMinor);
            }
            for (float y = 0f; y <= graphArea.height; y += 16f)
            {
                bool major = Mathf.Approximately(y % 128f, 0f);
                MetalDraw.Rect(0f, y, graphArea.width, 1f, major ? GridMajor : GridMinor);
            }
            MetalDraw.Text(new Vector2(graphArea.width - 20f, graphArea.height - 14f), "BLUEPRINT", 42, new Color(1f, 1f, 1f, 0.08f), TextAnchor.LowerRight, true);
            const string Brief = "// TODO: shoot RED, spare BLUE. Rewire, then Save & Compile.";
            Vector2 briefSize = MetalDraw.Measure(Brief, 14);
            MetalDraw.Rect(12f, 10f, briefSize.x + 20f, briefSize.y + 10f, new Color(1f, 1f, 1f, 0.1f));
            MetalDraw.Text(new Vector2(22f, 15f), Brief, 14, new Color(0.85f, 0.85f, 0.85f));

            Vector2 graphPointer = pointer - graphArea.position;
            int hoverPin = PinAt(graphPointer);
            // Wires, behind the nodes.
            foreach (Vector2Int link in links)
            {
                bool hot = hoverPin == link.x || hoverPin == link.y;
                Color c = PinColor(pins[link.x].type) * (hot ? 1f : 0.85f);
                c.a = 1f;
                DrawWire(PinPosition(link.x), PinPosition(link.y), c, hot ? 3.5f : 2.5f);
            }
            if (dragPin >= 0)
            {
                Vector2 pinPoint = PinPosition(dragPin);
                bool fromOutput = pins[dragPin].output;
                bool valid = hoverPin < 0 || CanConnect(dragPin, hoverPin);
                DrawWire(fromOutput ? pinPoint : graphPointer, fromOutput ? graphPointer : pinPoint,
                    valid ? PinColor(pins[dragPin].type) : new Color(0.9f, 0.15f, 0.1f), 3f);
            }

            for (int n = 0; n < nodes.Length; n++)
            {
                Node node = nodes[n];
                MetalDraw.Rect(node.position.x, node.position.y, node.width, NodeHeight(n), new Color(0.015f, 0.015f, 0.018f, 0.94f));
                MetalDraw.Rect(node.position.x, node.position.y, node.width, HeaderH, node.header);
                MetalDraw.Text(node.position + new Vector2(10f, 5f), node.title, 15, Color.white, TextAnchor.UpperLeft, true);
                for (int p = 0; p < pins.Length; p++)
                {
                    Pin pin = pins[p];
                    if (pin.node != n) continue;
                    Vector2 c = PinPosition(p);
                    Color color = PinColor(pin.type);
                    int pinIndex = p;
                    bool linked = links.Exists(l => l.x == pinIndex || l.y == pinIndex);
                    if (p == hoverPin || p == dragPin) MetalDraw.Disc(c, 10f, new Color(1f, 1f, 1f, 0.18f));
                    else if (CanConnect(dragPin, p)) MetalDraw.Disc(c, 10f, color.WithAlpha(0.3f));
                    if (pin.type == MetalHackPinType.Exec)
                    {
                        // The editor's exec arrow: an outlined pentagon, filled in when wired.
                        MetalDraw.Polyline(color, 1.5f, c + new Vector2(-5f, -6f), c + new Vector2(1f, -6f), c + new Vector2(6f, 0f),
                            c + new Vector2(1f, 6f), c + new Vector2(-5f, 6f), c + new Vector2(-5f, -6f));
                        if (linked)
                        {
                            MetalDraw.Rect(c.x - 5f, c.y - 6f, 6f, 12f, color);
                            MetalDraw.Polyline(color, 4f, c + new Vector2(1f, -4f), c + new Vector2(4f, 0f), c + new Vector2(1f, 4f));
                        }
                    }
                    else
                    {
                        // Data pins: a ring, filled in when wired.
                        if (linked) MetalDraw.Disc(c, 6f, color);
                        else MetalDraw.RingShape(c, 6f, color);
                    }
                    if (!string.IsNullOrEmpty(pin.label))
                    {
                        Color labelColor = pin.label == "red" ? new Color(1f, 0.4f, 0.35f)
                            : pin.label == "blue" ? new Color(0.45f, 0.65f, 1f) : new Color(0.85f, 0.85f, 0.85f);
                        MetalDraw.Text(new Vector2(pin.output ? c.x - 12f : c.x + 12f, c.y), pin.label, 14, labelColor,
                            pin.output ? TextAnchor.MiddleRight : TextAnchor.MiddleLeft);
                    }
                }
            }
            GUI.EndClip();

            // Status bar with the controls for whichever device is in use.
            MetalDraw.Rect(0f, WinH - StatusH, WinW, StatusH, StatusFill);
            string hint = wireSticky
                ? (gamepad ? "Wire held:  (A) on a ringed pin to connect it     (A) on empty space or (B) to let go"
                    : "Wire held:  click a ringed pin to connect it     Click empty space, right-click or Esc to let go")
                : gamepad
                    ? "Left stick: cursor     (A) a pin, then (A) another pin to wire them     (X) break a pin's links     (Y) Save & Compile     (B) cancel"
                    : "Click a pin, then another pin to wire them (or drag)     Alt+Click a pin to break its links     Ctrl+S  Save & Compile";
            MetalDraw.Text(new Vector2(12f, WinH - StatusH + 6f), hint, 14, TextDim);
            if (gamepad)
            {
                MetalDraw.Disc(pointer, 8f, Color.white);
                MetalDraw.Disc(pointer, 5f, new Color(0f, 0f, 0f, 0.8f));
                MetalDraw.Disc(pointer, 2f, Amber);
            }
            GUI.matrix = baseMatrix;
        }

        /// <summary>A graph wire: a horizontal-tangent spline that loops back when it runs right to left.</summary>
        static void DrawWire(Vector2 start, Vector2 end, Color color, float thickness)
        {
            float tangent = Mathf.Clamp(Mathf.Abs(end.x - start.x) * 1.1f + Mathf.Abs(end.y - start.y) * 0.3f, 90f, 520f);
            MetalDraw.Spline(start, new Vector2(tangent, 0f), end, new Vector2(tangent, 0f), color, thickness);
        }
    }
}
