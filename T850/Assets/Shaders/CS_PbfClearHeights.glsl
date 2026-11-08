#version 430
// GENERATED from CS_PbfClearHeights.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=24e8d58d909fbf6ee28ea384a940dfb7b298e82d65a0de0a28a7e88eb31aac3c; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float Pad6;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
} _18;

layout(binding = 1, std430) buffer ColumnHeights
{
    int _data[];
} ColumnHeights_1;

void main()
{
    uint _79 = (_18.SurfaceWidth * _18.SurfaceDepth) * 2u;
    for (uint _102 = 0u; _102 < 4u; _102++)
    {
        uint _89 = (gl_GlobalInvocationID.x * 4u) + _102;
        if (_89 < _79)
        {
            ColumnHeights_1._data[_89] = 0;
        }
    }
}

