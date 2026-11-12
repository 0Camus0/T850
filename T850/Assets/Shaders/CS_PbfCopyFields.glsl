#version 430
// GENERATED from CS_PbfCopyFields.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=7fa49586d155fb4e224afca547add2ede9a4e720721bd9a7d252440462307424; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, std140) uniform PbfConstants
{
    uint ParticleCount;
    uint GridX;
    uint GridY;
    uint GridZ;
    float DeltaTime;
    float SmoothingRadius;
    float ParticleRadius;
    float RestDensity;
    float StiffnessEps;
    float ArtPressureK;
    float ArtPressureN;
    float ViscosityC;
    float Gravity;
    float MaxVelocity;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float BasinMaxX;
    float BasinMaxY;
    float BasinMaxZ;
    uint BoxCount;
    uint Iteration;
    uint SurfaceWidth;
    uint SurfaceDepth;
    float WaterPlaneY;
    float RestScale;
    float RestDenomReference;
    float MinNeighbours;
    float WallTopY;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
} _22;

layout(binding = 2, std430) buffer Destination
{
    vec4 _data[];
} Destination_1;

layout(binding = 1, std430) readonly buffer Source
{
    vec4 _data[];
} Source_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _22.ParticleCount)
        {
            break;
        }
        Destination_1._data[gl_GlobalInvocationID.x] = Source_1._data[gl_GlobalInvocationID.x];
        break;
    } while(false);
}

