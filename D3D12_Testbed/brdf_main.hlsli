
/// Specular brdf_main

static const float PI = 3.14159265f;

float fs_d_ggx(const float n_dot_h, const float roughness)
{
    const float a = n_dot_h * roughness;
    const float k = roughness / (1.0 - n_dot_h * n_dot_h + a * a);
    return k * k * (1.0 / PI);
}

float fs_v_smith_ggx_correlated(const float n_dot_v, const float n_dot_l, const float roughness) {
    const float a2 = roughness * roughness;
    const float ggx_v = n_dot_l * (n_dot_v * (1.0 - a2) + a2);
    const float ggx_l = n_dot_v * (n_dot_l * (1.0 - a2) + a2);
    return 0.5 / (ggx_v + ggx_l);
}

float3 fs_fresnel_schlick(const float v_dot_h, const float3 f0)
{
    const float f = pow(1.0 - v_dot_h, 5.0);
    return f + f0 * (1.0 - f);
}

float3 brdf_specular(const float3 f0, const float roughness, const float v_dot_h, const float n_dot_l, const float n_dot_v, const float n_dot_h)
{
    const float d_ggx = fs_d_ggx(n_dot_h, roughness);
    const float g_ggx = fs_v_smith_ggx_correlated(n_dot_v, n_dot_l, roughness);
    const float fresnel = fs_fresnel_schlick(v_dot_h, f0);

    return (d_ggx * g_ggx * fresnel) / (4 * n_dot_v * n_dot_l); //FCS TODO: Do this division later?
}

/// Diffuse brdf_main

#define DIFFUSE_LAMBERT             0
#define DIFFUSE_BURLEY              1

#define DIFFUSE_METHOD                DIFFUSE_BURLEY

float fd_lambert() {
    return 1.0 / PI;
}

float fd_fresnel_schlick(const float f0, const float f90, const float v_dot_h) {
    return f0 + (f90 - f0) * pow(1.0 - v_dot_h, 5);
}

float fd_burley(const float roughness, const float n_dot_v, const float n_dot_l, const float l_dot_h) {
    // Burley 2012, "Physically-Based Shading at Disney"
    const float f90 = 0.5 + 2.0 * roughness * l_dot_h * l_dot_h;
    const float light_scatter = fd_fresnel_schlick(1.0, f90, n_dot_l);
    const float view_scatter  = fd_fresnel_schlick(1.0, f90, n_dot_v);
    return light_scatter * view_scatter * (1.0 / PI);
}

float3 brdf_diffuse(const float3 diffuse_color, const float roughness, const float n_dot_v, const float n_dot_l, const float l_dot_h)
{
#if DIFFUSE_METHOD == DIFFUSE_LAMBERT
    return diffuse_color * fd_lambert(); //FCS TODO: Consider removing this
#else
    return diffuse_color * fd_burley(roughness, n_dot_v, n_dot_l, l_dot_h);
#endif
}

/// Main BRDF Function
//  n	Surface normal unit vector
//  l	Incident light unit vector
//  v	View unit vector
//  h	Half unit vector between l and v
float3 brdf_main(const float3 n, const float3 l, const float3 v, const float3 h, const float3 diffuse_color, const float3 f0, const float roughness)
{
    const float n_dot_l = dot(n,l);
    const float n_dot_v = dot(n,v);
    const float n_dot_h = dot(n,h);
    
    const float l_dot_h = dot(l,h);

    const float v_dot_h = dot(v,h);

    const float3 specular = brdf_specular(f0, roughness, v_dot_h, n_dot_l, n_dot_v, n_dot_h);
    const float3 diffuse  = brdf_diffuse(diffuse_color, roughness, n_dot_v, n_dot_l, l_dot_h);

    return specular + diffuse; //FCS TODO: Test
}