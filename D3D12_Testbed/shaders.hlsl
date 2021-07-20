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
    float4x4 view;
    float4x4 proj;
    //TODO: vars for PBR
    float4 cam_pos;
    //TODO: light_array
};

//TODO: InstanceConstantBuffer

static matrix identity =
{
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 }
};

float4x4 m_translate(const float3 v)
{
    float4x4 m = identity;
    const float x = v.x, y = v.y, z = v.z;
    m[0][3] = x;
    m[1][3] = y;
    m[2][3] = z;
    return m;
}

struct PsInput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    uint instance_id : SV_InstanceID;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR,  const uint instance_id : SV_InstanceID)
{
    PsInput result;

    //FCS TODO: pass in Model_Matrix

    const float offset_x = 2.25 * (instance_id / 10) - 10.0;
    const float offset_y = -2.25 * fmod(instance_id, 10);
    
    const float4x4 model = m_translate(float3(offset_x, offset_y, 0));

    //Calculate world pos separately, as we pass it to the pixel shader as well
    const float4 world_pos = mul(model, float4(position, 1.0f));

    const float4x4 proj_view = mul(proj, view);
    const float4 out_position = mul(proj_view, world_pos);
    
    result.position = out_position;
    result.world_pos = world_pos;
    result.normal = normal;
    result.color = color;
    result.instance_id = instance_id;

    return result;
}

float4 ps_main(const PsInput input) : SV_TARGET
{
    const float3 light_pos = float3(10,10,0);

    const float3 to_light = light_pos - input.world_pos;
    const float distance = length(to_light);
    const float3 light_dir = normalize(to_light);
    const float3 view_dir = normalize(cam_pos - input.world_pos).xyz;
    const float3 normal = normalize(input.normal).xyz;

    //TODO: per-instance args
    const float3 albedo = input.color.rgb;
    const float roughness = (float)(input.instance_id / 10) / 10.0;
    const float metallic  = fmod(input.instance_id, 10) / 10.0;
    
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    //Once we're iterating over lights, get from there
    const float3 light_color = float3(1,1,1) * 10.0f;
    const float light_attenuation = 1.0 / (distance * distance);
    const float3 radiance = light_color * light_attenuation;
    
    const float3 brdf = brdf_main(normal, light_dir, view_dir, albedo, f0, roughness, metallic, radiance);

    // ambient lighting (note that the next IBL tutorial will replace 
    // this ambient lighting with environment lighting).
    const float ambient_intensity = 0.01;
    const float4 ambient = float4(0.03, 0.03, 0.03, 1.0) * float4(albedo,1) * ambient_intensity;
    
    float3 out_color = brdf + ambient;

    // HDR tonemapping
    out_color = out_color / (out_color + float3(1,1,1));
    
    // gamma correct
    float gamma_factor = 1.0/2.2;
    out_color = pow(out_color, float3(gamma_factor, gamma_factor, gamma_factor));

    return float4(out_color,1);
}
