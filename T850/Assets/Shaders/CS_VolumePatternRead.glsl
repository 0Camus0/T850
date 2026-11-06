#version 430
// GENERATED from CS_VolumePatternRead.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=5da6fe6ce23f926000041edc30a2ecbee80308f598c6cd9b1b82ad34f1fda80d; defines=T850_SPIRV=1,T850_VULKAN=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform VolumeConstantsBuffer
{
    uint Width;
    uint Height;
    uint Depth;
    uint Seed;
} _19;

layout(binding = 2, std430) buffer Result
{
    uint _data[];
} Result_1;

layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedInputSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _19.Width) || (gl_GlobalInvocationID.y >= _19.Height)) || (gl_GlobalInvocationID.z >= _19.Depth))
        {
            break;
        }
        uvec4 _181 = uvec4(texelFetch(SPIRV_Cross_CombinedInputSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0));
        uint _222 = atomicAdd(Result_1._data[0], uint((((_181.x != (((((gl_GlobalInvocationID.x * 3u) + (gl_GlobalInvocationID.y * 11u)) + (gl_GlobalInvocationID.z * 131u)) + _19.Seed) % 64u)) || (_181.y != gl_GlobalInvocationID.z)) || (_181.z != gl_GlobalInvocationID.y)) || (_181.w != gl_GlobalInvocationID.x)));
        uint _224 = atomicAdd(Result_1._data[1], 1u);
        break;
    } while(false);
}

