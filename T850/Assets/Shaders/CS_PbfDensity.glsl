#version 430
// GENERATED from CS_PbfDensity.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=020814e1d193692223ae66b1835d45673ca3f9415b514d3b17da959fbbb013bc; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

struct SolidBox
{
    vec4 lo;
    vec4 hi;
};

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
} _67;

layout(binding = 1, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 3, std430) buffer Lambda
{
    vec4 _data[];
} Lambda_1;

layout(binding = 2, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

layout(binding = 4, std430) readonly buffer SolidBoxes
{
    SolidBox _data[];
} SolidBoxes_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _67.ParticleCount)
        {
            break;
        }
        uint _993 = (_67.GridX * _67.GridY) * _67.GridZ;
        float _996 = 1.0 / _67.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            Lambda_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            break;
        }
        int _1022 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].x - _67.GridOriginX) * _996)), 0, int(_67.GridX) - 1);
        int _1036 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].y - _67.GridOriginY) * _996)), 0, int(_67.GridY) - 1);
        int _1050 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].z - _67.GridOriginZ) * _996)), 0, int(_67.GridZ) - 1);
        float _1055 = _67.SmoothingRadius * _67.SmoothingRadius;
        float _2226;
        vec3 _2227;
        float _2228;
        uint _2261;
        _2261 = 0u;
        _2228 = 0.0;
        _2227 = vec3(0.0);
        _2226 = 0.0;
        float _2287;
        vec3 _2294;
        float _2300;
        uint _2306;
        SPIRV_CROSS_LOOP
        for (int _2225 = -1; _2225 <= 1; _2261 = _2306, _2228 = _2300, _2227 = _2294, _2226 = _2287, _2225++)
        {
            _2306 = _2261;
            _2300 = _2228;
            _2294 = _2227;
            _2287 = _2226;
            float _2286;
            vec3 _2293;
            float _2299;
            uint _2305;
            SPIRV_CROSS_LOOP
            for (int _2275 = -1; _2275 <= 1; _2275++, _2306 = _2305, _2300 = _2299, _2294 = _2293, _2287 = _2286)
            {
                _2305 = _2306;
                _2299 = _2300;
                _2293 = _2294;
                _2286 = _2287;
                float _2331;
                vec3 _2334;
                float _2336;
                uint _2338;
                SPIRV_CROSS_LOOP
                for (int _2277 = -1; _2277 <= 1; _2305 = _2338, _2299 = _2336, _2293 = _2334, _2286 = _2331, _2277++)
                {
                    int _1073 = _1022 + _2277;
                    int _1076 = _1036 + _2275;
                    int _1079 = _1050 + _2225;
                    if ((((((_1073 < 0) || (_1076 < 0)) || (_1079 < 0)) || (_1073 >= int(_67.GridX))) || (_1076 >= int(_67.GridY))) || (_1079 >= int(_67.GridZ)))
                    {
                        _2338 = _2305;
                        _2336 = _2299;
                        _2334 = _2293;
                        _2331 = _2286;
                        continue;
                    }
                    uint _1121 = uint(_1073) + (_67.GridX * (uint(_1076) + (_67.GridY * uint(_1079))));
                    uint _1125 = min(GridBuffer_1._data[_1121], 64u);
                    float _2285;
                    vec3 _2292;
                    float _2298;
                    uint _2304;
                    _2304 = _2305;
                    _2298 = _2299;
                    _2292 = _2293;
                    _2285 = _2286;
                    float _2332;
                    vec3 _2335;
                    float _2337;
                    uint _2339;
                    SPIRV_CROSS_LOOP
                    for (uint _2280 = 0u; _2280 < _1125; _2304 = _2339, _2298 = _2337, _2292 = _2335, _2285 = _2332, _2280++)
                    {
                        uint _1138 = (64u * (_993 + _1121)) + _2280;
                        if (GridBuffer_1._data[_1138] >= _67.ParticleCount)
                        {
                            _2339 = _2304;
                            _2337 = _2298;
                            _2335 = _2292;
                            _2332 = _2285;
                            continue;
                        }
                        vec3 _1153 = Predicted_1._data[gl_GlobalInvocationID.x].xyz - Predicted_1._data[GridBuffer_1._data[_1138]].xyz;
                        vec3 _2281;
                        do
                        {
                            if (dot(_1153, _1153) > 9.9999999392252902907785028219223e-09)
                            {
                                _2281 = _1153;
                                break;
                            }
                            uint _1286 = (gl_GlobalInvocationID.x * 73856093u) ^ (GridBuffer_1._data[_1138] * 19349663u);
                            uint _1292 = (_1286 ^ (_1286 >> uint(13))) * 16777619u;
                            uint _1296 = _1292 ^ (_1292 >> uint(15));
                            vec3 _1311 = vec3(float(_1296 & 255u) - 127.5, float((_1296 >> uint(8)) & 255u) - 127.5, float((_1296 >> uint(16)) & 255u) - 127.5);
                            _2281 = _1311 * (9.9999997473787516355514526367188e-05 / max(length(_1311), 0.001000000047497451305389404296875));
                            break;
                        } while(false);
                        float _1159 = dot(_2281, _2281);
                        if (_1159 >= _1055)
                        {
                            _2339 = _2304;
                            _2337 = _2298;
                            _2335 = _2292;
                            _2332 = _2285;
                            continue;
                        }
                        float _2282;
                        do
                        {
                            if (_1159 >= (_67.SmoothingRadius * _67.SmoothingRadius))
                            {
                                _2282 = 0.0;
                                break;
                            }
                            float _1340 = (_67.SmoothingRadius * _67.SmoothingRadius) - _1159;
                            _2282 = ((((1.566681385040283203125 / pow(_67.SmoothingRadius, 9.0)) * _1340) * _1340) * _1340) / max(_67.RestScale, 9.9999999747524270787835121154785e-07);
                            break;
                        } while(false);
                        vec3 _2288;
                        do
                        {
                            float _1369 = length(_2281);
                            if ((_1369 <= 9.9999999747524270787835121154785e-07) || (_1369 >= _67.SmoothingRadius))
                            {
                                _2288 = vec3(0.0);
                                break;
                            }
                            _2288 = _2281 * ((((((-14.323944091796875) / pow(_67.SmoothingRadius, 6.0)) * (_67.SmoothingRadius - _1369)) * (_67.SmoothingRadius - _1369)) / _1369) / max(_67.RestScale, 9.9999999747524270787835121154785e-07));
                            break;
                        } while(false);
                        _2339 = _2304 + uint(1);
                        _2337 = _2298 + dot(_2288, _2288);
                        _2335 = _2292 + _2288;
                        _2332 = _2285 + _2282;
                    }
                    _2338 = _2304;
                    _2336 = _2298;
                    _2334 = _2292;
                    _2331 = _2285;
                }
            }
        }
        float _2232;
        vec3 _2233;
        float _2234;
        if ((((Predicted_1._data[gl_GlobalInvocationID.x].x > _67.BasinMinX) && (Predicted_1._data[gl_GlobalInvocationID.x].x < _67.BasinMaxX)) && (Predicted_1._data[gl_GlobalInvocationID.x].z > _67.BasinMinZ)) && (Predicted_1._data[gl_GlobalInvocationID.x].z < _67.BasinMaxZ))
        {
            float _1460 = Predicted_1._data[gl_GlobalInvocationID.x].y - _67.BasinMinY;
            float _2229;
            vec3 _2230;
            float _2231;
            do
            {
                if ((_1460 <= 0.0) || (_1460 >= _67.SmoothingRadius))
                {
                    _2231 = _2228;
                    _2230 = _2227;
                    _2229 = _2226;
                    break;
                }
                float _1574 = _1460 / _67.SmoothingRadius;
                float _1599 = 1.0 - (_1574 * _1574);
                float _1602 = _1599 * _1599;
                float _1611 = (((-1.23046875) / _67.SmoothingRadius) * _1602) * _1602;
                float _1622 = _1574 * _1574;
                float _1625 = _1622 * _1574;
                float _1628 = _1625 * _1622;
                float _1631 = _1628 * _1622;
                _2231 = _2228 + (_1611 * _1611);
                _2230 = _2227 + (vec3(0.0, 1.0, 0.0) * _1611);
                _2229 = _2226 + ((((((1.0 - _1574) - (1.33333337306976318359375 * (1.0 - _1625))) + (1.2000000476837158203125 * (1.0 - _1628))) - (0.571428596973419189453125 * (1.0 - _1631))) + (0.111111111938953399658203125 * (1.0 - (_1631 * _1622)))) * 1.23046875);
                break;
            } while(false);
            _2234 = _2231;
            _2233 = _2230;
            _2232 = _2229;
        }
        else
        {
            _2234 = _2228;
            _2233 = _2227;
            _2232 = _2226;
        }
        float _2241;
        vec3 _2242;
        float _2243;
        if (((Predicted_1._data[gl_GlobalInvocationID.x].y < _67.WallTopY) && (Predicted_1._data[gl_GlobalInvocationID.x].z > _67.BasinMinZ)) && (Predicted_1._data[gl_GlobalInvocationID.x].z < _67.BasinMaxZ))
        {
            float _1491 = Predicted_1._data[gl_GlobalInvocationID.x].x - _67.BasinMinX;
            float _2235;
            vec3 _2236;
            float _2237;
            do
            {
                if ((_1491 <= 0.0) || (_1491 >= _67.SmoothingRadius))
                {
                    _2237 = _2234;
                    _2236 = _2233;
                    _2235 = _2232;
                    break;
                }
                float _1675 = _1491 / _67.SmoothingRadius;
                float _1700 = 1.0 - (_1675 * _1675);
                float _1703 = _1700 * _1700;
                float _1712 = (((-1.23046875) / _67.SmoothingRadius) * _1703) * _1703;
                float _1723 = _1675 * _1675;
                float _1726 = _1723 * _1675;
                float _1729 = _1726 * _1723;
                float _1732 = _1729 * _1723;
                _2237 = _2234 + (_1712 * _1712);
                _2236 = _2233 + (vec3(1.0, 0.0, 0.0) * _1712);
                _2235 = _2232 + ((((((1.0 - _1675) - (1.33333337306976318359375 * (1.0 - _1726))) + (1.2000000476837158203125 * (1.0 - _1729))) - (0.571428596973419189453125 * (1.0 - _1732))) + (0.111111111938953399658203125 * (1.0 - (_1732 * _1723)))) * 1.23046875);
                break;
            } while(false);
            float _1503 = _67.BasinMaxX - Predicted_1._data[gl_GlobalInvocationID.x].x;
            float _2238;
            vec3 _2239;
            float _2240;
            do
            {
                if ((_1503 <= 0.0) || (_1503 >= _67.SmoothingRadius))
                {
                    _2240 = _2237;
                    _2239 = _2236;
                    _2238 = _2235;
                    break;
                }
                float _1776 = _1503 / _67.SmoothingRadius;
                float _1801 = 1.0 - (_1776 * _1776);
                float _1804 = _1801 * _1801;
                float _1813 = (((-1.23046875) / _67.SmoothingRadius) * _1804) * _1804;
                float _1824 = _1776 * _1776;
                float _1827 = _1824 * _1776;
                float _1830 = _1827 * _1824;
                float _1833 = _1830 * _1824;
                _2240 = _2237 + (_1813 * _1813);
                _2239 = _2236 + (vec3(-1.0, 0.0, 0.0) * _1813);
                _2238 = _2235 + ((((((1.0 - _1776) - (1.33333337306976318359375 * (1.0 - _1827))) + (1.2000000476837158203125 * (1.0 - _1830))) - (0.571428596973419189453125 * (1.0 - _1833))) + (0.111111111938953399658203125 * (1.0 - (_1833 * _1824)))) * 1.23046875);
                break;
            } while(false);
            _2243 = _2240;
            _2242 = _2239;
            _2241 = _2238;
        }
        else
        {
            _2243 = _2234;
            _2242 = _2233;
            _2241 = _2232;
        }
        float _2250;
        vec3 _2251;
        float _2252;
        if (((Predicted_1._data[gl_GlobalInvocationID.x].y < _67.WallTopY) && (Predicted_1._data[gl_GlobalInvocationID.x].x > _67.BasinMinX)) && (Predicted_1._data[gl_GlobalInvocationID.x].x < _67.BasinMaxX))
        {
            float _1534 = Predicted_1._data[gl_GlobalInvocationID.x].z - _67.BasinMinZ;
            float _2244;
            vec3 _2245;
            float _2246;
            do
            {
                if ((_1534 <= 0.0) || (_1534 >= _67.SmoothingRadius))
                {
                    _2246 = _2243;
                    _2245 = _2242;
                    _2244 = _2241;
                    break;
                }
                float _1877 = _1534 / _67.SmoothingRadius;
                float _1902 = 1.0 - (_1877 * _1877);
                float _1905 = _1902 * _1902;
                float _1914 = (((-1.23046875) / _67.SmoothingRadius) * _1905) * _1905;
                float _1925 = _1877 * _1877;
                float _1928 = _1925 * _1877;
                float _1931 = _1928 * _1925;
                float _1934 = _1931 * _1925;
                _2246 = _2243 + (_1914 * _1914);
                _2245 = _2242 + (vec3(0.0, 0.0, 1.0) * _1914);
                _2244 = _2241 + ((((((1.0 - _1877) - (1.33333337306976318359375 * (1.0 - _1928))) + (1.2000000476837158203125 * (1.0 - _1931))) - (0.571428596973419189453125 * (1.0 - _1934))) + (0.111111111938953399658203125 * (1.0 - (_1934 * _1925)))) * 1.23046875);
                break;
            } while(false);
            float _1546 = _67.BasinMaxZ - Predicted_1._data[gl_GlobalInvocationID.x].z;
            float _2247;
            vec3 _2248;
            float _2249;
            do
            {
                if ((_1546 <= 0.0) || (_1546 >= _67.SmoothingRadius))
                {
                    _2249 = _2246;
                    _2248 = _2245;
                    _2247 = _2244;
                    break;
                }
                float _1978 = _1546 / _67.SmoothingRadius;
                float _2003 = 1.0 - (_1978 * _1978);
                float _2006 = _2003 * _2003;
                float _2015 = (((-1.23046875) / _67.SmoothingRadius) * _2006) * _2006;
                float _2026 = _1978 * _1978;
                float _2029 = _2026 * _1978;
                float _2032 = _2029 * _2026;
                float _2035 = _2032 * _2026;
                _2249 = _2246 + (_2015 * _2015);
                _2248 = _2245 + (vec3(0.0, 0.0, -1.0) * _2015);
                _2247 = _2244 + ((((((1.0 - _1978) - (1.33333337306976318359375 * (1.0 - _2029))) + (1.2000000476837158203125 * (1.0 - _2032))) - (0.571428596973419189453125 * (1.0 - _2035))) + (0.111111111938953399658203125 * (1.0 - (_2035 * _2026)))) * 1.23046875);
                break;
            } while(false);
            _2252 = _2249;
            _2251 = _2248;
            _2250 = _2247;
        }
        else
        {
            _2252 = _2243;
            _2251 = _2242;
            _2250 = _2241;
        }
        vec3 _2254;
        float _2255;
        float _2256;
        _2256 = _2250;
        _2255 = _2252;
        _2254 = _2251;
        float _2270;
        vec3 _2271;
        float _2272;
        SPIRV_CROSS_LOOP
        for (uint _2253 = 0u; _2253 < _67.BoxCount; _2256 = _2270, _2255 = _2272, _2254 = _2271, _2253++)
        {
            do
            {
                vec3 _2075 = Predicted_1._data[gl_GlobalInvocationID.x].xyz - clamp(Predicted_1._data[gl_GlobalInvocationID.x].xyz, SolidBoxes_1._data[_2253].lo.xyz, SolidBoxes_1._data[_2253].hi.xyz);
                float _2077 = length(_2075);
                if ((_2077 <= 9.9999997473787516355514526367188e-06) || (_2077 >= _67.SmoothingRadius))
                {
                    _2272 = _2255;
                    _2271 = _2254;
                    _2270 = _2256;
                    break;
                }
                float _2267;
                vec3 _2268;
                float _2269;
                do
                {
                    if ((_2077 <= 0.0) || (_2077 >= _67.SmoothingRadius))
                    {
                        _2269 = _2255;
                        _2268 = _2254;
                        _2267 = _2256;
                        break;
                    }
                    float _2119 = _2077 / _67.SmoothingRadius;
                    float _2144 = 1.0 - (_2119 * _2119);
                    float _2147 = _2144 * _2144;
                    float _2156 = (((-1.23046875) / _67.SmoothingRadius) * _2147) * _2147;
                    float _2167 = _2119 * _2119;
                    float _2170 = _2167 * _2119;
                    float _2173 = _2170 * _2167;
                    float _2176 = _2173 * _2167;
                    _2269 = _2255 + (_2156 * _2156);
                    _2268 = _2254 + ((_2075 / vec3(_2077)) * _2156);
                    _2267 = _2256 + ((((((1.0 - _2119) - (1.33333337306976318359375 * (1.0 - _2170))) + (1.2000000476837158203125 * (1.0 - _2173))) - (0.571428596973419189453125 * (1.0 - _2176))) + (0.111111111938953399658203125 * (1.0 - (_2176 * _2167)))) * 1.23046875);
                    break;
                } while(false);
                _2272 = _2269;
                _2271 = _2268;
                _2270 = _2267;
                break;
            } while(false);
        }
        float _1243 = (dot(_2254, _2254) + _2255) + (_67.StiffnessEps * _67.RestDenomReference);
        Lambda_1._data[gl_GlobalInvocationID.x] = vec4((_1243 > 0.0) ? ((-max((_2256 / _67.RestDensity) - 1.0, 0.0)) / _1243) : 0.0, _2256, dot(_2254, _2254), float(_2261));
        break;
    } while(false);
}

