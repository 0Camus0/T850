#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=7298b1340d0ad2594b6bb3e5071366497dbc3d0e971d8564d570a004a0418750; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        vec4 _2027 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_2027.z <= 0.0)
        {
            break;
        }
        uvec2 _2034 = uvec2(textureSize(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, 0));
        uint _2036 = _2034.x;
        uint _2039 = _2034.y;
        vec2 _2053 = _2027.xy * vec2(float(_128.OutputWidth), float(_128.OutputHeight));
        float _2065 = min((_2027.z * float(_128.OutputHeight)) / float(max(_2039, 1u)), 64.0);
        float _2068 = max(_128.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _2077 = ivec2(int(_128.OutputWidth) - 1, int(_128.OutputHeight) - 1);
        uint _2084 = (_128.ScreenSpace > 0.5) ? min(_128.ThicknessShift, 1u) : 0u;
        float _2088 = 1.0 / float(1u << _2084);
        if (!(_128.ScreenSpace > 0.5))
        {
            if (_2065 < 0.5)
            {
                break;
            }
            ivec2 _2102 = max(ivec2(floor(_2053 - vec2(_2065))), ivec2(0));
            ivec2 _2110 = min(ivec2(ceil(_2053 + vec2(_2065))), _2077);
            if ((_2102.x > _2110.x) || (_2102.y > _2110.y))
            {
                break;
            }
            float _2127 = max(_2065 * _2065, 9.9999999747524270787835121154785e-07);
            int _2129 = _2102.y;
            for (int _3878 = _2129; _3878 <= _2110.y; _3878++)
            {
                int _2138 = _2102.x;
                for (int _3879 = _2138; _3879 <= _2110.x; _3879++)
                {
                    vec2 _2154 = vec2(float(_3879) + 0.5, float(_3878) + 0.5) - _2053;
                    float _2157 = dot(_2154, _2154);
                    if (_2157 > _2127)
                    {
                        continue;
                    }
                    float _2168 = sqrt(max(1.0 - (_2157 / _2127), 0.0));
                    float _2174 = _2027.w - (_2168 * _2068);
                    float _3886;
                    do
                    {
                        if ((_2036 == _128.OutputWidth) && (_2039 == _128.OutputHeight))
                        {
                            vec4 _3272 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3879, int((_2039 - 1u) - uint(_3878)), 0).xy, 0);
                            float _3273 = _3272.x;
                            _3886 = ((_3273 > 0.0) && (abs(_3273 - _128.ProjA) > 9.999999717180685365747194737196e-10)) ? (_128.ProjB / (_3273 - _128.ProjA)) : 1000000015047466219876688855040.0;
                            break;
                        }
                        vec2 _3178 = vec2(_2034) / vec2(float(_128.OutputWidth), float(_128.OutputHeight));
                        ivec2 _3187 = ivec2(floor(vec2(float(_3879), float(_3878)) * _3178));
                        ivec2 _3205 = min((ivec2(ceil(vec2(float(_3879 + 1), float(_3878 + 1)) * _3178)) - ivec2(1)), (ivec2(_2034) - ivec2(1)));
                        int _3881;
                        float _3882;
                        _3882 = 0.0;
                        _3881 = _3187.y;
                        float _3885;
                        SPIRV_CROSS_LOOP
                        for (; _3881 <= min(_3205.y, (_3187.y + 2)); _3882 = _3885, _3881++)
                        {
                            int _3220 = _3187.x;
                            _3885 = _3882;
                            SPIRV_CROSS_LOOP
                            for (int _3883 = _3220; _3883 <= min(_3205.x, (_3187.x + 2)); )
                            {
                                vec4 _3313 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3883, int((_2039 - 1u) - uint(_3881)), 0).xy, 0);
                                float _3314 = _3313.x;
                                _3885 = max(_3885, ((_3314 > 0.0) && (abs(_3314 - _128.ProjA) > 9.999999717180685365747194737196e-10)) ? (_128.ProjB / (_3314 - _128.ProjA)) : 1000000015047466219876688855040.0);
                                _3883++;
                                continue;
                            }
                        }
                        _3886 = _3882;
                        break;
                    } while(false);
                    if (_2174 >= _3886)
                    {
                        continue;
                    }
                    uint _2190 = uint(_3879) + (uint(_3878) * _128.OutputWidth);
                    uint _2196 = atomicMin(DepthBits_1._data[_2190], (uint(clamp((log2(max(_2174, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _2208 = atomicAdd(ThicknessBits_1._data[_2190], uint((((2.0 * _2168) * _2068) * _128.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _2222 = gl_GlobalInvocationID.x * 3u;
        vec4 _2225 = Anisotropy_1._data[_2222];
        bvec4 _2226 = bvec4(_128.Anisotropic > 0.5);
        vec4 _2227 = vec4(_2226.x ? _2225.x : vec4(0.0).x, _2226.y ? _2225.y : vec4(0.0).y, _2226.z ? _2225.z : vec4(0.0).z, _2226.w ? _2225.w : vec4(0.0).w);
        if (_2227.w < 0.0)
        {
            break;
        }
        float _2240 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].z;
        float _2241 = (_128.Anisotropic > 0.5) ? _2240 : 0.0;
        bool _2244 = _128.Iteration == 2u;
        if (_2244 && (_128.SurfelBand <= 0.0))
        {
            break;
        }
        vec4 _2257 = Whitewater_1._data[gl_GlobalInvocationID.x];
        bvec4 _2258 = bvec4(_128.AuxEnabled > 0.5);
        vec4 _2259 = vec4(_2258.x ? _2257.x : vec4(0.0).x, _2258.y ? _2257.y : vec4(0.0).y, _2258.z ? _2257.z : vec4(0.0).z, _2258.w ? _2257.w : vec4(0.0).w);
        float _2271 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].w;
        float _2272 = ((_128.AuxEnabled > 0.5) && (_128.Anisotropic > 0.5)) ? _2271 : 0.0;
        bool _2275 = _2259.x > 0.0199999995529651641845703125;
        bool _2278 = _2259.y > 0.0199999995529651641845703125;
        bool _2281 = _2259.z > 0.0199999995529651641845703125;
        bool _2283 = _2272 > 0.0199999995529651641845703125;
        bool _2290 = ((_2275 || _2278) || _2281) || _2283;
        if ((!_2244) && (_2065 >= 0.5))
        {
            vec2 _2299 = _2053 * _2088;
            float _2307 = (_2084 > 0u) ? max(_2065 * _2088, 0.75) : _2065;
            uint _2315 = ((_128.OutputWidth + (1u << _2084)) - 1u) >> _2084;
            ivec2 _2337 = max(ivec2(floor(_2299 - vec2(_2307))), ivec2(0));
            ivec2 _2345 = min(ivec2(ceil(_2299 + vec2(_2307))), ivec2(int(_2315) - 1, int(((_128.OutputHeight + (1u << _2084)) - 1u) >> _2084) - 1));
            float _2349 = max(_2307 * _2307, 9.9999999747524270787835121154785e-07);
            int _2351 = _2337.y;
            for (int _3842 = _2351; _3842 <= _2345.y; _3842++)
            {
                int _2360 = _2337.x;
                for (int _3870 = _2360; _3870 <= _2345.x; _3870++)
                {
                    vec2 _2376 = vec2(float(_3870) + 0.5, float(_3842) + 0.5) - _2299;
                    float _2379 = dot(_2376, _2376);
                    if (_2379 > _2349)
                    {
                        continue;
                    }
                    float _2390 = sqrt(max(1.0 - (_2379 / _2349), 0.0));
                    uint _2397 = uint(_3870) + (uint(_3842) * _2315);
                    float _2404 = ((2.0 * _2390) * _2068) * _128.InvPackingFactor;
                    uint _2410 = atomicAdd(ThicknessBits_1._data[_2397], uint(_2404 * 1024.0));
                    if (_2290)
                    {
                        if (_2275)
                        {
                            uint _2426 = atomicMax(AuxBits_1._data[_2397 * 4u], uint(clamp(_2259.x * _2390, 0.0, 1.0) * 65535.0));
                        }
                        if (_2278)
                        {
                            uint _2441 = atomicAdd(AuxBits_1._data[(_2397 * 4u) + 1u], uint((min(_2259.y, 2.0) * _2404) * 1024.0));
                        }
                        if (_2281)
                        {
                            uint _2455 = atomicAdd(AuxBits_1._data[(_2397 * 4u) + 2u], uint((_2259.z * _2404) * 1024.0));
                        }
                        if (_2283)
                        {
                            uint _2468 = atomicAdd(AuxBits_1._data[(_2397 * 4u) + 3u], uint((_2272 * _2404) * 1024.0));
                        }
                    }
                }
            }
        }
        float _2487 = (0.5 * float(_128.OutputHeight)) * max(_128.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _2495 = (0.5 * float(_128.OutputWidth)) * max(_128.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _2503 = vec2(float(_128.OutputWidth), float(_128.OutputHeight)) * 0.5;
        if (_2244 && (_2227.w <= 0.0))
        {
            break;
        }
        if (_2227.w > 0.0)
        {
            uint _2517 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _2522 = (gl_GlobalInvocationID.x * 3u) + 2u;
            if (_2241 < 0.0)
            {
                break;
            }
            mat3 _2581 = mat3(vec3(_128.ViewProjection[0].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[2].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_128.ViewProjection[0].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[2].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_128.ViewProjection[0].w, _128.ViewProjection[1].w, _128.ViewProjection[2].w));
            mat3 _2587 = transpose(_2581) * (mat3(vec3(Anisotropy_1._data[_2517].xyz), vec3(Anisotropy_1._data[_2517].yw, Anisotropy_1._data[_2522].x), vec3(Anisotropy_1._data[_2517].z, Anisotropy_1._data[_2522].xy)) * _2581);
            vec4 _3421 = (((_128.ViewProjection[0] * _2227.x) + (_128.ViewProjection[1] * _2227.y)) + (_128.ViewProjection[2] * _2227.z)) + _128.ViewProjection[3];
            float _3430 = _3421.x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _3436 = _3421.y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _3438 = _3421.w;
            vec3 _3439 = vec3(_3430, _3436, _3438);
            float _2592 = _2227.w;
            if ((_3438 - _2592) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _2617 = vec2((_3430 * _2495) / _3438, ((-_3436) * _2487) / _3438) + _2503;
            float _2626 = min((_2592 * _2487) / (_3438 - _2592), 64.0);
            if (_2626 < 0.5)
            {
                break;
            }
            float _3456 = _2587[0].x;
            float _3458 = _2587[0].y;
            float _3460 = _2587[0].z;
            float _3462 = _2587[1].y;
            float _3464 = _2587[1].z;
            float _3466 = _2587[2].z;
            float _3473 = (_3462 * _3466) - (_3464 * _3464);
            float _3480 = (_3460 * _3464) - (_3458 * _3466);
            float _3487 = (_3458 * _3464) - (_3460 * _3462);
            float _3501 = (_3458 * _3460) - (_3456 * _3464);
            mat3 _3535 = mat3(vec3(_3473, _3480, _3487), vec3(_3480, (_3456 * _3466) - (_3460 * _3460), _3501), vec3(_3487, _3501, (_3456 * _3462) - (_3458 * _3458))) * (1.0 / (((_3456 * _3473) + (_3458 * _3480)) + (_3460 * _3487)));
            float _2656 = _3535[2].z - (_3438 * _3438);
            float _2664 = _3535[0].z - (_3430 * _3438);
            float _2672 = _3535[1].z - (_3436 * _3438);
            float _2681 = sqrt(max((_2664 * _2664) - ((_3535[0].x - (_3430 * _3430)) * _2656), 0.0));
            float _2690 = sqrt(max((_2672 * _2672) - ((_3535[1].y - (_3436 * _3436)) * _2656), 0.0));
            vec2 _2700 = vec2(_2664 + _2681, _2664 - _2681) / vec2(_2656);
            vec2 _2710 = vec2(_2672 + _2690, _2672 - _2690) / vec2(_2656);
            ivec2 _2757 = max(max(ivec2(floor(_2617 - vec2(_2626))), ivec2(floor(vec2(min(_2700.x, _2700.y) * _2495, (-max(_2710.x, _2710.y)) * _2487) + _2503))), ivec2(0));
            ivec2 _2769 = min(min(ivec2(ceil(_2617 + vec2(_2626))), ivec2(ceil(vec2(max(_2700.x, _2700.y) * _2495, (-min(_2710.x, _2710.y)) * _2487) + _2503))), _2077);
            if ((_2757.x > _2769.x) || (_2757.y > _2769.y))
            {
                break;
            }
            if (_2244)
            {
                uvec2 _1975[5] = uvec2[](uvec2(max(min(ivec2(floor(_2617)), _2077), ivec2(0))), uvec2(uint(_2757.x), uint(_2757.y)), uvec2(uint(_2769.x), uint(_2757.y)), uvec2(uint(_2757.x), uint(_2769.y)), uvec2(uint(_2769.x), uint(_2769.y)));
                float _3848;
                _3848 = 0.0;
                float _2846;
                bool _3849;
                int _3847 = 0;
                SPIRV_CROSS_UNROLL
                for (;;)
                {
                    if (_3847 < 5)
                    {
                        uint _2836 = _1975[_3847].x + (_1975[_3847].y * _128.OutputWidth);
                        if (DepthBits_1._data[_2836] >= 2139095040u)
                        {
                            _3849 = false;
                            break;
                        }
                        _2846 = max(_3848, uintBitsToFloat(DepthBits_1._data[_2836]));
                        _3848 = _2846;
                        _3847++;
                        continue;
                    }
                    else
                    {
                        _3849 = true;
                        break;
                    }
                }
                if (_3849 && ((_3438 - _2592) > (_3848 + _128.SurfelBand)))
                {
                    break;
                }
            }
            vec3 _3861;
            float _3865;
            if (_2244)
            {
                vec3 _3853;
                _3853 = vec3(_128.ViewProjection[1].x / max(_128.ProjScaleX, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].y / max(_128.ProjScaleY, 9.9999999747524270787835121154785e-07), _128.ViewProjection[1].w);
                SPIRV_CROSS_UNROLL
                for (int _3852 = 0; _3852 < 6; )
                {
                    _3853 = (_3853 / vec3(max(length(_3853), 9.9999996826552253889678874634872e-21))) * _2587;
                    _3852++;
                    continue;
                }
                float _2888 = length(_3853);
                vec3 _3854;
                if (_2888 > 9.9999996826552253889678874634872e-21)
                {
                    _3854 = _3853 / vec3(_2888);
                }
                else
                {
                    _3854 = vec3(0.0, 0.0, -1.0);
                }
                vec3 _3862;
                if (dot(_3854, _3439) > 0.0)
                {
                    _3862 = -_3854;
                }
                else
                {
                    _3862 = _3854;
                }
                _3865 = 1.0 - clamp(Anisotropy_1._data[_2522].w, 0.0, 1.0);
                _3861 = _3862;
            }
            else
            {
                _3865 = 0.0;
                _3861 = vec3(0.0, 0.0, -1.0);
            }
            int _2911 = _2757.y;
            for (int _3856 = _2911; _3856 <= _2769.y; _3856++)
            {
                int _2920 = _2757.x;
                for (int _3857 = _2920; _3857 <= _2769.x; _3857++)
                {
                    vec3 _2945 = vec3(((float(_3857) + 0.5) - _2503.x) / _2495, (-((float(_3856) + 0.5) - _2503.y)) / _2487, 1.0);
                    vec3 _2951 = (_2945 * _3438) - _3439;
                    vec3 _2954 = _2945 * _2587;
                    float _2957 = dot(_2945, _2954);
                    float _2960 = dot(_2951, _2954);
                    float _2973 = (_2960 * _2960) - (_2957 * (dot(_2951, _2951 * _2587) - 1.0));
                    if ((_2973 < 0.0) || (_2957 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _2990 = _3438 + (((-_2960) - sqrt(_2973)) / _2957);
                    if (_2990 <= 0.0500000007450580596923828125)
                    {
                        continue;
                    }
                    uint _3002 = uint(_3857) + (uint(_3856) * _128.OutputWidth);
                    if (_2244)
                    {
                        do
                        {
                            if (DepthBits_1._data[_3002] >= 2139095040u)
                            {
                                break;
                            }
                            float _3576 = max(_2990 - uintBitsToFloat(DepthBits_1._data[_3002]), 0.0);
                            if (_3576 > _128.SurfelBand)
                            {
                                break;
                            }
                            vec3 _3585 = _2945 * _2990;
                            vec3 _3590 = (_3585 - _3439) * _2587;
                            float _3592 = length(_3590);
                            vec3 _3598 = _3590 / vec3(_3592);
                            bvec3 _3600 = bvec3(_3592 > 9.9999999747524270787835121154785e-07);
                            vec3 _3601 = vec3(_3600.x ? _3598.x : _3861.x, _3600.y ? _3598.y : _3861.y, _3600.z ? _3598.z : _3861.z);
                            vec3 _3866;
                            if (dot(_3601, _3585) > 0.0)
                            {
                                _3866 = -_3601;
                            }
                            else
                            {
                                _3866 = _3601;
                            }
                            vec3 _3615 = normalize(mix(_3866, _3861, vec3(_3865)));
                            float _3620 = 1.0 - (_3576 / _128.SurfelBand);
                            float _3630 = (pow(clamp(_2973 / _2957, 0.0, 1.0), max(_128.SurfelSharpness, 0.25)) * _3620) * _3620;
                            if (_3630 < 0.001000000047497451305389404296875)
                            {
                                break;
                            }
                            uint _3636 = _3002 * 5u;
                            uint _3644 = atomicAdd(SurfelBits_1._data[_3636], uint((_3630 * 4096.0) + 0.5));
                            uint _3657 = atomicAdd(SurfelBits_1._data[_3636 + 1u], uint(((_3630 * (_3576 / _128.SurfelBand)) * 4096.0) + 0.5));
                            uint _3669 = atomicAdd(SurfelBits_1._data[_3636 + 2u], uint(int(roundEven((_3630 * _3615.x) * 4096.0))));
                            uint _3681 = atomicAdd(SurfelBits_1._data[_3636 + 3u], uint(int(roundEven((_3630 * _3615.y) * 4096.0))));
                            uint _3693 = atomicAdd(SurfelBits_1._data[_3636 + 4u], uint(int(roundEven((_3630 * _3615.z) * 4096.0))));
                            break;
                        } while(false);
                        continue;
                    }
                    uint _3021 = atomicMin(DepthBits_1._data[_3002], floatBitsToUint(_2990));
                }
            }
            break;
        }
        if (_2241 < 0.0)
        {
            break;
        }
        float _3038 = _2065 * _128.DepthRadiusScale;
        if (_3038 < 0.5)
        {
            break;
        }
        float _3048 = max(_128.Radius * _128.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _3055 = max(ivec2(floor(_2053 - vec2(_3038))), ivec2(0));
        ivec2 _3063 = min(ivec2(ceil(_2053 + vec2(_3038))), _2077);
        float _3066 = _3038 * _3038;
        int _3068 = _3055.y;
        for (int _3843 = _3068; _3843 <= _3063.y; _3843++)
        {
            int _3077 = _3055.x;
            for (int _3844 = _3077; _3844 <= _3063.x; _3844++)
            {
                vec2 _3093 = vec2(float(_3844) + 0.5, float(_3843) + 0.5) - _2053;
                float _3096 = dot(_3093, _3093);
                if (_3096 > _3066)
                {
                    continue;
                }
                uint _3124 = atomicMin(DepthBits_1._data[uint(_3844) + (uint(_3843) * _128.OutputWidth)], floatBitsToUint(_2027.w - (sqrt(max(1.0 - (_3096 / _3066), 0.0)) * _3048)));
            }
        }
        break;
    } while(false);
}

