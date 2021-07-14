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

#include "brdf.hlsli"

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 view_proj;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput vs_main(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = mul(view_proj, position);
    result.color = color;

    return result;
}

float4 ps_main(PSInput input) : SV_TARGET
{
    return input.color;
}
