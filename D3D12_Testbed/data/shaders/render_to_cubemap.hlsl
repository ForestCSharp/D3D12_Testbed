#include "math.hlsl"

#include "scene.hlsl"
#include "instance.hlsl" //TODO: Remove
#include "bindless.hlsl"

SamplerState hdr_sampler : register(s0);

float2 spherical_uv(float3 v)
{
    v = normalize(v);
    float2 uv = float2(atan2(v.z, v.x), asin(v.y));
    const float2 inv_atan = float2(0.1591, 0.3183);
    uv *= inv_atan;
    uv += 0.5;
    return uv;
}

float4 sample_spherical_map(const float3 v)
{
    float2 uv = spherical_uv(v);
    const float3 color = Texture2DTable[texture_index].Sample(hdr_sampler, uv).rgb;
    return float4(color, 1);
}

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

PsOutput ps_main(const PsInput input) : SV_TARGET
{
    PsOutput output;

    float3 sample_dir = normalize(input.world_pos);

    output.rt_0		= sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), -90));
    output.rt_1     = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 90));

	float3 rt_2_dir = float3_rotate_angle_axis(sample_dir, float3(0,0,1), 180);
	rt_2_dir        = float3_rotate_angle_axis(rt_2_dir, float3(1,0,0), 90);
    output.rt_2     = sample_spherical_map(rt_2_dir);

	float3 rt_3_dir = float3_rotate_angle_axis(sample_dir, float3(0,0,1), -180);
	rt_3_dir        = float3_rotate_angle_axis(rt_3_dir, float3(1,0,0), -90);
    output.rt_3     = sample_spherical_map(rt_3_dir);

    output.rt_4     = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 180));
    output.rt_5     = sample_spherical_map(sample_dir);

    return output;
}
