#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=95391b163be65f40a0638a2ad8a3922cd6040bd4837fecf05904bd020ee77682; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        vec4 _1869 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_1869.z <= 0.0)
        {
            break;
        }
        uvec2 _1876 = uvec2(textureSize(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, 0));
        uint _1878 = _1876.x;
        uint _1881 = _1876.y;
        vec2 _1895 = _1869.xy * vec2(float(_126.OutputWidth), float(_126.OutputHeight));
        float _1907 = min((_1869.z * float(_126.OutputHeight)) / float(max(_1881, 1u)), 64.0);
        float _1910 = max(_126.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _1919 = ivec2(int(_126.OutputWidth) - 1, int(_126.OutputHeight) - 1);
        if (!(_126.ScreenSpace > 0.5))
        {
            if (_1907 < 0.5)
            {
                break;
            }
            ivec2 _1933 = max(ivec2(floor(_1895 - vec2(_1907))), ivec2(0));
            ivec2 _1941 = min(ivec2(ceil(_1895 + vec2(_1907))), _1919);
            if ((_1933.x > _1941.x) || (_1933.y > _1941.y))
            {
                break;
            }
            float _1958 = max(_1907 * _1907, 9.9999999747524270787835121154785e-07);
            int _1960 = _1933.y;
            for (int _3572 = _1960; _3572 <= _1941.y; _3572++)
            {
                int _1969 = _1933.x;
                for (int _3573 = _1969; _3573 <= _1941.x; _3573++)
                {
                    vec2 _1985 = vec2(float(_3573) + 0.5, float(_3572) + 0.5) - _1895;
                    float _1988 = dot(_1985, _1985);
                    if (_1988 > _1958)
                    {
                        continue;
                    }
                    float _1999 = sqrt(max(1.0 - (_1988 / _1958), 0.0));
                    float _2005 = _1869.w - (_1999 * _1910);
                    float _3580;
                    do
                    {
                        if ((_1878 == _126.OutputWidth) && (_1881 == _126.OutputHeight))
                        {
                            vec4 _3006 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3573, int((_1881 - 1u) - uint(_3572)), 0).xy, 0);
                            float _3007 = _3006.x;
                            _3580 = ((_3007 > 0.0) && (abs(_3007 - _126.ProjA) > 9.999999717180685365747194737196e-10)) ? (_126.ProjB / (_3007 - _126.ProjA)) : 1000000015047466219876688855040.0;
                            break;
                        }
                        vec2 _2912 = vec2(_1876) / vec2(float(_126.OutputWidth), float(_126.OutputHeight));
                        ivec2 _2921 = ivec2(floor(vec2(float(_3573), float(_3572)) * _2912));
                        ivec2 _2939 = min((ivec2(ceil(vec2(float(_3573 + 1), float(_3572 + 1)) * _2912)) - ivec2(1)), (ivec2(_1876) - ivec2(1)));
                        int _3575;
                        float _3576;
                        _3576 = 0.0;
                        _3575 = _2921.y;
                        float _3579;
                        SPIRV_CROSS_LOOP
                        for (; _3575 <= min(_2939.y, (_2921.y + 2)); _3576 = _3579, _3575++)
                        {
                            int _2954 = _2921.x;
                            _3579 = _3576;
                            SPIRV_CROSS_LOOP
                            for (int _3577 = _2954; _3577 <= min(_2939.x, (_2921.x + 2)); )
                            {
                                vec4 _3047 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3577, int((_1881 - 1u) - uint(_3575)), 0).xy, 0);
                                float _3048 = _3047.x;
                                _3579 = max(_3579, ((_3048 > 0.0) && (abs(_3048 - _126.ProjA) > 9.999999717180685365747194737196e-10)) ? (_126.ProjB / (_3048 - _126.ProjA)) : 1000000015047466219876688855040.0);
                                _3577++;
                                continue;
                            }
                        }
                        _3580 = _3576;
                        break;
                    } while(false);
                    if (_2005 >= _3580)
                    {
                        continue;
                    }
                    uint _2021 = uint(_3573) + (uint(_3572) * _126.OutputWidth);
                    uint _2027 = atomicMin(DepthBits_1._data[_2021], (uint(clamp((log2(max(_2005, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _2039 = atomicAdd(ThicknessBits_1._data[_2021], uint((((2.0 * _1999) * _1910) * _126.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _2053 = gl_GlobalInvocationID.x * 3u;
        vec4 _2056 = Anisotropy_1._data[_2053];
        bvec4 _2057 = bvec4(_126.Anisotropic > 0.5);
        vec4 _2058 = vec4(_2057.x ? _2056.x : vec4(0.0).x, _2057.y ? _2056.y : vec4(0.0).y, _2057.z ? _2056.z : vec4(0.0).z, _2057.w ? _2056.w : vec4(0.0).w);
        if (_2058.w < 0.0)
        {
            break;
        }
        bool _2066 = _126.Iteration == 2u;
        if (_2066 && (_126.SurfelBand <= 0.0))
        {
            break;
        }
        vec4 _2079 = Whitewater_1._data[gl_GlobalInvocationID.x];
        bvec4 _2080 = bvec4(_126.AuxEnabled > 0.5);
        vec4 _2081 = vec4(_2080.x ? _2079.x : vec4(0.0).x, _2080.y ? _2079.y : vec4(0.0).y, _2080.z ? _2079.z : vec4(0.0).z, _2080.w ? _2079.w : vec4(0.0).w);
        float _2093 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].w;
        float _2094 = ((_126.AuxEnabled > 0.5) && (_126.Anisotropic > 0.5)) ? _2093 : 0.0;
        bool _2097 = _2081.x > 0.0199999995529651641845703125;
        bool _2100 = _2081.y > 0.0199999995529651641845703125;
        bool _2103 = _2081.z > 0.0199999995529651641845703125;
        bool _2105 = _2094 > 0.0199999995529651641845703125;
        bool _2112 = ((_2097 || _2100) || _2103) || _2105;
        if ((!_2066) && (_1907 >= 0.5))
        {
            ivec2 _2125 = max(ivec2(floor(_1895 - vec2(_1907))), ivec2(0));
            ivec2 _2133 = min(ivec2(ceil(_1895 + vec2(_1907))), _1919);
            float _2137 = max(_1907 * _1907, 9.9999999747524270787835121154785e-07);
            int _2139 = _2125.y;
            for (int _3547 = _2139; _3547 <= _2133.y; _3547++)
            {
                int _2148 = _2125.x;
                for (int _3564 = _2148; _3564 <= _2133.x; _3564++)
                {
                    vec2 _2164 = vec2(float(_3564) + 0.5, float(_3547) + 0.5) - _1895;
                    float _2167 = dot(_2164, _2164);
                    if (_2167 > _2137)
                    {
                        continue;
                    }
                    float _2178 = sqrt(max(1.0 - (_2167 / _2137), 0.0));
                    uint _2186 = uint(_3564) + (uint(_3547) * _126.OutputWidth);
                    float _2193 = ((2.0 * _2178) * _1910) * _126.InvPackingFactor;
                    uint _2199 = atomicAdd(ThicknessBits_1._data[_2186], uint(_2193 * 1024.0));
                    if (_2112)
                    {
                        if (_2097)
                        {
                            uint _2215 = atomicMax(AuxBits_1._data[_2186 * 4u], uint(clamp(_2081.x * _2178, 0.0, 1.0) * 65535.0));
                        }
                        if (_2100)
                        {
                            uint _2230 = atomicAdd(AuxBits_1._data[(_2186 * 4u) + 1u], uint((min(_2081.y, 2.0) * _2193) * 1024.0));
                        }
                        if (_2103)
                        {
                            uint _2244 = atomicAdd(AuxBits_1._data[(_2186 * 4u) + 2u], uint((_2081.z * _2193) * 1024.0));
                        }
                        if (_2105)
                        {
                            uint _2257 = atomicAdd(AuxBits_1._data[(_2186 * 4u) + 3u], uint((_2094 * _2193) * 1024.0));
                        }
                    }
                }
            }
        }
        float _2276 = (0.5 * float(_126.OutputHeight)) * max(_126.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _2284 = (0.5 * float(_126.OutputWidth)) * max(_126.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _2292 = vec2(float(_126.OutputWidth), float(_126.OutputHeight)) * 0.5;
        if (_2066 && (_2058.w <= 0.0))
        {
            break;
        }
        if (_2058.w > 0.0)
        {
            uint _2306 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _2311 = (gl_GlobalInvocationID.x * 3u) + 2u;
            mat3 _2366 = mat3(vec3(_126.ViewProjection[0].x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07), _126.ViewProjection[1].x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07), _126.ViewProjection[2].x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_126.ViewProjection[0].y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07), _126.ViewProjection[1].y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07), _126.ViewProjection[2].y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_126.ViewProjection[0].w, _126.ViewProjection[1].w, _126.ViewProjection[2].w));
            mat3 _2372 = transpose(_2366) * (mat3(vec3(Anisotropy_1._data[_2306].xyz), vec3(Anisotropy_1._data[_2306].yw, Anisotropy_1._data[_2311].x), vec3(Anisotropy_1._data[_2306].z, Anisotropy_1._data[_2311].xy)) * _2366);
            vec4 _3155 = (((_126.ViewProjection[0] * _2058.x) + (_126.ViewProjection[1] * _2058.y)) + (_126.ViewProjection[2] * _2058.z)) + _126.ViewProjection[3];
            float _3164 = _3155.x / max(_126.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _3170 = _3155.y / max(_126.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _3172 = _3155.w;
            vec3 _3173 = vec3(_3164, _3170, _3172);
            float _2377 = _2058.w;
            if ((_3172 - _2377) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _2402 = vec2((_3164 * _2284) / _3172, ((-_3170) * _2276) / _3172) + _2292;
            float _2411 = min((_2377 * _2276) / (_3172 - _2377), 64.0);
            if (_2411 < 0.5)
            {
                break;
            }
            float _3190 = _2372[0].x;
            float _3192 = _2372[0].y;
            float _3194 = _2372[0].z;
            float _3196 = _2372[1].y;
            float _3198 = _2372[1].z;
            float _3200 = _2372[2].z;
            float _3207 = (_3196 * _3200) - (_3198 * _3198);
            float _3214 = (_3194 * _3198) - (_3192 * _3200);
            float _3221 = (_3192 * _3198) - (_3194 * _3196);
            float _3235 = (_3192 * _3194) - (_3190 * _3198);
            mat3 _3269 = mat3(vec3(_3207, _3214, _3221), vec3(_3214, (_3190 * _3200) - (_3194 * _3194), _3235), vec3(_3221, _3235, (_3190 * _3196) - (_3192 * _3192))) * (1.0 / (((_3190 * _3207) + (_3192 * _3214)) + (_3194 * _3221)));
            float _2441 = _3269[2].z - (_3172 * _3172);
            float _2449 = _3269[0].z - (_3164 * _3172);
            float _2457 = _3269[1].z - (_3170 * _3172);
            float _2466 = sqrt(max((_2449 * _2449) - ((_3269[0].x - (_3164 * _3164)) * _2441), 0.0));
            float _2475 = sqrt(max((_2457 * _2457) - ((_3269[1].y - (_3170 * _3170)) * _2441), 0.0));
            vec2 _2485 = vec2(_2449 + _2466, _2449 - _2466) / vec2(_2441);
            vec2 _2495 = vec2(_2457 + _2475, _2457 - _2475) / vec2(_2441);
            ivec2 _2542 = max(max(ivec2(floor(_2402 - vec2(_2411))), ivec2(floor(vec2(min(_2485.x, _2485.y) * _2284, (-max(_2495.x, _2495.y)) * _2276) + _2292))), ivec2(0));
            ivec2 _2554 = min(min(ivec2(ceil(_2402 + vec2(_2411))), ivec2(ceil(vec2(max(_2485.x, _2485.y) * _2284, (-min(_2495.x, _2495.y)) * _2276) + _2292))), _1919);
            if ((_2542.x > _2554.x) || (_2542.y > _2554.y))
            {
                break;
            }
            if (_2066)
            {
                uvec2 _1825[5] = uvec2[](uvec2(max(min(ivec2(floor(_2402)), _1919), ivec2(0))), uvec2(uint(_2542.x), uint(_2542.y)), uvec2(uint(_2554.x), uint(_2542.y)), uvec2(uint(_2542.x), uint(_2554.y)), uvec2(uint(_2554.x), uint(_2554.y)));
                float _3553;
                _3553 = 0.0;
                float _2631;
                bool _3554;
                int _3552 = 0;
                SPIRV_CROSS_UNROLL
                for (;;)
                {
                    if (_3552 < 5)
                    {
                        uint _2621 = _1825[_3552].x + (_1825[_3552].y * _126.OutputWidth);
                        if (DepthBits_1._data[_2621] >= 2139095040u)
                        {
                            _3554 = false;
                            break;
                        }
                        _2631 = max(_3553, uintBitsToFloat(DepthBits_1._data[_2621]));
                        _3553 = _2631;
                        _3552++;
                        continue;
                    }
                    else
                    {
                        _3554 = true;
                        break;
                    }
                }
                if (_3554 && ((_3172 - _2377) > (_3553 + _126.SurfelBand)))
                {
                    break;
                }
            }
            int _2651 = _2542.y;
            for (int _3557 = _2651; _3557 <= _2554.y; _3557++)
            {
                int _2660 = _2542.x;
                for (int _3558 = _2660; _3558 <= _2554.x; _3558++)
                {
                    vec3 _2685 = vec3(((float(_3558) + 0.5) - _2292.x) / _2284, (-((float(_3557) + 0.5) - _2292.y)) / _2276, 1.0);
                    vec3 _2691 = (_2685 * _3172) - _3173;
                    vec3 _2694 = _2685 * _2372;
                    float _2697 = dot(_2685, _2694);
                    float _2700 = dot(_2691, _2694);
                    float _2713 = (_2700 * _2700) - (_2697 * (dot(_2691, _2691 * _2372) - 1.0));
                    if ((_2713 < 0.0) || (_2697 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _2730 = _3172 + (((-_2700) - sqrt(_2713)) / _2697);
                    if (_2730 <= 0.0500000007450580596923828125)
                    {
                        continue;
                    }
                    uint _2742 = uint(_3558) + (uint(_3557) * _126.OutputWidth);
                    if (_2066)
                    {
                        do
                        {
                            if (DepthBits_1._data[_2742] >= 2139095040u)
                            {
                                break;
                            }
                            float _3292 = max(_2730 - uintBitsToFloat(DepthBits_1._data[_2742]), 0.0);
                            if (_3292 > _126.SurfelBand)
                            {
                                break;
                            }
                            vec3 _3301 = _2685 * _2730;
                            vec3 _3306 = (_3301 - _3173) * _2372;
                            float _3308 = length(_3306);
                            if (_3308 < 9.9999999747524270787835121154785e-07)
                            {
                                break;
                            }
                            vec3 _3316 = _3306 / vec3(_3308);
                            vec3 _3560;
                            if (dot(_3316, _3301) > 0.0)
                            {
                                _3560 = -_3316;
                            }
                            else
                            {
                                _3560 = _3316;
                            }
                            float _3329 = 1.0 - (_3292 / _126.SurfelBand);
                            float _3339 = (pow(clamp(_2713 / _2697, 0.0, 1.0), max(_126.SurfelSharpness, 0.25)) * _3329) * _3329;
                            if (_3339 < 0.001000000047497451305389404296875)
                            {
                                break;
                            }
                            uint _3345 = _2742 * 5u;
                            uint _3353 = atomicAdd(SurfelBits_1._data[_3345], uint((_3339 * 4096.0) + 0.5));
                            uint _3366 = atomicAdd(SurfelBits_1._data[_3345 + 1u], uint(((_3339 * (_3292 / _126.SurfelBand)) * 4096.0) + 0.5));
                            uint _3378 = atomicAdd(SurfelBits_1._data[_3345 + 2u], uint(int(roundEven((_3339 * _3560.x) * 4096.0))));
                            uint _3390 = atomicAdd(SurfelBits_1._data[_3345 + 3u], uint(int(roundEven((_3339 * _3560.y) * 4096.0))));
                            uint _3402 = atomicAdd(SurfelBits_1._data[_3345 + 4u], uint(int(roundEven((_3339 * _3560.z) * 4096.0))));
                            break;
                        } while(false);
                        continue;
                    }
                    uint _2759 = atomicMin(DepthBits_1._data[_2742], floatBitsToUint(_2730));
                }
            }
            break;
        }
        float _2772 = _1907 * _126.DepthRadiusScale;
        if (_2772 < 0.5)
        {
            break;
        }
        float _2782 = max(_126.Radius * _126.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _2789 = max(ivec2(floor(_1895 - vec2(_2772))), ivec2(0));
        ivec2 _2797 = min(ivec2(ceil(_1895 + vec2(_2772))), _1919);
        float _2800 = _2772 * _2772;
        int _2802 = _2789.y;
        for (int _3548 = _2802; _3548 <= _2797.y; _3548++)
        {
            int _2811 = _2789.x;
            for (int _3549 = _2811; _3549 <= _2797.x; _3549++)
            {
                vec2 _2827 = vec2(float(_3549) + 0.5, float(_3548) + 0.5) - _1895;
                float _2830 = dot(_2827, _2827);
                if (_2830 > _2800)
                {
                    continue;
                }
                uint _2858 = atomicMin(DepthBits_1._data[uint(_3549) + (uint(_3548) * _126.OutputWidth)], floatBitsToUint(_1869.w - (sqrt(max(1.0 - (_2830 / _2800), 0.0)) * _2782)));
            }
        }
        break;
    } while(false);
}

