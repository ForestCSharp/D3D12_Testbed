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
    //TODO: light_array
};

//TODO: InstanceConstantBuffer

struct PsInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PsInput vs_main(const float4 position : POSITION, const float4 color : COLOR)
{
    PsInput result;

    result.position = mul(view_proj, position);
    result.color = color;

    return result;
}

float4 ps_main(const PsInput input) : SV_TARGET
{
    return input.color;
}
