#version 430
// GENERATED from CS_ParticleSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=2fe7cfaf568c63e587c511820373cac9523cabcc941cb6db0bd9c224be378307; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float DepthRadiusScale;
    float InvPackingFactor;
    float ProjA;
    float ProjB;
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
            vec3 _2014;
            float _2015;
            if (_48.Source == 0u)
            {
                float _2013;
                do
                {
                    if (_48.Lifetime <= 9.9999997473787516355514526367188e-05)
                    {
                        _2013 = 0.0;
                        break;
                    }
                    uint _1721 = (_1081 * 9781u) + 17u;
                    uint _1738 = (_1721 << 13u) ^ _1721;
                    _2013 = fract((_48.TimeSeconds / _48.Lifetime) + (float(((_1738 * (((_1738 * _1738) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 4.656612873077392578125e-10));
                    break;
                } while(false);
                uint _1636 = (_1081 * 6271u) + 43u;
                uint _1756 = (_1636 << 13u) ^ _1636;
                float _1638 = float(((_1756 * (((_1756 * _1756) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 2.9258362399531279152142815291882e-09;
                uint _1641 = (_1081 * 3253u) + 91u;
                uint _1774 = (_1641 << 13u) ^ _1641;
                float _1649 = (_48.Spread * (float(((_1774 * (((_1774 * _1774) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 4.656612873077392578125e-10)) * (0.20000000298023223876953125 + _2013);
                uint _1654 = (_1081 * 1877u) + 7u;
                uint _1792 = (_1654 << 13u) ^ _1654;
                float _1664 = sin((_48.TimeSeconds * (1.0 + (float(((_1792 * (((_1792 * _1792) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 9.31322574615478515625e-10))) + _1638) * _48.Spread;
                _2015 = _2013;
                _2014 = _48.EmitterPosition + vec3((cos(_1638) * _1649) + (_1664 * 0.25), _2013 * _48.Rise, (sin(_1638) * _1649) - (_1664 * 0.125));
            }
            else
            {
                if (Positions_1._data[_1081].w < 0.5)
                {
                    Projected_1._data[_1081] = vec4(0.0, 0.0, -1.0, 0.0);
                    break;
                }
                _2015 = 1.0;
                _2014 = Positions_1._data[_1081].xyz;
            }
            vec4 _1826 = (((_48.ViewProjection[0] * _2014.x) + (_48.ViewProjection[1] * _2014.y)) + (_48.ViewProjection[2] * _2014.z)) + _48.ViewProjection[3];
            if (_1826.w <= 9.9999997473787516355514526367188e-05)
            {
                Projected_1._data[_1081] = vec4(0.0, 0.0, -1.0, 0.0);
                break;
            }
            vec2 _1130 = _1826.xy / vec2(_1826.w);
            float _1134 = (_1130.x * 0.5) + 0.5;
            float _1138 = 0.5 - (_1130.y * 0.5);
            float _1161 = ((((_48.Radius * mix(_48.SizeScaleMin, _48.SizeScaleMax, _2015)) * _48.ProjScaleY) * 0.5) * float(_48.OutputHeight)) / max(_1826.w, 0.001000000047497451305389404296875);
            Projected_1._data[_1081] = vec4(_1134, _1138, _1161, _1826.w);
            vec2 _1179 = vec2(_1134, _1138) * vec2(float(_48.OutputWidth), float(_48.OutputHeight));
            int _1187 = int(floor((_1179.x - _1161) * 0.125));
            int _1194 = int(floor((_1179.x + _1161) * 0.125));
            int _1208 = int(floor((_1179.y + _1161) * 0.125));
            int _1213 = int(max(_48.TileCountX - 1.0, 0.0));
            int _1218 = int(max(_48.TileCountY - 1.0, 0.0));
            int _2016;
            _2016 = max(int(floor((_1179.y - _1161) * 0.125)), 0);
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_2016 <= min(_1208, _1218))
                {
                    int _2017;
                    _2017 = max(_1187, 0);
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_2017 <= min(_1194, _1213))
                        {
                            uint _1249 = atomicMax(TileMasks_1._data[_2017 + (int(_48.TileCountX) * _2016)], _48.FrameIndex);
                            _2017++;
                            continue;
                        }
                        else
                        {
                            break;
                        }
                    }
                    _2016++;
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
        vec4 _1336 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(int(gl_GlobalInvocationID.x), int((_48.OutputHeight - 1u) - gl_GlobalInvocationID.y), 0).xy, 0);
        float _1337 = _1336.x;
        float _1354 = ((_1337 > 0.0) && (abs(_1337 - _48.ProjA) > 9.999999717180685365747194737196e-10)) ? (_48.ProjB / (_1337 - _48.ProjA)) : 1000000000.0;
        float _1357 = float(_1294 * 8u);
        float _1360 = float(_1296 * 8u);
        vec2 _1363 = vec2(_1357, _1360) + vec2(8.0);
        float _1980;
        vec3 _1981;
        _1981 = vec3(0.0);
        _1980 = 0.0;
        float _2019;
        vec3 _2021;
        SPIRV_CROSS_LOOP
        for (uint _1979 = 0u; _1979 < _48.ParticleCount; _1981 = _2021, _1980 = _2019, _1979++)
        {
            if (Projected_1._data[_1979].z <= 0.0)
            {
                _2021 = _1981;
                _2019 = _1980;
                continue;
            }
            vec2 _1388 = Projected_1._data[_1979].xy * vec2(float(_48.OutputWidth), float(_48.OutputHeight));
            if (((((_1388.x + Projected_1._data[_1979].z) < _1357) || ((_1388.x - Projected_1._data[_1979].z) > _1363.x)) || ((_1388.y + Projected_1._data[_1979].z) < _1360)) || ((_1388.y - Projected_1._data[_1979].z) > _1363.y))
            {
                _2021 = _1981;
                _2019 = _1980;
                continue;
            }
            vec2 _1426 = _1292 - _1388;
            float _1432 = Projected_1._data[_1979].z * (1.0 - clamp(_48.Softness, 0.0, 1.0));
            float _1984;
            if (_48.Shape == 1u)
            {
                _1984 = max(abs(_1426.x), abs(_1426.y));
            }
            else
            {
                float _1985;
                if (_48.Shape == 2u)
                {
                    _1985 = length(_1426 * vec2(1.0, 0.25));
                }
                else
                {
                    _1985 = length(_1426);
                }
                _1984 = _1985;
            }
            float _1986;
            if (_48.Style >= 1.5)
            {
                _1986 = float(_1984 <= Projected_1._data[_1979].z);
            }
            else
            {
                float _1987;
                if (_48.Style >= 0.5)
                {
                    _1987 = 1.0 - smoothstep(0.0, max(1.0, Projected_1._data[_1979].z * 0.100000001490116119384765625), abs(_1984 - (Projected_1._data[_1979].z * 0.87999999523162841796875)));
                }
                else
                {
                    _1987 = 1.0 - smoothstep(_1432, max(Projected_1._data[_1979].z, _1432 + 9.9999997473787516355514526367188e-05), _1984);
                }
                _1986 = _1987;
            }
            if (_1986 <= 0.0)
            {
                _2021 = _1981;
                _2019 = _1980;
                continue;
            }
            float _1997;
            if (_48.DepthBias > 0.0)
            {
                float _1512 = clamp((_1354 - (Projected_1._data[_1979].w - _48.Radius)) / _48.DepthBias, 0.0, 1.0);
                if (_1512 <= 0.0)
                {
                    _2021 = _1981;
                    _2019 = _1980;
                    continue;
                }
                _1997 = _1512;
            }
            else
            {
                _1997 = 1.0;
            }
            uint _1519 = _1979 * 2654435761u;
            uint _1832 = (_1519 << 13u) ^ _1519;
            float _1993;
            do
            {
                if (_48.Lifetime <= 9.9999997473787516355514526367188e-05)
                {
                    _1993 = 0.0;
                    break;
                }
                uint _1859 = (_1979 * 9781u) + 17u;
                uint _1876 = (_1859 << 13u) ^ _1859;
                _1993 = fract((_48.TimeSeconds / _48.Lifetime) + (float(((_1876 * (((_1876 * _1876) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 4.656612873077392578125e-10));
                break;
            } while(false);
            float _1899 = clamp((_48.Source == 0u) ? _1993 : (float(((_1832 * (((_1832 * _1832) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10), 0.0, 1.0) * 3.0;
            uint _1903 = min(uint(floor(_1899)), 2u);
            bvec3 _1918 = bvec3(_1903 == 1u);
            vec3 _1919 = vec3(_1918.x ? _48.Color1.x : _48.Color2.x, _1918.y ? _48.Color1.y : _48.Color2.y, _1918.z ? _48.Color1.z : _48.Color2.z);
            bvec3 _1920 = bvec3(_1903 == 0u);
            bvec3 _1932 = bvec3(_1903 == 1u);
            vec3 _1933 = vec3(_1932.x ? _48.Color2.x : _48.Color3.x, _1932.y ? _48.Color2.y : _48.Color3.y, _1932.z ? _48.Color2.z : _48.Color3.z);
            bvec3 _1934 = bvec3(_1903 == 0u);
            vec3 _1940 = mix(vec3(_1920.x ? _48.Color0.x : _1919.x, _1920.y ? _48.Color0.y : _1919.y, _1920.z ? _48.Color0.z : _1919.z), vec3(_1934.x ? _48.Color1.x : _1933.x, _1934.y ? _48.Color1.y : _1933.y, _1934.z ? _48.Color1.z : _1933.z), vec3(_1899 - float(_1903)));
            float _2020;
            vec3 _2022;
            if (_48.Blend == 0u)
            {
                _2022 = _1981 + (((_1940 * _1986) * _1997) * _48.Intensity);
                _2020 = _1980;
            }
            else
            {
                float _1548 = _1986 * _1997;
                _2022 = _1981 + (_1940 * _1548);
                _2020 = _1980 + _1548;
            }
            _2021 = _2022;
            _2019 = _2020;
        }
        float _1982;
        vec3 _1983;
        if ((_48.Blend != 0u) && (_1980 > 9.9999997473787516355514526367188e-06))
        {
            _1983 = _1981 / vec3(_1980);
            _1982 = _48.Opacity * (1.0 - exp(_1980 * (-0.3499999940395355224609375)));
        }
        else
        {
            _1983 = _1981;
            _1982 = 0.0;
        }
        if (_48.Blend == 0u)
        {
            if (dot(_1983, _1983) <= 9.9999999747524270787835121154785e-07)
            {
                imageStore(OutputTexture, ivec2(_1285), vec4(0.0));
                break;
            }
            imageStore(OutputTexture, ivec2(_1285), vec4(_1983, 1.0));
            break;
        }
        if (_1982 <= 0.001000000047497451305389404296875)
        {
            imageStore(OutputTexture, ivec2(_1285), vec4(0.0));
            break;
        }
        imageStore(OutputTexture, ivec2(_1285), vec4(_1983, _1982));
        break;
    } while(false);
}

