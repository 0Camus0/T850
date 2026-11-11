#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=b12a6742bbb8bee8b242a4b2838e840626bdf56c39d39e9af3b2f76acd064df9; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _128;

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
        if (gl_GlobalInvocationID.x >= _128.ParticleCount)
        {
            break;
        }
        vec4 _1946 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_1946.z <= 0.0)
        {
            break;
        }
        uvec2 _1953 = uvec2(textureSize(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, 0));
        uint _1955 = _1953.x;
        uint _1958 = _1953.y;
        vec2 _1972 = _1946.xy * vec2(float(_128.OutputWidth), float(_128.OutputHeight));
        float _1984 = min((_1946.z * float(_128.OutputHeight)) / float(max(_1958, 1u)), 64.0);
        float _1987 = max(_128.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _1996 = ivec2(int(_128.OutputWidth) - 1, int(_128.OutputHeight) - 1);
        if (!(_128.ScreenSpace > 0.5))
        {
            if (_1984 < 0.5)
            {
                break;
            }
            ivec2 _2010 = max(ivec2(floor(_1972 - vec2(_1984))), ivec2(0));
            ivec2 _2018 = min(ivec2(ceil(_1972 + vec2(_1984))), _1996);
            if ((_2010.x > _2018.x) || (_2010.y > _2018.y))
            {
                break;
            }
            float _2035 = max(_1984 * _1984, 9.9999999747524270787835121154785e-07);
            int _2037 = _2010.y;
            for (int _3742 = _2037; _3742 <= _2018.y; _3742++)
            {
                int _2046 = _2010.x;
                for (int _3743 = _2046; _3743 <= _2018.x; _3743++)
                {
                    vec2 _2062 = vec2(float(_3743) + 0.5, float(_3742) + 0.5) - _1972;
                    float _2065 = dot(_2062, _2062);
                    if (_2065 > _2035)
                    {
                        continue;
                    }
                    float _2076 = sqrt(max(1.0 - (_2065 / _2035), 0.0));
                    float _2082 = _1946.w - (_2076 * _1987);
                    float _3750;
                    do
                    {
                        if ((_1955 == _128.OutputWidth) && (_1958 == _128.OutputHeight))
                        {
                            vec4 _3130 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3743, int((_1958 - 1u) - uint(_3742)), 0).xy, 0);
                            float _3131 = _3130.x;
                            _3750 = ((_3131 > 0.0) && (abs(_3131 - _128.ProjA) > 9.999999717180685365747194737196e-10)) ? (_128.ProjB / (_3131 - _128.ProjA)) : 1000000015047466219876688855040.0;
                            break;
                        }
                        vec2 _3036 = vec2(_1953) / vec2(float(_128.OutputWidth), float(_128.OutputHeight));
                        ivec2 _3045 = ivec2(floor(vec2(float(_3743), float(_3742)) * _3036));
                        ivec2 _3063 = min((ivec2(ceil(vec2(float(_3743 + 1), float(_3742 + 1)) * _3036)) - ivec2(1)), (ivec2(_1953) - ivec2(1)));
                        int _3745;
                        float _3746;
                        _3746 = 0.0;
                        _3745 = _3045.y;
                        float _3749;
                        SPIRV_CROSS_LOOP
                        for (; _3745 <= min(_3063.y, (_3045.y + 2)); _3746 = _3749, _3745++)
                        {
                            int _3078 = _3045.x;
                            _3749 = _3746;
                            SPIRV_CROSS_LOOP
                            for (int _3747 = _3078; _3747 <= min(_3063.x, (_3045.x + 2)); )
                            {
                                vec4 _3171 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3747, int((_1958 - 1u) - uint(_3745)), 0).xy, 0);
                                float _3172 = _3171.x;
                                _3749 = max(_3749, ((_3172 > 0.0) && (abs(_3172 - _128.ProjA) > 9.999999717180685365747194737196e-10)) ? (_128.ProjB / (_3172 - _128.ProjA)) : 1000000015047466219876688855040.0);
                                _3747++;
                                continue;
                            }
                        }
                        _3750 = _3746;
                        break;
                    } while(false);
                    if (_2082 >= _3750)
                    {
                        continue;
                    }
                    uint _2098 = uint(_3743) + (uint(_3742) * _128.OutputWidth);
                    uint _2104 = atomicMin(DepthBits_1._data[_2098], (uint(clamp((log2(max(_2082, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _2116 = atomicAdd(ThicknessBits_1._data[_2098], uint((((2.0 * _2076) * _1987) * _128.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _2130 = gl_GlobalInvocationID.x * 3u;
        vec4 _2133 = Anisotropy_1._data[_2130];
        bvec4 _2134 = bvec4(_128.Anisotropic > 0.5);
        vec4 _2135 = vec4(_2134.x ? _2133.x : vec4(0.0).x, _2134.y ? _2133.y : vec4(0.0).y, _2134.z ? _2133.z : vec4(0.0).z, _2134.w ? _2133.w : vec4(0.0).w);
        if (_2135.w < 0.0)
        {
            break;
        }
        bool _2143 = _128.Iteration == 2u;
        if (_2143 && (_128.SurfelBand <= 0.0))
        {
            break;
        }
        vec4 _2156 = Whitewater_1._data[gl_GlobalInvocationID.x];
        bvec4 _2157 = bvec4(_128.AuxEnabled > 0.5);
        vec4 _2158 = vec4(_2157.x ? _2156.x : vec4(0.0).x, _2157.y ? _2156.y : vec4(0.0).y, _2157.z ? _2156.z : vec4(0.0).z, _2157.w ? _2156.w : vec4(0.0).w);
        float _2170 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].w;
        float _2171 = ((_128.AuxEnabled > 0.5) && (_128.Anisotropic > 0.5)) ? _2170 : 0.0;
        bool _2174 = _2158.x > 0.0199999995529651641845703125;
        bool _2177 = _2158.y > 0.0199999995529651641845703125;
        bool _2180 = _2158.z > 0.0199999995529651641845703125;
        bool _2182 = _2171 > 0.0199999995529651641845703125;
        bool _2189 = ((_2174 || _2177) || _2180) || _2182;
        if ((!_2143) && (_1984 >= 0.5))
        {
            ivec2 _2202 = max(ivec2(floor(_1972 - vec2(_1984))), ivec2(0));
            ivec2 _2210 = min(ivec2(ceil(_1972 + vec2(_1984))), _1996);
            float _2214 = max(_1984 * _1984, 9.9999999747524270787835121154785e-07);
            int _2216 = _2202.y;
            for (int _3700 = _2216; _3700 <= _2210.y; _3700++)
            {
                int _2225 = _2202.x;
                for (int _3734 = _2225; _3734 <= _2210.x; _3734++)
                {
                    vec2 _2241 = vec2(float(_3734) + 0.5, float(_3700) + 0.5) - _1972;
                    float _2244 = dot(_2241, _2241);
                    if (_2244 > _2214)
                    {
                        continue;
                    }
                    float _2255 = sqrt(max(1.0 - (_2244 / _2214), 0.0));
                    uint _2263 = uint(_3734) + (uint(_3700) * _128.OutputWidth);
                    float _2270 = ((2.0 * _2255) * _1987) * _128.InvPackingFactor;
                    uint _2276 = atomicAdd(ThicknessBits_1._data[_2263], uint(_2270 * 1024.0));
                    if (_2189)
                    {
                        if (_2174)
                        {
                            uint _2292 = atomicMax(AuxBits_1._data[_2263 * 4u], uint(clamp(_2158.x * _2255, 0.0, 1.0) * 65535.0));
                        }
                        if (_2177)
                        {
                            uint _2307 = atomicAdd(AuxBits_1._data[(_2263 * 4u) + 1u], uint((min(_2158.y, 2.0) * _2270) * 1024.0));
                        }
                        if (_2180)
                        {
                            uint _2321 = atomicAdd(AuxBits_1._data[(_2263 * 4u) + 2u], uint((_2158.z * _2270) * 1024.0));
                        }
                        if (_2182)
                        {
                            uint _2334 = atomicAdd(AuxBits_1._data[(_2263 * 4u) + 3u], uint((_2171 * _2270) * 1024.0));
                        }
                    }
                }
            }
        }
        float _2353 = (0.5 * float(_128.OutputHeight)) * max(_128.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _2361 = (0.5 * float(_128.OutputWidth)) * max(_128.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _2369 = vec2(float(_128.OutputWidth), float(_128.OutputHeight)) * 0.5;
        if (_2143 && (_2135.w <= 0.0))
        {
            break;
        }
        if (_2135.w > 0.0)
        {
            uint _2383 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _2388 = (gl_GlobalInvocationID.x * 3u) + 2u;
            mat3 _2443 = mat3(vec3(_128.ViewProjection[0].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[2].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_128.ViewProjection[0].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[2].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_128.ViewProjection[0].w, _128.ViewProjection[1].w, _128.ViewProjection[2].w));
            mat3 _2449 = transpose(_2443) * (mat3(vec3(Anisotropy_1._data[_2383].xyz), vec3(Anisotropy_1._data[_2383].yw, Anisotropy_1._data[_2388].x), vec3(Anisotropy_1._data[_2383].z, Anisotropy_1._data[_2388].xy)) * _2443);
            vec4 _3279 = (((_128.ViewProjection[0] * _2135.x) + (_128.ViewProjection[1] * _2135.y)) + (_128.ViewProjection[2] * _2135.z)) + _128.ViewProjection[3];
            float _3288 = _3279.x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _3294 = _3279.y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _3296 = _3279.w;
            vec3 _3297 = vec3(_3288, _3294, _3296);
            float _2454 = _2135.w;
            if ((_3296 - _2454) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _2479 = vec2((_3288 * _2361) / _3296, ((-_3294) * _2353) / _3296) + _2369;
            float _2488 = min((_2454 * _2353) / (_3296 - _2454), 64.0);
            if (_2488 < 0.5)
            {
                break;
            }
            float _3314 = _2449[0].x;
            float _3316 = _2449[0].y;
            float _3318 = _2449[0].z;
            float _3320 = _2449[1].y;
            float _3322 = _2449[1].z;
            float _3324 = _2449[2].z;
            float _3331 = (_3320 * _3324) - (_3322 * _3322);
            float _3338 = (_3318 * _3322) - (_3316 * _3324);
            float _3345 = (_3316 * _3322) - (_3318 * _3320);
            float _3359 = (_3316 * _3318) - (_3314 * _3322);
            mat3 _3393 = mat3(vec3(_3331, _3338, _3345), vec3(_3338, (_3314 * _3324) - (_3318 * _3318), _3359), vec3(_3345, _3359, (_3314 * _3320) - (_3316 * _3316))) * (1.0 / (((_3314 * _3331) + (_3316 * _3338)) + (_3318 * _3345)));
            float _2518 = _3393[2].z - (_3296 * _3296);
            float _2526 = _3393[0].z - (_3288 * _3296);
            float _2534 = _3393[1].z - (_3294 * _3296);
            float _2543 = sqrt(max((_2526 * _2526) - ((_3393[0].x - (_3288 * _3288)) * _2518), 0.0));
            float _2552 = sqrt(max((_2534 * _2534) - ((_3393[1].y - (_3294 * _3294)) * _2518), 0.0));
            vec2 _2562 = vec2(_2526 + _2543, _2526 - _2543) / vec2(_2518);
            vec2 _2572 = vec2(_2534 + _2552, _2534 - _2552) / vec2(_2518);
            ivec2 _2619 = max(max(ivec2(floor(_2479 - vec2(_2488))), ivec2(floor(vec2(min(_2562.x, _2562.y) * _2361, (-max(_2572.x, _2572.y)) * _2353) + _2369))), ivec2(0));
            ivec2 _2631 = min(min(ivec2(ceil(_2479 + vec2(_2488))), ivec2(ceil(vec2(max(_2562.x, _2562.y) * _2361, (-min(_2572.x, _2572.y)) * _2353) + _2369))), _1996);
            if ((_2619.x > _2631.x) || (_2619.y > _2631.y))
            {
                break;
            }
            vec3 _3720;
            float _3727;
            if (_2143)
            {
                vec3 _3706;
                _3706 = vec3(_128.ViewProjection[1].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].w);
                SPIRV_CROSS_UNROLL
                for (int _3705 = 0; _3705 < 6; )
                {
                    _3706 = (_3706 / vec3(max(length(_3706), 9.9999996826552253889678874634872e-21))) * _2449;
                    _3705++;
                    continue;
                }
                float _2668 = length(_3706);
                vec3 _3707;
                if (_2668 > 9.9999996826552253889678874634872e-21)
                {
                    _3707 = _3706 / vec3(_2668);
                }
                else
                {
                    _3707 = vec3(0.0, 0.0, -1.0);
                }
                vec3 _3721;
                if (dot(_3707, _3297) > 0.0)
                {
                    _3721 = -_3707;
                }
                else
                {
                    _3721 = _3707;
                }
                _3727 = 1.0 - clamp(Anisotropy_1._data[_2388].w, 0.0, 1.0);
                _3720 = _3721;
            }
            else
            {
                _3727 = 0.0;
                _3720 = vec3(0.0, 0.0, -1.0);
            }
            if (_2143)
            {
                uvec2 _1900[5] = uvec2[](uvec2(max(min(ivec2(floor(_2479)), _1996), ivec2(0))), uvec2(uint(_2619.x), uint(_2619.y)), uvec2(uint(_2631.x), uint(_2619.y)), uvec2(uint(_2619.x), uint(_2631.y)), uvec2(uint(_2631.x), uint(_2631.y)));
                float _3710;
                _3710 = 0.0;
                float _2753;
                bool _3711;
                int _3709 = 0;
                SPIRV_CROSS_UNROLL
                for (;;)
                {
                    if (_3709 < 5)
                    {
                        uint _2743 = _1900[_3709].x + (_1900[_3709].y * _128.OutputWidth);
                        if (DepthBits_1._data[_2743] >= 2139095040u)
                        {
                            _3711 = false;
                            break;
                        }
                        _2753 = max(_3710, uintBitsToFloat(DepthBits_1._data[_2743]));
                        _3710 = _2753;
                        _3709++;
                        continue;
                    }
                    else
                    {
                        _3711 = true;
                        break;
                    }
                }
                if (_3711 && ((_3296 - _2454) > (_3710 + _128.SurfelBand)))
                {
                    break;
                }
            }
            int _2773 = _2619.y;
            for (int _3714 = _2773; _3714 <= _2631.y; _3714++)
            {
                int _2782 = _2619.x;
                for (int _3715 = _2782; _3715 <= _2631.x; _3715++)
                {
                    vec3 _2807 = vec3(((float(_3715) + 0.5) - _2369.x) / _2361, (-((float(_3714) + 0.5) - _2369.y)) / _2353, 1.0);
                    vec3 _2813 = (_2807 * _3296) - _3297;
                    vec3 _2816 = _2807 * _2449;
                    float _2819 = dot(_2807, _2816);
                    float _2822 = dot(_2813, _2816);
                    float _2835 = (_2822 * _2822) - (_2819 * (dot(_2813, _2813 * _2449) - 1.0));
                    if ((_2835 < 0.0) || (_2819 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _2852 = _3296 + (((-_2822) - sqrt(_2835)) / _2819);
                    if (_2852 <= 0.0500000007450580596923828125)
                    {
                        continue;
                    }
                    uint _2864 = uint(_3715) + (uint(_3714) * _128.OutputWidth);
                    if (_2143)
                    {
                        do
                        {
                            if (DepthBits_1._data[_2864] >= 2139095040u)
                            {
                                break;
                            }
                            float _3434 = max(_2852 - uintBitsToFloat(DepthBits_1._data[_2864]), 0.0);
                            if (_3434 > _128.SurfelBand)
                            {
                                break;
                            }
                            vec3 _3443 = _2807 * _2852;
                            vec3 _3448 = (_3443 - _3297) * _2449;
                            float _3450 = length(_3448);
                            vec3 _3456 = _3448 / vec3(_3450);
                            bvec3 _3458 = bvec3(_3450 > 9.9999999747524270787835121154785e-07);
                            vec3 _3459 = vec3(_3458.x ? _3456.x : _3720.x, _3458.y ? _3456.y : _3720.y, _3458.z ? _3456.z : _3720.z);
                            vec3 _3730;
                            if (dot(_3459, _3443) > 0.0)
                            {
                                _3730 = -_3459;
                            }
                            else
                            {
                                _3730 = _3459;
                            }
                            vec3 _3473 = normalize(mix(_3730, _3720, vec3(_3727)));
                            float _3478 = 1.0 - (_3434 / _128.SurfelBand);
                            float _3488 = (pow(clamp(_2835 / _2819, 0.0, 1.0), max(_128.SurfelSharpness, 0.25)) * _3478) * _3478;
                            if (_3488 < 0.001000000047497451305389404296875)
                            {
                                break;
                            }
                            uint _3494 = _2864 * 5u;
                            uint _3502 = atomicAdd(SurfelBits_1._data[_3494], uint((_3488 * 4096.0) + 0.5));
                            uint _3515 = atomicAdd(SurfelBits_1._data[_3494 + 1u], uint(((_3488 * (_3434 / _128.SurfelBand)) * 4096.0) + 0.5));
                            uint _3527 = atomicAdd(SurfelBits_1._data[_3494 + 2u], uint(int(roundEven((_3488 * _3473.x) * 4096.0))));
                            uint _3539 = atomicAdd(SurfelBits_1._data[_3494 + 3u], uint(int(roundEven((_3488 * _3473.y) * 4096.0))));
                            uint _3551 = atomicAdd(SurfelBits_1._data[_3494 + 4u], uint(int(roundEven((_3488 * _3473.z) * 4096.0))));
                            break;
                        } while(false);
                        continue;
                    }
                    uint _2883 = atomicMin(DepthBits_1._data[_2864], floatBitsToUint(_2852));
                }
            }
            break;
        }
        float _2896 = _1984 * _128.DepthRadiusScale;
        if (_2896 < 0.5)
        {
            break;
        }
        float _2906 = max(_128.Radius * _128.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _2913 = max(ivec2(floor(_1972 - vec2(_2896))), ivec2(0));
        ivec2 _2921 = min(ivec2(ceil(_1972 + vec2(_2896))), _1996);
        float _2924 = _2896 * _2896;
        int _2926 = _2913.y;
        for (int _3701 = _2926; _3701 <= _2921.y; _3701++)
        {
            int _2935 = _2913.x;
            for (int _3702 = _2935; _3702 <= _2921.x; _3702++)
            {
                vec2 _2951 = vec2(float(_3702) + 0.5, float(_3701) + 0.5) - _1972;
                float _2954 = dot(_2951, _2951);
                if (_2954 > _2924)
                {
                    continue;
                }
                uint _2982 = atomicMin(DepthBits_1._data[uint(_3702) + (uint(_3701) * _128.OutputWidth)], floatBitsToUint(_1946.w - (sqrt(max(1.0 - (_2954 / _2924), 0.0)) * _2906)));
            }
        }
        break;
    } while(false);
}

