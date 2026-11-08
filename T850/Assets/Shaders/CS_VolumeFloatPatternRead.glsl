#version 430
// GENERATED from CS_VolumeFloatPatternRead.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=8d656e80ced9bfbabf03de150a46dd2147a60226acb2d96b20814f00cd2a57c3; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        uint _197 = atomicAdd(Result_1._data[0], uint(abs(texelFetch(SPIRV_Cross_CombinedInputSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0).x - (float(((((gl_GlobalInvocationID.x * 3u) + (gl_GlobalInvocationID.y * 11u)) + (gl_GlobalInvocationID.z * 131u)) + _19.Seed) % 64u) + 0.3333333432674407958984375)) > 9.9999997473787516355514526367188e-05));
        uint _199 = atomicAdd(Result_1._data[1], 1u);
        break;
    } while(false);
}

