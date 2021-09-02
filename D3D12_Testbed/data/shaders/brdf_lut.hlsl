#include "math.hlsl"
// #include "brdf.hlsl"

float RadicalInverse_VdC(unsigned int bits)
{
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(unsigned int i, unsigned int N)
{
	return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float roughness, float3 N)
{
	float a = roughness*roughness;

	float phi = 2.0 * PI * Xi.x;
	float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
	float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

	// from spherical coordinates to cartesian coordinates
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	// from tangent-space vector to world-space sample vector
	float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
	float3 tangent = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);

	float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
	return normalize(sampleVec);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
	float a = roughness;
	float k = (a * a) / 2.0;

	float nom = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return nom / denom;
}

float GeometrySmith(float roughness, float NoV, float NoL)
{
	float ggx2 = GeometrySchlickGGX(NoV, roughness);
	float ggx1 = GeometrySchlickGGX(NoL, roughness);

	return ggx1 * ggx2;
}

float2 IntegrateBRDF(float NdotV, float roughness, uint samples)
{
	float3 V;
	V.x = sqrt(1.0 - NdotV * NdotV);
	V.y = 0.0;
	V.z = NdotV;

	float A = 0.0;
	float B = 0.0;

	float3 N = float3(0.0, 0.0, 1.0);

	for (unsigned int i = 0u; i < samples; ++i)
	{
		float2 Xi = Hammersley(i, samples);
		float3 H = ImportanceSampleGGX(Xi, roughness, N);
		float3 L = normalize(2.0f * dot(V, H) * H - V);

		float NoL = max(L.z, 0.0f);
		float NoH = max(H.z, 0.0f);
		float VoH = max(dot(V, H), 0.0f);
		float NoV = max(dot(N, V), 0.0f);

		if (NoL > 0.0)
		{
			float G = GeometrySmith(roughness, NoV, NoL);

			float G_Vis = (G * VoH) / (NoH * NoV);
			float Fc = pow(1.0 - VoH, 5.0);

			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	return float2(A / float(samples), B / float(samples));
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
	const uint SAMPLE_COUNT = 1024;
    float2 integrated_brdf = IntegrateBRDF(input.uv.x, input.uv.y, SAMPLE_COUNT);
    return integrated_brdf;
}