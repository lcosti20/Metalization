// Immediate-mode drawing for the HUD, menus and hack window (the Unreal version drew them all with Canvas /
// Slate calls in code). Everything is in reference units of a 1920x1080 screen: call Begin() at the top of
// OnGUI and the GUI matrix scales it to the real resolution.
using UnityEngine;

namespace Metalization
{
    public static class MetalDraw
    {
        /// <summary>Reference height every layout is authored at.</summary>
        public const float ReferenceHeight = 1080f;

        static Texture2D white;
        static Texture2D circle;
        static Texture2D ring;
        static GUIStyle style;
        static readonly GUIContent Content = new GUIContent();

        public static Texture2D White
        {
            get
            {
                if (white == null)
                {
                    white = new Texture2D(1, 1, TextureFormat.RGBA32, false) { hideFlags = HideFlags.DontSave };
                    white.SetPixel(0, 0, Color.white);
                    white.Apply();
                }
                return white;
            }
        }

        static Texture2D MakeDisc(float inner)
        {
            const int Size = 64;
            var t = new Texture2D(Size, Size, TextureFormat.RGBA32, false) { hideFlags = HideFlags.DontSave, wrapMode = TextureWrapMode.Clamp };
            float c = (Size - 1) * 0.5f;
            for (int y = 0; y < Size; y++)
            {
                for (int x = 0; x < Size; x++)
                {
                    float d = Mathf.Sqrt((x - c) * (x - c) + (y - c) * (y - c)) / c;
                    float a = Mathf.Clamp01((1f - d) * c);
                    if (inner > 0f) a *= Mathf.Clamp01((d - inner) * c);
                    t.SetPixel(x, y, new Color(1f, 1f, 1f, a));
                }
            }
            t.Apply();
            return t;
        }

        public static Texture2D Circle => circle != null ? circle : (circle = MakeDisc(0f));
        public static Texture2D Ring => ring != null ? ring : (ring = MakeDisc(0.78f));

        /// <summary>Scale from reference units to pixels.</summary>
        public static float Scale => Mathf.Max(0.1f, Screen.height / ReferenceHeight);

        /// <summary>Width of the screen in reference units.</summary>
        public static float Width => Screen.width / Scale;
        public static float Height => ReferenceHeight;

        /// <summary>Sets the GUI matrix so the rest of OnGUI draws in reference units.</summary>
        public static void Begin()
        {
            float s = Scale;
            GUI.matrix = Matrix4x4.Scale(new Vector3(s, s, 1f));
        }

        /// <summary>Converts a screen-space point (pixels, origin bottom-left) to reference GUI units (origin top-left).</summary>
        public static Vector2 FromScreen(Vector2 screen) => new Vector2(screen.x, Screen.height - screen.y) / Scale;

        /// <summary>Projects a world point to reference GUI units; false if it is behind the camera.</summary>
        public static bool Project(Camera cam, Vector3 world, out Vector2 gui)
        {
            gui = Vector2.zero;
            if (cam == null) return false;
            Vector3 sp = cam.WorldToScreenPoint(world);
            if (sp.z <= 0.01f) return false;
            gui = FromScreen(sp);
            return true;
        }

        public static void Rect(float x, float y, float w, float h, Color color)
        {
            if (Event.current.type != EventType.Repaint) return;
            Color old = GUI.color;
            GUI.color = color;
            GUI.DrawTexture(new Rect(x, y, w, h), White);
            GUI.color = old;
        }

        public static void Rect(Rect r, Color color) => Rect(r.x, r.y, r.width, r.height, color);

        /// <summary>Outline of a rectangle.</summary>
        public static void Frame(Rect r, Color color, float thickness = 1f)
        {
            Rect(r.x, r.y, r.width, thickness, color);
            Rect(r.x, r.yMax - thickness, r.width, thickness, color);
            Rect(r.x, r.y, thickness, r.height, color);
            Rect(r.xMax - thickness, r.y, thickness, r.height, color);
        }

        public static void Disc(Vector2 center, float radius, Color color)
        {
            if (Event.current.type != EventType.Repaint) return;
            Color old = GUI.color;
            GUI.color = color;
            GUI.DrawTexture(new Rect(center.x - radius, center.y - radius, radius * 2f, radius * 2f), Circle);
            GUI.color = old;
        }

        public static void RingShape(Vector2 center, float radius, Color color)
        {
            if (Event.current.type != EventType.Repaint) return;
            Color old = GUI.color;
            GUI.color = color;
            GUI.DrawTexture(new Rect(center.x - radius, center.y - radius, radius * 2f, radius * 2f), Ring);
            GUI.color = old;
        }

        /// <summary>A thick line between two points.</summary>
        public static void Line(Vector2 a, Vector2 b, Color color, float thickness = 1f)
        {
            if (Event.current.type != EventType.Repaint) return;
            Vector2 d = b - a;
            float len = d.magnitude;
            if (len < 0.01f) return;
            Matrix4x4 old = GUI.matrix;
            Color oldColor = GUI.color;
            float angle = Mathf.Atan2(d.y, d.x) * Mathf.Rad2Deg;
            // Compose onto the current (scaled) matrix: GUIUtility.RotateAroundPivot ignores a scaled GUI.matrix.
            GUI.matrix = old * Matrix4x4.TRS(new Vector3(a.x, a.y, 0f), Quaternion.Euler(0f, 0f, angle), Vector3.one);
            GUI.color = color;
            GUI.DrawTexture(new Rect(0f, -thickness * 0.5f, len, thickness), White);
            GUI.matrix = old;
            GUI.color = oldColor;
        }

        public static void Polyline(Color color, float thickness, params Vector2[] points)
        {
            for (int i = 1; i < points.Length; i++) Line(points[i - 1], points[i], color, thickness);
        }

        /// <summary>A cubic Hermite spline (Slate's MakeSpline) as a polyline.</summary>
        public static void Spline(Vector2 start, Vector2 startTangent, Vector2 end, Vector2 endTangent, Color color, float thickness)
        {
            const int Segments = 24;
            Vector2 prev = start;
            for (int i = 1; i <= Segments; i++)
            {
                float t = i / (float)Segments;
                float t2 = t * t, t3 = t2 * t;
                Vector2 p = (2f * t3 - 3f * t2 + 1f) * start + (t3 - 2f * t2 + t) * startTangent
                    + (-2f * t3 + 3f * t2) * end + (t3 - t2) * endTangent;
                Line(prev, p, color, thickness);
                prev = p;
            }
        }

        static GUIStyle Style(int size, bool bold, TextAnchor anchor)
        {
            if (style == null)
            {
                style = new GUIStyle(GUI.skin.label) { richText = false, wordWrap = false, clipping = TextClipping.Overflow };
                style.padding = new RectOffset(0, 0, 0, 0);
                style.margin = new RectOffset(0, 0, 0, 0);
                MetalGameAssets assets = MetalGameAssets.Get();
                if (assets != null && assets.uiFont != null) style.font = assets.uiFont;
            }
            style.fontSize = size;
            style.fontStyle = bold ? FontStyle.Bold : FontStyle.Normal;
            style.alignment = anchor;
            return style;
        }

        public static Vector2 Measure(string text, int size, bool bold = false)
        {
            Content.text = text;
            return Style(size, bold, TextAnchor.UpperLeft).CalcSize(Content);
        }

        /// <summary>Draws text with its anchor at the given point (UpperLeft = top-left corner at pos).</summary>
        public static void Text(Vector2 pos, string text, int size, Color color, TextAnchor anchor = TextAnchor.UpperLeft, bool bold = false, bool shadow = false)
        {
            if (string.IsNullOrEmpty(text)) return;
            Content.text = text;
            GUIStyle s = Style(size, bold, anchor);
            Vector2 sz = s.CalcSize(Content);
            float x = pos.x, y = pos.y;
            switch (anchor)
            {
                case TextAnchor.UpperCenter: case TextAnchor.MiddleCenter: case TextAnchor.LowerCenter: x -= sz.x * 0.5f; break;
                case TextAnchor.UpperRight: case TextAnchor.MiddleRight: case TextAnchor.LowerRight: x -= sz.x; break;
            }
            switch (anchor)
            {
                case TextAnchor.MiddleLeft: case TextAnchor.MiddleCenter: case TextAnchor.MiddleRight: y -= sz.y * 0.5f; break;
                case TextAnchor.LowerLeft: case TextAnchor.LowerCenter: case TextAnchor.LowerRight: y -= sz.y; break;
            }
            s.alignment = TextAnchor.UpperLeft;
            var r = new Rect(x, y, sz.x + 2f, sz.y + 2f);
            if (shadow)
            {
                s.normal.textColor = new Color(0f, 0f, 0f, color.a * 0.8f);
                GUI.Label(new Rect(r.x + 2f, r.y + 2f, r.width, r.height), Content, s);
            }
            s.normal.textColor = color;
            GUI.Label(r, Content, s);
        }

        /// <summary>Word-wrapped text in a box.</summary>
        public static float TextWrapped(Rect r, string text, int size, Color color, bool bold = false, TextAnchor anchor = TextAnchor.UpperLeft)
        {
            Content.text = text;
            GUIStyle s = Style(size, bold, anchor);
            s.wordWrap = true;
            s.normal.textColor = color;
            float h = s.CalcHeight(Content, r.width);
            GUI.Label(new Rect(r.x, r.y, r.width, Mathf.Max(r.height, h)), Content, s);
            s.wordWrap = false;
            return h;
        }

        public static Color WithAlpha(this Color c, float a) => new Color(c.r, c.g, c.b, a);
    }
}
