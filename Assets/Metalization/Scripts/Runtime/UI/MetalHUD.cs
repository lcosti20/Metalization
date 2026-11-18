// The in-game HUD, drawn every frame in code (immediate mode) from the player controller, the possessed robot,
// the game mode and the dialogue queue: crosshair, health, weapon and ammo, hack prompts and timer, objectives
// and markers, stealth awareness, the scope, radio dialogue, the space battle status and the results screen.
using System.Collections.Generic;
using UnityEngine;

namespace Metalization
{
    public class MetalHUD : MonoBehaviour
    {
        static readonly Color Amber = new Color(1f, 0.65f, 0.15f);
        static readonly Color Cyan = new Color(0.3f, 0.9f, 1f);
        static readonly Color Danger = new Color(1f, 0.2f, 0.1f);
        static readonly Color Panel = new Color(0f, 0f, 0f, 0.55f);

        // Font sizes at 1080p (the Unreal HUD's small/medium/large fonts at its text scale).
        const int Small = 18, Medium = 26, Large = 36;

        MetalPlayerController pc;
        Camera cam;
        readonly List<Vector3> markers = new List<Vector3>();

        void Awake() => pc = GetComponent<MetalPlayerController>();

        static int Size(int baseSize, float scale) => Mathf.Max(8, Mathf.RoundToInt(baseSize * scale));

        static void TextShadowed(string text, float x, float y, Color color, int size, bool centered = false)
        {
            MetalDraw.Text(new Vector2(x, y), text, size, color, centered ? TextAnchor.UpperCenter : TextAnchor.UpperLeft, false, true);
        }

        static float TextH(int size) => MetalDraw.Measure("Ag", size).y;
        static float TextW(string text, int size) => MetalDraw.Measure(text, size).x;

        static void Bar(float x, float y, float w, float h, float fraction, Color color)
        {
            MetalDraw.Rect(x - 3f, y - 3f, w + 6f, h + 6f, Panel);
            MetalDraw.Rect(x, y, w * Mathf.Clamp01(fraction), h, color);
        }

        static Color LerpHSV(Color a, Color b, float t)
        {
            Color.RGBToHSV(a, out float ha, out float sa, out float va);
            Color.RGBToHSV(b, out float hb, out float sb, out float vb);
            float dh = Mathf.DeltaAngle(ha * 360f, hb * 360f) / 360f;
            Color c = Color.HSVToRGB(Mathf.Repeat(ha + dh * t, 1f), Mathf.Lerp(sa, sb, t), Mathf.Lerp(va, vb, t));
            c.a = Mathf.Lerp(a.a, b.a, t);
            return c;
        }

        bool Project(Vector3 world, out Vector2 screen) => MetalDraw.Project(cam, world, out screen);

        void OnGUI()
        {
            if (Event.current.type != EventType.Repaint || pc == null) return;
            GUI.depth = 10;
            cam = pc.CameraRig != null ? pc.CameraRig.Camera : Camera.main;
            MetalDraw.Begin();
            float W = MetalDraw.Width, H = MetalDraw.Height;
            float now = Time.time;
            MetalGameMode gm = MetalGameMode.Current;
            MetalBot bot = pc.Pawn;

            // Damage vignette.
            float damageAlpha = Mathf.Clamp01(1f - (now - pc.LastDamageTime) / 0.5f);
            if (damageAlpha > 0f) MetalDraw.Rect(0f, 0f, W, H, new Color(0.8f, 0f, 0f, 0.25f * damageAlpha));

            // The arena keeps the screen clean while exploring; combat UI appears when shooting, hurt or hijacking.
            bool combatUI = bot != null && (gm == null || !gm.minimalHUD || bot.IsInCombatStance || pc.Host != null || now - bot.LastDamageTakenTime < 4f);
            float scope = bot != null ? bot.ScopeAlpha : 0f;
            if (bot != null && bot.IsAlive && scope > 0.02f) DrawScope(bot, scope, W, H);
            if (bot != null && bot.IsAlive && !(bot is MetalStalker)) DrawAwareness(bot);

            if (bot != null && bot.IsAlive && combatUI) DrawCombat(bot, scope, W, H, now);

            if (bot != null && bot.IsAlive)
            {
                // Possession timer (friendly links have none).
                if (pc.Host != null && pc.IsAllyLink)
                {
                    TextShadowed("LINKED   [F] disconnect", W * 0.5f, 92f, Cyan, Medium, true);
                }
                else if (pc.Host != null)
                {
                    float fraction = pc.PossessionTimeRemaining / Mathf.Max(1f, pc.PossessionDuration);
                    const float TimerW = 420f;
                    TextShadowed($"HACK TETHER  {pc.PossessionTimeRemaining:0}s   [F] disconnect", W * 0.5f, 92f, Cyan, Medium, true);
                    Bar(W * 0.5f - TimerW * 0.5f, 92f + TextH(Medium) + 4f, TimerW, 10f, fraction,
                        fraction < 0.25f && Mathf.Repeat(now, 0.4f) < 0.2f ? Danger : Cyan);
                }
                if (pc.NearbyConsole != null) TextShadowed(pc.NearbyConsole.promptText, W * 0.5f, H * 0.62f, Color.white, Medium, true);
            }

            if (gm is MetalSpaceBattleGameMode battle && !battle.IsMissionComplete) DrawSpaceBattle(battle, bot, W);

            // Dialogue. Every line is a radio call unless the player's own robot is the one speaking.
            MetalDialogue dialogue = FindAnyObjectByType<MetalDialogue>();
            if (dialogue != null && dialogue.Current != null)
            {
                MetalDialogueLine line = dialogue.Current;
                DrawDialogueBox(line, dialogue.LineTime, dialogue.OpenTime, W, H);
                MetalBot ownBody = pc.PlayerBody;
                if (dialogue.IsTalking && bot != null && !(ownBody != null && string.Equals(line.speaker, ownBody.DisplayName, System.StringComparison.OrdinalIgnoreCase)))
                {
                    DrawRadioIcon(bot, now);
                }
            }

            if (gm != null)
            {
                float labelH = TextH(Size(Small, 1.2f));
                float objH = TextH(Medium);
                float objectiveY = 24f + labelH;
                MetalMission mission = gm.Mission;
                if (mission != null && !gm.IsMissionComplete)
                {
                    // The current group's objectives: active in white, still locked in grey, done in green.
                    List<MetalObjectiveStatus> list = mission.GetObjectiveList();
                    float hintY = objectiveY;
                    if (list.Count > 0)
                    {
                        TextShadowed(list.Count > 1 ? "OBJECTIVES" : "OBJECTIVE", 40f, 24f, Amber, Size(Small, 1.2f));
                        float y = objectiveY;
                        foreach (MetalObjectiveStatus status in list)
                        {
                            string text = mission.objectives[status.index].description;
                            Color color = Color.white;
                            if (list.Count > 1)
                            {
                                switch (status.state)
                                {
                                    case MetalObjectiveState.Done: text = "[x] " + text; color = new Color(0.35f, 0.95f, 0.45f); break;
                                    case MetalObjectiveState.Locked: text = "[ ] " + text; color = new Color(0.5f, 0.5f, 0.5f); break;
                                    default: text = "[ ] " + text; break;
                                }
                            }
                            TextShadowed(text, 40f, y, color, Medium);
                            y += objH + 6f;
                        }
                        hintY = y + 10f;
                    }
                    // Control tips for the active steps, in panels under the objectives.
                    foreach (string hint in mission.GetCurrentHints())
                    {
                        Vector2 sz = MetalDraw.Measure(hint, Medium);
                        MetalDraw.Rect(34f, hintY - 6f, sz.x + 24f, sz.y + 12f, Panel);
                        MetalDraw.Rect(34f, hintY - 6f, 4f, sz.y + 12f, Cyan);
                        TextShadowed(hint, 46f, hintY, new Color(0.85f, 0.97f, 1f), Medium);
                        hintY += sz.y + 20f;
                    }
                    // Where to go (hidden while scoped).
                    if (bot != null && scope < 0.5f)
                    {
                        markers.Clear();
                        mission.GetObjectiveMarkers(markers);
                        foreach (Vector3 target in markers)
                        {
                            int meters = Mathf.RoundToInt(Vector3.Distance(target, bot.transform.position));
                            DrawMarker(target, $"{meters} m", Amber, true, W, H);
                        }
                    }
                }
                MetalMessage notice = gm.Notice;
                if (notice != null) TextShadowed(notice.text, W * 0.5f, H * 0.30f, Color.white, Medium, true);
                MetalMessage banner = gm.Banner;
                if (banner != null) TextShadowed(banner.text, W * 0.5f, H * 0.38f, Amber, Size(Large, 1.1f), true);
                if (gm.IsMissionComplete) DrawResults(gm, W, H);
            }
        }

        void DrawCombat(MetalBot bot, float scope, float W, float H, float now)
        {
            // Crosshair: cyan over a hackable robot, red briefly after a confirmed hit. The scope has its own.
            bool hackable = pc.HackCandidate != null;
            bool hit = now - pc.LastHitConfirmTime < 0.15f;
            Color cross = hit ? Danger : (hackable ? Cyan : Color.white);
            float gap = bot.IsAiming ? 4f : 8f;
            const float Len = 10f;
            float cx = W * 0.5f, cy = H * 0.5f;
            if (scope < 0.5f)
            {
                MetalDraw.Line(new Vector2(cx - gap - Len, cy), new Vector2(cx - gap, cy), cross, 2f);
                MetalDraw.Line(new Vector2(cx + gap, cy), new Vector2(cx + gap + Len, cy), cross, 2f);
                MetalDraw.Line(new Vector2(cx, cy - gap - Len), new Vector2(cx, cy - gap), cross, 2f);
                MetalDraw.Line(new Vector2(cx, cy + gap), new Vector2(cx, cy + gap + Len), cross, 2f);
            }
            else if (hit)
            {
                MetalDraw.Rect(cx - 3f, cy - 3f, 6f, 6f, Danger);
            }
            if (hackable)
            {
                MetalBot c = pc.HackCandidate;
                string verb = c.Team == bot.Team ? "PILOT" : "HACK";
                TextShadowed($"[{(pc.UsingGamepad ? "LB" : "F")}] {verb} {c.DisplayName.ToUpperInvariant()}", cx, cy + 30f, Cyan, Medium, true);
            }
            else if (pc.FirewalledCandidate != null)
            {
                MetalBot w = pc.FirewalledCandidate;
                TextShadowed($"FIREWALL  {w.DisplayName.ToUpperInvariant()}  {w.FirewallTimeRemaining:0.0}s", cx, cy + 30f, Danger, Medium, true);
            }

            // Health: laid out from the bottom up: stance line, health bar, then the name above it.
            const float BarW = 360f, BarX = 40f;
            float stanceH = TextH(Size(Small, 1.2f));
            float nameH = TextH(Medium);
            float barY = H - 14f - stanceH - 6f - 18f;
            float hf = bot.Health / Mathf.Max(1f, bot.MaxHealth);
            string name = bot.DisplayName.ToUpperInvariant() + (bot.IsHijacked ? "  (HIJACKED)" : "");
            TextShadowed(name, BarX, barY - nameH - 4f, bot.IsHijacked ? Cyan : Amber, Medium);
            // The bar's hue slides from green at full health, through yellow and orange, to red when empty.
            Bar(BarX, barY, BarW, 18f, hf, LerpHSV(Danger, new Color(0.3f, 1f, 0.4f), Mathf.Clamp01(hf)));
            if (bot.IsSneaking) TextShadowed("CONSOLE DISGUISE - keep still", BarX, barY + 24f, Cyan, Size(Small, 1.2f));
            else if (bot.IsSprinting) TextShadowed("SPRINTING - loud", BarX, barY + 24f, Amber, Size(Small, 1.2f));

            // Flying: airspeed and the boost meter.
            if (bot is MetalStalker flyer)
            {
                DrawLeadReticle(flyer, W, H);
                int speed = Mathf.RoundToInt(flyer.Velocity.magnitude);
                TextShadowed($"SPEED {speed} m/s{(flyer.IsBoosting ? "   BOOST" : "")}", BarX, barY - 2f * nameH - 8f, Color.white, Size(Small, 1.3f));
                Bar(BarX, barY + 24f, BarW, 8f, flyer.BoostFraction, flyer.IsBoosting ? Amber : Cyan);
            }

            // Weapon and ammo, laid out from the bottom up: grenade count, ammo, weapon name, then the weapon list.
            float rightX = W - 40f;
            float gh = TextH(Size(Small, 1.3f));
            float ah = TextH(Large);
            float wh = TextH(Medium);
            float lh = TextH(Size(Small, 1.2f));
            float grenadeY = H - 14f - gh;
            float ammoY = grenadeY - ah;
            const float ReloadGap = 16f;
            float weaponNameY = ammoY - wh - ReloadGap;
            MetalWeaponSlot slot = bot.CurrentWeapon;
            if (slot != null && slot.data != null)
            {
                string weaponName = $"{slot.data.displayName.ToUpperInvariant()}  LV{slot.level + 1}";
                int guns = bot.MuzzleCount;
                string ammo = bot.IsWeaponDisabled ? (guns > 1 ? "GUNS DESTROYED" : "WEAPON ARM DESTROYED")
                    : bot.IsSwappingWeapon ? "SWAPPING ARM..."
                    : bot.IsReloading ? "RELOADING..."
                    : guns > 1 && bot.WorkingMuzzleCount < guns ? $"GUNS {bot.WorkingMuzzleCount} / {guns}"
                    : slot.data.infiniteAmmo ? "INF"
                    : $"{slot.clip} / {slot.reserve}";
                TextShadowed(weaponName, rightX - TextW(weaponName, Medium), weaponNameY, Amber, Medium);
                float tw = TextW(ammo, Large);
                TextShadowed(ammo, rightX - tw, ammoY, Color.white, Large);
                if (bot.IsReloading)
                {
                    float reloadW = Mathf.Max(tw, 260f);
                    Bar(rightX - reloadW, ammoY - ReloadGap + 5f, reloadW, 6f, bot.ReloadFraction, Amber);
                }
            }
            if (bot.GrenadeWeapon != null && !bot.IsHijacked)
            {
                string g = $"[{(pc.UsingGamepad ? "RB" : "G")}] {bot.GrenadeWeapon.displayName.ToUpperInvariant()} x{bot.Grenades}";
                TextShadowed(g, rightX - TextW(g, Size(Small, 1.3f)), grenadeY, new Color(0.6f, 0.75f, 1f), Size(Small, 1.3f));
            }
            else if (bot.HasSecondaryWeapon)
            {
                // Built-in second weapon (a hacked Brute's rockets) on the aim button, with a cooldown bar.
                bool ready = bot.SecondaryReadyFraction >= 1f;
                string s = $"[{(pc.UsingGamepad ? "LT" : "RMB")}] {bot.archetype.secondaryWeapon.displayName.ToUpperInvariant()} {(ready ? "READY" : "LOADING")}";
                float tw = TextW(s, Size(Small, 1.3f));
                TextShadowed(s, rightX - tw, grenadeY, ready ? Amber : new Color(1f, 1f, 1f, 0.6f), Size(Small, 1.3f));
                if (!ready) Bar(rightX - tw, grenadeY - 2f, tw, 4f, bot.SecondaryReadyFraction, Amber);
            }
            // Weapon list (1-6).
            IReadOnlyList<MetalWeaponSlot> weapons = bot.Weapons;
            for (int i = 0; i < weapons.Count && !bot.IsHijacked; i++)
            {
                if (weapons[i].data == null) continue;
                bool current = bot.IsSwappingWeapon ? i == bot.SwapTargetSlot : i == bot.CurrentWeaponIndex;
                string entry = $"{i + 1} {weapons[i].data.displayName}";
                TextShadowed(entry, rightX - TextW(entry, Size(Small, 1.2f)), weaponNameY - 6f - (lh + 2f) * (weapons.Count - i),
                    current ? Amber : new Color(1f, 1f, 1f, 0.6f), Size(Small, 1.2f));
            }
        }

        void DrawScope(MetalBot bot, float alpha, float W, float H)
        {
            float cx = W * 0.5f, cy = H * 0.5f;
            // The lens grows open as the scope comes up.
            float radius = H * 0.44f * Mathf.Lerp(0.6f, 1f, alpha);
            var black = new Color(0f, 0f, 0f, Mathf.Clamp01(alpha * 1.2f));
            MetalDraw.Rect(0f, 0f, W, Mathf.Max(0f, cy - radius), black);
            MetalDraw.Rect(0f, cy + radius, W, Mathf.Max(0f, H - cy - radius), black);
            const float Step = 3f;
            for (float y = cy - radius; y < cy + radius; y += Step)
            {
                float dy = Mathf.Abs(y + Step * 0.5f - cy);
                float half = Mathf.Sqrt(Mathf.Max(0f, radius * radius - dy * dy));
                MetalDraw.Rect(0f, y, cx - half, Step, black);
                MetalDraw.Rect(cx + half, y, W - cx - half, Step, black);
            }
            MetalDraw.RingShape(new Vector2(cx, cy), radius + 3f, new Color(0.05f, 0.05f, 0.05f, alpha));
            if (alpha < 0.7f) return;
            // Fine crosshair with thick outer posts and mil-dots.
            var reticle = new Color(0.02f, 0.02f, 0.02f, 0.9f);
            var red = new Color(1f, 0.15f, 0.1f, 0.9f);
            MetalDraw.Line(new Vector2(cx - radius, cy), new Vector2(cx + radius, cy), reticle, 1.5f);
            MetalDraw.Line(new Vector2(cx, cy - radius), new Vector2(cx, cy + radius), reticle, 1.5f);
            foreach (float sign in new[] { -1f, 1f })
            {
                MetalDraw.Line(new Vector2(cx + sign * radius, cy), new Vector2(cx + sign * radius * 0.35f, cy), reticle, 5f);
                MetalDraw.Line(new Vector2(cx, cy + sign * radius), new Vector2(cx, cy + sign * radius * 0.35f), reticle, 5f);
                for (int dot = 1; dot <= 4; dot++)
                {
                    float d = radius * 0.07f * dot;
                    MetalDraw.Rect(cx + sign * d - 2f, cy - 2f, 4f, 4f, reticle);
                    MetalDraw.Rect(cx - 2f, cy + sign * d - 2f, 4f, 4f, reticle);
                }
            }
            MetalDraw.Rect(cx - 2f, cy - 2f, 4f, 4f, red);
            int range = Mathf.RoundToInt(Vector3.Distance(bot.GetAimPoint(), bot.transform.position));
            TextShadowed($"RANGE {range} m", cx + radius * 0.35f, cy + 12f, new Color(0.9f, 1f, 0.9f), Size(Small, 1.3f));
        }

        void DrawAwareness(MetalBot viewer)
        {
            float now = Time.time;
            foreach (MetalBotAI ai in MetalBotAI.All)
            {
                MetalBot enemy = ai.Pawn;
                if (enemy == null || !enemy.IsAlive || !enemy.IsHostileTo(viewer) || (enemy.transform.position - viewer.transform.position).sqrMagnitude > 60f * 60f) continue;
                bool alert = now - ai.AlertTime < 2.5f;
                float suspicion = ai.Suspicion;
                if (!alert && (suspicion < 0.02f || ai.State == MetalAIState.Combat)) continue;
                if (!Project(enemy.transform.position + Vector3.up * (enemy.Movement.FullHalfHeight + 0.45f), out Vector2 s)) continue;
                if (alert)
                {
                    // Pops in big, then settles.
                    float pop = 1f + 0.6f * Mathf.Max(0f, 1f - (now - ai.AlertTime) / 0.25f);
                    TextShadowed("!", s.x, s.y - 30f * pop, Danger, Size(Large, 0.8f * pop), true);
                    continue;
                }
                // A "?" whose meter fills from yellow to orange as the robot gets closer to noticing.
                Color c = LerpHSV(new Color(1f, 0.95f, 0.3f), new Color(1f, 0.45f, 0.1f), suspicion);
                TextShadowed("?", s.x, s.y - 30f, c, Size(Large, 0.65f), true);
                const float BarW = 34f;
                Bar(s.x - BarW * 0.5f, s.y + 6f, BarW, 4f, suspicion, c);
            }
        }

        void DrawResults(MetalGameMode gm, float W, float H)
        {
            int row = Size(Medium, 1.1f);
            int foot = Size(Small, 1.3f);
            float rowStep = TextH(row) + 10f;
            const float PanelW = 760f;
            float panelH = 22f + 4f * rowStep + 12f + TextH(foot) + 18f;
            float x = (W - PanelW) * 0.5f;
            float y = H * 0.46f;
            MetalDraw.Rect(x, y, PanelW, panelH, new Color(0.02f, 0.03f, 0.05f, 0.8f));
            MetalDraw.Rect(x, y, PanelW, 3f, Amber);
            float time = gm.MissionTime;
            float accuracy = pc.Accuracy;
            string[,] rows =
            {
                { "TIME", $"{Mathf.FloorToInt(time / 60f)}:{Mathf.FloorToInt(time % 60f):00}" },
                { "ENEMIES DESTROYED", gm.Kills.ToString() },
                { "TIMES SCRAPPED", gm.Deaths.ToString() },
                { "ACCURACY", accuracy < 0f ? "-" : $"{Mathf.RoundToInt(accuracy * 100f)}%" },
            };
            float rowY = y + 22f;
            for (int i = 0; i < 4; i++)
            {
                TextShadowed(rows[i, 0], x + 30f, rowY, new Color(1f, 1f, 1f, 0.75f), Medium);
                TextShadowed(rows[i, 1], x + PanelW - 30f - TextW(rows[i, 1], row), rowY, Color.white, row);
                rowY += rowStep;
            }
            TextShadowed(pc.UsingGamepad ? "[START] menu" : "[ENTER] play again      [ESC] menu", W * 0.5f, y + panelH - TextH(foot) - 16f, Amber, foot, true);
        }

        void DrawDialogueBox(MetalDialogueLine line, float lineTime, float openTime, float W, float H)
        {
            // Centred between the health panel (bottom left) and the weapon panel (bottom right).
            float boxW = Mathf.Clamp(W - 960f, 520f, 1000f);
            const float Pad = 16f;
            float textW = boxW - 2f * Pad;
            // Word-wrap the line to the box, keeping any line breaks the writer put in.
            var rows = new List<string>();
            foreach (string paragraph in line.line.Split('\n'))
            {
                string row = "";
                foreach (string word in paragraph.Split(new[] { ' ', '\t' }, System.StringSplitOptions.RemoveEmptyEntries))
                {
                    string candidate = row.Length == 0 ? word : row + " " + word;
                    if (TextW(candidate, Medium) > textW && row.Length > 0)
                    {
                        rows.Add(row);
                        row = word;
                    }
                    else
                    {
                        row = candidate;
                    }
                }
                rows.Add(row);
            }
            string name = (line.speaker ?? "").ToUpperInvariant();
            int nameSize = Size(Small, 1.3f);
            float nameH = name.Length > 0 ? TextH(nameSize) + 6f : 0f;
            float rowH = TextH(Medium);
            float boxH = Pad + nameH + rows.Count * rowH + Pad;
            // Pops up: slides in from below and fades in over the first 0.2 s.
            float pop = 1f - Mathf.Pow(1f - Mathf.Clamp01(openTime / 0.2f), 2f);
            float x = (W - boxW) * 0.5f;
            float y = H - 24f - boxH + (1f - pop) * 40f;
            MetalDraw.Rect(x, y, boxW, boxH, new Color(0.02f, 0.04f, 0.06f, 0.75f * pop));
            MetalDraw.Rect(x, y, boxW, 3f, Amber.WithAlpha(pop));
            float textY = y + Pad;
            if (name.Length > 0)
            {
                TextShadowed(name, x + Pad, textY, Amber.WithAlpha(pop), nameSize);
                textY += nameH;
            }
            // Type the line out one character at a time, so the finished line holds for a second before the next.
            int total = 0;
            foreach (string r in rows) total += r.Length;
            float cps = total / MetalDialogue.GetTalkTime(line);
            int remaining = Mathf.Min(total, Mathf.FloorToInt(lineTime * cps) + 1);
            foreach (string r in rows)
            {
                if (remaining <= 0) break;
                TextShadowed(r.Substring(0, Mathf.Min(remaining, r.Length)), x + Pad, textY, new Color(1f, 0.95f, 0.8f, pop), Medium);
                remaining -= r.Length;
                textY += rowH;
            }
        }

        void DrawRadioIcon(MetalBot pawn, float now)
        {
            if (!Project(pawn.transform.position + Vector3.up * (pawn.Movement.FullHalfHeight + 0.05f), out Vector2 s)) return;
            const float BodyW = 16f, BodyH = 22f;
            float l = s.x - BodyW * 0.5f;
            float t = s.y - BodyH - (1f + Mathf.Sin(now * 4f)) * 2f;
            MetalDraw.Rect(l, t, BodyW, BodyH, new Color(0.02f, 0.05f, 0.07f, 0.85f));
            MetalDraw.Frame(new Rect(l, t, BodyW, BodyH), Cyan, 2f);
            MetalDraw.Rect(l + 3f, t + 3f, BodyW - 6f, 5f, Cyan.WithAlpha(0.8f));
            for (int i = 0; i < 3; i++)
            {
                float gy = t + 12f + 3f * i;
                MetalDraw.Line(new Vector2(l + 4f, gy), new Vector2(l + BodyW - 4f, gy), Cyan.WithAlpha(0.6f), 1f);
            }
            // Antenna, with signal waves going out both sides of its tip.
            var tip = new Vector2(s.x, t - 14f);
            MetalDraw.Line(new Vector2(tip.x, t), tip, Cyan, 2f);
            MetalDraw.Rect(tip.x - 2f, tip.y - 2f, 4f, 4f, Cyan);
            const int Segments = 6;
            float halfArc = 50f * Mathf.Deg2Rad;
            for (int wave = 0; wave < 3; wave++)
            {
                // Each ring flashes a quarter cycle after the one inside it, so the signal ripples outward.
                float phase = Mathf.Repeat(now * 1.5f + 1f - wave * 0.25f, 1f);
                Color c = Cyan.WithAlpha(Mathf.Lerp(0.2f, 1f, 1f - phase));
                float r = 7f + 5f * wave;
                foreach (float facing in new[] { 0f, Mathf.PI })
                {
                    for (int seg = 0; seg < Segments; seg++)
                    {
                        float a0 = facing - halfArc + seg * 2f * halfArc / Segments;
                        float a1 = facing - halfArc + (seg + 1) * 2f * halfArc / Segments;
                        MetalDraw.Line(tip + new Vector2(Mathf.Cos(a0), Mathf.Sin(a0)) * r, tip + new Vector2(Mathf.Cos(a1), Mathf.Sin(a1)) * r, c, 2f);
                    }
                }
            }
        }

        void DrawMarker(Vector3 world, string label, Color color, bool edgeArrow, float W, float H)
        {
            Vector3 sp = cam != null ? cam.WorldToScreenPoint(world) : Vector3.zero;
            Vector2 screen = MetalDraw.FromScreen(sp);
            bool front = sp.z > 0f;
            const float Margin = 50f;
            bool onScreen = front && screen.x > Margin && screen.x < W - Margin && screen.y > Margin && screen.y < H - Margin;
            if (onScreen)
            {
                const float S = 9f;
                MetalDraw.Polyline(color, 2f, screen + new Vector2(0f, -S), screen + new Vector2(S, 0f), screen + new Vector2(0f, S),
                    screen + new Vector2(-S, 0f), screen + new Vector2(0f, -S));
                TextShadowed(label, screen.x, screen.y + 12f, color, Size(Small, 1.1f), true);
                return;
            }
            if (!edgeArrow) return;
            // Off screen (or behind): an arrow on the edge pointing the way to turn.
            Vector2 dir = screen - new Vector2(W * 0.5f, H * 0.5f);
            if (!front) dir = -dir;
            dir = dir.sqrMagnitude > 1e-4f ? dir.normalized : new Vector2(0f, 1f);
            float scale = Mathf.Min((W * 0.5f - Margin) / Mathf.Max(Mathf.Abs(dir.x), 1e-4f), (H * 0.5f - Margin) / Mathf.Max(Mathf.Abs(dir.y), 1e-4f));
            Vector2 tip = new Vector2(W * 0.5f, H * 0.5f) + dir * scale;
            var side = new Vector2(-dir.y, dir.x);
            Vector2 back = tip - dir * 18f;
            MetalDraw.Line(tip, back + side * 10f, color, 3f);
            MetalDraw.Line(tip, back - side * 10f, color, 3f);
            Vector2 textAt = tip - dir * 40f;
            TextShadowed(label, textAt.x, textAt.y - 8f, color, Size(Small, 1.1f), true);
        }

        void DrawSpaceBattle(MetalSpaceBattleGameMode battle, MetalBot viewer, float W)
        {
            MetalTeam mine = battle.playerTeam;
            MetalTeam theirs = mine == MetalTeam.Heroes ? MetalTeam.Villains : MetalTeam.Heroes;
            float now = Time.time;
            void Side(MetalTeam team, float centerX, string teamName)
            {
                Color color = team == MetalTeam.Heroes ? new Color(0.35f, 0.6f, 1f) : new Color(1f, 0.4f, 0.3f);
                int big = Size(Large, 0.9f);
                TextShadowed($"{teamName}  {battle.GetTickets(team)}", centerX, 18f, color, big, true);
                float hullY = 18f + TextH(big) + 4f;
                MetalCapitalShip ship = battle.GetShip(team);
                if (ship == null) return;
                const float BarW = 240f;
                float hull = ship.HullHealth / Mathf.Max(1f, ship.maxHullHealth);
                Bar(centerX - BarW * 0.5f, hullY, BarW, 8f, hull, ship.IsShielded ? new Color(0.4f, 0.8f, 1f) : new Color(1f, 0.6f, 0.2f));
                IReadOnlyList<MetalShipSubsystem> parts = ship.Subsystems;
                const float Pip = 14f, Gap = 6f;
                float x = centerX - (parts.Count * (Pip + Gap) - Gap) * 0.5f;
                foreach (MetalShipSubsystem part in parts)
                {
                    float f = part.Health / Mathf.Max(1f, part.maxHealth);
                    MetalDraw.Rect(x, hullY + 16f, Pip, Pip, new Color(0f, 0f, 0f, 0.6f));
                    MetalDraw.Rect(x + 2f, hullY + 18f, Pip - 4f, Pip - 4f,
                        part.IsDestroyed ? new Color(0.25f, 0.05f, 0.05f) : LerpHSV(new Color(1f, 0.2f, 0.1f), new Color(0.3f, 1f, 0.4f), f));
                    x += Pip + Gap;
                }
            }
            Side(mine, W * 0.5f - 200f, mine == MetalTeam.Heroes ? "HEROES" : "VILLAINS");
            Side(theirs, W * 0.5f + 200f, theirs == MetalTeam.Heroes ? "HEROES" : "VILLAINS");
            if (viewer == null) return;
            // Out in space: the way home, and the enemy's targets. Ship interiors sit far above the battle plane.
            MetalCapitalShip home = battle.GetShip(mine);
            MetalCapitalShip enemy = battle.GetShip(theirs);
            bool inSpace = Mathf.Abs(viewer.transform.position.y) < MetalCapitalShip.InteriorAltitude * 0.5f;
            if (inSpace && home != null && home.OuterPortal != null)
            {
                DrawMarker(home.OuterPortal.transform.position, "HANGAR", new Color(0.3f, 0.9f, 1f), true, W, MetalDraw.Height);
            }
            if (enemy != null)
            {
                float blink = Mathf.Repeat(now, 0.6f) < 0.3f ? 1f : 0.6f;
                foreach (MetalShipSubsystem part in enemy.Subsystems)
                {
                    if (!part.IsDestroyed && (part.Center - viewer.transform.position).sqrMagnitude < 2500f * 2500f)
                    {
                        DrawMarker(part.Center, part.Label, new Color(1f, 0.35f, 0.25f, blink), false, W, MetalDraw.Height);
                    }
                }
                if (inSpace && enemy.OuterPortal != null)
                {
                    DrawMarker(enemy.OuterPortal.transform.position, "ENEMY HANGAR", new Color(1f, 0.6f, 0.3f), false, W, MetalDraw.Height);
                }
            }
        }

        void DrawLeadReticle(MetalStalker flyer, float W, float H)
        {
            MetalWeaponSlot slot = flyer.CurrentWeapon;
            if (slot == null || slot.data == null || slot.data.fireMode != MetalFireMode.Projectile) return;
            float shotSpeed = Mathf.Max(10f, slot.data.projectileSpeed);
            var center = new Vector2(W * 0.5f, H * 0.5f);
            MetalBot best = null;
            Vector3 bestLead = Vector3.zero;
            float bestDist = 300f;
            foreach (MetalBot b in MetalBot.All)
            {
                if (!(b is MetalStalker s) || !s.IsAlive || s.IsParked || !flyer.IsHostileTo(s)
                    || (s.transform.position - flyer.transform.position).sqrMagnitude > 2000f * 2000f) continue;
                // First-order lead: where the target will be after a slug covers the current distance.
                Vector3 lead = s.transform.position + s.Velocity * (Vector3.Distance(s.transform.position, flyer.transform.position) / shotSpeed);
                if (!Project(lead, out Vector2 sp)) continue;
                float d = Vector2.Distance(sp, center);
                if (d < bestDist)
                {
                    best = s;
                    bestLead = lead;
                    bestDist = d;
                }
            }
            if (best == null || !Project(bestLead, out Vector2 leadScreen)) return;
            var color = new Color(1f, 0.85f, 0.2f, 0.9f);
            MetalDraw.RingShape(leadScreen, 12f, color);
            if (Project(best.transform.position, out Vector2 targetScreen)) MetalDraw.Line(targetScreen, leadScreen, color.WithAlpha(0.35f), 1f);
        }
    }
}
