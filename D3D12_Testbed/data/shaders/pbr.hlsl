#include "math.hlsl"
#include "brdf.hlsl"

#include "scene.hlsl"

cbuffer InstanceConstantBuffer : register(b1)
{
    uint diffuse_ibl_texture_index;
    uint specular_ibl_texture_index;
    uint specular_ibl_mip_count;
    uint specular_lut_texture_index;
};

#include "bindless.hlsl"

SamplerState texture_sampler : register(s0);

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
    const float roughness = 1.0 - (float)(input.instance_id / 10) / 10.0;
    const float metallic  = 1.0 - fmod(input.instance_id, 10) / 10.0;
    
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
        lights[i].intensity = 50.0f;
        
        const float3 to_light = lights[i].pos - input.world_pos;
        const float distance_to_light_squared = pow(length(to_light),2);
        const float3 light_dir = normalize(to_light);
        const float light_attenuation = 1.0 / distance_to_light_squared * lights[i].intensity;
        const float3 radiance = lights[i].color * light_attenuation;
    
        brdf_lighting += brdf_main(normal, light_dir, view_dir, albedo, f0, roughness, metallic, radiance);
    }

    float n_dot_v = saturate(dot(normal, view_dir));
    float3 F = fresnel_schlick_roughness(n_dot_v, f0, roughness);
    const float3 ks = F;
    float3 kd = 1.0 - ks;
    kd *= (1.0 - metallic);

    //Diffuse IBL
	const float3 irradiance = TextureCubeTable[diffuse_ibl_texture_index].Sample(texture_sampler, normal).rgb;
    const float3 diffuse = irradiance * albedo;

    //Specular IBL
    const float3 r = reflect(-view_dir, normal);
    const float max_lod = specular_ibl_mip_count - 1.0;
    float3 prefiltered_color = TextureCubeTable[specular_ibl_texture_index].SampleLevel(texture_sampler, r, roughness * max_lod).rgb;
    float2 env_brdf = Texture2DTable[specular_lut_texture_index].Sample(texture_sampler, float2(n_dot_v, roughness)).rg;
    float3 specular = prefiltered_color * (F * env_brdf.x + env_brdf.y);

    const float3 ambient = (kd * diffuse + specular) * 0.75f;
    
    float3 out_color = brdf_lighting + ambient;

    // HDR tonemapping
    out_color = out_color / (out_color + float3(1,1,1));
    
    // gamma correct
    float gamma = 2.2;
    float gamma_factor = 1.0/gamma;
    out_color = pow(out_color, float3(gamma_factor, gamma_factor, gamma_factor));

    return float4(out_color,1);
}
