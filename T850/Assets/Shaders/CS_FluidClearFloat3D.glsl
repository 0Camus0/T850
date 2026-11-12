#version 430
// GENERATED from CS_FluidClearFloat3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=98e5502db13e23b551385bd7bab7a69c24be11224abdef83eb0d53c33bb0b2e8; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 1, r32f) uniform writeonly image3D TargetOut;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _20.Width) || (gl_GlobalInvocationID.y >= _20.Height)) || (gl_GlobalInvocationID.z >= _20.Depth))
        {
            break;
        }
        imageStore(TargetOut, ivec3(gl_GlobalInvocationID), vec4(0.0));
        break;
    } while(false);
}

