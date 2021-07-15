
static const float PI = 3.14159265359;

float distribution_ggx(const float n_dot_h, const float roughness)
{
    const float a = roughness*roughness;
    const float a2 = a*a;
    const float n_dot_h_squared = n_dot_h*n_dot_h;

    const float nom   = a2;
    float denom = (n_dot_h_squared * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0000001); // prevent divide by zero for roughness=0.0 and NdotH=1.0
}

float geometry_schlick_ggx(const float n_dot_v, const float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r*r) / 8.0;

    const float nom   = n_dot_v;
    const float denom = n_dot_v * (1.0 - k) + k;

    return nom / denom;
}

float geometry_smith(const float n_dot_v, const float n_dot_l, const float roughness)
{
    const float ggx2 = geometry_schlick_ggx(n_dot_v, roughness);
    const float ggx1 = geometry_schlick_ggx(n_dot_l, roughness);
    return ggx1 * ggx2;
}

float3 fresnel_schlick(const float cos_theta, const float3 f0)
{
    return f0 + (1.0 - f0) * pow(max(1.0 - cos_theta, 0.0), 5.0);
}


// Diffuse BRDF //
#define MIN_N_DOT_V 1e-4

float clamp_n_dot_v(const float n_dot_v) {
    // Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    return max(n_dot_v, MIN_N_DOT_V);
}

// Main BRDF Function //
//  n	Surface normal unit vector
//  l	Incident light unit vector (from surface to light)
//  v	View unit vector (from surface to eye)
//  h	Half unit vector between l and v
float4 brdf_main(const float3 n, const float3 l, const float3 v, const float3 albedo, const float3 f0, const float roughness, const float metallic, const float3 radiance)
{
    const float3 h = normalize(v + l);

    const float n_dot_v = saturate(dot(n,v));   
    const float n_dot_l = saturate(dot(n,l));
    const float n_dot_h = saturate(dot(n,h));

    const float h_dot_v = saturate(dot(h,v));

    const float  ndf  = distribution_ggx(n_dot_h, roughness);   
    const float  g    = geometry_smith(n_dot_v, n_dot_l, roughness); 
    const float3 f    = fresnel_schlick(h_dot_v, f0);
           
    const float3 numerator  = ndf * g * f; 
    const float denominator = 4 * n_dot_v * n_dot_l;
    const float3 specular   = numerator / max(denominator, 0.001); // prevent divide by zero for NdotV=0.0 or NdotL=0.0

    // kS is equal to Fresnel
    const float3 ks = f;
    // for energy conservation, the diffuse and specular light can't
    // be above 1.0 (unless the surface emits light); to preserve this
    // relationship the diffuse component (kD) should equal 1.0 - kS.
    float3 kd = float3(1,1,1) - ks;
    // multiply kD by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kd *= 1.0 - metallic;

    float3 out_color = (kd * albedo / PI + specular) * radiance * n_dot_l;

    //TODO: Pull tonemap and gamma correct out of brdf
    // HDR tonemapping
    out_color = out_color / (out_color + float3(1,1,1));
    
    // gamma correct
    float gamma_factor = 1.0/2.2;
    out_color = pow(out_color, float3(gamma_factor, gamma_factor, gamma_factor));
    
    return float4(out_color, 1.f);
}