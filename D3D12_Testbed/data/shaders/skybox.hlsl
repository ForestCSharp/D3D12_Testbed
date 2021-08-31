#include "math.hlsl"

#include "scene.hlsl"
#include "instance.hlsl" //TODO: Remove
#include "bindless.hlsl"


//Testing Equirectangular Sampling
SamplerState      cubemap_sampler : register(s0);

struct PsInput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR, const float2 uv : TEXCOORD)
{
    PsInput result;
    
    const float4x4 model = matrix_uniform_scale(5000);

    //Calculate world pos separately, as we pass it to the pixel shader as well
    const float4 world_pos = mul(model, float4(position, 1.0f));

    const float4x4 proj_view = mul(proj, view);
    const float4 out_position = mul(proj_view, world_pos);
    
    result.position = out_position;
    
    result.world_pos = position;

    return result;
}

float4 ps_main(const PsInput input) : SV_TARGET
{
    const float3 dir = normalize(input.world_pos);    
    float4 out_color = TextureCubeTable[texture_index].SampleLevel(cubemap_sampler, dir, texture_lod);

    return out_color;
}
