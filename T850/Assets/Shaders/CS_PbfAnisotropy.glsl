#version 430
// GENERATED from CS_PbfAnisotropy.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=a0214b666be266358738f10346eb7eb130f640f5f9e1cff81731d606da26afab; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 0, std140) uniform PbfAnisotropyConstants
{
    uint ParticleCount;
    uint GridX;
    uint GridY;
    uint GridZ;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float CellSize;
    float Spacing;
    float Smoothing;
    float MaxRatio;
    float RadiusScale;
    float SparseScale;
    float FullNeighbours;
    float SparseNeighbours;
    float MaxStretch;
} _123;

layout(binding = 1, std430) readonly buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 3, std430) buffer Anisotropy
{
    vec4 _data[];
} Anisotropy_1;

layout(binding = 2, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

layout(binding = 4, std430) readonly buffer Velocities
{
    vec4 _data[];
} Velocities_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _123.ParticleCount)
        {
            break;
        }
        vec4 _1115 = Positions_1._data[gl_GlobalInvocationID.x];
        if (_1115.w < 0.5)
        {
            Anisotropy_1._data[gl_GlobalInvocationID.x * 3u] = vec4(0.0, -1000.0, 0.0, 0.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 1u] = vec4(0.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u] = vec4(0.0, 0.0, 0.0, 1.0);
            break;
        }
        uint _1142 = (_123.GridX * _123.GridY) * _123.GridZ;
        float _1145 = max(_123.CellSize, 9.9999997473787516355514526367188e-05);
        float _1147 = 1.0 / _1145;
        int _1161 = clamp(int(floor((_1115.x - _123.GridOriginX) * _1147)), 0, int(_123.GridX) - 1);
        int _1175 = clamp(int(floor((_1115.y - _123.GridOriginY) * _1147)), 0, int(_123.GridY) - 1);
        int _1189 = clamp(int(floor((_1115.z - _123.GridOriginZ) * _1147)), 0, int(_123.GridZ) - 1);
        float _1192 = _1145 * _1145;
        float _2181;
        vec3 _2182;
        float _2183;
        float _2184;
        float _2185;
        float _2186;
        float _2187;
        float _2188;
        float _2194;
        _2194 = 0.0;
        _2188 = 0.0;
        _2187 = 0.0;
        _2186 = 0.0;
        _2185 = 0.0;
        _2184 = 0.0;
        _2183 = 0.0;
        _2182 = vec3(0.0);
        _2181 = 0.0;
        float _2238;
        vec3 _2241;
        float _2244;
        float _2247;
        float _2250;
        float _2253;
        float _2256;
        float _2259;
        float _2262;
        SPIRV_CROSS_LOOP
        for (int _2180 = -1; _2180 <= 1; _2194 = _2262, _2188 = _2259, _2187 = _2256, _2186 = _2253, _2185 = _2250, _2184 = _2247, _2183 = _2244, _2182 = _2241, _2181 = _2238, _2180++)
        {
            _2262 = _2194;
            _2259 = _2188;
            _2256 = _2187;
            _2253 = _2186;
            _2250 = _2185;
            _2247 = _2184;
            _2244 = _2183;
            _2241 = _2182;
            _2238 = _2181;
            float _2237;
            vec3 _2240;
            float _2243;
            float _2246;
            float _2249;
            float _2252;
            float _2255;
            float _2258;
            float _2261;
            SPIRV_CROSS_LOOP
            for (int _2230 = -1; _2230 <= 1; _2230++, _2262 = _2261, _2259 = _2258, _2256 = _2255, _2253 = _2252, _2250 = _2249, _2247 = _2246, _2244 = _2243, _2241 = _2240, _2238 = _2237)
            {
                _2261 = _2262;
                _2258 = _2259;
                _2255 = _2256;
                _2252 = _2253;
                _2249 = _2250;
                _2246 = _2247;
                _2243 = _2244;
                _2240 = _2241;
                _2237 = _2238;
                float _2281;
                vec3 _2283;
                float _2285;
                float _2287;
                float _2289;
                float _2291;
                float _2293;
                float _2295;
                float _2297;
                SPIRV_CROSS_LOOP
                for (int _2232 = -1; _2232 <= 1; _2261 = _2297, _2258 = _2295, _2255 = _2293, _2252 = _2291, _2249 = _2289, _2246 = _2287, _2243 = _2285, _2240 = _2283, _2237 = _2281, _2232++)
                {
                    int _1210 = _1161 + _2232;
                    int _1213 = _1175 + _2230;
                    int _1216 = _1189 + _2180;
                    if ((((((_1210 < 0) || (_1213 < 0)) || (_1216 < 0)) || (_1210 >= int(_123.GridX))) || (_1213 >= int(_123.GridY))) || (_1216 >= int(_123.GridZ)))
                    {
                        _2297 = _2261;
                        _2295 = _2258;
                        _2293 = _2255;
                        _2291 = _2252;
                        _2289 = _2249;
                        _2287 = _2246;
                        _2285 = _2243;
                        _2283 = _2240;
                        _2281 = _2237;
                        continue;
                    }
                    uint _1258 = uint(_1210) + (_123.GridX * (uint(_1213) + (_123.GridY * uint(_1216))));
                    uint _1262 = min(GridBuffer_1._data[_1258], 64u);
                    float _2236;
                    vec3 _2239;
                    float _2242;
                    float _2245;
                    float _2248;
                    float _2251;
                    float _2254;
                    float _2257;
                    float _2260;
                    _2260 = _2261;
                    _2257 = _2258;
                    _2254 = _2255;
                    _2251 = _2252;
                    _2248 = _2249;
                    _2245 = _2246;
                    _2242 = _2243;
                    _2239 = _2240;
                    _2236 = _2237;
                    float _2282;
                    vec3 _2284;
                    float _2286;
                    float _2288;
                    float _2290;
                    float _2292;
                    float _2294;
                    float _2296;
                    float _2298;
                    SPIRV_CROSS_LOOP
                    for (uint _2235 = 0u; _2235 < _1262; _2260 = _2298, _2257 = _2296, _2254 = _2294, _2251 = _2292, _2248 = _2290, _2245 = _2288, _2242 = _2286, _2239 = _2284, _2236 = _2282, _2235++)
                    {
                        uint _1275 = (64u * (_1142 + _1258)) + _2235;
                        if (GridBuffer_1._data[_1275] >= _123.ParticleCount)
                        {
                            _2298 = _2260;
                            _2296 = _2257;
                            _2294 = _2254;
                            _2292 = _2251;
                            _2290 = _2248;
                            _2288 = _2245;
                            _2286 = _2242;
                            _2284 = _2239;
                            _2282 = _2236;
                            continue;
                        }
                        vec3 _1289 = Positions_1._data[GridBuffer_1._data[_1275]].xyz - _1115.xyz;
                        float _1292 = dot(_1289, _1289);
                        if (_1292 >= _1192)
                        {
                            _2298 = _2260;
                            _2296 = _2257;
                            _2294 = _2254;
                            _2292 = _2251;
                            _2290 = _2248;
                            _2288 = _2245;
                            _2286 = _2242;
                            _2284 = _2239;
                            _2282 = _2236;
                            continue;
                        }
                        float _1301 = sqrt(_1292) * _1147;
                        float _1307 = 1.0 - ((_1301 * _1301) * _1301);
                        _2298 = _2260 + 1.0;
                        _2296 = _2257 + ((_1307 * _1289.z) * _1289.z);
                        _2294 = _2254 + ((_1307 * _1289.y) * _1289.z);
                        _2292 = _2251 + ((_1307 * _1289.y) * _1289.y);
                        _2290 = _2248 + ((_1307 * _1289.x) * _1289.z);
                        _2288 = _2245 + ((_1307 * _1289.x) * _1289.y);
                        _2286 = _2242 + ((_1307 * _1289.x) * _1289.x);
                        _2284 = _2239 + (_1289 * _1307);
                        _2282 = _2236 + _1307;
                    }
                    _2297 = _2260;
                    _2295 = _2257;
                    _2293 = _2254;
                    _2291 = _2251;
                    _2289 = _2248;
                    _2287 = _2245;
                    _2285 = _2242;
                    _2283 = _2239;
                    _2281 = _2236;
                }
            }
        }
        float _1390 = 1.0 / max(_2181, 9.9999999747524270787835121154785e-07);
        vec3 _1393 = _2182 * _1390;
        float _1411 = (_2184 * _1390) - (_1393.x * _1393.y);
        float _1420 = (_2185 * _1390) - (_1393.x * _1393.z);
        float _1438 = (_2187 * _1390) - (_1393.y * _1393.z);
        float _2190;
        float _2191;
        float _2192;
        vec3 _2196;
        vec3 _2197;
        vec3 _2198;
        _2198 = vec3(0.0, 0.0, 1.0);
        _2197 = vec3(0.0, 1.0, 0.0);
        _2196 = vec3(1.0, 0.0, 0.0);
        _2192 = (_2188 * _1390) - (_1393.z * _1393.z);
        _2191 = (_2186 * _1390) - (_1393.y * _1393.y);
        _2190 = (_2183 * _1390) - (_1393.x * _1393.x);
        float _2211;
        vec3 _2216;
        float _2220;
        float _2221;
        float _2222;
        float _2223;
        float _2224;
        vec3 _2225;
        vec3 _2226;
        int _2189 = 0;
        float _2199 = _1411;
        float _2200 = _1420;
        float _2201 = _1438;
        SPIRV_CROSS_UNROLL
        for (; _2189 < 5; _2201 = _2222, _2200 = _2224, _2199 = _2223, _2198 = _2226, _2197 = _2225, _2196 = _2216, _2192 = _2221, _2191 = _2220, _2190 = _2211, _2189++)
        {
            float _2202;
            float _2203;
            float _2204;
            float _2205;
            float _2206;
            vec3 _2207;
            vec3 _2208;
            do
            {
                if (abs(_2199) < 9.9999998245167004418121337039338e-15)
                {
                    _2208 = _2197;
                    _2207 = _2196;
                    _2206 = _2201;
                    _2205 = _2200;
                    _2204 = _2199;
                    _2203 = _2191;
                    _2202 = _2190;
                    break;
                }
                float _1855 = (0.5 * (_2191 - _2190)) / _2199;
                float _1867 = ((_1855 >= 0.0) ? 1.0 : (-1.0)) / (abs(_1855) + sqrt((_1855 * _1855) + 1.0));
                float _1872 = inversesqrt((_1867 * _1867) + 1.0);
                float _1875 = _1867 * _1872;
                _2208 = (_2196 * _1875) + (_2197 * _1872);
                _2207 = (_2196 * _1872) - (_2197 * _1875);
                _2206 = (_1875 * _2200) + (_1872 * _2201);
                _2205 = (_1872 * _2200) - (_1875 * _2201);
                _2204 = 0.0;
                _2203 = _2191 + (_1867 * _2199);
                _2202 = _2190 - (_1867 * _2199);
                break;
            } while(false);
            float _2212;
            float _2213;
            float _2214;
            float _2215;
            vec3 _2217;
            do
            {
                if (abs(_2205) < 9.9999998245167004418121337039338e-15)
                {
                    _2217 = _2198;
                    _2216 = _2207;
                    _2215 = _2206;
                    _2214 = _2204;
                    _2213 = _2205;
                    _2212 = _2192;
                    _2211 = _2202;
                    break;
                }
                float _1940 = (0.5 * (_2192 - _2202)) / _2205;
                float _1952 = ((_1940 >= 0.0) ? 1.0 : (-1.0)) / (abs(_1940) + sqrt((_1940 * _1940) + 1.0));
                float _1957 = inversesqrt((_1952 * _1952) + 1.0);
                float _1960 = _1952 * _1957;
                _2217 = (_2207 * _1960) + (_2198 * _1957);
                _2216 = (_2207 * _1957) - (_2198 * _1960);
                _2215 = (_1960 * _2204) + (_1957 * _2206);
                _2214 = (_1957 * _2204) - (_1960 * _2206);
                _2213 = 0.0;
                _2212 = _2192 + (_1952 * _2205);
                _2211 = _2202 - (_1952 * _2205);
                break;
            } while(false);
            do
            {
                if (abs(_2215) < 9.9999998245167004418121337039338e-15)
                {
                    _2226 = _2217;
                    _2225 = _2208;
                    _2224 = _2213;
                    _2223 = _2214;
                    _2222 = _2215;
                    _2221 = _2212;
                    _2220 = _2203;
                    break;
                }
                float _2025 = (0.5 * (_2212 - _2203)) / _2215;
                float _2037 = ((_2025 >= 0.0) ? 1.0 : (-1.0)) / (abs(_2025) + sqrt((_2025 * _2025) + 1.0));
                float _2042 = inversesqrt((_2037 * _2037) + 1.0);
                float _2045 = _2037 * _2042;
                _2226 = (_2208 * _2045) + (_2217 * _2042);
                _2225 = (_2208 * _2042) - (_2217 * _2045);
                _2224 = (_2045 * _2214) + (_2042 * _2213);
                _2223 = (_2042 * _2214) - (_2045 * _2213);
                _2222 = 0.0;
                _2221 = _2212 + (_2037 * _2215);
                _2220 = _2203 - (_2037 * _2215);
                break;
            } while(false);
        }
        vec3 _1514 = sqrt(max(vec3(_2190, _2191, _2192), vec3(0.0)) / vec3((0.1500000059604644775390625 * _1145) * _1145));
        float _1526 = clamp(max(_1514.x, max(_1514.y, _1514.z)), 0.001000000047497451305389404296875, max(_123.MaxStretch, 1.0));
        float _1548 = clamp((_2194 - _123.SparseNeighbours) / max(_123.FullNeighbours - _123.SparseNeighbours, 1.0), 0.0, 1.0);
        vec4 _1578 = Velocities_1._data[gl_GlobalInvocationID.x];
        float _1586 = 1.0 - ((1.0 - clamp((_2194 - _123.SparseNeighbours) * 0.5, 0.0, 1.0)) * (1.0 - smoothstep(0.300000011920928955078125, 1.2000000476837158203125, length(_1578.xyz))));
        if (_1586 < 0.100000001490116119384765625)
        {
            Anisotropy_1._data[gl_GlobalInvocationID.x * 3u] = vec4(_1115.xyz, -1.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 1u] = vec4(0.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u] = vec4(0.0, 0.0, _2194, 1.0);
            break;
        }
        vec3 _1612 = mix(vec3(max(_123.SparseScale, 0.0500000007450580596923828125) * _123.Spacing), clamp(_1514, vec3(_1526 / max(_123.MaxRatio, 1.0)), vec3(_1526)) * (max(_123.RadiusScale, 0.0500000007450580596923828125) * _123.Spacing), vec3(_1548)) * _1586;
        vec3 _1626 = vec3(1.0) / max(_1612 * _1612, vec3(9.9999999392252902907785028219223e-09));
        Anisotropy_1._data[gl_GlobalInvocationID.x * 3u] = vec4(_1115.xyz + (_1393 * (_123.Smoothing * _1548)), max(_1612.x, max(_1612.y, _1612.z)));
        Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 1u] = vec4((((_1626.x * _2196.x) * _2196.x) + ((_1626.y * _2197.x) * _2197.x)) + ((_1626.z * _2198.x) * _2198.x), (((_1626.x * _2196.x) * _2196.y) + ((_1626.y * _2197.x) * _2197.y)) + ((_1626.z * _2198.x) * _2198.y), (((_1626.x * _2196.x) * _2196.z) + ((_1626.y * _2197.x) * _2197.z)) + ((_1626.z * _2198.x) * _2198.z), (((_1626.x * _2196.y) * _2196.y) + ((_1626.y * _2197.y) * _2197.y)) + ((_1626.z * _2198.y) * _2198.y));
        Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u] = vec4((((_1626.x * _2196.y) * _2196.z) + ((_1626.y * _2197.y) * _2197.z)) + ((_1626.z * _2198.y) * _2198.z), (((_1626.x * _2196.z) * _2196.z) + ((_1626.y * _2197.z) * _2197.z)) + ((_1626.z * _2198.z) * _2198.z), ((_2194 >= 24.0) && (length(_1393) <= (0.100000001490116119384765625 * _1145))) ? (-_2194) : _2194, 1.0 - _1548);
        break;
    } while(false);
}

