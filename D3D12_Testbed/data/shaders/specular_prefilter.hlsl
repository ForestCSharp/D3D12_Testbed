#include "math.hlsl"

#include "brdf.hlsl"
#include "scene.hlsl"

cbuffer InstanceConstantBuffer : register(b1)
{
    int texture_index;
	float roughness;
};

#include "bindless.hlsl"

SamplerState cubemap_sampler : register(s0);

struct PsInput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    uint instance_id : SV_InstanceID;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR, const float2 uv : TEXCOORD, const uint instance_id : SV_InstanceID)
{
    PsInput result;
    
    const float4x4 model = identity;

    //Calculate world pos separately, as we pass it to the pixel shader as well
    const float4 world_pos = mul(model, float4(position, 1.0f));

    const float4x4 proj_view = mul(proj, view);
    const float4 out_position = mul(proj_view, world_pos);
    
    result.position = out_position;
    
    result.world_pos = position;
    result.instance_id = instance_id;

    return result;
}

struct PsOutput
{
    float4 rt_0: SV_Target0;
    float4 rt_1: SV_Target1;
    float4 rt_2: SV_Target2;
    float4 rt_3: SV_Target3;
    float4 rt_4: SV_Target4;
    float4 rt_5: SV_Target5;
};

float4 prefilter_env_map(float3 N)
{
    //float roughness = 0.0; //TODO: from cbuffer
    
    N = normalize(N); //TODO: Remove
    
    // make the simplyfying assumption that V equals R equals the normal 
    float3 R = N;
    float3 V = R;

    const uint SAMPLE_COUNT = 1024;
    float3 prefilteredColor = float3(0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        // generates a sample vector that's biased towards the preferred alignment direction (importance sampling).
        float2 Xi = hammersley_sequence(i, SAMPLE_COUNT);
        float3 H = importance_sample_ggx(Xi, N, roughness);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            // sample from the environment's mip level based on roughness/pdf
			float NoH = saturate(dot(N,H));
            float D   = distribution_ggx(NoH, roughness);
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001; 

            float resolution = 512.0; // resolution of source cubemap (per face)
            float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

            prefilteredColor += TextureCubeTable[texture_index].SampleLevel(cubemap_sampler , L, 0).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / totalWeight;

    return float4(prefilteredColor, 1.0);
}

PsOutput ps_main(const PsInput input) : SV_TARGET
{
    PsOutput output;

    float3 sample_dir = normalize(input.world_pos);

    output.rt_0     = prefilter_env_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), -90));
    output.rt_1     = prefilter_env_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 90));

	float3 rt_2_dir = float3_rotate_angle_axis(sample_dir, float3(0,0,1), 180);
	rt_2_dir        = float3_rotate_angle_axis(rt_2_dir, float3(1,0,0), 90);
    output.rt_2     = prefilter_env_map(rt_2_dir);

	float3 rt_3_dir = float3_rotate_angle_axis(sample_dir, float3(0,0,1), -180);
	rt_3_dir        = float3_rotate_angle_axis(rt_3_dir, float3(1,0,0), -90);
    output.rt_3     = prefilter_env_map(rt_3_dir);

    output.rt_4     = prefilter_env_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 180));
    output.rt_5     = prefilter_env_map(sample_dir);

    return output;
}