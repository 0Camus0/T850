#version 430
// GENERATED from CS_PbfWhitewater.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=b51fd5bacb36483681c1e7df18db69a0cb152f3ed69d580c2fbdc302618169e2; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _89;

layout(binding = 7, std430) buffer Wetness
{
    uint _data[];
} Wetness_1;

layout(binding = 1, std430) readonly buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 4, std430) buffer Whitewater
{
    vec4 _data[];
} Whitewater_1;

layout(binding = 2, std430) readonly buffer Velocities
{
    vec4 _data[];
} Velocities_1;

layout(binding = 3, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

layout(binding = 6, std430) buffer Counter
{
    uint _data[];
} Counter_1;

layout(binding = 5, std430) buffer Diffuse
{
    vec4 _data[];
} Diffuse_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _89.ParticleCount)
        {
            break;
        }
        vec4 _1124 = Positions_1._data[gl_GlobalInvocationID.x];
        if (_1124.w < 0.5)
        {
            Whitewater_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            break;
        }
        vec4 _1136 = Velocities_1._data[gl_GlobalInvocationID.x];
        uint _1145 = (_89.GridX * _89.GridY) * _89.GridZ;
        float _1148 = max(_89.CellSize, 9.9999997473787516355514526367188e-05);
        float _1150 = 1.0 / _1148;
        int _1164 = clamp(int(floor((_1124.x - _89.GridOriginX) * _1150)), 0, int(_89.GridX) - 1);
        int _1178 = clamp(int(floor((_1124.y - _89.GridOriginY) * _1150)), 0, int(_89.GridY) - 1);
        int _1192 = clamp(int(floor((_1124.z - _89.GridOriginZ) * _1150)), 0, int(_89.GridZ) - 1);
        float _2066;
        vec3 _2067;
        float _2068;
        _2068 = 0.0;
        _2067 = vec3(0.0);
        _2066 = 0.0;
        float _2089;
        vec3 _2093;
        float _2097;
        SPIRV_CROSS_LOOP
        for (int _2065 = -1; _2065 <= 1; _2068 = _2089, _2067 = _2093, _2066 = _2097, _2065++)
        {
            _2097 = _2066;
            _2093 = _2067;
            _2089 = _2068;
            float _2088;
            vec3 _2092;
            float _2096;
            SPIRV_CROSS_LOOP
            for (int _2081 = -1; _2081 <= 1; _2081++, _2097 = _2096, _2093 = _2092, _2089 = _2088)
            {
                _2096 = _2097;
                _2092 = _2093;
                _2088 = _2089;
                float _2114;
                vec3 _2117;
                float _2119;
                SPIRV_CROSS_LOOP
                for (int _2083 = -1; _2083 <= 1; _2096 = _2119, _2092 = _2117, _2088 = _2114, _2083++)
                {
                    int _1210 = _1164 + _2083;
                    int _1213 = _1178 + _2081;
                    int _1216 = _1192 + _2065;
                    if ((((((_1210 < 0) || (_1213 < 0)) || (_1216 < 0)) || (_1210 >= int(_89.GridX))) || (_1213 >= int(_89.GridY))) || (_1216 >= int(_89.GridZ)))
                    {
                        _2119 = _2096;
                        _2117 = _2092;
                        _2114 = _2088;
                        continue;
                    }
                    uint _1258 = uint(_1210) + (_89.GridX * (uint(_1213) + (_89.GridY * uint(_1216))));
                    uint _1262 = min(GridBuffer_1._data[_1258], 64u);
                    float _2087;
                    vec3 _2091;
                    float _2095;
                    _2095 = _2096;
                    _2091 = _2092;
                    _2087 = _2088;
                    float _2115;
                    vec3 _2118;
                    float _2120;
                    SPIRV_CROSS_LOOP
                    for (uint _2086 = 0u; _2086 < _1262; _2095 = _2120, _2091 = _2118, _2087 = _2115, _2086++)
                    {
                        uint _1275 = (64u * (_1145 + _1258)) + _2086;
                        if ((GridBuffer_1._data[_1275] == gl_GlobalInvocationID.x) || (GridBuffer_1._data[_1275] >= _89.ParticleCount))
                        {
                            _2120 = _2095;
                            _2118 = _2091;
                            _2115 = _2087;
                            continue;
                        }
                        if (Positions_1._data[GridBuffer_1._data[_1275]].w < 0.5)
                        {
                            _2120 = _2095;
                            _2118 = _2091;
                            _2115 = _2087;
                            continue;
                        }
                        vec3 _1299 = _1124.xyz - Positions_1._data[GridBuffer_1._data[_1275]].xyz;
                        float _1302 = dot(_1299, _1299);
                        if ((_1302 >= (_1148 * _1148)) || (_1302 < 1.0000000133514319600180897396058e-10))
                        {
                            _2120 = _2095;
                            _2118 = _2091;
                            _2115 = _2087;
                            continue;
                        }
                        float _1314 = sqrt(_1302);
                        float _1318 = 1.0 - (_1314 * _1150);
                        vec3 _1324 = _1136.xyz - Velocities_1._data[GridBuffer_1._data[_1275]].xyz;
                        float _1326 = length(_1324);
                        float _2116;
                        if (_1326 > 9.9999997473787516355514526367188e-05)
                        {
                            _2116 = _2087 + ((_1326 * (1.0 - dot(_1324 / vec3(_1326), _1299 / vec3(_1314)))) * _1318);
                        }
                        else
                        {
                            _2116 = _2087;
                        }
                        _2120 = _2095 + 1.0;
                        _2118 = _2091 + ((_1299 / vec3(_1314)) * _1318);
                        _2115 = _2116;
                    }
                    _2119 = _2095;
                    _2117 = _2091;
                    _2114 = _2087;
                }
            }
        }
        float _1374 = length(_1136.xyz);
        float _1383 = length(_2067);
        vec3 _1389 = _2067 / vec3(_1383);
        bvec3 _1390 = bvec3(_1383 > 9.9999997473787516355514526367188e-05);
        float _1752 = clamp((_2068 - _89.TrappedAirMin) / max(_89.TrappedAirMax - _89.TrappedAirMin, 9.9999999747524270787835121154785e-07), 0.0, 1.0);
        float _1763 = clamp(((((1.0 - clamp(_2066 / max(_89.SurfaceNeighbours, 1.0), 0.0, 1.0)) * clamp((((_1374 > 0.001000000047497451305389404296875) ? dot(_1136.xyz / vec3(_1374), vec3(_1390.x ? _1389.x : vec3(0.0, 1.0, 0.0).x, _1390.y ? _1389.y : vec3(0.0, 1.0, 0.0).y, _1390.z ? _1389.z : vec3(0.0, 1.0, 0.0).z)) : 0.0) - 0.4000000059604644775390625) * 1.66666662693023681640625, 0.0, 1.0)) * _1374) - _89.CrestMin) / max(_89.CrestMax - _89.CrestMin, 9.9999999747524270787835121154785e-07), 0.0, 1.0);
        float _1774 = clamp((((0.5 * _1374) * _1374) - _89.EnergyMin) / max(_89.EnergyMax - _89.EnergyMin, 9.9999999747524270787835121154785e-07), 0.0, 1.0);
        float _1435 = (_1752 + _1763) * _1774;
        float _1443 = Whitewater_1._data[gl_GlobalInvocationID.x].x * _89.FoamDecay;
        float _1448 = Whitewater_1._data[gl_GlobalInvocationID.x].y * _89.AerationDecay;
        float _2069;
        float _2070;
        if (_2066 >= _89.SurfaceNeighbours)
        {
            _2070 = _1448 + (((_1435 * _89.AerationGain) * _89.DeltaTime) + (_1443 * 0.5));
            _2069 = _1443 * 0.5;
        }
        else
        {
            _2070 = _1448;
            _2069 = _1443 + (((_1435 * _89.FoamGain) * _89.DeltaTime) + (_1448 * (1.0 - _89.AerationDecay)));
        }
        Whitewater_1._data[gl_GlobalInvocationID.x] = vec4(min(_2069, 2.0), min(_2070, 2.0), mix(Whitewater_1._data[gl_GlobalInvocationID.x].z, clamp((_1374 - 0.0500000007450580596923828125) * 1.25, 0.0, 1.0), 1.0 - exp(_89.DeltaTime * (-4.0))), _1752);
        if (((_89.DiffuseCapacity > 0u) && (_1435 > 0.0)) && (_89.DeltaTime > 0.0))
        {
            float _1540 = (_1774 * ((_89.SprayRate * _1752) + (_89.CrestRate * _1763))) * _89.DeltaTime;
            uint _1542 = uint(_1540);
            uint _1790 = ((((gl_GlobalInvocationID.x * 9781u) + (_89.FrameSeed * 6271u)) + 1u) * 747796405u) + 2891336453u;
            uint _1798 = ((_1790 >> ((_1790 >> 28u) + 4u)) ^ _1790) * 277803737u;
            uint _1802 = (_1798 >> 22u) ^ _1798;
            uint _2071;
            if ((float(_1802 >> 8u) * 5.9604644775390625e-08) < fract(_1540))
            {
                _2071 = _1542 + 1u;
            }
            else
            {
                _2071 = _1542;
            }
            uint _1554 = min(_2071, 4u);
            if (_1554 > 0u)
            {
                uint _1560 = atomicAdd(Counter_1._data[0], _1554);
                vec3 _1566 = _1136.xyz / vec3(_1374);
                bvec3 _1567 = bvec3(_1374 > 0.001000000047497451305389404296875);
                vec3 _1568 = vec3(_1567.x ? _1566.x : vec3(0.0, 1.0, 0.0).x, _1567.y ? _1566.y : vec3(0.0, 1.0, 0.0).y, _1567.z ? _1566.z : vec3(0.0, 1.0, 0.0).z);
                bvec3 _1573 = bvec3(abs(_1568.y) < 0.89999997615814208984375);
                vec3 _1578 = normalize(cross(_1568, vec3(_1573.x ? vec3(0.0, 1.0, 0.0).x : vec3(1.0, 0.0, 0.0).x, _1573.y ? vec3(0.0, 1.0, 0.0).y : vec3(1.0, 0.0, 0.0).y, _1573.z ? vec3(0.0, 1.0, 0.0).z : vec3(1.0, 0.0, 0.0).z)));
                vec3 _1581 = cross(_1568, _1578);
                SPIRV_CROSS_LOOP
                for (uint _2072 = 0u, _2079 = _1802; _2072 < _1554; )
                {
                    uint _1818 = (_2079 * 747796405u) + 2891336453u;
                    uint _1826 = ((_1818 >> ((_1818 >> 28u) + 4u)) ^ _1818) * 277803737u;
                    uint _1830 = (_1826 >> 22u) ^ _1826;
                    uint _1846 = (_1830 * 747796405u) + 2891336453u;
                    uint _1854 = ((_1846 >> ((_1846 >> 28u) + 4u)) ^ _1846) * 277803737u;
                    uint _1858 = (_1854 >> 22u) ^ _1854;
                    float _1599 = float(_1858 >> 8u) * 3.7450703871400037314742803573608e-07;
                    uint _1874 = (_1858 * 747796405u) + 2891336453u;
                    uint _1882 = ((_1874 >> ((_1874 >> 28u) + 4u)) ^ _1874) * 277803737u;
                    uint _1886 = (_1882 >> 22u) ^ _1882;
                    vec3 _1613 = ((_1578 * cos(_1599)) + (_1581 * sin(_1599))) * ((0.5 * _89.Spacing) * sqrt(float(_1830 >> 8u) * 5.9604644775390625e-08));
                    vec3 _1623 = (_1124.xyz + _1613) + ((_1136.xyz * _89.DeltaTime) * (float(_1886 >> 8u) * 5.9604644775390625e-08));
                    uint _1902 = (_1886 * 747796405u) + 2891336453u;
                    uint _1910 = ((_1902 >> ((_1902 >> 28u) + 4u)) ^ _1902) * 277803737u;
                    uint _1914 = (_1910 >> 22u) ^ _1910;
                    uint _1640 = (_1560 + _2072) % _89.DiffuseCapacity;
                    Diffuse_1._data[_1640 * 2u] = vec4(_1623, _89.DiffuseLifetime * (0.5 + (float(_1914 >> 8u) * 5.9604644775390625e-08)));
                    Diffuse_1._data[(_1640 * 2u) + 1u] = vec4(_1136.xyz + (_1613 * 2.0), 0.0);
                    _2079 = _1914;
                    _2072++;
                    continue;
                }
            }
        }
        bool _1688 = (((_1124.x < _89.BasinMinX) || (_1124.x > _89.BasinMaxX)) || (_1124.z < _89.BasinMinZ)) || (_1124.z > _89.BasinMaxZ);
        if (((_1688 && (_1124.y < (_89.DeckY + 0.60000002384185791015625))) && (_1136.y < 0.5)) || (((!_1688) && (min(min(_1124.x - _89.BasinMinX, _89.BasinMaxX - _1124.x), min(_1124.z - _89.BasinMinZ, _89.BasinMaxZ - _1124.z)) < 0.1500000059604644775390625)) && (_1124.y > _89.BasinMaxY)))
        {
            do
            {
                if ((_89.WetWidth == 0u) || (_89.WetHeight == 0u))
                {
                    break;
                }
                vec2 _1945 = (_1124.xz - vec2(_89.WetOriginX, _89.WetOriginZ)) / vec2(max(_89.WetTexelSize, 9.9999997473787516355514526367188e-05));
                ivec2 _1948 = ivec2(floor(_1945));
                SPIRV_CROSS_UNROLL
                for (int _2074 = -1; _2074 <= 1; _2074++)
                {
                    SPIRV_CROSS_UNROLL
                    for (int _2075 = -1; _2075 <= 1; _2075++)
                    {
                        ivec2 _1963 = _1948 + ivec2(_2075, _2074);
                        if ((((_1963.x < 0) || (_1963.y < 0)) || (_1963.x >= int(_89.WetWidth))) || (_1963.y >= int(_89.WetHeight)))
                        {
                            continue;
                        }
                        uint _2002 = uint(clamp(clamp(1.0 - (length(_1945 - (vec2(_1963) + vec2(0.5))) * 0.625), 0.0, 1.0), 0.0, 1.0) * 65535.0);
                        if (_2002 > 0u)
                        {
                            uint _2018 = atomicMax(Wetness_1._data[uint(_1963.x) + (uint(_1963.y) * _89.WetWidth)], _2002);
                        }
                    }
                }
                break;
            } while(false);
        }
        break;
    } while(false);
}

