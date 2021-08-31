#include "math.hlsl"
#include "brdf.hlsl"


float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V;
    V.x = sqrt(1.0 - NdotV*NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    float3 N = float3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 1024u;
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = hammersley_sequence(i, SAMPLE_COUNT);
        float3 H  = importance_sample_ggx(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if(NdotL > 0.0)
        {
            float G = geometry_smith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return float2(A, B);
}

struct PsInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PsInput vs_main(const float2 position : POSITION, const  float2 uv : TEXCOORD0)
{
    PsInput ps_input;
    ps_input.position = float4(position,0,1);
    ps_input.uv = uv;
    return ps_input;
}

float2 ps_main(const PsInput input) : SV_TARGET
{
    float2 integrated_brdf = IntegrateBRDF(input.uv.x, input.uv.y);
    return integrated_brdf;
}