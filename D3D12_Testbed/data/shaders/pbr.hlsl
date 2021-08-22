#include "brdf_main.hlsli"

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 view;
    float4x4 proj;
    //TODO: vars for PBR
    float4 cam_pos;
	float4 cam_dir;
    //TODO: light_array
};

//TODO: InstanceConstantBuffer

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

//TODO: replace with cubemap once we've computed it
float4 sample_environment_map(const float3 v)
{
    const float2 uv = spherical_uv(v);
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

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR, const float2 uv : TEXCOORD, const uint instance_id : SV_InstanceID)
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

struct Light
{
    float3 pos;
    float3 color;
    float intensity;
};

#define NUM_LIGHTS 4

float4 ps_main(const PsInput input) : SV_TARGET
{
    const float3 view_dir = normalize(cam_pos - input.world_pos).xyz;
    const float3 normal = normalize(input.normal).xyz;

    //TODO: per-instance args
    const float3 albedo = input.color.rgb;
    const float roughness = (float)(input.instance_id / 10) / 10.0;
    const float metallic  = fmod(input.instance_id, 10) / 10.0;
    
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    //TODO: Pass from CPU
    Light lights[NUM_LIGHTS];
    float offset = 2;
	float3 forward_offset = cam_dir.xyz * 3;
	float3 cam_up 	 = float3(0,1,0);
	float3 cam_right = cross(cam_up, cam_dir);
    lights[0].pos = cam_pos.xyz + forward_offset + (cam_up * offset);
    lights[1].pos = cam_pos.xyz + forward_offset - (cam_up * offset);
    lights[2].pos = cam_pos.xyz + forward_offset + (cam_right * offset);
    lights[3].pos = cam_pos.xyz + forward_offset - (cam_right * offset);

    float3 brdf_lighting = float3(0,0,0);

    for (int i = 0; i < NUM_LIGHTS; ++i)
    {
        lights[i].color = float3(1,1,1);
        lights[i].intensity = 5.0f;
        
        const float3 to_light = lights[i].pos - input.world_pos;
        const float distance_to_light_squared = pow(length(to_light),2);
        const float3 light_dir = normalize(to_light);
        const float light_attenuation = 1.0 / distance_to_light_squared * lights[i].intensity;
        const float3 radiance = lights[i].color * light_attenuation;
    
        brdf_lighting += brdf_main(normal, light_dir, view_dir, albedo, f0, roughness, metallic, radiance);
    }

    //Sample environment map
    const float3 irradiance = sample_environment_map(normal).rgb;
    
    const float3 ks = fresnel_schlick(max(dot(normal, view_dir), 0.0), f0);
    float3 kd = 1.0 - ks;
    kd *= 1.0 - metallic;
    const float3 diffuse = irradiance * albedo;
    const float3 ambient = (kd * diffuse) * 0.75f;
    
    float3 out_color = brdf_lighting + ambient;

    // HDR tonemapping
    out_color = out_color / (out_color + float3(1,1,1));
    
    // gamma correct
    float gamma_factor = 1.0/2.2;
    out_color = pow(out_color, float3(gamma_factor, gamma_factor, gamma_factor));

    return float4(out_color,1);
}
