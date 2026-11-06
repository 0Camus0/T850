#version 430
// GENERATED from CS_FluidSurfaceToBuffer.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=fff927bece81c3f72043f703d2e8a04a5efe48fc5e394c7173f15d50efebc1b0; defines=T850_SPIRV=1,T850_VULKAN=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform FluidConstants
{
    uint Width;
    uint Height;
    uint Depth;
    uint Iteration;
    float DeltaTime;
    float CellSize;
    float Viscosity;
    float Gravity;
    float WindX;
    float WindY;
    float WindZ;
    float Damping;
    float SurfaceLevel;
    float TimeValue;
    float PressureScale;
    float Padding0;
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
} _20;

layout(binding = 2, std430) buffer SurfaceOut
{
    vec4 _data[];
} SurfaceOut_1;

layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSurfaceInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _20.Width) || (gl_GlobalInvocationID.y >= _20.Depth))
        {
            break;
        }
        SurfaceOut_1._data[gl_GlobalInvocationID.x + (_20.Width * gl_GlobalInvocationID.y)] = texelFetch(SPIRV_Cross_CombinedSurfaceInSPIRV_Cross_DummySampler, ivec3(int(gl_GlobalInvocationID.x), int(gl_GlobalInvocationID.y), 0).xy, 0);
        break;
    } while(false);
}

