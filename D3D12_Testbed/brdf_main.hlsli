
// Specular BRDF //

static const float PI = 3.14159265f;

// distribution
float fr_d_ggx(const float roughness, const float n_dot_h)
{
    const float one_minus_n_dot_h_squared = 1.0 - n_dot_h * n_dot_h;
    const float a = n_dot_h * roughness;
    const float k = roughness / (one_minus_n_dot_h_squared + a * a);
    const float d = k * k * (1.0 / PI);
    return d;
}

// visibility
float fr_v_smith_ggx_correlated(const float roughness, const float n_dot_v, const float n_dot_l)
{
    const float a2 = roughness * roughness;
    const float lambda_v = n_dot_l * sqrt((n_dot_v - a2 * n_dot_v) * n_dot_v + a2);
    const float lambda_l = n_dot_v * sqrt((n_dot_l - a2 * n_dot_l) * n_dot_l + a2);
    const float v = 0.5 / (lambda_v + lambda_l);
    return v;
}

// TODO: specular fresnel

float3 isotropic_lobe(const float roughness, const float n_dot_h, const float n_dot_v, const float n_dot_l)
{
    const float distribution = fr_d_ggx(roughness, n_dot_h);
    const float visibility = fr_v_smith_ggx_correlated(roughness, n_dot_v, n_dot_l);
    // vec3  F = fresnel(pixel.f0, LoH); //TODO:
    return (distribution * visibility) /* * fresnel */;
}

float3 specular_lobe(const float roughness, const float n_dot_h, const float n_dot_v, const float n_dot_l)
{
    //TODO: anistropic_lobe option?
    return isotropic_lobe(roughness, n_dot_h, n_dot_v, n_dot_l);
}

// Diffuse BRDF //

float fd_burley_shlick(const float f0, const float f90, const float VoH) {
    return f0 + (f90 - f0) * pow(1.0 - VoH, 5.0);
}

float fd_burley(const float roughness, const float n_dot_v, const float n_dot_l, const float l_dot_h)
{
    // Burley 2012, "Physically-Based Shading at Disney"
    const float f90 = 0.5 + 2.0 * roughness * l_dot_h * l_dot_h;
    const float light_scatter = fd_burley_shlick(1.0, f90, n_dot_l);
    const float view_scatter  = fd_burley_shlick(1.0, f90, n_dot_v);
    return light_scatter * view_scatter * (1.0 / PI);
}

float3 diffuse_lobe(const float3 diffuse_color, const float roughness, const float n_dot_v, const float n_dot_l, const float l_dot_h)
{
    return diffuse_color * fd_burley(roughness, n_dot_v, n_dot_l, l_dot_h);
}

// Main BRDF Function //
//  n	Surface normal unit vector
//  l	Incident light unit vector (from light to surface) //FCS TODO: is this right
//  v	View unit vector (from surface to eye)
//  h	Half unit vector between l and v
float3 brdf_main(const float3 n, const float3 l, const float3 v, const float3 diffuse_color, const float3 f0, const float roughness)
{
    const float3 h = normalize(l + v);
    
    const float n_dot_l = dot(n,l);
    const float n_dot_v = dot(n,v);
    const float n_dot_h = dot(n,h);
    
    const float l_dot_h = dot(l,h);

    //TODO: Remap roughness -> roughness * roughness

    const float3 specular = specular_lobe(roughness, n_dot_h, n_dot_v, n_dot_l);
    const float3 diffuse = diffuse_lobe(diffuse_color, roughness, n_dot_v, n_dot_l, l_dot_h);

    //TODO: light attenuation arg
    const float light_attenuation = 0.1f;
    
    return (specular + diffuse) * light_attenuation;
}