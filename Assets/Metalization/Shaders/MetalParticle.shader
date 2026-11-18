// Code-built effects (MetalFX): a soft round sprite tinted by the particle / line colour, no texture needed.
// Additive materials (Dst Blend One) add light (sparks, flashes, fire, beams); blended ones (smoke, dust) use
// alpha. _Beam 1 fades only across the width (line renderers), 0 fades radially (particles).
Shader "Metalization/Particle"
{
    Properties
    {
        [HDR] _BaseColor ("Tint", Color) = (1, 1, 1, 1)
        [Toggle] _Beam ("Beam (fade across width only)", Float) = 0
        [Enum(UnityEngine.Rendering.BlendMode)] _SrcBlend ("Src Blend", Float) = 1
        [Enum(UnityEngine.Rendering.BlendMode)] _DstBlend ("Dst Blend", Float) = 1
    }
    SubShader
    {
        Tags { "RenderType" = "Transparent" "Queue" = "Transparent" "RenderPipeline" = "UniversalPipeline" "IgnoreProjector" = "True" }
        Blend [_SrcBlend] [_DstBlend]
        ZWrite Off
        Cull Off
        Pass
        {
            Name "Particle"
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            CBUFFER_START(UnityPerMaterial)
                half4 _BaseColor;
                float _Beam;
                float _SrcBlend;
                float _DstBlend;
            CBUFFER_END
            struct Attributes { float4 positionOS : POSITION; half4 color : COLOR; float2 uv : TEXCOORD0; };
            struct Varyings { float4 positionHCS : SV_POSITION; half4 color : COLOR; float2 uv : TEXCOORD0; };
            Varyings vert(Attributes v)
            {
                Varyings o;
                o.positionHCS = TransformObjectToHClip(v.positionOS.xyz);
                o.color = v.color * _BaseColor;
                o.uv = v.uv;
                return o;
            }
            half4 frag(Varyings i) : SV_Target
            {
                float2 p = i.uv * 2 - 1;
                float d = _Beam > 0.5 ? abs(p.y) : length(p);
                float a = saturate(1 - d);
                a = a * a * (3 - 2 * a);
                half4 c = i.color;
                // Additive: brightness carries the falloff; blended: alpha does.
                if (_DstBlend == 1) return half4(c.rgb * a * saturate(c.a), 1);
                return half4(c.rgb, saturate(c.a) * a);
            }
            ENDHLSL
        }
    }
}
