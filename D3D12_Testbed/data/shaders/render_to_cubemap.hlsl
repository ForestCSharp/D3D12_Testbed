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
    //TODO: vars for PBR
    float4 cam_pos;
    //TODO: light_array
};

//Testing Equirectangular Sampling
Texture2D<float4> hdr_texture : register(t0);
SamplerState      hdr_sampler : register(s0);

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
    const float3 color = hdr_texture.Sample(hdr_sampler, uv).rgb;
    return float4(color, 1);
}

static matrix identity =
{
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 }
};

struct PsInput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    uint instance_id : SV_InstanceID;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR,  const uint instance_id : SV_InstanceID)
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
    float4 front: SV_Target0;
    float4 back: SV_Target1;
    float4 left: SV_Target2;
    float4 right: SV_Target3;
    float4 top: SV_Target4;
    float4 bottom: SV_Target5;
};

float3x3 angle_axis_3x3(float angle, float3 axis)
{
    float c, s;
    sincos(angle, s, c);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return float3x3(
        t * x * x + c,      t * x * y - s * z,  t * x * z + s * y,
        t * x * y + s * z,  t * y * y + c,      t * y * z - s * x,
        t * x * z - s * y,  t * y * z + s * x,  t * z * z + c
    );
}

float3 float3_rotate_angle_axis(float3 in_vector, float3 axis, float in_degrees)
{
    return mul(angle_axis_3x3(radians(in_degrees), axis), in_vector);
}

PsOutput ps_main(const PsInput input) : SV_TARGET
{
    PsOutput output;

    float3 sample_dir = normalize(input.world_pos);

    output.front  = sample_spherical_map(sample_dir);
    output.back   = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 180));
    output.left   = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), -90));
    output.right  = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 90));
    output.top    = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(1,0,0), 90));
    output.bottom = sample_spherical_map(float3_rotate_angle_axis(sample_dir, float3(1,0,0), -90));

    return output;
}
