#version 430
// GENERATED from CS_VolumeFloatPatternWrite.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=fa7ff9fb51435d85d11631af4ed9b69ed8b759d47ad009fa48e9e8226f016d2a; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform VolumeConstantsBuffer
{
    uint Width;
    uint Height;
    uint Depth;
    uint Seed;
} _19;

layout(binding = 1, r32f) uniform writeonly image3D Output;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _19.Width) || (gl_GlobalInvocationID.y >= _19.Height)) || (gl_GlobalInvocationID.z >= _19.Depth))
        {
            break;
        }
        imageStore(Output, ivec3(gl_GlobalInvocationID), vec4(float(((((gl_GlobalInvocationID.x * 3u) + (gl_GlobalInvocationID.y * 11u)) + (gl_GlobalInvocationID.z * 131u)) + _19.Seed) % 64u) + 0.3333333432674407958984375));
        break;
    } while(false);
}

