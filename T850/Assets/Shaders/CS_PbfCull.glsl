#version 430
// GENERATED from CS_PbfCull.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=9cf5cc9e0d77d920ab13c00e73f28d6a10c9460bb18002dbf7fa9b3493dac658; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _22;

layout(binding = 1, std430) buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 2, std430) buffer Velocities
{
    vec4 _data[];
} Velocities_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _22.ParticleCount)
        {
            break;
        }
        vec4 _201 = Positions_1._data[gl_GlobalInvocationID.x];
        if (_201.y <= (-999.0))
        {
            Positions_1._data[gl_GlobalInvocationID.x].w = 0.0;
            break;
        }
        if (((!((!isnan(_201.x)) && (!isinf(_201.x)))) || (!((!isnan(_201.y)) && (!isinf(_201.y))))) || (!((!isnan(_201.z)) && (!isinf(_201.z)))))
        {
            Positions_1._data[gl_GlobalInvocationID.x] = vec4(0.0, -1000.0, 0.0, 0.0);
            Velocities_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            break;
        }
        if ((!(_201.y < (_22.BasinMinY - (2.0 * _22.ParticleRadius)))) && (!((((_201.x < (_22.BasinMinX - 0.60000002384185791015625)) || (_201.x > (_22.BasinMaxX + 0.60000002384185791015625))) || (_201.z < (_22.BasinMinZ - 0.60000002384185791015625))) || (_201.z > (_22.BasinMaxZ + 0.60000002384185791015625)))))
        {
            break;
        }
        Positions_1._data[gl_GlobalInvocationID.x] = vec4(0.0, -1000.0, 0.0, 0.0);
        Velocities_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
        break;
    } while(false);
}

