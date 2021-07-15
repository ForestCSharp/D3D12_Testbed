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

#include "brdf_main.hlsli"

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 view_proj;
    //TODO: vars for PBR
    float4 view_pos;
    //TODO: light_array
};

//TODO: InstanceConstantBuffer

struct PsInput
{
    float4 position : SV_POSITION;
    float4 world_pos : POSITION;
    float4 normal : NORMAL;
    float4 color : COLOR;
};

PsInput vs_main(const float4 position : POSITION, const float4 normal : NORMAL, const float4 color : COLOR)
{
    PsInput result;

    //FCS TODO: Model_Matrix

    result.position = mul(view_proj, position);
    result.world_pos = position;
    result.normal = normal;
    result.color = color;

    return result;
}

float4 ps_main(const PsInput input) : SV_TARGET
{
    const float3 light_dir = float3(-1,-1,-1);
    const float3 view_dir = normalize(view_pos - input.world_pos).xyz;
    const float3 normal = normalize(input.normal).xyz;

    //TODO: per-instance args
    const float roughness = 0.5f;

    float3 f0 = float3(0,0,0);
    float3 brdf = brdf_main(normal, light_dir, view_dir, input.color.xyz, f0, roughness);
    return float4(brdf,1);
}
