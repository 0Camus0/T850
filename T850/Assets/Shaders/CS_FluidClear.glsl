#version 430
// GENERATED from CS_FluidClear.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=fc17a66dfae60138fe30255a4c6e9975a29392007dd6e0a13ada2e2e1feb5471; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    uint ThicknessShift;
    float ThicknessPad0;
    float ThicknessPad1;
    float ThicknessPad2;
} _24;

layout(binding = 1, std430) buffer DepthBits
{
    uint _data[];
} DepthBits_1;

layout(binding = 2, std430) buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

layout(binding = 3, std430) buffer AuxBits
{
    uint _data[];
} AuxBits_1;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _24.OutputWidth) || (gl_GlobalInvocationID.y >= _24.OutputHeight))
        {
            break;
        }
        uint _148 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _24.OutputWidth);
        DepthBits_1._data[_148] = 4294967295u;
        uint _153 = min(_24.ThicknessShift, 1u);
        ThicknessBits_1._data[_148] = 0u;
        uint _176 = ((gl_GlobalInvocationID.x >> _153) + ((gl_GlobalInvocationID.y >> _153) * (((_24.OutputWidth + (1u << _153)) - 1u) >> _153))) * 4u;
        AuxBits_1._data[_176] = 0u;
        AuxBits_1._data[_176 + 1u] = 0u;
        AuxBits_1._data[_176 + 2u] = 0u;
        AuxBits_1._data[_176 + 3u] = 0u;
        break;
    } while(false);
}

