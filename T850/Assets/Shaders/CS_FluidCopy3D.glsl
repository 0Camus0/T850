#version 430
// GENERATED from CS_FluidCopy3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=b409dddf4d1d5a0f5460a279054b2d9c48aee22b5a2690b5867773347232bc8d; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

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

layout(binding = 2, std430) buffer VoxelsOut
{
    uvec4 _data[];
} VoxelsOut_1;

layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _20.Width) || (gl_GlobalInvocationID.y >= _20.Height)) || (gl_GlobalInvocationID.z >= _20.Depth))
        {
            break;
        }
        vec4 _160 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0);
        VoxelsOut_1._data[(((gl_GlobalInvocationID.z * _20.Height) + gl_GlobalInvocationID.y) * _20.Width) + gl_GlobalInvocationID.x] = uvec4(floatBitsToUint(_160.x), floatBitsToUint(_160.y), floatBitsToUint(_160.z), 0u);
        break;
    } while(false);
}

