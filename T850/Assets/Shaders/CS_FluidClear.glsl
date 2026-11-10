#version 430
// GENERATED from /home/camus000/testBitso/T850/Assets/Shaders/CS_FluidClear.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=1a533598a5456712bc5b0b6eb6ab872e506b866ff9673c7ade95d5de8a767fe9; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform ParticleConstants
{
    uint ParticleCount;
    uint OutputWidth;
    uint OutputHeight;
    uint Iteration;
    float Radius;
    float Opacity;
    float Intensity;
    float Softness;
    float DepthBias;
    float Lifetime;
    float Spread;
    float Rise;
    float TimeSeconds;
    float SizeScaleMin;
    float SizeScaleMax;
    uint Shape;
    uint Blend;
    uint Source;
    float ScreenSpace;
    float TileCountX;
    float TileCountY;
    uint FrameIndex;
    float ProjScaleY;
    float Style;
    float DepthRadiusScale;
    float InvPackingFactor;
    float ProjA;
    float ProjB;
    vec3 Color0;
    float Color0Pad;
    vec3 Color1;
    float Color1Pad;
    vec3 Color2;
    float Color2Pad;
    vec3 Color3;
    float Color3Pad;
    vec3 EmitterPosition;
    float EmitterPad;
    vec4 ViewProjection[4];
} _24;

layout(binding = 1, std430) buffer DepthBits
{
    uint _data[];
} DepthBits_1;

layout(binding = 2, std430) buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _24.OutputWidth) || (gl_GlobalInvocationID.y >= _24.OutputHeight))
        {
            break;
        }
        uint _100 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _24.OutputWidth);
        DepthBits_1._data[_100] = 4294967295u;
        ThicknessBits_1._data[_100] = 0u;
        break;
    } while(false);
}

