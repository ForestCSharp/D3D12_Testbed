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
    float3 world_pos : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR)
{
    PsInput result;

    //FCS TODO: Model_Matrix

    result.position = mul(view_proj, float4(position, 1));
    result.world_pos = position;
    result.normal = normal;
    result.color = color;

    return result;
}

float4 ps_main(const PsInput input) : SV_TARGET
{
    const float3 light_pos = float3(10,10,0);

    const float3 to_light = light_pos - input.world_pos;
    const float distance = length(to_light);
    const float3 light_dir = normalize(to_light);
    const float3 view_dir = normalize(view_pos - input.world_pos).xyz;
    const float3 normal = normalize(input.normal).xyz;

    //TODO: per-instance args
    const float3 albedo = input.color.rgb;
    const float roughness = 0.15f;
    const float metallic = 0.5f;
    
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    //Once we're iterating over lights, get from there
    const float3 light_color = float3(1,1,1) * 10.0f;
    const float light_attenuation = 1.0 / (distance * distance);
    const float3 radiance = light_color * light_attenuation;
    
    const float4 brdf = brdf_main(normal, light_dir, view_dir, albedo, f0, roughness, metallic, radiance);

    // ambient lighting (note that the next IBL tutorial will replace 
    // this ambient lighting with environment lighting).
    const float3 ambient = float3(0.03, 0.03, 0.03) * albedo * 0.1;
    
    return brdf + float4(ambient, 1);
}
