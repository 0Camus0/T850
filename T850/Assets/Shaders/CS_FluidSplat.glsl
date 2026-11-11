#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=e69cfaa1a5dc75f227f7d8241ea2faa8a2b7e11228c2814428f1151369954cb9; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    uint ThicknessShift;
    float ThicknessPad0;
    float ThicknessPad1;
    float ThicknessPad2;
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
        vec4 _2006 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_2006.z <= 0.0)
        {
            break;
        }
        uvec2 _2013 = uvec2(textureSize(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, 0));
        uint _2015 = _2013.x;
        uint _2018 = _2013.y;
        vec2 _2032 = _2006.xy * vec2(float(_128.OutputWidth), float(_128.OutputHeight));
        float _2044 = min((_2006.z * float(_128.OutputHeight)) / float(max(_2018, 1u)), 64.0);
        float _2047 = max(_128.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _2056 = ivec2(int(_128.OutputWidth) - 1, int(_128.OutputHeight) - 1);
        uint _2063 = (_128.ScreenSpace > 0.5) ? min(_128.ThicknessShift, 1u) : 0u;
        float _2067 = 1.0 / float(1u << _2063);
        if (!(_128.ScreenSpace > 0.5))
        {
            if (_2044 < 0.5)
            {
                break;
            }
            ivec2 _2081 = max(ivec2(floor(_2032 - vec2(_2044))), ivec2(0));
            ivec2 _2089 = min(ivec2(ceil(_2032 + vec2(_2044))), _2056);
            if ((_2081.x > _2089.x) || (_2081.y > _2089.y))
            {
                break;
            }
            float _2106 = max(_2044 * _2044, 9.9999999747524270787835121154785e-07);
            int _2108 = _2081.y;
            for (int _3840 = _2108; _3840 <= _2089.y; _3840++)
            {
                int _2117 = _2081.x;
                for (int _3841 = _2117; _3841 <= _2089.x; _3841++)
                {
                    vec2 _2133 = vec2(float(_3841) + 0.5, float(_3840) + 0.5) - _2032;
                    float _2136 = dot(_2133, _2133);
                    if (_2136 > _2106)
                    {
                        continue;
                    }
                    float _2147 = sqrt(max(1.0 - (_2136 / _2106), 0.0));
                    float _2153 = _2006.w - (_2147 * _2047);
                    float _3848;
                    do
                    {
                        if ((_2015 == _128.OutputWidth) && (_2018 == _128.OutputHeight))
                        {
                            vec4 _3234 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3841, int((_2018 - 1u) - uint(_3840)), 0).xy, 0);
                            float _3235 = _3234.x;
                            _3848 = ((_3235 > 0.0) && (abs(_3235 - _128.ProjA) > 9.999999717180685365747194737196e-10)) ? (_128.ProjB / (_3235 - _128.ProjA)) : 1000000015047466219876688855040.0;
                            break;
                        }
                        vec2 _3140 = vec2(_2013) / vec2(float(_128.OutputWidth), float(_128.OutputHeight));
                        ivec2 _3149 = ivec2(floor(vec2(float(_3841), float(_3840)) * _3140));
                        ivec2 _3167 = min((ivec2(ceil(vec2(float(_3841 + 1), float(_3840 + 1)) * _3140)) - ivec2(1)), (ivec2(_2013) - ivec2(1)));
                        int _3843;
                        float _3844;
                        _3844 = 0.0;
                        _3843 = _3149.y;
                        float _3847;
                        SPIRV_CROSS_LOOP
                        for (; _3843 <= min(_3167.y, (_3149.y + 2)); _3844 = _3847, _3843++)
                        {
                            int _3182 = _3149.x;
                            _3847 = _3844;
                            SPIRV_CROSS_LOOP
                            for (int _3845 = _3182; _3845 <= min(_3167.x, (_3149.x + 2)); )
                            {
                                vec4 _3275 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3845, int((_2018 - 1u) - uint(_3843)), 0).xy, 0);
                                float _3276 = _3275.x;
                                _3847 = max(_3847, ((_3276 > 0.0) && (abs(_3276 - _128.ProjA) > 9.999999717180685365747194737196e-10)) ? (_128.ProjB / (_3276 - _128.ProjA)) : 1000000015047466219876688855040.0);
                                _3845++;
                                continue;
                            }
                        }
                        _3848 = _3844;
                        break;
                    } while(false);
                    if (_2153 >= _3848)
                    {
                        continue;
                    }
                    uint _2169 = uint(_3841) + (uint(_3840) * _128.OutputWidth);
                    uint _2175 = atomicMin(DepthBits_1._data[_2169], (uint(clamp((log2(max(_2153, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _2187 = atomicAdd(ThicknessBits_1._data[_2169], uint((((2.0 * _2147) * _2047) * _128.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _2201 = gl_GlobalInvocationID.x * 3u;
        vec4 _2204 = Anisotropy_1._data[_2201];
        bvec4 _2205 = bvec4(_128.Anisotropic > 0.5);
        vec4 _2206 = vec4(_2205.x ? _2204.x : vec4(0.0).x, _2205.y ? _2204.y : vec4(0.0).y, _2205.z ? _2204.z : vec4(0.0).z, _2205.w ? _2204.w : vec4(0.0).w);
        if (_2206.w < 0.0)
        {
            break;
        }
        bool _2214 = _128.Iteration == 2u;
        if (_2214 && (_128.SurfelBand <= 0.0))
        {
            break;
        }
        vec4 _2227 = Whitewater_1._data[gl_GlobalInvocationID.x];
        bvec4 _2228 = bvec4(_128.AuxEnabled > 0.5);
        vec4 _2229 = vec4(_2228.x ? _2227.x : vec4(0.0).x, _2228.y ? _2227.y : vec4(0.0).y, _2228.z ? _2227.z : vec4(0.0).z, _2228.w ? _2227.w : vec4(0.0).w);
        float _2241 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].w;
        float _2242 = ((_128.AuxEnabled > 0.5) && (_128.Anisotropic > 0.5)) ? _2241 : 0.0;
        bool _2245 = _2229.x > 0.0199999995529651641845703125;
        bool _2248 = _2229.y > 0.0199999995529651641845703125;
        bool _2251 = _2229.z > 0.0199999995529651641845703125;
        bool _2253 = _2242 > 0.0199999995529651641845703125;
        bool _2260 = ((_2245 || _2248) || _2251) || _2253;
        if ((!_2214) && (_2044 >= 0.5))
        {
            vec2 _2269 = _2032 * _2067;
            float _2277 = (_2063 > 0u) ? max(_2044 * _2067, 0.75) : _2044;
            uint _2285 = ((_128.OutputWidth + (1u << _2063)) - 1u) >> _2063;
            ivec2 _2307 = max(ivec2(floor(_2269 - vec2(_2277))), ivec2(0));
            ivec2 _2315 = min(ivec2(ceil(_2269 + vec2(_2277))), ivec2(int(_2285) - 1, int(((_128.OutputHeight + (1u << _2063)) - 1u) >> _2063) - 1));
            float _2319 = max(_2277 * _2277, 9.9999999747524270787835121154785e-07);
            int _2321 = _2307.y;
            for (int _3804 = _2321; _3804 <= _2315.y; _3804++)
            {
                int _2330 = _2307.x;
                for (int _3832 = _2330; _3832 <= _2315.x; _3832++)
                {
                    vec2 _2346 = vec2(float(_3832) + 0.5, float(_3804) + 0.5) - _2269;
                    float _2349 = dot(_2346, _2346);
                    if (_2349 > _2319)
                    {
                        continue;
                    }
                    float _2360 = sqrt(max(1.0 - (_2349 / _2319), 0.0));
                    uint _2367 = uint(_3832) + (uint(_3804) * _2285);
                    float _2374 = ((2.0 * _2360) * _2047) * _128.InvPackingFactor;
                    uint _2380 = atomicAdd(ThicknessBits_1._data[_2367], uint(_2374 * 1024.0));
                    if (_2260)
                    {
                        if (_2245)
                        {
                            uint _2396 = atomicMax(AuxBits_1._data[_2367 * 4u], uint(clamp(_2229.x * _2360, 0.0, 1.0) * 65535.0));
                        }
                        if (_2248)
                        {
                            uint _2411 = atomicAdd(AuxBits_1._data[(_2367 * 4u) + 1u], uint((min(_2229.y, 2.0) * _2374) * 1024.0));
                        }
                        if (_2251)
                        {
                            uint _2425 = atomicAdd(AuxBits_1._data[(_2367 * 4u) + 2u], uint((_2229.z * _2374) * 1024.0));
                        }
                        if (_2253)
                        {
                            uint _2438 = atomicAdd(AuxBits_1._data[(_2367 * 4u) + 3u], uint((_2242 * _2374) * 1024.0));
                        }
                    }
                }
            }
        }
        float _2457 = (0.5 * float(_128.OutputHeight)) * max(_128.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _2465 = (0.5 * float(_128.OutputWidth)) * max(_128.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _2473 = vec2(float(_128.OutputWidth), float(_128.OutputHeight)) * 0.5;
        if (_2214 && (_2206.w <= 0.0))
        {
            break;
        }
        if (_2206.w > 0.0)
        {
            uint _2487 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _2492 = (gl_GlobalInvocationID.x * 3u) + 2u;
            mat3 _2547 = mat3(vec3(_128.ViewProjection[0].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[2].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_128.ViewProjection[0].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[2].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_128.ViewProjection[0].w, _128.ViewProjection[1].w, _128.ViewProjection[2].w));
            mat3 _2553 = transpose(_2547) * (mat3(vec3(Anisotropy_1._data[_2487].xyz), vec3(Anisotropy_1._data[_2487].yw, Anisotropy_1._data[_2492].x), vec3(Anisotropy_1._data[_2487].z, Anisotropy_1._data[_2492].xy)) * _2547);
            vec4 _3383 = (((_128.ViewProjection[0] * _2206.x) + (_128.ViewProjection[1] * _2206.y)) + (_128.ViewProjection[2] * _2206.z)) + _128.ViewProjection[3];
            float _3392 = _3383.x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _3398 = _3383.y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _3400 = _3383.w;
            vec3 _3401 = vec3(_3392, _3398, _3400);
            float _2558 = _2206.w;
            if ((_3400 - _2558) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _2583 = vec2((_3392 * _2465) / _3400, ((-_3398) * _2457) / _3400) + _2473;
            float _2592 = min((_2558 * _2457) / (_3400 - _2558), 64.0);
            if (_2592 < 0.5)
            {
                break;
            }
            float _3418 = _2553[0].x;
            float _3420 = _2553[0].y;
            float _3422 = _2553[0].z;
            float _3424 = _2553[1].y;
            float _3426 = _2553[1].z;
            float _3428 = _2553[2].z;
            float _3435 = (_3424 * _3428) - (_3426 * _3426);
            float _3442 = (_3422 * _3426) - (_3420 * _3428);
            float _3449 = (_3420 * _3426) - (_3422 * _3424);
            float _3463 = (_3420 * _3422) - (_3418 * _3426);
            mat3 _3497 = mat3(vec3(_3435, _3442, _3449), vec3(_3442, (_3418 * _3428) - (_3422 * _3422), _3463), vec3(_3449, _3463, (_3418 * _3424) - (_3420 * _3420))) * (1.0 / (((_3418 * _3435) + (_3420 * _3442)) + (_3422 * _3449)));
            float _2622 = _3497[2].z - (_3400 * _3400);
            float _2630 = _3497[0].z - (_3392 * _3400);
            float _2638 = _3497[1].z - (_3398 * _3400);
            float _2647 = sqrt(max((_2630 * _2630) - ((_3497[0].x - (_3392 * _3392)) * _2622), 0.0));
            float _2656 = sqrt(max((_2638 * _2638) - ((_3497[1].y - (_3398 * _3398)) * _2622), 0.0));
            vec2 _2666 = vec2(_2630 + _2647, _2630 - _2647) / vec2(_2622);
            vec2 _2676 = vec2(_2638 + _2656, _2638 - _2656) / vec2(_2622);
            ivec2 _2723 = max(max(ivec2(floor(_2583 - vec2(_2592))), ivec2(floor(vec2(min(_2666.x, _2666.y) * _2465, (-max(_2676.x, _2676.y)) * _2457) + _2473))), ivec2(0));
            ivec2 _2735 = min(min(ivec2(ceil(_2583 + vec2(_2592))), ivec2(ceil(vec2(max(_2666.x, _2666.y) * _2465, (-min(_2676.x, _2676.y)) * _2457) + _2473))), _2056);
            if ((_2723.x > _2735.x) || (_2723.y > _2735.y))
            {
                break;
            }
            if (_2214)
            {
                uvec2 _1954[5] = uvec2[](uvec2(max(min(ivec2(floor(_2583)), _2056), ivec2(0))), uvec2(uint(_2723.x), uint(_2723.y)), uvec2(uint(_2735.x), uint(_2723.y)), uvec2(uint(_2723.x), uint(_2735.y)), uvec2(uint(_2735.x), uint(_2735.y)));
                float _3810;
                _3810 = 0.0;
                float _2812;
                bool _3811;
                int _3809 = 0;
                SPIRV_CROSS_UNROLL
                for (;;)
                {
                    if (_3809 < 5)
                    {
                        uint _2802 = _1954[_3809].x + (_1954[_3809].y * _128.OutputWidth);
                        if (DepthBits_1._data[_2802] >= 2139095040u)
                        {
                            _3811 = false;
                            break;
                        }
                        _2812 = max(_3810, uintBitsToFloat(DepthBits_1._data[_2802]));
                        _3810 = _2812;
                        _3809++;
                        continue;
                    }
                    else
                    {
                        _3811 = true;
                        break;
                    }
                }
                if (_3811 && ((_3400 - _2558) > (_3810 + _128.SurfelBand)))
                {
                    break;
                }
            }
            vec3 _3823;
            float _3827;
            if (_2214)
            {
                vec3 _3815;
                _3815 = vec3(_128.ViewProjection[1].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].w);
                SPIRV_CROSS_UNROLL
                for (int _3814 = 0; _3814 < 6; )
                {
                    _3815 = (_3815 / vec3(max(length(_3815), 9.9999996826552253889678874634872e-21))) * _2553;
                    _3814++;
                    continue;
                }
                float _2854 = length(_3815);
                vec3 _3816;
                if (_2854 > 9.9999996826552253889678874634872e-21)
                {
                    _3816 = _3815 / vec3(_2854);
                }
                else
                {
                    _3816 = vec3(0.0, 0.0, -1.0);
                }
                vec3 _3824;
                if (dot(_3816, _3401) > 0.0)
                {
                    _3824 = -_3816;
                }
                else
                {
                    _3824 = _3816;
                }
                _3827 = 1.0 - clamp(Anisotropy_1._data[_2492].w, 0.0, 1.0);
                _3823 = _3824;
            }
            else
            {
                _3827 = 0.0;
                _3823 = vec3(0.0, 0.0, -1.0);
            }
            int _2877 = _2723.y;
            for (int _3818 = _2877; _3818 <= _2735.y; _3818++)
            {
                int _2886 = _2723.x;
                for (int _3819 = _2886; _3819 <= _2735.x; _3819++)
                {
                    vec3 _2911 = vec3(((float(_3819) + 0.5) - _2473.x) / _2465, (-((float(_3818) + 0.5) - _2473.y)) / _2457, 1.0);
                    vec3 _2917 = (_2911 * _3400) - _3401;
                    vec3 _2920 = _2911 * _2553;
                    float _2923 = dot(_2911, _2920);
                    float _2926 = dot(_2917, _2920);
                    float _2939 = (_2926 * _2926) - (_2923 * (dot(_2917, _2917 * _2553) - 1.0));
                    if ((_2939 < 0.0) || (_2923 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _2956 = _3400 + (((-_2926) - sqrt(_2939)) / _2923);
                    if (_2956 <= 0.0500000007450580596923828125)
                    {
                        continue;
                    }
                    uint _2968 = uint(_3819) + (uint(_3818) * _128.OutputWidth);
                    if (_2214)
                    {
                        do
                        {
                            if (DepthBits_1._data[_2968] >= 2139095040u)
                            {
                                break;
                            }
                            float _3538 = max(_2956 - uintBitsToFloat(DepthBits_1._data[_2968]), 0.0);
                            if (_3538 > _128.SurfelBand)
                            {
                                break;
                            }
                            vec3 _3547 = _2911 * _2956;
                            vec3 _3552 = (_3547 - _3401) * _2553;
                            float _3554 = length(_3552);
                            vec3 _3560 = _3552 / vec3(_3554);
                            bvec3 _3562 = bvec3(_3554 > 9.9999999747524270787835121154785e-07);
                            vec3 _3563 = vec3(_3562.x ? _3560.x : _3823.x, _3562.y ? _3560.y : _3823.y, _3562.z ? _3560.z : _3823.z);
                            vec3 _3828;
                            if (dot(_3563, _3547) > 0.0)
                            {
                                _3828 = -_3563;
                            }
                            else
                            {
                                _3828 = _3563;
                            }
                            vec3 _3577 = normalize(mix(_3828, _3823, vec3(_3827)));
                            float _3582 = 1.0 - (_3538 / _128.SurfelBand);
                            float _3592 = (pow(clamp(_2939 / _2923, 0.0, 1.0), max(_128.SurfelSharpness, 0.25)) * _3582) * _3582;
                            if (_3592 < 0.001000000047497451305389404296875)
                            {
                                break;
                            }
                            uint _3598 = _2968 * 5u;
                            uint _3606 = atomicAdd(SurfelBits_1._data[_3598], uint((_3592 * 4096.0) + 0.5));
                            uint _3619 = atomicAdd(SurfelBits_1._data[_3598 + 1u], uint(((_3592 * (_3538 / _128.SurfelBand)) * 4096.0) + 0.5));
                            uint _3631 = atomicAdd(SurfelBits_1._data[_3598 + 2u], uint(int(roundEven((_3592 * _3577.x) * 4096.0))));
                            uint _3643 = atomicAdd(SurfelBits_1._data[_3598 + 3u], uint(int(roundEven((_3592 * _3577.y) * 4096.0))));
                            uint _3655 = atomicAdd(SurfelBits_1._data[_3598 + 4u], uint(int(roundEven((_3592 * _3577.z) * 4096.0))));
                            break;
                        } while(false);
                        continue;
                    }
                    uint _2987 = atomicMin(DepthBits_1._data[_2968], floatBitsToUint(_2956));
                }
            }
            break;
        }
        float _3000 = _2044 * _128.DepthRadiusScale;
        if (_3000 < 0.5)
        {
            break;
        }
        float _3010 = max(_128.Radius * _128.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _3017 = max(ivec2(floor(_2032 - vec2(_3000))), ivec2(0));
        ivec2 _3025 = min(ivec2(ceil(_2032 + vec2(_3000))), _2056);
        float _3028 = _3000 * _3000;
        int _3030 = _3017.y;
        for (int _3805 = _3030; _3805 <= _3025.y; _3805++)
        {
            int _3039 = _3017.x;
            for (int _3806 = _3039; _3806 <= _3025.x; _3806++)
            {
                vec2 _3055 = vec2(float(_3806) + 0.5, float(_3805) + 0.5) - _2032;
                float _3058 = dot(_3055, _3055);
                if (_3058 > _3028)
                {
                    continue;
                }
                uint _3086 = atomicMin(DepthBits_1._data[uint(_3806) + (uint(_3805) * _128.OutputWidth)], floatBitsToUint(_2006.w - (sqrt(max(1.0 - (_3058 / _3028), 0.0)) * _3010)));
            }
        }
        break;
    } while(false);
}

