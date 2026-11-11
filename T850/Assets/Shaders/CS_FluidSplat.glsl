#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=27dd371d1869af73d8764a09dfe31976a5b3f948329c73680eb3002b7e997b83; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float ProjScaleX;
    vec3 Color1;
    float AuxEnabled;
    vec3 Color2;
    float SurfelBand;
    vec3 Color3;
    float SurfelSharpness;
    vec3 EmitterPosition;
    float Anisotropic;
    vec4 ViewProjection[4];
} _126;

layout(binding = 3, std430) buffer DepthBits
{
    uint _data[];
} DepthBits_1;

layout(binding = 8, std430) buffer SurfelBits
{
    uint _data[];
} SurfelBits_1;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 4, std430) buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

layout(binding = 5, std430) readonly buffer Anisotropy
{
    vec4 _data[];
} Anisotropy_1;

layout(binding = 6, std430) readonly buffer Whitewater
{
    vec4 _data[];
} Whitewater_1;

layout(binding = 7, std430) buffer AuxBits
{
    uint _data[];
} AuxBits_1;

layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _126.ParticleCount)
        {
            break;
        }
        vec4 _1888 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_1888.z <= 0.0)
        {
            break;
        }
        uvec2 _1895 = uvec2(textureSize(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, 0));
        uint _1897 = _1895.x;
        uint _1900 = _1895.y;
        vec2 _1914 = _1888.xy * vec2(float(_126.OutputWidth), float(_126.OutputHeight));
        float _1926 = min((_1888.z * float(_126.OutputHeight)) / float(max(_1900, 1u)), 64.0);
        float _1929 = max(_126.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _1938 = ivec2(int(_126.OutputWidth) - 1, int(_126.OutputHeight) - 1);
        if (!(_126.ScreenSpace > 0.5))
        {
            if (_1926 < 0.5)
            {
                break;
            }
            ivec2 _1952 = max(ivec2(floor(_1914 - vec2(_1926))), ivec2(0));
            ivec2 _1960 = min(ivec2(ceil(_1914 + vec2(_1926))), _1938);
            if ((_1952.x > _1960.x) || (_1952.y > _1960.y))
            {
                break;
            }
            float _1977 = max(_1926 * _1926, 9.9999999747524270787835121154785e-07);
            int _1979 = _1952.y;
            for (int _3610 = _1979; _3610 <= _1960.y; _3610++)
            {
                int _1988 = _1952.x;
                for (int _3611 = _1988; _3611 <= _1960.x; _3611++)
                {
                    vec2 _2004 = vec2(float(_3611) + 0.5, float(_3610) + 0.5) - _1914;
                    float _2007 = dot(_2004, _2004);
                    if (_2007 > _1977)
                    {
                        continue;
                    }
                    float _2018 = sqrt(max(1.0 - (_2007 / _1977), 0.0));
                    float _2024 = _1888.w - (_2018 * _1929);
                    float _3618;
                    do
                    {
                        if ((_1897 == _126.OutputWidth) && (_1900 == _126.OutputHeight))
                        {
                            vec4 _3044 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3611, int((_1900 - 1u) - uint(_3610)), 0).xy, 0);
                            float _3045 = _3044.x;
                            _3618 = ((_3045 > 0.0) && (abs(_3045 - _126.ProjA) > 9.999999717180685365747194737196e-10)) ? (_126.ProjB / (_3045 - _126.ProjA)) : 1000000015047466219876688855040.0;
                            break;
                        }
                        vec2 _2950 = vec2(_1895) / vec2(float(_126.OutputWidth), float(_126.OutputHeight));
                        ivec2 _2959 = ivec2(floor(vec2(float(_3611), float(_3610)) * _2950));
                        ivec2 _2977 = min((ivec2(ceil(vec2(float(_3611 + 1), float(_3610 + 1)) * _2950)) - ivec2(1)), (ivec2(_1895) - ivec2(1)));
                        int _3613;
                        float _3614;
                        _3614 = 0.0;
                        _3613 = _2959.y;
                        float _3617;
                        SPIRV_CROSS_LOOP
                        for (; _3613 <= min(_2977.y, (_2959.y + 2)); _3614 = _3617, _3613++)
                        {
                            int _2992 = _2959.x;
                            _3617 = _3614;
                            SPIRV_CROSS_LOOP
                            for (int _3615 = _2992; _3615 <= min(_2977.x, (_2959.x + 2)); )
                            {
                                vec4 _3085 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3615, int((_1900 - 1u) - uint(_3613)), 0).xy, 0);
                                float _3086 = _3085.x;
                                _3617 = max(_3617, ((_3086 > 0.0) && (abs(_3086 - _126.ProjA) > 9.999999717180685365747194737196e-10)) ? (_126.ProjB / (_3086 - _126.ProjA)) : 1000000015047466219876688855040.0);
                                _3615++;
                                continue;
                            }
                        }
                        _3618 = _3614;
                        break;
                    } while(false);
                    if (_2024 >= _3618)
                    {
                        continue;
                    }
                    uint _2040 = uint(_3611) + (uint(_3610) * _126.OutputWidth);
                    uint _2046 = atomicMin(DepthBits_1._data[_2040], (uint(clamp((log2(max(_2024, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _2058 = atomicAdd(ThicknessBits_1._data[_2040], uint((((2.0 * _2018) * _1929) * _126.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _2072 = gl_GlobalInvocationID.x * 3u;
        vec4 _2075 = Anisotropy_1._data[_2072];
        bvec4 _2076 = bvec4(_126.Anisotropic > 0.5);
        vec4 _2077 = vec4(_2076.x ? _2075.x : vec4(0.0).x, _2076.y ? _2075.y : vec4(0.0).y, _2076.z ? _2075.z : vec4(0.0).z, _2076.w ? _2075.w : vec4(0.0).w);
        if (_2077.w < 0.0)
        {
            break;
        }
        bool _2085 = _126.Iteration == 2u;
        if (_2085 && (_126.SurfelBand <= 0.0))
        {
            break;
        }
        vec4 _2098 = Whitewater_1._data[gl_GlobalInvocationID.x];
        bvec4 _2099 = bvec4(_126.AuxEnabled > 0.5);
        vec4 _2100 = vec4(_2099.x ? _2098.x : vec4(0.0).x, _2099.y ? _2098.y : vec4(0.0).y, _2099.z ? _2098.z : vec4(0.0).z, _2099.w ? _2098.w : vec4(0.0).w);
        float _2112 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].w;
        float _2113 = ((_126.AuxEnabled > 0.5) && (_126.Anisotropic > 0.5)) ? _2112 : 0.0;
        bool _2116 = _2100.x > 0.0199999995529651641845703125;
        bool _2119 = _2100.y > 0.0199999995529651641845703125;
        bool _2122 = _2100.z > 0.0199999995529651641845703125;
        bool _2124 = _2113 > 0.0199999995529651641845703125;
        bool _2131 = ((_2116 || _2119) || _2122) || _2124;
        if ((!_2085) && (_1926 >= 0.5))
        {
            ivec2 _2144 = max(ivec2(floor(_1914 - vec2(_1926))), ivec2(0));
            ivec2 _2152 = min(ivec2(ceil(_1914 + vec2(_1926))), _1938);
            float _2156 = max(_1926 * _1926, 9.9999999747524270787835121154785e-07);
            int _2158 = _2144.y;
            for (int _3587 = _2158; _3587 <= _2152.y; _3587++)
            {
                int _2167 = _2144.x;
                for (int _3602 = _2167; _3602 <= _2152.x; _3602++)
                {
                    vec2 _2183 = vec2(float(_3602) + 0.5, float(_3587) + 0.5) - _1914;
                    float _2186 = dot(_2183, _2183);
                    if (_2186 > _2156)
                    {
                        continue;
                    }
                    float _2197 = sqrt(max(1.0 - (_2186 / _2156), 0.0));
                    uint _2205 = uint(_3602) + (uint(_3587) * _126.OutputWidth);
                    float _2212 = ((2.0 * _2197) * _1929) * _126.InvPackingFactor;
                    uint _2218 = atomicAdd(ThicknessBits_1._data[_2205], uint(_2212 * 1024.0));
                    if (_2131)
                    {
                        if (_2116)
                        {
                            uint _2234 = atomicMax(AuxBits_1._data[_2205 * 4u], uint(clamp(_2100.x * _2197, 0.0, 1.0) * 65535.0));
                        }
                        if (_2119)
                        {
                            uint _2249 = atomicAdd(AuxBits_1._data[(_2205 * 4u) + 1u], uint((min(_2100.y, 2.0) * _2212) * 1024.0));
                        }
                        if (_2122)
                        {
                            uint _2263 = atomicAdd(AuxBits_1._data[(_2205 * 4u) + 2u], uint((_2100.z * _2212) * 1024.0));
                        }
                        if (_2124)
                        {
                            uint _2276 = atomicAdd(AuxBits_1._data[(_2205 * 4u) + 3u], uint((_2113 * _2212) * 1024.0));
                        }
                    }
                }
            }
        }
        float _2295 = (0.5 * float(_126.OutputHeight)) * max(_126.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _2303 = (0.5 * float(_126.OutputWidth)) * max(_126.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _2311 = vec2(float(_126.OutputWidth), float(_126.OutputHeight)) * 0.5;
        if (_2085 && (_2077.w <= 0.0))
        {
            break;
        }
        if (_2077.w > 0.0)
        {
            uint _2325 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _2330 = (gl_GlobalInvocationID.x * 3u) + 2u;
            mat3 _2385 = mat3(vec3(_126.ViewProjection[0].x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07), _126.ViewProjection[1].x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07), _126.ViewProjection[2].x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_126.ViewProjection[0].y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07), _126.ViewProjection[1].y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07), _126.ViewProjection[2].y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_126.ViewProjection[0].w, _126.ViewProjection[1].w, _126.ViewProjection[2].w));
            mat3 _2391 = transpose(_2385) * (mat3(vec3(Anisotropy_1._data[_2325].xyz), vec3(Anisotropy_1._data[_2325].yw, Anisotropy_1._data[_2330].x), vec3(Anisotropy_1._data[_2325].z, Anisotropy_1._data[_2330].xy)) * _2385);
            vec4 _3193 = (((_126.ViewProjection[0] * _2077.x) + (_126.ViewProjection[1] * _2077.y)) + (_126.ViewProjection[2] * _2077.z)) + _126.ViewProjection[3];
            float _3202 = _3193.x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _3208 = _3193.y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _3210 = _3193.w;
            vec3 _3211 = vec3(_3202, _3208, _3210);
            float _2396 = _2077.w;
            if ((_3210 - _2396) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _2421 = vec2((_3202 * _2303) / _3210, ((-_3208) * _2295) / _3210) + _2311;
            float _2430 = min((_2396 * _2295) / (_3210 - _2396), 64.0);
            if (_2430 < 0.5)
            {
                break;
            }
            float _3228 = _2391[0].x;
            float _3230 = _2391[0].y;
            float _3232 = _2391[0].z;
            float _3234 = _2391[1].y;
            float _3236 = _2391[1].z;
            float _3238 = _2391[2].z;
            float _3245 = (_3234 * _3238) - (_3236 * _3236);
            float _3252 = (_3232 * _3236) - (_3230 * _3238);
            float _3259 = (_3230 * _3236) - (_3232 * _3234);
            float _3273 = (_3230 * _3232) - (_3228 * _3236);
            mat3 _3307 = mat3(vec3(_3245, _3252, _3259), vec3(_3252, (_3228 * _3238) - (_3232 * _3232), _3273), vec3(_3259, _3273, (_3228 * _3234) - (_3230 * _3230))) * (1.0 / (((_3228 * _3245) + (_3230 * _3252)) + (_3232 * _3259)));
            float _2460 = _3307[2].z - (_3210 * _3210);
            float _2468 = _3307[0].z - (_3202 * _3210);
            float _2476 = _3307[1].z - (_3208 * _3210);
            float _2485 = sqrt(max((_2468 * _2468) - ((_3307[0].x - (_3202 * _3202)) * _2460), 0.0));
            float _2494 = sqrt(max((_2476 * _2476) - ((_3307[1].y - (_3208 * _3208)) * _2460), 0.0));
            vec2 _2504 = vec2(_2468 + _2485, _2468 - _2485) / vec2(_2460);
            vec2 _2514 = vec2(_2476 + _2494, _2476 - _2494) / vec2(_2460);
            ivec2 _2561 = max(max(ivec2(floor(_2421 - vec2(_2430))), ivec2(floor(vec2(min(_2504.x, _2504.y) * _2303, (-max(_2514.x, _2514.y)) * _2295) + _2311))), ivec2(0));
            ivec2 _2573 = min(min(ivec2(ceil(_2421 + vec2(_2430))), ivec2(ceil(vec2(max(_2504.x, _2504.y) * _2303, (-min(_2514.x, _2514.y)) * _2295) + _2311))), _1938);
            if ((_2561.x > _2573.x) || (_2561.y > _2573.y))
            {
                break;
            }
            if (_2085)
            {
                vec2 _2604 = vec2(_2573 - _2561) / max(vec2(_2573 - _2561), vec2(1.0));
                uvec2 _1845[5] = uvec2[](uvec2(max(min(ivec2(floor(_2421)), _1938), ivec2(0))), uvec2(uint(_2561.x), uint(_2561.y)), uvec2(uint(_2573.x), uint(_2561.y)), uvec2(uint(_2561.x), uint(_2573.y)), uvec2(uint(float(_2573.x) - _2604.x), uint(float(_2573.y) - _2604.y)));
                float _3593;
                _3593 = 1000000015047466219876688855040.0;
                float _3627;
                SPIRV_CROSS_UNROLL
                for (int _3592 = 0; _3592 < 5; _3593 = _3627, _3592++)
                {
                    uint _2658 = _1845[_3592].x + (_1845[_3592].y * _126.OutputWidth);
                    if (DepthBits_1._data[_2658] < 2139095040u)
                    {
                        _3627 = min(_3593, uintBitsToFloat(DepthBits_1._data[_2658]));
                    }
                    else
                    {
                        _3627 = _3593;
                    }
                }
                if ((_3593 < 1000000015047466219876688855040.0) && ((_3210 - _2396) > (_3593 + _126.SurfelBand)))
                {
                    break;
                }
            }
            int _2689 = _2561.y;
            for (int _3594 = _2689; _3594 <= _2573.y; _3594++)
            {
                int _2698 = _2561.x;
                for (int _3595 = _2698; _3595 <= _2573.x; _3595++)
                {
                    vec3 _2723 = vec3(((float(_3595) + 0.5) - _2311.x) / _2303, (-((float(_3594) + 0.5) - _2311.y)) / _2295, 1.0);
                    vec3 _2729 = (_2723 * _3210) - _3211;
                    vec3 _2732 = _2723 * _2391;
                    float _2735 = dot(_2723, _2732);
                    float _2738 = dot(_2729, _2732);
                    float _2751 = (_2738 * _2738) - (_2735 * (dot(_2729, _2729 * _2391) - 1.0));
                    if ((_2751 < 0.0) || (_2735 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _2768 = _3210 + (((-_2738) - sqrt(_2751)) / _2735);
                    if (_2768 <= 0.0500000007450580596923828125)
                    {
                        continue;
                    }
                    uint _2780 = uint(_3595) + (uint(_3594) * _126.OutputWidth);
                    if (_2085)
                    {
                        do
                        {
                            if (DepthBits_1._data[_2780] >= 2139095040u)
                            {
                                break;
                            }
                            float _3330 = max(_2768 - uintBitsToFloat(DepthBits_1._data[_2780]), 0.0);
                            if (_3330 > _126.SurfelBand)
                            {
                                break;
                            }
                            vec3 _3339 = _2723 * _2768;
                            vec3 _3344 = (_3339 - _3211) * _2391;
                            float _3346 = length(_3344);
                            if (_3346 < 9.9999999747524270787835121154785e-07)
                            {
                                break;
                            }
                            vec3 _3354 = _3344 / vec3(_3346);
                            vec3 _3597;
                            if (dot(_3354, _3339) > 0.0)
                            {
                                _3597 = -_3354;
                            }
                            else
                            {
                                _3597 = _3354;
                            }
                            float _3367 = 1.0 - (_3330 / _126.SurfelBand);
                            float _3377 = (pow(clamp(_2751 / _2735, 0.0, 1.0), max(_126.SurfelSharpness, 0.25)) * _3367) * _3367;
                            if (_3377 < 0.001000000047497451305389404296875)
                            {
                                break;
                            }
                            uint _3383 = _2780 * 5u;
                            uint _3391 = atomicAdd(SurfelBits_1._data[_3383], uint((_3377 * 4096.0) + 0.5));
                            uint _3404 = atomicAdd(SurfelBits_1._data[_3383 + 1u], uint(((_3377 * (_3330 / _126.SurfelBand)) * 4096.0) + 0.5));
                            uint _3416 = atomicAdd(SurfelBits_1._data[_3383 + 2u], uint(int(roundEven((_3377 * _3597.x) * 4096.0))));
                            uint _3428 = atomicAdd(SurfelBits_1._data[_3383 + 3u], uint(int(roundEven((_3377 * _3597.y) * 4096.0))));
                            uint _3440 = atomicAdd(SurfelBits_1._data[_3383 + 4u], uint(int(roundEven((_3377 * _3597.z) * 4096.0))));
                            break;
                        } while(false);
                        continue;
                    }
                    uint _2797 = atomicMin(DepthBits_1._data[_2780], floatBitsToUint(_2768));
                }
            }
            break;
        }
        float _2810 = _1926 * _126.DepthRadiusScale;
        if (_2810 < 0.5)
        {
            break;
        }
        float _2820 = max(_126.Radius * _126.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _2827 = max(ivec2(floor(_1914 - vec2(_2810))), ivec2(0));
        ivec2 _2835 = min(ivec2(ceil(_1914 + vec2(_2810))), _1938);
        float _2838 = _2810 * _2810;
        int _2840 = _2827.y;
        for (int _3588 = _2840; _3588 <= _2835.y; _3588++)
        {
            int _2849 = _2827.x;
            for (int _3589 = _2849; _3589 <= _2835.x; _3589++)
            {
                vec2 _2865 = vec2(float(_3589) + 0.5, float(_3588) + 0.5) - _1914;
                float _2868 = dot(_2865, _2865);
                if (_2868 > _2838)
                {
                    continue;
                }
                uint _2896 = atomicMin(DepthBits_1._data[uint(_3589) + (uint(_3588) * _126.OutputWidth)], floatBitsToUint(_1888.w - (sqrt(max(1.0 - (_2868 / _2838), 0.0)) * _2820)));
            }
        }
        break;
    } while(false);
}

