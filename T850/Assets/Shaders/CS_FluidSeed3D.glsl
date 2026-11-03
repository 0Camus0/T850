#version 430
// GENERATED from CS_FluidSeed3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=ff5003ba9f5ab61f95c29814ae2bb9375db3d70658aceccbb1403ee82386ed0a; defines=T850_SPIRV=1,T850_VULKAN=1
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

layout(binding = 1, rgba16f) uniform writeonly image3D VelocityOut;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _20.Width) || (gl_GlobalInvocationID.y >= _20.Height)) || (gl_GlobalInvocationID.z >= _20.Depth))
        {
            break;
        }
        imageStore(VelocityOut, ivec3(gl_GlobalInvocationID), vec4(1.0, 0.0, 0.0, 0.0));
        break;
    } while(false);
}

