//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 view;
    float4x4 proj;
    float4 cam_pos;
};

#define myTex2DSpace space1
#define myTexCubeSpace space2

#define BINDLESS_TABLE_SIZE 10000 //TODO Sync up with C++

Texture2D Texture2DTable[BINDLESS_TABLE_SIZE]      : register(t0, myTex2DSpace);
TextureCube TextureCubeTable[BINDLESS_TABLE_SIZE]  : register(t0, myTexCubeSpace);

//Testing Equirectangular Sampling
//TextureCube       cubemap_texture : register(t0);
SamplerState      cubemap_sampler : register(s0);

static matrix identity =
{
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 }
};

float4x4 uniform_scale(float scalar)
{
    return float4x4
    (
        scalar, 0,      0,      0,
        0,      scalar, 0,      0,
        0,      0,      scalar, 0,
        0,      0,      0,      1
    );
}

struct PsInput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR, const float2 uv : TEXCOORD)
{
    PsInput result;
    
    const float4x4 model = uniform_scale(5000);

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
    float4 out_color = TextureCubeTable[0].Sample(cubemap_sampler, dir);

    return out_color;
}
