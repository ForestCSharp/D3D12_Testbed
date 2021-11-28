#include "math.hlsl"
#include "brdf.hlsl"

float2 IntegrateBRDF(float NoV, float roughness)
{
	float3 V;
	V.x = sqrt(1.0 - NoV * NoV);
	V.y = 0.0;
	V.z = NoV;

	float A = 0.0;
	float B = 0.0;

	const float3 N = float3(0.0, 0.0, 1.0);

	const uint sample_count = 1024;
	for (unsigned int i = 0u; i < sample_count; ++i)
	{
		float2 Xi = hammersley_sequence(i, sample_count);
		float3 H = importance_sample_ggx(Xi, N, roughness);
		float3 L = normalize(2.0f * dot(V, H) * H - V);

		float NoL = saturate(L.z);
		float NoH = saturate(H.z);
		float VoH = saturate(dot(V,H));

		if (NoL > 0.0)
		{
			float G = geometry_smith(NoV, NoL, roughness);

			float G_Vis = (G * VoH) / (NoH * NoV);
			float Fc = pow(1.0 - VoH, 5.0);

			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	return float2( A, B ) / sample_count;
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
    return integrated_brdf.xy;
}