// A minimal JSON reader for the Blender export manifest (Unity's JsonUtility can't read dictionaries).
// Objects become Dictionary<string, object>, arrays List<object>, numbers double.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Metalization.Editor
{
    public static class MetalJson
    {
        public static object Parse(string text)
        {
            int i = 0;
            object v = Value(text, ref i);
            return v;
        }

        static void Skip(string s, ref int i)
        {
            while (i < s.Length && char.IsWhiteSpace(s[i])) i++;
        }

        static object Value(string s, ref int i)
        {
            Skip(s, ref i);
            char c = s[i];
            if (c == '{') return Obj(s, ref i);
            if (c == '[') return Arr(s, ref i);
            if (c == '"') return Str(s, ref i);
            if (s.Substring(i).StartsWith("true")) { i += 4; return true; }
            if (s.Substring(i).StartsWith("false")) { i += 5; return false; }
            if (s.Substring(i).StartsWith("null")) { i += 4; return null; }
            int start = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            return double.Parse(s.Substring(start, i - start), CultureInfo.InvariantCulture);
        }

        static Dictionary<string, object> Obj(string s, ref int i)
        {
            var d = new Dictionary<string, object>();
            i++;
            Skip(s, ref i);
            if (s[i] == '}') { i++; return d; }
            while (true)
            {
                Skip(s, ref i);
                string key = Str(s, ref i);
                Skip(s, ref i);
                i++; // :
                d[key] = Value(s, ref i);
                Skip(s, ref i);
                if (s[i] == ',') { i++; continue; }
                i++; // }
                return d;
            }
        }

        static List<object> Arr(string s, ref int i)
        {
            var l = new List<object>();
            i++;
            Skip(s, ref i);
            if (s[i] == ']') { i++; return l; }
            while (true)
            {
                l.Add(Value(s, ref i));
                Skip(s, ref i);
                if (s[i] == ',') { i++; continue; }
                i++; // ]
                return l;
            }
        }

        static string Str(string s, ref int i)
        {
            var sb = new StringBuilder();
            i++;
            while (s[i] != '"')
            {
                if (s[i] == '\\')
                {
                    i++;
                    char e = s[i];
                    switch (e)
                    {
                        case 'n': sb.Append('\n'); break;
                        case 't': sb.Append('\t'); break;
                        case 'r': sb.Append('\r'); break;
                        case 'u': sb.Append((char)Convert.ToInt32(s.Substring(i + 1, 4), 16)); i += 4; break;
                        default: sb.Append(e); break;
                    }
                }
                else sb.Append(s[i]);
                i++;
            }
            i++;
            return sb.ToString();
        }

        // --- Access helpers ---------------------------------------------------------------------------------

        public static Dictionary<string, object> O(this object o, string key) =>
            o is Dictionary<string, object> d && d.TryGetValue(key, out object v) ? v as Dictionary<string, object> : null;

        public static List<object> A(this object o, string key) =>
            o is Dictionary<string, object> d && d.TryGetValue(key, out object v) ? v as List<object> : null;

        public static string S(this object o, string key) =>
            o is Dictionary<string, object> d && d.TryGetValue(key, out object v) ? v as string : null;

        public static float F(this object o, string key, float fallback = 0f) =>
            o is Dictionary<string, object> d && d.TryGetValue(key, out object v) && v is double n ? (float)n : fallback;

        public static float[] Floats(this object o)
        {
            if (!(o is List<object> l)) return new float[0];
            var r = new float[l.Count];
            for (int i = 0; i < l.Count; i++) r[i] = l[i] is double d ? (float)d : 0f;
            return r;
        }
    }
}
