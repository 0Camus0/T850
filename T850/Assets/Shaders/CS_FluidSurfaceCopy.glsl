#version 430
// GENERATED from CS_FluidSurfaceCopy.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=2481dc1e92dc578b50b1e1b37ac8da4dc7aa7a035afa3e8130349dc046c73d30; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 1, rgba16f) uniform writeonly image2D SurfaceOut;
layout(binding = 0) uniform sampler2D SPIRV_Cross_CombinedSurfaceInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        uvec2 _96 = uvec2(imageSize(SurfaceOut));
        if ((gl_GlobalInvocationID.x >= _96.x) || (gl_GlobalInvocationID.y >= _96.y))
        {
            break;
        }
        imageStore(SurfaceOut, ivec2(gl_GlobalInvocationID.xy), texelFetch(SPIRV_Cross_CombinedSurfaceInSPIRV_Cross_DummySampler, ivec3(ivec2(gl_GlobalInvocationID.xy), 0).xy, 0));
        break;
    } while(false);
}

