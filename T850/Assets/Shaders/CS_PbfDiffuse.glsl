#version 430
// GENERATED from CS_PbfDiffuse.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=f2669fe351884ca9cce8dd9ea57dec13ce9c28e1f9ecab8785fa4ee7415fbcfc; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
#if defined(GL_EXT_control_flow_attributes)
#extension GL_EXT_control_flow_attributes : require
#define SPIRV_CROSS_FLATTEN [[flatten]]
#define SPIRV_CROSS_BRANCH [[dont_flatten]]
#define SPIRV_CROSS_UNROLL [[unroll]]
#define SPIRV_CROSS_LOOP [[dont_unroll]]
#else
#define SPIRV_CROSS_FLATTEN
#define SPIRV_CROSS_BRANCH
#define SPIRV_CROSS_UNROLL
#define SPIRV_CROSS_LOOP
#endif
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, std140) uniform PbfWhitewaterConstants
{
    uint ParticleCount;
    uint DiffuseCapacity;
    uint GridX;
    uint GridY;
    uint GridZ;
    uint FrameSeed;
    uint SurfaceWidth;
    uint SurfaceDepth;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float CellSize;
    float DeltaTime;
    float Spacing;
    float Gravity;
    float SurfaceNeighbours;
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float BasinMaxX;
    float BasinMaxY;
    float BasinMaxZ;
    float WaterPlaneY;
    float DeckY;
    float TrappedAirMin;
    float TrappedAirMax;
    float CrestMin;
    float CrestMax;
    float EnergyMin;
    float EnergyMax;
    float FoamGain;
    float FoamDecay;
    float AerationGain;
    float AerationDecay;
    float SprayRate;
    float CrestRate;
    float DiffuseLifetime;
    float BubbleBuoyancy;
    float BubbleDrag;
    float SprayDrag;
    float WetOriginX;
    float WetOriginZ;
    float WetTexelSize;
    float WetDecay;
    uint WetWidth;
    uint WetHeight;
    uint Mode;
    float CausticGain;
} _24;

layout(binding = 5, std430) buffer Wetness
{
    uint _data[];
} Wetness_1;

layout(binding = 4, std430) buffer Diffuse
{
    vec4 _data[];
} Diffuse_1;

layout(binding = 3, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

layout(binding = 1, std430) readonly buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 2, std430) readonly buffer Velocities
{
    vec4 _data[];
} Velocities_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _24.DiffuseCapacity)
        {
            break;
        }
        uint _726 = gl_GlobalInvocationID.x * 2u;
        vec4 _729 = Diffuse_1._data[_726];
        if (_729.w <= 0.0)
        {
            break;
        }
        uint _754 = (_24.GridX * _24.GridY) * _24.GridZ;
        float _757 = max(_24.CellSize, 9.9999997473787516355514526367188e-05);
        float _759 = 1.0 / _757;
        int _768 = int(floor((_729.x - _24.GridOriginX) * _759));
        int _777 = int(floor((_729.y - _24.GridOriginY) * _759));
        int _786 = int(floor((_729.z - _24.GridOriginZ) * _759));
        float _1265;
        vec3 _1266;
        float _1267;
        _1267 = 0.0;
        _1266 = vec3(0.0);
        _1265 = 0.0;
        float _1326;
        vec3 _1329;
        float _1332;
        SPIRV_CROSS_LOOP
        for (int _1264 = -1; _1264 <= 1; _1267 = _1332, _1266 = _1329, _1265 = _1326, _1264++)
        {
            _1332 = _1267;
            _1329 = _1266;
            _1326 = _1265;
            float _1325;
            vec3 _1328;
            float _1331;
            SPIRV_CROSS_LOOP
            for (int _1315 = -1; _1315 <= 1; _1315++, _1332 = _1331, _1329 = _1328, _1326 = _1325)
            {
                _1331 = _1332;
                _1328 = _1329;
                _1325 = _1326;
                float _1357;
                vec3 _1359;
                float _1361;
                SPIRV_CROSS_LOOP
                for (int _1317 = -1; _1317 <= 1; _1331 = _1361, _1328 = _1359, _1325 = _1357, _1317++)
                {
                    int _804 = _768 + _1317;
                    int _807 = _777 + _1315;
                    int _810 = _786 + _1264;
                    if ((((((_804 < 0) || (_807 < 0)) || (_810 < 0)) || (_804 >= int(_24.GridX))) || (_807 >= int(_24.GridY))) || (_810 >= int(_24.GridZ)))
                    {
                        _1361 = _1331;
                        _1359 = _1328;
                        _1357 = _1325;
                        continue;
                    }
                    uint _852 = uint(_804) + (_24.GridX * (uint(_807) + (_24.GridY * uint(_810))));
                    uint _856 = min(GridBuffer_1._data[_852], 64u);
                    float _1324;
                    vec3 _1327;
                    float _1330;
                    _1330 = _1331;
                    _1327 = _1328;
                    _1324 = _1325;
                    float _1358;
                    vec3 _1360;
                    float _1362;
                    SPIRV_CROSS_LOOP
                    for (uint _1320 = 0u; _1320 < _856; _1330 = _1362, _1327 = _1360, _1324 = _1358, _1320++)
                    {
                        uint _869 = (64u * (_754 + _852)) + _1320;
                        if (GridBuffer_1._data[_869] >= _24.ParticleCount)
                        {
                            _1362 = _1330;
                            _1360 = _1327;
                            _1358 = _1324;
                            continue;
                        }
                        if (Positions_1._data[GridBuffer_1._data[_869]].w < 0.5)
                        {
                            _1362 = _1330;
                            _1360 = _1327;
                            _1358 = _1324;
                            continue;
                        }
                        vec3 _889 = _729.xyz - Positions_1._data[GridBuffer_1._data[_869]].xyz;
                        float _892 = dot(_889, _889);
                        if (_892 >= (_757 * _757))
                        {
                            _1362 = _1330;
                            _1360 = _1327;
                            _1358 = _1324;
                            continue;
                        }
                        float _904 = 1.0 - (sqrt(_892) * _759);
                        _1362 = _1330 + 1.0;
                        _1360 = _1327 + (Velocities_1._data[GridBuffer_1._data[_869]].xyz * _904);
                        _1358 = _1324 + _904;
                    }
                    _1361 = _1330;
                    _1359 = _1327;
                    _1357 = _1324;
                }
            }
        }
        vec3 _938 = _1266 / vec3(max(_1265, 9.9999999747524270787835121154785e-07));
        vec3 _942 = vec3(0.0, -_24.Gravity, 0.0);
        float _961 = _729.w - _24.DeltaTime;
        vec3 _1270;
        float _1307;
        if (_1267 < 6.0)
        {
            _1307 = 0.0;
            _1270 = (Diffuse_1._data[(gl_GlobalInvocationID.x * 2u) + 1u].xyz + (_942 * _24.DeltaTime)) * max(1.0 - (_24.SprayDrag * _24.DeltaTime), 0.0);
        }
        else
        {
            bool _964 = _1267 <= 20.0;
            vec3 _1271;
            if (_964)
            {
                _1271 = _938;
            }
            else
            {
                _1271 = Diffuse_1._data[(gl_GlobalInvocationID.x * 2u) + 1u].xyz + (((_942 * (-_24.BubbleBuoyancy)) * _24.DeltaTime) + ((_938 - Diffuse_1._data[(gl_GlobalInvocationID.x * 2u) + 1u].xyz) * _24.BubbleDrag));
            }
            _1307 = _964 ? 1.0 : 2.0;
            _1270 = _1271;
        }
        vec3 _996 = _729.xyz + (_1270 * _24.DeltaTime);
        vec3 _1282;
        float _1292;
        vec3 _1296;
        if ((((_996.x > _24.BasinMinX) && (_996.x < _24.BasinMaxX)) && (_996.z > _24.BasinMinZ)) && (_996.z < _24.BasinMaxZ))
        {
            vec3 _1283;
            vec3 _1297;
            if (_996.y < (_24.BasinMinY + 0.00999999977648258209228515625))
            {
                vec3 _1249 = _996;
                _1249.y = _24.BasinMinY + 0.00999999977648258209228515625;
                vec3 _1252 = _1270;
                _1252.y = max(_1270.y, 0.0);
                _1297 = _1252;
                _1283 = _1249;
            }
            else
            {
                _1297 = _1270;
                _1283 = _996;
            }
            _1296 = _1297;
            _1292 = _961;
            _1282 = _1283;
        }
        else
        {
            bool _1047 = _996.y < (max(_24.DeckY, _24.BasinMaxY) + 0.00999999977648258209228515625);
            if (_1047)
            {
                float _1052 = 0.3499999940395355224609375 + (0.64999997615814208984375 * clamp(_961, 0.0, 1.0));
                do
                {
                    if ((_24.WetWidth == 0u) || (_24.WetHeight == 0u))
                    {
                        break;
                    }
                    vec2 _1152 = (_996.xz - vec2(_24.WetOriginX, _24.WetOriginZ)) / vec2(max(_24.WetTexelSize, 9.9999997473787516355514526367188e-05));
                    ivec2 _1155 = ivec2(floor(_1152));
                    SPIRV_CROSS_UNROLL
                    for (int _1277 = -1; _1277 <= 1; _1277++)
                    {
                        SPIRV_CROSS_UNROLL
                        for (int _1278 = -1; _1278 <= 1; _1278++)
                        {
                            ivec2 _1170 = _1155 + ivec2(_1278, _1277);
                            if ((((_1170.x < 0) || (_1170.y < 0)) || (_1170.x >= int(_24.WetWidth))) || (_1170.y >= int(_24.WetHeight)))
                            {
                                continue;
                            }
                            uint _1209 = uint(clamp(_1052 * clamp(1.0 - (length(_1152 - (vec2(_1170) + vec2(0.5))) * 0.625), 0.0, 1.0), 0.0, 1.0) * 65535.0);
                            if (_1209 > 0u)
                            {
                                uint _1225 = atomicMax(Wetness_1._data[uint(_1170.x) + (uint(_1170.y) * _24.WetWidth)], _1209);
                            }
                        }
                    }
                    break;
                } while(false);
            }
            _1296 = _1270;
            _1292 = _1047 ? 0.0 : _961;
            _1282 = _996;
        }
        bvec3 _1091 = not(isnan(_1282));
        bvec3 _1094 = not(isinf(_1282));
        Diffuse_1._data[gl_GlobalInvocationID.x * 2u] = vec4(_1282, (((_1282.y < (_24.BasinMinY - 0.5)) || any(greaterThan(abs(_1282 - (vec3(_24.BasinMinX + _24.BasinMaxX, _24.BasinMinY + _24.BasinMaxY, _24.BasinMinZ + _24.BasinMaxZ) * 0.5)), vec3(25.0)))) || (!all(bvec3(_1091.x && _1094.x, _1091.y && _1094.y, _1091.z && _1094.z)))) ? 0.0 : _1292);
        Diffuse_1._data[(gl_GlobalInvocationID.x * 2u) + 1u] = vec4(_1296, _1307);
        break;
    } while(false);
}

