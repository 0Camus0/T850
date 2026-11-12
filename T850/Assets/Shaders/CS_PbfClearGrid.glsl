#version 430
// GENERATED from CS_PbfClearGrid.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=bf27bdfb47ce1ea75965eaae69f5b9ebf1c77123680e86c30b73116abfbb0d0f; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _18;

layout(binding = 1, std430) buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

void main()
{
    uint _91 = ((_18.GridX * _18.GridY) * _18.GridZ) * 128u;
    for (uint _115 = 0u; _115 < 4u; _115++)
    {
        uint _101 = (gl_GlobalInvocationID.x * 4u) + _115;
        if (_101 < _91)
        {
            GridBuffer_1._data[_101] = 0u;
        }
    }
}

