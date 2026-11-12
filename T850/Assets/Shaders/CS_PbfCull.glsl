#version 430
// GENERATED from CS_PbfCull.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=8fd26e4be10d12c79609decc642c60f335e2e1a2fdac54e9cb31b76d7f24dcb4; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        vec4 _237 = Positions_1._data[gl_GlobalInvocationID.x];
        if (_237.y <= (-999.0))
        {
            Positions_1._data[gl_GlobalInvocationID.x].w = 0.0;
            break;
        }
        bvec3 _252 = notEqual((floatBitsToUint(_237.xyz) & uvec3(2139095040u)), uvec3(2139095040u));
        if (((!_252.x) || (!_252.y)) || (!_252.z))
        {
            Positions_1._data[gl_GlobalInvocationID.x] = vec4(0.0, -1000.0, 0.0, 0.0);
            Velocities_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            break;
        }
        if (((!(_237.y < (_22.BasinMinY - (2.0 * _22.ParticleRadius)))) && (!((((_237.x < (_22.BasinMinX - 0.60000002384185791015625)) || (_237.x > (_22.BasinMaxX + 0.60000002384185791015625))) || (_237.z < (_22.BasinMinZ - 0.60000002384185791015625))) || (_237.z > (_22.BasinMaxZ + 0.60000002384185791015625))))) && (!(((((_237.x < _22.BasinMinX) || (_237.x > _22.BasinMaxX)) || (_237.z < _22.BasinMinZ)) || (_237.z > _22.BasinMaxZ)) && (_237.y < max(_22.WallTopY, (_22.WaterPlaneY + 0.1599999964237213134765625) + (2.0 * _22.ParticleRadius))))))
        {
            break;
        }
        Positions_1._data[gl_GlobalInvocationID.x] = vec4(0.0, -1000.0, 0.0, 0.0);
        Velocities_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
        break;
    } while(false);
}

