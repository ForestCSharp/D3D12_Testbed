#include "math.hlsl"

#include "scene.hlsl"
#include "instance.hlsl"
#include "bindless.hlsl"

SamplerState cubemap_sampler : register(s0);

struct PsInput
{
    float4 position : SV_POSITION;
    float3 world_pos : POSITION;
    uint instance_id : SV_InstanceID;
};

PsInput vs_main(const float3 position : POSITION, const float3 normal : NORMAL, const float4 color : COLOR, const float2 uv : TEXCOORD, const uint instance_id : SV_InstanceID)
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
	//TODO: FIX Names
    float4 front: SV_Target0;
    float4 back: SV_Target1;
    float4 top: SV_Target2;
    float4 bottom: SV_Target3;
    float4 left: SV_Target4;
    float4 right: SV_Target5;
};

float4 convolute(const float3 in_dir)
{
	float3 irradiance = float3(0.0, 0.0, 0.0);   
    
    // tangent space calculation from origin point
    float3    up = float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(up, in_dir));
    		  up = normalize(cross(in_dir, right));
       
    float sample_delta = 0.025;
    float nr_samples = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sample_delta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sample_delta)
        {
            // spherical to cartesian (in tangent space)
            float3 tangent_sample = float3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            float3 sample_vec = tangent_sample.x * right + tangent_sample.y * up + tangent_sample.z * in_dir; 

            irradiance += TextureCubeTable[texture_index].Sample(cubemap_sampler, sample_vec).rgb * cos(theta) * sin(theta);
            nr_samples++;
        }
    }

    irradiance = PI * irradiance * (1.0 / float(nr_samples));

	return float4(irradiance,1);
}

PsOutput ps_main(const PsInput input) : SV_TARGET
{
    PsOutput output;

    float3 sample_dir = normalize(input.world_pos);

    output.front   	  = convolute(sample_dir);
    output.back       = convolute(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 180));

	float3 top_dir 	  = float3_rotate_angle_axis(sample_dir, float3(0,0,1), 90);
	top_dir 	   	  = float3_rotate_angle_axis(top_dir, float3(1,0,0), 90);
    output.top     	  = convolute(top_dir);

	float3 bot_dir 	  = float3_rotate_angle_axis(sample_dir, float3(0,0,1), -90);
	bot_dir 	   	  = float3_rotate_angle_axis(bot_dir, float3(1,0,0), -90);
    output.bottom  	  = convolute(bot_dir);

    output.left    	  = convolute(float3_rotate_angle_axis(sample_dir, float3(0,1,0), -90));
    output.right   	  = convolute(float3_rotate_angle_axis(sample_dir, float3(0,1,0), 90));

    return output;
}