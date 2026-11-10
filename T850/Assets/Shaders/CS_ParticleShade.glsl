#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=50692541a4e0d153b2f455dca2487678a135a37f121cc068c5486e8a444ab451; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

float _3374;

layout(binding = 0, std140) uniform ParticleShadeConstants
{
    uint OutputWidth;
    uint OutputHeight;
    uint Mode;
    uint DebugView;
    float ProjScaleY;
    float ProjA;
    float ProjB;
    float MaxTravel;
    float ThicknessScale;
    float Refraction;
    float Specular;
    float FresnelPower;
    float NormalScale;
    float Foam;
    float ReflectionDistance;
    float NormalStep;
    vec4 FluidTint;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    vec4 ReflectionColor;
    vec4 ShadeParams;
    vec4 FluidColor;
} _78;

layout(binding = 5, rgba16f) uniform writeonly image2D OutputTexture;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler;
layout(binding = 6) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;
layout(binding = 3) uniform sampler2D SPIRV_Cross_CombinedSceneColorLinearSampler;

void main()
{
    do
    {
        if (_78.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _78.OutputWidth) || (gl_GlobalInvocationID.y >= _78.OutputHeight))
        {
            break;
        }
        uvec2 _1405 = uvec2(gl_GlobalInvocationID.x, (_78.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _1848 = clamp(ivec2(gl_GlobalInvocationID.xy), ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
        vec4 _1874 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_1848.x, int((_78.OutputHeight - 1u) - uint(_1848.y)), 0).xy, 0);
        float _1875 = _1874.x;
        ivec2 _1889 = clamp(ivec2(gl_GlobalInvocationID.xy), ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
        vec4 _1915 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_1889.x, int((_78.OutputHeight - 1u) - uint(_1889.y)), 0).xy, 0);
        if ((_1875 <= 0.0) || (_1915.w <= 0.001000000047497451305389404296875))
        {
            if (_78.ShadeParams.x > 0.5)
            {
                imageStore(OutputTexture, ivec2(_1405), vec4(0.0));
            }
            break;
        }
        float _1436 = max(_1915.x, 0.0) * max(_78.ThicknessScale, 0.0);
        vec2 _1443 = vec2(float(_78.OutputWidth), float(_78.OutputHeight));
        vec2 _1448 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        ivec2 _1451 = ivec2(gl_GlobalInvocationID.xy);
        int _1456 = clamp(int(_78.NormalStep + 0.5), 1, 8);
        float _1958 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec3 _1948 = vec3((((float(_1451.x) + 0.5) - (0.5 * float(_78.OutputWidth))) * _1875) / _1958, ((-((float(_1451.y) + 0.5) - (0.5 * float(_78.OutputHeight)))) * _1875) / _1958, _1875);
        float _1466 = max(2.0 * _78.ShadeParams.y, 0.001000000047497451305389404296875) * float(_1456);
        ivec2 _1468 = ivec2(_1456, 0);
        vec3 _3335;
        do
        {
            ivec2 _2051 = clamp(_1451 + _1468, ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
            vec4 _2077 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_2051.x, int((_78.OutputHeight - 1u) - uint(_2051.y)), 0).xy, 0);
            float _2078 = _2077.x;
            ivec2 _2092 = clamp(_1451 - _1468, ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
            vec4 _2118 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_2092.x, int((_78.OutputHeight - 1u) - uint(_2092.y)), 0).xy, 0);
            float _2119 = _2118.x;
            bool _1992 = (_2078 > 0.0) && (abs(_2078 - _1875) < _1466);
            bool _2001 = (_2119 > 0.0) && (abs(_1875 - _2119) < _1466);
            ivec2 _2005 = _1451 + _1468;
            float _2162 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2009 = vec3((((float(_2005.x) + 0.5) - (0.5 * float(_78.OutputWidth))) * _2078) / _2162, ((-((float(_2005.y) + 0.5) - (0.5 * float(_78.OutputHeight)))) * _2078) / _2162, _2078) - _1948;
            bvec3 _2010 = bvec3(_1992);
            vec3 _2011 = vec3(_2010.x ? _2009.x : vec3(0.0).x, _2010.y ? _2009.y : vec3(0.0).y, _2010.z ? _2009.z : vec3(0.0).z);
            ivec2 _2016 = _1451 - _1468;
            float _2205 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2019 = _1948 - vec3((((float(_2016.x) + 0.5) - (0.5 * float(_78.OutputWidth))) * _2119) / _2205, ((-((float(_2016.y) + 0.5) - (0.5 * float(_78.OutputHeight)))) * _2119) / _2205, _2119);
            bvec3 _2020 = bvec3(_2001);
            vec3 _2021 = vec3(_2020.x ? _2019.x : vec3(0.0).x, _2020.y ? _2019.y : vec3(0.0).y, _2020.z ? _2019.z : vec3(0.0).z);
            if (_1992 && _2001)
            {
                _3335 = (_2011 + _2021) * 0.5;
                break;
            }
            bvec3 _2034 = bvec3(_1992);
            _3335 = vec3(_2034.x ? _2011.x : _2021.x, _2034.y ? _2011.y : _2021.y, _2034.z ? _2011.z : _2021.z);
            break;
        } while(false);
        ivec2 _1475 = ivec2(0, _1456);
        vec3 _3336;
        do
        {
            ivec2 _2298 = clamp(_1451 + _1475, ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
            vec4 _2324 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_2298.x, int((_78.OutputHeight - 1u) - uint(_2298.y)), 0).xy, 0);
            float _2325 = _2324.x;
            ivec2 _2339 = clamp(_1451 - _1475, ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
            vec4 _2365 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_2339.x, int((_78.OutputHeight - 1u) - uint(_2339.y)), 0).xy, 0);
            float _2366 = _2365.x;
            bool _2239 = (_2325 > 0.0) && (abs(_2325 - _1875) < _1466);
            bool _2248 = (_2366 > 0.0) && (abs(_1875 - _2366) < _1466);
            ivec2 _2252 = _1451 + _1475;
            float _2409 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2256 = vec3((((float(_2252.x) + 0.5) - (0.5 * float(_78.OutputWidth))) * _2325) / _2409, ((-((float(_2252.y) + 0.5) - (0.5 * float(_78.OutputHeight)))) * _2325) / _2409, _2325) - _1948;
            bvec3 _2257 = bvec3(_2239);
            vec3 _2258 = vec3(_2257.x ? _2256.x : vec3(0.0).x, _2257.y ? _2256.y : vec3(0.0).y, _2257.z ? _2256.z : vec3(0.0).z);
            ivec2 _2263 = _1451 - _1475;
            float _2452 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2266 = _1948 - vec3((((float(_2263.x) + 0.5) - (0.5 * float(_78.OutputWidth))) * _2366) / _2452, ((-((float(_2263.y) + 0.5) - (0.5 * float(_78.OutputHeight)))) * _2366) / _2452, _2366);
            bvec3 _2267 = bvec3(_2248);
            vec3 _2268 = vec3(_2267.x ? _2266.x : vec3(0.0).x, _2267.y ? _2266.y : vec3(0.0).y, _2267.z ? _2266.z : vec3(0.0).z);
            if (_2239 && _2248)
            {
                _3336 = (_2258 + _2268) * 0.5;
                break;
            }
            bvec3 _2281 = bvec3(_2239);
            _3336 = vec3(_2281.x ? _2258.x : _2268.x, _2281.y ? _2258.y : _2268.y, _2281.z ? _2258.z : _2268.z);
            break;
        } while(false);
        vec2 _1486 = _3335.xy * max(_78.NormalScale, 0.0);
        vec3 _3266 = _3335;
        _3266.x = _1486.x;
        _3266.y = _1486.y;
        vec2 _1496 = _3336.xy * max(_78.NormalScale, 0.0);
        vec3 _3270 = _3336;
        _3270.x = _1496.x;
        _3270.y = _1496.y;
        bvec3 _3416 = bvec3(length(_3266) < 9.9999999747524270787835121154785e-07);
        bvec3 _3418 = bvec3(length(_3270) < 9.9999999747524270787835121154785e-07);
        vec3 _1514 = normalize(cross(vec3(_3416.x ? vec3(1.0, 0.0, 0.0).x : _3266.x, _3416.y ? vec3(1.0, 0.0, 0.0).y : _3266.y, _3416.z ? vec3(1.0, 0.0, 0.0).z : _3266.z), vec3(_3418.x ? vec3(0.0, -1.0, 0.0).x : _3270.x, _3418.y ? vec3(0.0, -1.0, 0.0).y : _3270.y, _3418.z ? vec3(0.0, -1.0, 0.0).z : _3270.z)));
        vec3 _1516 = normalize(_1948);
        vec3 _1518 = -_1516;
        vec3 _3342;
        if (dot(_1514, _1518) < 0.0)
        {
            _3342 = -_1514;
        }
        else
        {
            _3342 = _1514;
        }
        if (_78.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_1405), vec4((_3342 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_78.DebugView == 2u)
        {
            float _1549 = clamp(_1875 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_1405), vec4(_1549, _1549, _1549, 1.0));
            break;
        }
        if (_78.DebugView == 3u)
        {
            float _1564 = clamp(_1436, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_1405), vec4(_1564, _1564, _1564, 1.0));
            break;
        }
        if (_78.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_1405), vec4(_1915.www, 1.0));
            break;
        }
        vec3 _1591 = refract(_1516, _3342, 0.75187969207763671875);
        bvec3 _3420 = bvec3(dot(_1591, _1591) < 9.9999999747524270787835121154785e-07);
        vec3 _1612 = _1948 + (vec3(_3420.x ? _1516.x : _1591.x, _3420.y ? _1516.y : _1591.y, _3420.z ? _1516.z : _1591.z) * min(_1436 * max(_78.Refraction, 0.0), max(_78.MaxTravel, 0.0)));
        float _2492 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec2 _2482 = vec2(((_1612.x * _2492) / _1612.z) + (0.5 * float(_78.OutputWidth)), (((-_1612.y) * _2492) / _1612.z) + (0.5 * float(_78.OutputHeight)));
        bvec2 _1619 = bvec2(_1612.z > 0.0500000007450580596923828125);
        vec2 _1625 = clamp(vec2(_1619.x ? _2482.x : _1448.x, _1619.y ? _2482.y : _1448.y), vec2(0.5), _1443 - vec2(0.5));
        ivec2 _2507 = clamp(ivec2(_1625), ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
        vec4 _2533 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2507.x, int((_78.OutputHeight - 1u) - uint(_2507.y)), 0).xy, 0);
        float _2534 = _2533.x;
        bvec2 _3422 = bvec2((((_2534 > 0.0) && (abs(_2534 - _78.ProjA) > 9.999999717180685365747194737196e-10)) ? (_78.ProjB / (_2534 - _78.ProjA)) : 1000000000.0) < _1875);
        vec2 _3423 = vec2(_3422.x ? _1448.x : _1625.x, _3422.y ? _1448.y : _1625.y);
        vec3 _1656 = exp(max(_78.FluidTint.xyz, vec3(0.0)) * (-_1436));
        vec3 _1670 = reflect(_1516, _3342);
        float _3364;
        vec3 _3376;
        vec3 _3381;
        float _3388;
        do
        {
            if (_78.ReflectionDistance <= 0.0)
            {
                _3388 = 0.0;
                _3381 = vec3(0.0);
                _3376 = vec3(0.0);
                _3364 = 0.0;
                break;
            }
            float _2588 = max(0.004999999888241291046142578125 * _1875, 0.00999999977648258209228515625);
            float _2591 = _2588 * fract(52.98291778564453125 * fract(dot(_1448, vec2(0.067110560834407806396484375, 0.005837149918079376220703125))));
            vec2 _3360;
            _3360 = vec2(-1.0);
            float _2600;
            float _2711;
            vec2 _3427;
            bool _3356;
            float _3365;
            vec3 _3377;
            int _3347 = 0;
            float _3348 = _2591;
            float _3349 = _2588;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_3347 < 64)
                {
                    _2600 = _3348 + _3349;
                    if (_2600 > _78.ReflectionDistance)
                    {
                        _3377 = vec3(0.0);
                        _3365 = _3374;
                        _3356 = false;
                        break;
                    }
                    vec3 _2611 = _1948 + (_1670 * _2600);
                    if (_2611.z <= 0.0500000007450580596923828125)
                    {
                        _3377 = vec3(0.0);
                        _3365 = _3374;
                        _3356 = false;
                        break;
                    }
                    float _2766 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _2742 = ((_2611.x * _2766) / _2611.z) + (0.5 * float(_78.OutputWidth));
                    float _2755 = (((-_2611.y) * _2766) / _2611.z) + (0.5 * float(_78.OutputHeight));
                    vec2 _2756 = vec2(_2742, _2755);
                    if (!((((_2742 >= 0.0) && (_2755 >= 0.0)) && (_2742 < float(_78.OutputWidth))) && (_2755 < float(_78.OutputHeight))))
                    {
                        _3377 = vec3(0.0);
                        _3365 = _3374;
                        _3356 = false;
                        break;
                    }
                    ivec2 _2804 = clamp(ivec2(_2756), ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
                    vec4 _2830 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2804.x, int((_78.OutputHeight - 1u) - uint(_2804.y)), 0).xy, 0);
                    float _2831 = _2830.x;
                    float _2848 = ((_2831 > 0.0) && (abs(_2831 - _78.ProjA) > 9.999999717180685365747194737196e-10)) ? (_78.ProjB / (_2831 - _78.ProjA)) : 1000000000.0;
                    bool _2628 = _2848 > 100000000.0;
                    if (_2628)
                    {
                    }
                    else
                    {
                        if ((_2611.z > _2848) && ((_2611.z - _2848) < max(2.0 * _3349, 0.25)))
                        {
                            float _3351;
                            _3351 = _2600;
                            float _3424;
                            float _3425;
                            int _3350 = 0;
                            float _3352 = _3348;
                            SPIRV_CROSS_UNROLL
                            for (; _3350 < 5; _3352 = _3424, _3351 = _3425, _3350++)
                            {
                                float _2656 = 0.5 * (_3352 + _3351);
                                vec3 _2661 = _1948 + (_1670 * _2656);
                                float _2888 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                ivec2 _2903 = clamp(ivec2(vec2(((_2661.x * _2888) / _2661.z) + (0.5 * float(_78.OutputWidth)), (((-_2661.y) * _2888) / _2661.z) + (0.5 * float(_78.OutputHeight)))), ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
                                vec4 _2929 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2903.x, int((_78.OutputHeight - 1u) - uint(_2903.y)), 0).xy, 0);
                                float _2930 = _2929.x;
                                bool _2668 = _2661.z > (((_2930 > 0.0) && (abs(_2930 - _78.ProjA) > 9.999999717180685365747194737196e-10)) ? (_78.ProjB / (_2930 - _78.ProjA)) : 1000000000.0);
                                _3424 = _2668 ? _3352 : _2656;
                                _3425 = _2668 ? _2656 : _3351;
                            }
                            vec3 _2682 = _1948 + (_1670 * _3351);
                            float _2987 = (0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec2 _2977 = vec2(((_2682.x * _2987) / _2682.z) + (0.5 * float(_78.OutputWidth)), (((-_2682.y) * _2987) / _2682.z) + (0.5 * float(_78.OutputHeight)));
                            vec2 _2694 = min(_2977, vec2(float(_78.OutputWidth), float(_78.OutputHeight)) - _2977);
                            vec2 _3002 = clamp(_2977 / vec2(float(_78.OutputWidth), float(_78.OutputHeight)), vec2(0.0), vec2(1.0));
                            _3377 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_3002.x, 1.0 - _3002.y), 0.0).xyz;
                            _3365 = clamp(min(_2694.x, _2694.y) / (0.0599999986588954925537109375 * float(_78.OutputHeight)), 0.0, 1.0);
                            _3356 = true;
                            break;
                        }
                    }
                    bvec2 _3426 = bvec2(_2628);
                    _3427 = vec2(_3426.x ? _2756.x : _3360.x, _3426.y ? _2756.y : _3360.y);
                    _2711 = _3349 * 1.08000004291534423828125;
                    _3349 = _2711;
                    _3348 = _2600;
                    _3347++;
                    _3360 = _3427;
                    continue;
                }
                else
                {
                    _3377 = vec3(0.0);
                    _3365 = _3374;
                    _3356 = false;
                    break;
                }
            }
            if (_3356)
            {
                _3388 = 0.0;
                _3381 = vec3(0.0);
                _3376 = _3377;
                _3364 = _3365;
                break;
            }
            bool _2720 = _3360.x >= 0.0;
            vec3 _3387;
            if (_2720)
            {
                vec2 _3028 = clamp(_3360 / vec2(float(_78.OutputWidth), float(_78.OutputHeight)), vec2(0.0), vec2(1.0));
                _3387 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_3028.x, 1.0 - _3028.y), 0.0).xyz;
            }
            else
            {
                _3387 = vec3(0.0);
            }
            _3388 = float(_2720);
            _3381 = _3387;
            _3376 = _3377;
            _3364 = 0.0;
            break;
        } while(false);
        float _3396;
        vec3 _3397;
        _3397 = vec3(0.0);
        _3396 = 0.0;
        float _3413;
        vec3 _3414;
        SPIRV_CROSS_UNROLL
        for (int _3395 = 1; _3395 <= 3; _3397 = _3414, _3396 = _3413, _3395++)
        {
            ivec2 _3059 = ivec2((int(_78.OutputWidth) * _3395) / 4, 1);
            ivec2 _3103 = clamp(_3059, ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
            vec4 _3129 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3103.x, int((_78.OutputHeight - 1u) - uint(_3103.y)), 0).xy, 0);
            float _3130 = _3129.x;
            if ((((_3130 > 0.0) && (abs(_3130 - _78.ProjA) > 9.999999717180685365747194737196e-10)) ? (_78.ProjB / (_3130 - _78.ProjA)) : 1000000000.0) > 100000000.0)
            {
                vec2 _3162 = clamp((vec2(_3059) + vec2(0.5)) / vec2(float(_78.OutputWidth), float(_78.OutputHeight)), vec2(0.0), vec2(1.0));
                _3414 = _3397 + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_3162.x, 1.0 - _3162.y), 0.0).xyz;
                _3413 = _3396 + 1.0;
            }
            else
            {
                _3414 = _3397;
                _3413 = _3396;
            }
        }
        vec3 _3083 = _3397 / vec3(_3396);
        bvec3 _3087 = bvec3(_3396 > 0.0);
        vec3 _3088 = vec3(_3087.x ? _3083.x : _78.ReflectionColor.xyz.x, _3087.y ? _3083.y : _78.ReflectionColor.xyz.y, _3087.z ? _3083.z : _78.ReflectionColor.xyz.z);
        bvec3 _1697 = bvec3(_3388 > 0.5);
        vec3 _1698 = vec3(_1697.x ? _3381.x : _3088.x, _1697.y ? _3381.y : _3088.y, _1697.z ? _3381.z : _3088.z);
        float _1759 = clamp(2.5 * ((_78.ShadeParams.y * ((0.5 * float(_78.OutputHeight)) * max(_78.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _1875), 2.0, 48.0);
        float _1762 = 4.0 * _78.ShadeParams.y;
        float _3407;
        _3407 = 0.0;
        SPIRV_CROSS_UNROLL
        for (int _3406 = 0; _3406 < 8; )
        {
            float _1770 = float(_3406) * 0.785398185253143310546875;
            ivec2 _3197 = clamp(_1451 + ivec2(roundEven(vec2(cos(_1770), sin(_1770)) * _1759)), ivec2(0), ivec2(int(_78.OutputWidth) - 1, int(_78.OutputHeight) - 1));
            vec4 _3223 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_3197.x, int((_78.OutputHeight - 1u) - uint(_3197.y)), 0).xy, 0);
            float _3224 = _3223.x;
            _3407 += float((_3224 <= 0.0) || (_3224 > (_1875 + _1762)));
            _3406++;
            continue;
        }
        float _1806 = clamp((_3407 - 5.0) * 0.5, 0.0, 1.0) * clamp(_78.Foam, 0.0, 1.0);
        vec3 _1818 = mix(mix((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2((_3423 / _1443).x, 1.0 - (_3423 / _1443).y), 0.0).xyz * _1656) + ((vec3(1.0) - _1656) * _78.FluidColor.xyz), mix(mix(_1698 * 0.25, _1698, vec3(smoothstep(-0.100000001490116119384765625, 0.20000000298023223876953125, dot(_1670, vec3(_78.CameraRight.y, _78.CameraUp.y, _78.CameraForward.y))))), _3376, vec3(_3364)), vec3(clamp(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_3342, _1518), 0.0, 1.0), max(_78.FresnelPower, 1.0))), 0.0, 1.0))) + (_78.SunColor.xyz * (pow(clamp(dot(_3342, normalize(normalize(_78.SunDirection.xyz) - _1516)), 0.0, 1.0), 96.0) * _78.Specular)), (_1698 + (_78.SunColor.xyz * 0.1500000059604644775390625)) * 0.85000002384185791015625, vec3(_1806));
        imageStore(OutputTexture, ivec2(_1405), vec4(_1818, clamp(_1915.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _1806))));
        break;
    } while(false);
}

