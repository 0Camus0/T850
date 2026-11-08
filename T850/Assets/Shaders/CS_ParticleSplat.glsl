#version 430
// GENERATED from CS_ParticleSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=6e8fb547d9b5f11829b34172f11f15df1df5f0165e61b7202cb2acec22b32656; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 0, std140) uniform ParticleConstants
{
    uint ParticleCount;
    uint OutputWidth;
    uint OutputHeight;
    uint Iteration;
    float Radius;
    float Opacity;
    float Intensity;
    float Softness;
    float DepthBias;
    float Lifetime;
    float Spread;
    float Rise;
    float TimeSeconds;
    float SizeScaleMin;
    float SizeScaleMax;
    uint Shape;
    uint Blend;
    uint Source;
    float ScreenSpace;
    float TileCountX;
    float TileCountY;
    uint FrameIndex;
    float ProjScaleY;
    float Style;
    vec3 Color0;
    float Color0Pad;
    vec3 Color1;
    float Color1Pad;
    vec3 Color2;
    float Color2Pad;
    vec3 Color3;
    float Color3Pad;
    vec3 EmitterPosition;
    float EmitterPad;
    vec4 ViewProjection[4];
} _48;

layout(binding = 1, std430) readonly buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 2, std430) buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 3, std430) buffer TileMasks
{
    uint _data[];
} TileMasks_1;

layout(binding = 4, rgba16f) uniform writeonly image2D OutputTexture;
layout(binding = 5) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        uint _1081 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * (((_48.OutputWidth + 63u) / 64u) * 64u));
        if (_48.Iteration == 0u)
        {
            if (_1081 >= _48.ParticleCount)
            {
                break;
            }
            vec3 _2012;
            float _2013;
            if (_48.Source == 0u)
            {
                float _2011;
                do
                {
                    if (_48.Lifetime <= 9.9999997473787516355514526367188e-05)
                    {
                        _2011 = 0.0;
                        break;
                    }
                    uint _1719 = (_1081 * 9781u) + 17u;
                    uint _1736 = (_1719 << 13u) ^ _1719;
                    _2011 = fract((_48.TimeSeconds / _48.Lifetime) + (float(((_1736 * (((_1736 * _1736) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 4.656612873077392578125e-10));
                    break;
                } while(false);
                uint _1634 = (_1081 * 6271u) + 43u;
                uint _1754 = (_1634 << 13u) ^ _1634;
                float _1636 = float(((_1754 * (((_1754 * _1754) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 2.9258362399531279152142815291882e-09;
                uint _1639 = (_1081 * 3253u) + 91u;
                uint _1772 = (_1639 << 13u) ^ _1639;
                float _1647 = (_48.Spread * (float(((_1772 * (((_1772 * _1772) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 4.656612873077392578125e-10)) * (0.20000000298023223876953125 + _2011);
                uint _1652 = (_1081 * 1877u) + 7u;
                uint _1790 = (_1652 << 13u) ^ _1652;
                float _1662 = sin((_48.TimeSeconds * (1.0 + (float(((_1790 * (((_1790 * _1790) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 9.31322574615478515625e-10))) + _1636) * _48.Spread;
                _2013 = _2011;
                _2012 = _48.EmitterPosition + vec3((cos(_1636) * _1647) + (_1662 * 0.25), _2011 * _48.Rise, (sin(_1636) * _1647) - (_1662 * 0.125));
            }
            else
            {
                if (Positions_1._data[_1081].w < 0.5)
                {
                    Projected_1._data[_1081] = vec4(0.0, 0.0, -1.0, 0.0);
                    break;
                }
                _2013 = 1.0;
                _2012 = Positions_1._data[_1081].xyz;
            }
            vec4 _1824 = (((_48.ViewProjection[0] * _2012.x) + (_48.ViewProjection[1] * _2012.y)) + (_48.ViewProjection[2] * _2012.z)) + _48.ViewProjection[3];
            if (_1824.w <= 9.9999997473787516355514526367188e-05)
            {
                Projected_1._data[_1081] = vec4(0.0, 0.0, -1.0, 0.0);
                break;
            }
            vec2 _1130 = _1824.xy / vec2(_1824.w);
            float _1134 = (_1130.x * 0.5) + 0.5;
            float _1138 = 0.5 - (_1130.y * 0.5);
            float _1161 = ((((_48.Radius * mix(_48.SizeScaleMin, _48.SizeScaleMax, _2013)) * _48.ProjScaleY) * 0.5) * float(_48.OutputHeight)) / max(_1824.w, 0.001000000047497451305389404296875);
            Projected_1._data[_1081] = vec4(_1134, _1138, _1161, _1824.w);
            vec2 _1179 = vec2(_1134, _1138) * vec2(float(_48.OutputWidth), float(_48.OutputHeight));
            int _1187 = int(floor((_1179.x - _1161) * 0.125));
            int _1194 = int(floor((_1179.x + _1161) * 0.125));
            int _1208 = int(floor((_1179.y + _1161) * 0.125));
            int _1213 = int(max(_48.TileCountX - 1.0, 0.0));
            int _1218 = int(max(_48.TileCountY - 1.0, 0.0));
            int _2014;
            _2014 = max(int(floor((_1179.y - _1161) * 0.125)), 0);
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_2014 <= min(_1208, _1218))
                {
                    int _2015;
                    _2015 = max(_1187, 0);
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_2015 <= min(_1194, _1213))
                        {
                            uint _1249 = atomicMax(TileMasks_1._data[_2015 + (int(_48.TileCountX) * _2014)], _48.FrameIndex);
                            _2015++;
                            continue;
                        }
                        else
                        {
                            break;
                        }
                    }
                    _2014++;
                    continue;
                }
                else
                {
                    break;
                }
            }
            break;
        }
        if ((gl_GlobalInvocationID.x >= _48.OutputWidth) || (gl_GlobalInvocationID.y >= _48.OutputHeight))
        {
            break;
        }
        uvec2 _1285 = uvec2(gl_GlobalInvocationID.x, (_48.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        vec2 _1292 = vec2(float(gl_GlobalInvocationID.x) + 0.5, float(gl_GlobalInvocationID.y) + 0.5);
        uint _1294 = gl_GlobalInvocationID.x / 8u;
        uint _1296 = gl_GlobalInvocationID.y / 8u;
        if (TileMasks_1._data[_1294 + (uint(_48.TileCountX) * _1296)] != _48.FrameIndex)
        {
            imageStore(OutputTexture, ivec2(_1285), vec4(0.0));
            break;
        }
        vec4 _1340 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(int(gl_GlobalInvocationID.x), int((_48.OutputHeight - 1u) - gl_GlobalInvocationID.y), 0).xy, 0);
        float _1341 = _1340.x;
        float _1352 = (abs(_1341 - _48.ViewProjection[2].z) > 9.999999717180685365747194737196e-10) ? (_48.ViewProjection[3].z / (_1341 - _48.ViewProjection[2].z)) : 1000000000.0;
        float _1355 = float(_1294 * 8u);
        float _1358 = float(_1296 * 8u);
        vec2 _1361 = vec2(_1355, _1358) + vec2(8.0);
        float _1978;
        vec3 _1979;
        _1979 = vec3(0.0);
        _1978 = 0.0;
        float _2017;
        vec3 _2019;
        SPIRV_CROSS_LOOP
        for (uint _1977 = 0u; _1977 < _48.ParticleCount; _1979 = _2019, _1978 = _2017, _1977++)
        {
            if (Projected_1._data[_1977].z <= 0.0)
            {
                _2019 = _1979;
                _2017 = _1978;
                continue;
            }
            vec2 _1386 = Projected_1._data[_1977].xy * vec2(float(_48.OutputWidth), float(_48.OutputHeight));
            if (((((_1386.x + Projected_1._data[_1977].z) < _1355) || ((_1386.x - Projected_1._data[_1977].z) > _1361.x)) || ((_1386.y + Projected_1._data[_1977].z) < _1358)) || ((_1386.y - Projected_1._data[_1977].z) > _1361.y))
            {
                _2019 = _1979;
                _2017 = _1978;
                continue;
            }
            vec2 _1424 = _1292 - _1386;
            float _1430 = Projected_1._data[_1977].z * (1.0 - clamp(_48.Softness, 0.0, 1.0));
            float _1982;
            if (_48.Shape == 1u)
            {
                _1982 = max(abs(_1424.x), abs(_1424.y));
            }
            else
            {
                float _1983;
                if (_48.Shape == 2u)
                {
                    _1983 = length(_1424 * vec2(1.0, 0.25));
                }
                else
                {
                    _1983 = length(_1424);
                }
                _1982 = _1983;
            }
            float _1984;
            if (_48.Style >= 1.5)
            {
                _1984 = float(_1982 <= Projected_1._data[_1977].z);
            }
            else
            {
                float _1985;
                if (_48.Style >= 0.5)
                {
                    _1985 = 1.0 - smoothstep(0.0, max(1.0, Projected_1._data[_1977].z * 0.100000001490116119384765625), abs(_1982 - (Projected_1._data[_1977].z * 0.87999999523162841796875)));
                }
                else
                {
                    _1985 = 1.0 - smoothstep(_1430, max(Projected_1._data[_1977].z, _1430 + 9.9999997473787516355514526367188e-05), _1982);
                }
                _1984 = _1985;
            }
            if (_1984 <= 0.0)
            {
                _2019 = _1979;
                _2017 = _1978;
                continue;
            }
            float _1995;
            if (_48.DepthBias > 0.0)
            {
                float _1510 = clamp((_1352 - (Projected_1._data[_1977].w - _48.Radius)) / _48.DepthBias, 0.0, 1.0);
                if (_1510 <= 0.0)
                {
                    _2019 = _1979;
                    _2017 = _1978;
                    continue;
                }
                _1995 = _1510;
            }
            else
            {
                _1995 = 1.0;
            }
            uint _1517 = _1977 * 2654435761u;
            uint _1830 = (_1517 << 13u) ^ _1517;
            float _1991;
            do
            {
                if (_48.Lifetime <= 9.9999997473787516355514526367188e-05)
                {
                    _1991 = 0.0;
                    break;
                }
                uint _1857 = (_1977 * 9781u) + 17u;
                uint _1874 = (_1857 << 13u) ^ _1857;
                _1991 = fract((_48.TimeSeconds / _48.Lifetime) + (float(((_1874 * (((_1874 * _1874) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 4.656612873077392578125e-10));
                break;
            } while(false);
            float _1897 = clamp((_48.Source == 0u) ? _1991 : (float(((_1830 * (((_1830 * _1830) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10), 0.0, 1.0) * 3.0;
            uint _1901 = min(uint(floor(_1897)), 2u);
            bvec3 _1916 = bvec3(_1901 == 1u);
            vec3 _1917 = vec3(_1916.x ? _48.Color1.x : _48.Color2.x, _1916.y ? _48.Color1.y : _48.Color2.y, _1916.z ? _48.Color1.z : _48.Color2.z);
            bvec3 _1918 = bvec3(_1901 == 0u);
            bvec3 _1930 = bvec3(_1901 == 1u);
            vec3 _1931 = vec3(_1930.x ? _48.Color2.x : _48.Color3.x, _1930.y ? _48.Color2.y : _48.Color3.y, _1930.z ? _48.Color2.z : _48.Color3.z);
            bvec3 _1932 = bvec3(_1901 == 0u);
            vec3 _1938 = mix(vec3(_1918.x ? _48.Color0.x : _1917.x, _1918.y ? _48.Color0.y : _1917.y, _1918.z ? _48.Color0.z : _1917.z), vec3(_1932.x ? _48.Color1.x : _1931.x, _1932.y ? _48.Color1.y : _1931.y, _1932.z ? _48.Color1.z : _1931.z), vec3(_1897 - float(_1901)));
            float _2018;
            vec3 _2020;
            if (_48.Blend == 0u)
            {
                _2020 = _1979 + (((_1938 * _1984) * _1995) * _48.Intensity);
                _2018 = _1978;
            }
            else
            {
                float _1546 = _1984 * _1995;
                _2020 = _1979 + (_1938 * _1546);
                _2018 = _1978 + _1546;
            }
            _2019 = _2020;
            _2017 = _2018;
        }
        float _1980;
        vec3 _1981;
        if ((_48.Blend != 0u) && (_1978 > 9.9999997473787516355514526367188e-06))
        {
            _1981 = _1979 / vec3(_1978);
            _1980 = _48.Opacity * (1.0 - exp(_1978 * (-0.3499999940395355224609375)));
        }
        else
        {
            _1981 = _1979;
            _1980 = 0.0;
        }
        if (_48.Blend == 0u)
        {
            if (dot(_1981, _1981) <= 9.9999999747524270787835121154785e-07)
            {
                imageStore(OutputTexture, ivec2(_1285), vec4(0.0));
                break;
            }
            imageStore(OutputTexture, ivec2(_1285), vec4(_1981, 1.0));
            break;
        }
        if (_1980 <= 0.001000000047497451305389404296875)
        {
            imageStore(OutputTexture, ivec2(_1285), vec4(0.0));
            break;
        }
        imageStore(OutputTexture, ivec2(_1285), vec4(_1981, _1980));
        break;
    } while(false);
}

