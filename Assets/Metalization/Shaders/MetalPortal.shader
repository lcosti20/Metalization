// Hangar portal window: shows the far end's view (a render texture) sampled in screen space, so it lines up
// exactly with the main view (MetalPortal renders it from the matching position on the other side).
Shader "Metalization/Portal"
{
    Properties
    {
        _BaseMap ("View", 2D) = "black" {}
    }
    SubShader
    {
        Tags { "RenderType" = "Opaque" "Queue" = "Geometry" "RenderPipeline" = "UniversalPipeline" }
        Cull Off
        Pass
        {
            Name "Portal"
            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
            TEXTURE2D(_BaseMap);
            SAMPLER(sampler_BaseMap);
            CBUFFER_START(UnityPerMaterial)
                float4 _BaseMap_ST;
            CBUFFER_END
            struct Attributes { float4 positionOS : POSITION; };
            struct Varyings { float4 positionHCS : SV_POSITION; float4 screen : TEXCOORD0; };
            Varyings vert(Attributes v)
            {
                Varyings o;
                o.positionHCS = TransformObjectToHClip(v.positionOS.xyz);
                o.screen = ComputeScreenPos(o.positionHCS);
                return o;
            }
            half4 frag(Varyings i) : SV_Target
            {
                float2 uv = i.screen.xy / i.screen.w;
                return SAMPLE_TEXTURE2D(_BaseMap, sampler_BaseMap, uv);
            }
            ENDHLSL
        }
    }
}
