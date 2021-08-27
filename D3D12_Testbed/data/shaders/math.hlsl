#ifndef __MATH_HLSL__
#define __MATH_HLSL__

static const float PI = 3.14159265359;

static matrix identity =
{
    { 1, 0, 0, 0 },
    { 0, 1, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, 0, 1 }
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

float4x4 matrix_uniform_scale(float scalar)
{
    return float4x4
    (
        scalar, 0,      0,      0,
        0,      scalar, 0,      0,
        0,      0,      scalar, 0,
        0,      0,      0,      1
    );
}

#endif //__MATH_HLSL__