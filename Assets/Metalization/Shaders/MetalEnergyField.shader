// See-through energy field: a tinted, scanlined slab with bright edges, and up to four ring ripples spreading
// from where shots hit (MetalEnergyField writes _Hit0.._Hit3 = world centre xyz, age w; w < 0 = unused).
Shader "Metalization/EnergyField"
{
    Properties
    {
        [HDR] _BaseColor ("Color", Color) = (1, 0.3, 0.1, 1)
        _RippleLife ("Ripple Life", Float) = 0.9
        _Hit0 ("Hit 0", Vector) = (0, 0, 0, -1)
        _Hit1 ("Hit 1", Vector) = (0, 0, 0, -1)
        _Hit2 ("Hit 2", Vector) = (0, 0, 0, -1)
        _Hit3 ("Hit 3", Vector) = (0, 0, 0, -1)
    }
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent" "RenderPipeline" = "UniversalPipeline" "IgnoreProjector" = "True" }
        Blend SrcAlpha One
        ZWrite Off
        Cull Off
        Pass
        {
            Name "EnergyField"
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            CBUFFER_START(UnityPerMaterial)
                half4 _BaseColor;
                float _RippleLife;
                float4 _Hit0, _Hit1, _Hit2, _Hit3;
            CBUFFER_END
            struct Attributes { float4 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
            struct Varyings { float4 positionHCS : SV_POSITION; float3 positionWS : TEXCOORD0; float3 normalWS : TEXCOORD1; float2 uv : TEXCOORD2; };
            Varyings vert(Attributes v)
            {
                Varyings o;
                o.positionWS = TransformObjectToWorld(v.positionOS.xyz);
                o.positionHCS = TransformWorldToHClip(o.positionWS);
                o.normalWS = TransformObjectToWorldNormal(v.normalOS);
                o.uv = v.uv;
                return o;
            }
            float Ripple(float4 hit, float3 p)
            {
                if (hit.w < 0) return 0;
                float t = saturate(hit.w / max(0.01, _RippleLife));
                float r = t * 3.0;
                float d = abs(distance(p, hit.xyz) - r);
                return saturate(1 - d / 0.25) * (1 - t) * 2.5;
            }
            half4 frag(Varyings i) : SV_Target
            {
                float3 view = normalize(GetCameraPositionWS() - i.positionWS);
                float rim = pow(1 - abs(dot(normalize(i.normalWS), view)), 3);
                float scan = 0.6 + 0.4 * sin(i.positionWS.y * 18 + _Time.y * 4);
                float edge = max(smoothstep(0.96, 1.0, abs(i.uv.x * 2 - 1)), smoothstep(0.96, 1.0, abs(i.uv.y * 2 - 1)));
                float ripple = Ripple(_Hit0, i.positionWS) + Ripple(_Hit1, i.positionWS) + Ripple(_Hit2, i.positionWS) + Ripple(_Hit3, i.positionWS);
                float a = saturate(0.18 * scan + 0.5 * rim + 0.6 * edge + ripple);
                return half4(_BaseColor.rgb * (1 + ripple * 2), a);
            }
            ENDHLSL
        }
    }
}
