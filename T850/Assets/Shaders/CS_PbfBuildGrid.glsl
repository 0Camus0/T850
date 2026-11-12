#version 430
// GENERATED from CS_PbfBuildGrid.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=4a4cf31cec7a8eacf4bbe81b87058aa0397f392101751bd3dcd6f8ce05cccb9f; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 1, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 2, std430) buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _22.ParticleCount)
        {
            break;
        }
        float _206 = 1.0 / _22.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            break;
        }
        vec4 _215 = Predicted_1._data[gl_GlobalInvocationID.x];
        uint _272 = uint(clamp(int(floor((_215.x - _22.GridOriginX) * _206)), 0, int(_22.GridX) - 1)) + (_22.GridX * (uint(clamp(int(floor((_215.y - _22.GridOriginY) * _206)), 0, int(_22.GridY) - 1)) + (_22.GridY * uint(clamp(int(floor((_215.z - _22.GridOriginZ) * _206)), 0, int(_22.GridZ) - 1)))));
        uint _275 = atomicAdd(GridBuffer_1._data[_272], 1u);
        if (_275 < 64u)
        {
            GridBuffer_1._data[(64u * (((_22.GridX * _22.GridY) * _22.GridZ) + _272)) + _275] = gl_GlobalInvocationID.x;
        }
        break;
    } while(false);
}

