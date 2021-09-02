#include "bindless.hlsl"

cbuffer InstanceConstantBuffer : register(b1)
{
    uint texture_index;
    uint texture_lod;
};

SamplerState texture_sampler : register(s0);

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

float4 ps_main(const PsInput input) : SV_TARGET
{
    return Texture2DTable[texture_index].SampleLevel(texture_sampler, input.uv, texture_lod);
}