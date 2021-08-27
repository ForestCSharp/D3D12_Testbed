
cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 view;
    float4x4 proj;
    float4 cam_pos;
    float4 cam_dir;
    uint texture_index; //TODO: move to instance cbuffer
};