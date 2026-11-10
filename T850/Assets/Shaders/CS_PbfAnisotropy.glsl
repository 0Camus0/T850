#version 430
// GENERATED from CS_PbfAnisotropy.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=6cc578f8a46eb6f86a142959e3f226edcc0dd82e461da6a578a4c8e7346e76fb; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        vec4 _1097 = Positions_1._data[gl_GlobalInvocationID.x];
        if (_1097.w < 0.5)
        {
            Anisotropy_1._data[gl_GlobalInvocationID.x * 3u] = vec4(0.0, -1000.0, 0.0, 0.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 1u] = vec4(0.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u] = vec4(0.0, 0.0, 0.0, 1.0);
            break;
        }
        uint _1124 = (_123.GridX * _123.GridY) * _123.GridZ;
        float _1127 = max(_123.CellSize, 9.9999997473787516355514526367188e-05);
        float _1129 = 1.0 / _1127;
        int _1143 = clamp(int(floor((_1097.x - _123.GridOriginX) * _1129)), 0, int(_123.GridX) - 1);
        int _1157 = clamp(int(floor((_1097.y - _123.GridOriginY) * _1129)), 0, int(_123.GridY) - 1);
        int _1171 = clamp(int(floor((_1097.z - _123.GridOriginZ) * _1129)), 0, int(_123.GridZ) - 1);
        float _1174 = _1127 * _1127;
        float _2150;
        vec3 _2151;
        float _2152;
        float _2153;
        float _2154;
        float _2155;
        float _2156;
        float _2157;
        float _2163;
        _2163 = 0.0;
        _2157 = 0.0;
        _2156 = 0.0;
        _2155 = 0.0;
        _2154 = 0.0;
        _2153 = 0.0;
        _2152 = 0.0;
        _2151 = vec3(0.0);
        _2150 = 0.0;
        float _2207;
        vec3 _2210;
        float _2213;
        float _2216;
        float _2219;
        float _2222;
        float _2225;
        float _2228;
        float _2231;
        SPIRV_CROSS_LOOP
        for (int _2149 = -1; _2149 <= 1; _2163 = _2231, _2157 = _2228, _2156 = _2225, _2155 = _2222, _2154 = _2219, _2153 = _2216, _2152 = _2213, _2151 = _2210, _2150 = _2207, _2149++)
        {
            _2231 = _2163;
            _2228 = _2157;
            _2225 = _2156;
            _2222 = _2155;
            _2219 = _2154;
            _2216 = _2153;
            _2213 = _2152;
            _2210 = _2151;
            _2207 = _2150;
            float _2206;
            vec3 _2209;
            float _2212;
            float _2215;
            float _2218;
            float _2221;
            float _2224;
            float _2227;
            float _2230;
            SPIRV_CROSS_LOOP
            for (int _2199 = -1; _2199 <= 1; _2199++, _2231 = _2230, _2228 = _2227, _2225 = _2224, _2222 = _2221, _2219 = _2218, _2216 = _2215, _2213 = _2212, _2210 = _2209, _2207 = _2206)
            {
                _2230 = _2231;
                _2227 = _2228;
                _2224 = _2225;
                _2221 = _2222;
                _2218 = _2219;
                _2215 = _2216;
                _2212 = _2213;
                _2209 = _2210;
                _2206 = _2207;
                float _2250;
                vec3 _2252;
                float _2254;
                float _2256;
                float _2258;
                float _2260;
                float _2262;
                float _2264;
                float _2266;
                SPIRV_CROSS_LOOP
                for (int _2201 = -1; _2201 <= 1; _2230 = _2266, _2227 = _2264, _2224 = _2262, _2221 = _2260, _2218 = _2258, _2215 = _2256, _2212 = _2254, _2209 = _2252, _2206 = _2250, _2201++)
                {
                    int _1192 = _1143 + _2201;
                    int _1195 = _1157 + _2199;
                    int _1198 = _1171 + _2149;
                    if ((((((_1192 < 0) || (_1195 < 0)) || (_1198 < 0)) || (_1192 >= int(_123.GridX))) || (_1195 >= int(_123.GridY))) || (_1198 >= int(_123.GridZ)))
                    {
                        _2266 = _2230;
                        _2264 = _2227;
                        _2262 = _2224;
                        _2260 = _2221;
                        _2258 = _2218;
                        _2256 = _2215;
                        _2254 = _2212;
                        _2252 = _2209;
                        _2250 = _2206;
                        continue;
                    }
                    uint _1240 = uint(_1192) + (_123.GridX * (uint(_1195) + (_123.GridY * uint(_1198))));
                    uint _1244 = min(GridBuffer_1._data[_1240], 64u);
                    float _2205;
                    vec3 _2208;
                    float _2211;
                    float _2214;
                    float _2217;
                    float _2220;
                    float _2223;
                    float _2226;
                    float _2229;
                    _2229 = _2230;
                    _2226 = _2227;
                    _2223 = _2224;
                    _2220 = _2221;
                    _2217 = _2218;
                    _2214 = _2215;
                    _2211 = _2212;
                    _2208 = _2209;
                    _2205 = _2206;
                    float _2251;
                    vec3 _2253;
                    float _2255;
                    float _2257;
                    float _2259;
                    float _2261;
                    float _2263;
                    float _2265;
                    float _2267;
                    SPIRV_CROSS_LOOP
                    for (uint _2204 = 0u; _2204 < _1244; _2229 = _2267, _2226 = _2265, _2223 = _2263, _2220 = _2261, _2217 = _2259, _2214 = _2257, _2211 = _2255, _2208 = _2253, _2205 = _2251, _2204++)
                    {
                        uint _1257 = (64u * (_1124 + _1240)) + _2204;
                        if (GridBuffer_1._data[_1257] >= _123.ParticleCount)
                        {
                            _2267 = _2229;
                            _2265 = _2226;
                            _2263 = _2223;
                            _2261 = _2220;
                            _2259 = _2217;
                            _2257 = _2214;
                            _2255 = _2211;
                            _2253 = _2208;
                            _2251 = _2205;
                            continue;
                        }
                        vec3 _1271 = Positions_1._data[GridBuffer_1._data[_1257]].xyz - _1097.xyz;
                        float _1274 = dot(_1271, _1271);
                        if (_1274 >= _1174)
                        {
                            _2267 = _2229;
                            _2265 = _2226;
                            _2263 = _2223;
                            _2261 = _2220;
                            _2259 = _2217;
                            _2257 = _2214;
                            _2255 = _2211;
                            _2253 = _2208;
                            _2251 = _2205;
                            continue;
                        }
                        float _1283 = sqrt(_1274) * _1129;
                        float _1289 = 1.0 - ((_1283 * _1283) * _1283);
                        _2267 = _2229 + 1.0;
                        _2265 = _2226 + ((_1289 * _1271.z) * _1271.z);
                        _2263 = _2223 + ((_1289 * _1271.y) * _1271.z);
                        _2261 = _2220 + ((_1289 * _1271.y) * _1271.y);
                        _2259 = _2217 + ((_1289 * _1271.x) * _1271.z);
                        _2257 = _2214 + ((_1289 * _1271.x) * _1271.y);
                        _2255 = _2211 + ((_1289 * _1271.x) * _1271.x);
                        _2253 = _2208 + (_1271 * _1289);
                        _2251 = _2205 + _1289;
                    }
                    _2266 = _2229;
                    _2264 = _2226;
                    _2262 = _2223;
                    _2260 = _2220;
                    _2258 = _2217;
                    _2256 = _2214;
                    _2254 = _2211;
                    _2252 = _2208;
                    _2250 = _2205;
                }
            }
        }
        float _1372 = 1.0 / max(_2150, 9.9999999747524270787835121154785e-07);
        vec3 _1375 = _2151 * _1372;
        float _1393 = (_2153 * _1372) - (_1375.x * _1375.y);
        float _1402 = (_2154 * _1372) - (_1375.x * _1375.z);
        float _1420 = (_2156 * _1372) - (_1375.y * _1375.z);
        float _2159;
        float _2160;
        float _2161;
        vec3 _2165;
        vec3 _2166;
        vec3 _2167;
        _2167 = vec3(0.0, 0.0, 1.0);
        _2166 = vec3(0.0, 1.0, 0.0);
        _2165 = vec3(1.0, 0.0, 0.0);
        _2161 = (_2157 * _1372) - (_1375.z * _1375.z);
        _2160 = (_2155 * _1372) - (_1375.y * _1375.y);
        _2159 = (_2152 * _1372) - (_1375.x * _1375.x);
        float _2180;
        vec3 _2185;
        float _2189;
        float _2190;
        float _2191;
        float _2192;
        float _2193;
        vec3 _2194;
        vec3 _2195;
        int _2158 = 0;
        float _2168 = _1393;
        float _2169 = _1402;
        float _2170 = _1420;
        SPIRV_CROSS_UNROLL
        for (; _2158 < 5; _2170 = _2191, _2169 = _2193, _2168 = _2192, _2167 = _2195, _2166 = _2194, _2165 = _2185, _2161 = _2190, _2160 = _2189, _2159 = _2180, _2158++)
        {
            float _2171;
            float _2172;
            float _2173;
            float _2174;
            float _2175;
            vec3 _2176;
            vec3 _2177;
            do
            {
                if (abs(_2168) < 9.9999998245167004418121337039338e-15)
                {
                    _2177 = _2166;
                    _2176 = _2165;
                    _2175 = _2170;
                    _2174 = _2169;
                    _2173 = _2168;
                    _2172 = _2160;
                    _2171 = _2159;
                    break;
                }
                float _1824 = (0.5 * (_2160 - _2159)) / _2168;
                float _1836 = ((_1824 >= 0.0) ? 1.0 : (-1.0)) / (abs(_1824) + sqrt((_1824 * _1824) + 1.0));
                float _1841 = inversesqrt((_1836 * _1836) + 1.0);
                float _1844 = _1836 * _1841;
                _2177 = (_2165 * _1844) + (_2166 * _1841);
                _2176 = (_2165 * _1841) - (_2166 * _1844);
                _2175 = (_1844 * _2169) + (_1841 * _2170);
                _2174 = (_1841 * _2169) - (_1844 * _2170);
                _2173 = 0.0;
                _2172 = _2160 + (_1836 * _2168);
                _2171 = _2159 - (_1836 * _2168);
                break;
            } while(false);
            float _2181;
            float _2182;
            float _2183;
            float _2184;
            vec3 _2186;
            do
            {
                if (abs(_2174) < 9.9999998245167004418121337039338e-15)
                {
                    _2186 = _2167;
                    _2185 = _2176;
                    _2184 = _2175;
                    _2183 = _2173;
                    _2182 = _2174;
                    _2181 = _2161;
                    _2180 = _2171;
                    break;
                }
                float _1909 = (0.5 * (_2161 - _2171)) / _2174;
                float _1921 = ((_1909 >= 0.0) ? 1.0 : (-1.0)) / (abs(_1909) + sqrt((_1909 * _1909) + 1.0));
                float _1926 = inversesqrt((_1921 * _1921) + 1.0);
                float _1929 = _1921 * _1926;
                _2186 = (_2176 * _1929) + (_2167 * _1926);
                _2185 = (_2176 * _1926) - (_2167 * _1929);
                _2184 = (_1929 * _2173) + (_1926 * _2175);
                _2183 = (_1926 * _2173) - (_1929 * _2175);
                _2182 = 0.0;
                _2181 = _2161 + (_1921 * _2174);
                _2180 = _2171 - (_1921 * _2174);
                break;
            } while(false);
            do
            {
                if (abs(_2184) < 9.9999998245167004418121337039338e-15)
                {
                    _2195 = _2186;
                    _2194 = _2177;
                    _2193 = _2182;
                    _2192 = _2183;
                    _2191 = _2184;
                    _2190 = _2181;
                    _2189 = _2172;
                    break;
                }
                float _1994 = (0.5 * (_2181 - _2172)) / _2184;
                float _2006 = ((_1994 >= 0.0) ? 1.0 : (-1.0)) / (abs(_1994) + sqrt((_1994 * _1994) + 1.0));
                float _2011 = inversesqrt((_2006 * _2006) + 1.0);
                float _2014 = _2006 * _2011;
                _2195 = (_2177 * _2014) + (_2186 * _2011);
                _2194 = (_2177 * _2011) - (_2186 * _2014);
                _2193 = (_2014 * _2183) + (_2011 * _2182);
                _2192 = (_2011 * _2183) - (_2014 * _2182);
                _2191 = 0.0;
                _2190 = _2181 + (_2006 * _2184);
                _2189 = _2172 - (_2006 * _2184);
                break;
            } while(false);
        }
        vec3 _1496 = sqrt(max(vec3(_2159, _2160, _2161), vec3(0.0)) / vec3((0.1500000059604644775390625 * _1127) * _1127));
        float _1508 = clamp(max(_1496.x, max(_1496.y, _1496.z)), 0.001000000047497451305389404296875, max(_123.MaxStretch, 1.0));
        float _1530 = clamp((_2163 - _123.SparseNeighbours) / max(_123.FullNeighbours - _123.SparseNeighbours, 1.0), 0.0, 1.0);
        vec4 _1560 = Velocities_1._data[gl_GlobalInvocationID.x];
        float _1568 = 1.0 - ((1.0 - clamp((_2163 - _123.SparseNeighbours) * 0.5, 0.0, 1.0)) * (1.0 - smoothstep(0.300000011920928955078125, 1.2000000476837158203125, length(_1560.xyz))));
        if (_1568 < 0.100000001490116119384765625)
        {
            Anisotropy_1._data[gl_GlobalInvocationID.x * 3u] = vec4(_1097.xyz, -1.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 1u] = vec4(0.0);
            Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u] = vec4(0.0, 0.0, _2163, 1.0);
            break;
        }
        vec3 _1594 = mix(vec3(max(_123.SparseScale, 0.0500000007450580596923828125) * _123.Spacing), clamp(_1496, vec3(_1508 / max(_123.MaxRatio, 1.0)), vec3(_1508)) * (max(_123.RadiusScale, 0.0500000007450580596923828125) * _123.Spacing), vec3(_1530)) * _1568;
        vec3 _1608 = vec3(1.0) / max(_1594 * _1594, vec3(9.9999999392252902907785028219223e-09));
        Anisotropy_1._data[gl_GlobalInvocationID.x * 3u] = vec4(_1097.xyz + (_1375 * (_123.Smoothing * _1530)), max(_1594.x, max(_1594.y, _1594.z)));
        Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 1u] = vec4((((_1608.x * _2165.x) * _2165.x) + ((_1608.y * _2166.x) * _2166.x)) + ((_1608.z * _2167.x) * _2167.x), (((_1608.x * _2165.x) * _2165.y) + ((_1608.y * _2166.x) * _2166.y)) + ((_1608.z * _2167.x) * _2167.y), (((_1608.x * _2165.x) * _2165.z) + ((_1608.y * _2166.x) * _2166.z)) + ((_1608.z * _2167.x) * _2167.z), (((_1608.x * _2165.y) * _2165.y) + ((_1608.y * _2166.y) * _2166.y)) + ((_1608.z * _2167.y) * _2167.y));
        Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u] = vec4((((_1608.x * _2165.y) * _2165.z) + ((_1608.y * _2166.y) * _2166.z)) + ((_1608.z * _2167.y) * _2167.z), (((_1608.x * _2165.z) * _2165.z) + ((_1608.y * _2166.z) * _2166.z)) + ((_1608.z * _2167.z) * _2167.z), _2163, 1.0 - _1530);
        break;
    } while(false);
}

