#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=38e8473e3404f0d7ee84d9d7e1d680dc2f9e6c68915d1070dcc30c9c5d9da3e5; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

float _5842;

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
    vec4 SkyHorizon;
    vec4 RippleParams;
    vec4 CausticParams;
} _104;

layout(binding = 5, rgba16f) uniform writeonly image2D OutputTexture;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler;
layout(binding = 6) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;
layout(binding = 3) uniform sampler2D SPIRV_Cross_CombinedSceneColorLinearSampler;

void main()
{
    do
    {
        if (_104.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _104.OutputWidth) || (gl_GlobalInvocationID.y >= _104.OutputHeight))
        {
            break;
        }
        uvec2 _2127 = uvec2(gl_GlobalInvocationID.x, (_104.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _2788 = clamp(ivec2(gl_GlobalInvocationID.xy), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
        vec4 _2814 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_2788.x, int((_104.OutputHeight - 1u) - uint(_2788.y)), 0).xy, 0);
        float _2815 = _2814.x;
        ivec2 _2829 = clamp(ivec2(gl_GlobalInvocationID.xy), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
        vec4 _2855 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_2829.x, int((_104.OutputHeight - 1u) - uint(_2829.y)), 0).xy, 0);
        if ((_2815 <= 0.0) || (_2855.w <= 0.001000000047497451305389404296875))
        {
            if (_104.ShadeParams.x > 0.5)
            {
                imageStore(OutputTexture, ivec2(_2127), vec4(0.0));
            }
            break;
        }
        vec2 _2158 = vec2(float(_104.OutputWidth), float(_104.OutputHeight));
        vec2 _2163 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        ivec2 _2166 = ivec2(gl_GlobalInvocationID.xy);
        float _2169 = max(_104.ShadeParams.y, 0.001000000047497451305389404296875);
        int _2176 = clamp(int(_104.NormalStep + 0.5), 1, 8);
        float _2894 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _2870 = ((_2163.x - (0.5 * float(_104.OutputWidth))) * _2815) / _2894;
        float _2882 = ((-(_2163.y - (0.5 * float(_104.OutputHeight)))) * _2815) / _2894;
        vec3 _2884 = vec3(_2870, _2882, _2815);
        float _2184 = (2.0 * _2169) * float(_2176);
        ivec2 _2186 = ivec2(_2176, 0);
        vec3 _5785;
        do
        {
            ivec2 _2993 = clamp(_2166 + _2186, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
            vec4 _3019 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_2993.x, int((_104.OutputHeight - 1u) - uint(_2993.y)), 0).xy, 0);
            float _3020 = _3019.x;
            ivec2 _3034 = clamp(_2166 - _2186, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
            vec4 _3060 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_3034.x, int((_104.OutputHeight - 1u) - uint(_3034.y)), 0).xy, 0);
            float _3061 = _3060.x;
            bool _2928 = (_3020 > 0.0) && (abs(_3020 - _2815) < _2184);
            bool _2937 = (_3061 > 0.0) && (abs(_2815 - _3061) < _2184);
            vec2 _2944 = vec2(_2166 + _2186) + vec2(0.5);
            float _3100 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2948 = vec3(((_2944.x - (0.5 * float(_104.OutputWidth))) * _3020) / _3100, ((-(_2944.y - (0.5 * float(_104.OutputHeight)))) * _3020) / _3100, _3020) - _2884;
            bvec3 _2949 = bvec3(_2928);
            vec3 _2950 = vec3(_2949.x ? _2948.x : vec3(0.0).x, _2949.y ? _2948.y : vec3(0.0).y, _2949.z ? _2948.z : vec3(0.0).z);
            vec2 _2958 = vec2(_2166 - _2186) + vec2(0.5);
            float _3139 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2961 = _2884 - vec3(((_2958.x - (0.5 * float(_104.OutputWidth))) * _3061) / _3139, ((-(_2958.y - (0.5 * float(_104.OutputHeight)))) * _3061) / _3139, _3061);
            bvec3 _2962 = bvec3(_2937);
            vec3 _2963 = vec3(_2962.x ? _2961.x : vec3(0.0).x, _2962.y ? _2961.y : vec3(0.0).y, _2962.z ? _2961.z : vec3(0.0).z);
            if (_2928 && _2937)
            {
                _5785 = (_2950 + _2963) * 0.5;
                break;
            }
            bvec3 _2976 = bvec3(_2928);
            _5785 = vec3(_2976.x ? _2950.x : _2963.x, _2976.y ? _2950.y : _2963.y, _2976.z ? _2950.z : _2963.z);
            break;
        } while(false);
        ivec2 _2193 = ivec2(0, _2176);
        vec3 _5786;
        do
        {
            ivec2 _3238 = clamp(_2166 + _2193, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
            vec4 _3264 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_3238.x, int((_104.OutputHeight - 1u) - uint(_3238.y)), 0).xy, 0);
            float _3265 = _3264.x;
            ivec2 _3279 = clamp(_2166 - _2193, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
            vec4 _3305 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_3279.x, int((_104.OutputHeight - 1u) - uint(_3279.y)), 0).xy, 0);
            float _3306 = _3305.x;
            bool _3173 = (_3265 > 0.0) && (abs(_3265 - _2815) < _2184);
            bool _3182 = (_3306 > 0.0) && (abs(_2815 - _3306) < _2184);
            vec2 _3189 = vec2(_2166 + _2193) + vec2(0.5);
            float _3345 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _3193 = vec3(((_3189.x - (0.5 * float(_104.OutputWidth))) * _3265) / _3345, ((-(_3189.y - (0.5 * float(_104.OutputHeight)))) * _3265) / _3345, _3265) - _2884;
            bvec3 _3194 = bvec3(_3173);
            vec3 _3195 = vec3(_3194.x ? _3193.x : vec3(0.0).x, _3194.y ? _3193.y : vec3(0.0).y, _3194.z ? _3193.z : vec3(0.0).z);
            vec2 _3203 = vec2(_2166 - _2193) + vec2(0.5);
            float _3384 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _3206 = _2884 - vec3(((_3203.x - (0.5 * float(_104.OutputWidth))) * _3306) / _3384, ((-(_3203.y - (0.5 * float(_104.OutputHeight)))) * _3306) / _3384, _3306);
            bvec3 _3207 = bvec3(_3182);
            vec3 _3208 = vec3(_3207.x ? _3206.x : vec3(0.0).x, _3207.y ? _3206.y : vec3(0.0).y, _3207.z ? _3206.z : vec3(0.0).z);
            if (_3173 && _3182)
            {
                _5786 = (_3195 + _3208) * 0.5;
                break;
            }
            bvec3 _3221 = bvec3(_3173);
            _5786 = vec3(_3221.x ? _3195.x : _3208.x, _3221.y ? _3195.y : _3208.y, _3221.z ? _3195.z : _3208.z);
            break;
        } while(false);
        vec2 _2204 = _5785.xy * max(_104.NormalScale, 0.0);
        vec3 _5612 = _5785;
        _5612.x = _2204.x;
        _5612.y = _2204.y;
        vec2 _2214 = _5786.xy * max(_104.NormalScale, 0.0);
        vec3 _5616 = _5786;
        _5616.x = _2214.x;
        _5616.y = _2214.y;
        bvec3 _5871 = bvec3(length(_5612) < 9.9999999747524270787835121154785e-07);
        bvec3 _5873 = bvec3(length(_5616) < 9.9999999747524270787835121154785e-07);
        vec3 _2230 = normalize(_2884);
        vec3 _2232 = -_2230;
        vec3 _2236 = normalize(cross(vec3(_5871.x ? vec3(1.0, 0.0, 0.0).x : _5612.x, _5871.y ? vec3(1.0, 0.0, 0.0).y : _5612.y, _5871.z ? vec3(1.0, 0.0, 0.0).z : _5612.z), vec3(_5873.x ? vec3(0.0, -1.0, 0.0).x : _5616.x, _5873.y ? vec3(0.0, -1.0, 0.0).y : _5616.y, _5873.z ? vec3(0.0, -1.0, 0.0).z : _5616.z)));
        vec3 _5792;
        if (dot(_2236, _2232) < 0.0)
        {
            _5792 = -_2236;
        }
        else
        {
            _5792 = _2236;
        }
        vec3 _2248 = vec3(_104.CameraRight.w, _104.CameraUp.w, _104.CameraForward.w) + (((_104.CameraRight.xyz * _2870) + (_104.CameraUp.xyz * _2882)) + (_104.CameraForward.xyz * _2815));
        vec3 _3437 = ((_104.CameraRight.xyz * _5792.x) + (_104.CameraUp.xyz * _5792.y)) + (_104.CameraForward.xyz * _5792.z);
        float _2259 = (_2815 / ((0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_5792, _2232)), 0.07999999821186065673828125);
        float _2271 = smoothstep(0.75, 0.949999988079071044921875, _3437.y) * clamp(1.0 - (length(_2884) / max(_104.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0);
        vec3 _5796;
        if ((_2271 > 0.0) && (_104.RippleParams.x > 0.0))
        {
            vec2 _2281 = _2248.xz;
            float _3467 = max(_104.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _5794;
            _5794 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _5793 = 0; _5793 < 5; )
            {
                float _3476 = float(_5793);
                float _3480 = _3467 * pow(0.62000000476837158203125, _3476);
                float _3490 = 0.89999997615814208984375 + (_3476 * 2.399960041046142578125);
                vec2 _3495 = vec2(cos(_3490), sin(_3490));
                _5794 += (_3495 * (((_104.RippleParams.x * pow(0.800000011920928955078125, _3476)) * clamp((_3480 / max(_2259 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_3495, _2281) * (6.283185482025146484375 / _3480)) + ((sqrt(61.63805389404296875 / _3480) * _104.RippleParams.z) * _104.ShadeParams.z)) + (_3476 * 1.7000000476837158203125))));
                _5793++;
                continue;
            }
            vec2 _2286 = _5794 * _2271;
            vec3 _2296 = normalize(_3437 + vec3(-_2286.x, 0.0, -_2286.y));
            vec3 _2298 = normalize(vec3(dot(_2296, _104.CameraRight.xyz), dot(_2296, _104.CameraUp.xyz), dot(_2296, _104.CameraForward.xyz)));
            bvec3 _5875 = bvec3(dot(_2298, _2232) < 0.0199999995529651641845703125);
            _5796 = vec3(_5875.x ? _5792.x : _2298.x, _5875.y ? _5792.y : _2298.y, _5875.z ? _5792.z : _2298.z);
        }
        else
        {
            _5796 = _5792;
        }
        if (_104.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_2127), vec4((_5796 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_104.DebugView == 2u)
        {
            float _2329 = clamp(_2815 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_2127), vec4(_2329, _2329, _2329, 1.0));
            break;
        }
        ivec2 _3565 = clamp(_2166, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
        vec4 _3591 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3565.x, int((_104.OutputHeight - 1u) - uint(_3565.y)), 0).xy, 0);
        float _3592 = _3591.x;
        float _3609 = ((_3592 > 0.0) && (abs(_3592 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_3592 - _104.ProjA)) : 1000000000.0;
        float _2351 = (_3609 < 100000000.0) ? (max(_3609 - _2815, 0.0) / max(_2230.z, 0.0500000007450580596923828125)) : 10000.0;
        float _2358 = max(_2855.x, 0.0) * max(_104.ThicknessScale, 0.0);
        float _2372 = mix(_2358, min(_2351, (_2358 * 1.5) + _2169), smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _2358 / max(_2351, 0.001000000047497451305389404296875)));
        vec3 _2375 = refract(_2230, _5796, 0.75187969207763671875);
        bvec3 _5877 = bvec3(dot(_2375, _2375) < 9.9999999747524270787835121154785e-07);
        vec3 _2402 = _2884 + (normalize(mix(_2230, vec3(_5877.x ? _2230.x : _2375.x, _5877.y ? _2230.y : _2375.y, _5877.z ? _2230.z : _2375.z), vec3(clamp(_104.Refraction, 0.0, 1.0)))) * min(min(_2372, _2351), max(_104.MaxTravel, 0.0)));
        float _3649 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec2 _3639 = vec2(((_2402.x * _3649) / _2402.z) + (0.5 * float(_104.OutputWidth)), (((-_2402.y) * _3649) / _2402.z) + (0.5 * float(_104.OutputHeight)));
        bvec2 _2409 = bvec2(_2402.z > 0.0500000007450580596923828125);
        vec2 _5800;
        _5800 = vec2(_2409.x ? _3639.x : _2163.x, _2409.y ? _3639.y : _2163.y) - _2163;
        vec2 _2445;
        vec2 _5801;
        float _5803;
        int _5799 = 0;
        SPIRV_CROSS_LOOP
        for (;;)
        {
            if (_5799 < 4)
            {
                vec2 _2426 = clamp(_2163 + _5800, vec2(0.5), _2158 - vec2(0.5));
                ivec2 _2430 = ivec2(_2426 - vec2(0.5));
                ivec2 _3684 = clamp(_2430, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                vec4 _3710 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_3684.x, int((_104.OutputHeight - 1u) - uint(_3684.y)), 0).xy, 0);
                float _3711 = _3710.x;
                ivec2 _3725 = clamp(_2430, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                ivec2 _3766 = clamp(_2430, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                vec4 _3792 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3766.x, int((_104.OutputHeight - 1u) - uint(_3766.y)), 0).xy, 0);
                float _3793 = _3792.x;
                ivec2 _2435 = ivec2(_2426 + vec2(0.5));
                ivec2 _3845 = clamp(_2435, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                vec4 _3871 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_3845.x, int((_104.OutputHeight - 1u) - uint(_3845.y)), 0).xy, 0);
                float _3872 = _3871.x;
                ivec2 _3886 = clamp(_2435, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                ivec2 _3927 = clamp(_2435, ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                vec4 _3953 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3927.x, int((_104.OutputHeight - 1u) - uint(_3927.y)), 0).xy, 0);
                float _3954 = _3953.x;
                if ((((_3711 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_3725.x, int((_104.OutputHeight - 1u) - uint(_3725.y)), 0).xy, 0).w > 0.5)) && ((((_3793 > 0.0) && (abs(_3793 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_3793 - _104.ProjA)) : 1000000000.0) >= (_3711 + 0.0500000007450580596923828125))) && (((_3872 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_3886.x, int((_104.OutputHeight - 1u) - uint(_3886.y)), 0).xy, 0).w > 0.5)) && ((((_3954 > 0.0) && (abs(_3954 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_3954 - _104.ProjA)) : 1000000000.0) >= (_3872 + 0.0500000007450580596923828125))))
                {
                    ivec2 _3986 = clamp(ivec2(_2426), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                    vec4 _4012 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_3986.x, int((_104.OutputHeight - 1u) - uint(_3986.y)), 0).xy, 0);
                    float _4013 = _4012.x;
                    _5803 = ((_4013 > 0.0) && (abs(_4013 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_4013 - _104.ProjA)) : 1000000000.0;
                    _5801 = _2426;
                    break;
                }
                _2445 = _5800 * 0.5;
                _5800 = _2445;
                _5799++;
                continue;
            }
            else
            {
                _5803 = _3609;
                _5801 = _2163;
                break;
            }
        }
        vec3 _2464 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2((_5801 / _2158).x, 1.0 - (_5801 / _2158).y), 0.0).xyz;
        vec3 _5805;
        if ((_104.CausticParams.x > 0.0) && (_5803 < 100000000.0))
        {
            float _4078 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _2477 = vec3(_104.CameraRight.w, _104.CameraUp.w, _104.CameraForward.w) + (((_104.CameraRight.xyz * (((_5801.x - (0.5 * float(_104.OutputWidth))) * _5803) / _4078)) + (_104.CameraUp.xyz * (((-(_5801.y - (0.5 * float(_104.OutputHeight)))) * _5803) / _4078))) + (_104.CameraForward.xyz * _5803));
            float _2482 = _2248.y - _2477.y;
            vec3 _5806;
            if (_2482 > 0.0199999995529651641845703125)
            {
                vec3 _2489 = normalize(_104.SunDirection.xyz);
                vec3 _4122 = ((_104.CameraRight.xyz * _2489.x) + (_104.CameraUp.xyz * _2489.y)) + (_104.CameraForward.xyz * _2489.z);
                vec2 _2501 = (_4122.xz / vec2(_4122.y)) * _2482;
                bvec2 _2502 = bvec2(_4122.y > 0.100000001490116119384765625);
                vec2 _4139 = (_2477.xz + vec2(_2502.x ? _2501.x : vec2(0.0).x, _2502.y ? _2501.y : vec2(0.0).y)) * (6.283185482025146484375 / max(_104.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _4162 = _4139 + (vec2(sin((_4139.y * 0.730000019073486328125) + ((_104.ShadeParams.z * 0.89999997615814208984375) * _104.CausticParams.z)), sin((_4139.x * 0.61000001430511474609375) - ((_104.ShadeParams.z * 1.10000002384185791015625) * _104.CausticParams.z))) * 0.89999997615814208984375);
                _5806 = _2464 * (1.0 + (((_104.CausticParams.x * (exp((-_2482) / max(_104.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _2482))) * clamp(dot(_104.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) * 0.5, 0.0, 1.0)) * ((pow(clamp((1.0 - abs(sin(_4162.x + ((_104.ShadeParams.z * 0.60000002384185791015625) * _104.CausticParams.z)) * sin((_4162.y * 1.12999999523162841796875) - ((_104.ShadeParams.z * 0.4000000059604644775390625) * _104.CausticParams.z)))) * (1.0 - abs(sin(((_4162.x + _4162.y) * 0.709999978542327880859375) - ((_104.ShadeParams.z * 0.5) * _104.CausticParams.z)) * sin(((_4162.x - _4162.y) * 0.829999983310699462890625) + ((_104.ShadeParams.z * 0.699999988079071044921875) * _104.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.2000000476837158203125) - 0.25)));
            }
            else
            {
                _5806 = _2464;
            }
            _5805 = _5806;
        }
        else
        {
            _5805 = _2464;
        }
        vec3 _2548 = exp(max(_104.FluidTint.xyz, vec3(0.0)) * (-_2372));
        vec3 _2559 = (_5805 * _2548) + ((vec3(1.0) - _2548) * _104.FluidColor.xyz);
        if (_104.DebugView == 3u)
        {
            float _2566 = clamp(_2372 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_2127), vec4(_2566, _2566, _2566, 1.0));
            break;
        }
        if (_104.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_2127), vec4(_2855.www, 1.0));
            break;
        }
        if (_104.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_2127), vec4(_2559, 1.0));
            break;
        }
        vec3 _2606 = reflect(_2230, _5796);
        vec3 _4236 = ((_104.CameraRight.xyz * _2606.x) + (_104.CameraUp.xyz * _2606.y)) + (_104.CameraForward.xyz * _2606.z);
        float _5829;
        vec3 _5846;
        do
        {
            if (_104.ReflectionDistance <= 0.0)
            {
                _5846 = vec3(0.0);
                _5829 = 0.0;
                break;
            }
            float _4282 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _2606.z);
            if (_4282 <= 0.0)
            {
                _5846 = vec3(0.0);
                _5829 = 0.0;
                break;
            }
            float _4288 = 0.014999999664723873138427734375 * ((0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05));
            float _4292 = max(0.0040000001899898052215576171875 * _2815, 0.00999999977648258209228515625);
            float _4294 = 0.5 * _4292;
            float _4303;
            float _4398;
            bool _5819;
            bool _5821;
            float _5825;
            float _5830;
            vec3 _5847;
            int _5812 = 0;
            float _5813 = _4294;
            float _5814 = _4292;
            float _5826 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_5812 < 80)
                {
                    _4303 = _5813 + _5814;
                    if (_4303 > _104.ReflectionDistance)
                    {
                        _5847 = vec3(0.0);
                        _5830 = 0.0;
                        _5825 = _5826;
                        _5821 = false;
                        _5819 = true;
                        break;
                    }
                    vec3 _4314 = _2884 + (_2606 * _4303);
                    if (_4314.z <= 0.0500000007450580596923828125)
                    {
                        _5847 = vec3(0.0);
                        _5830 = 0.0;
                        _5825 = _5826;
                        _5821 = false;
                        _5819 = true;
                        break;
                    }
                    float _4506 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _4482 = ((_4314.x * _4506) / _4314.z) + (0.5 * float(_104.OutputWidth));
                    float _4495 = (((-_4314.y) * _4506) / _4314.z) + (0.5 * float(_104.OutputHeight));
                    if (!((((_4482 >= 0.0) && (_4495 >= 0.0)) && (_4482 < float(_104.OutputWidth))) && (_4495 < float(_104.OutputHeight))))
                    {
                        _5847 = vec3(0.0);
                        _5830 = _5842;
                        _5825 = _5826;
                        _5821 = true;
                        _5819 = false;
                        break;
                    }
                    ivec2 _4544 = clamp(ivec2(vec2(_4482, _4495)), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                    vec4 _4570 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_4544.x, int((_104.OutputHeight - 1u) - uint(_4544.y)), 0).xy, 0);
                    float _4571 = _4570.x;
                    float _4588 = ((_4571 > 0.0) && (abs(_4571 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_4571 - _104.ProjA)) : 1000000000.0;
                    if (((_4588 < 100000000.0) && (_4314.z > _4588)) && ((_4314.z - _4588) < max(2.0 * _5814, 0.25)))
                    {
                        float _5816;
                        _5816 = _4303;
                        float _5879;
                        float _5880;
                        int _5815 = 0;
                        float _5817 = _5813;
                        SPIRV_CROSS_UNROLL
                        for (; _5815 < 6; _5817 = _5879, _5816 = _5880, _5815++)
                        {
                            float _4358 = 0.5 * (_5817 + _5816);
                            vec3 _4363 = _2884 + (_2606 * _4358);
                            float _4628 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _4643 = clamp(ivec2(vec2(((_4363.x * _4628) / _4363.z) + (0.5 * float(_104.OutputWidth)), (((-_4363.y) * _4628) / _4363.z) + (0.5 * float(_104.OutputHeight)))), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
                            vec4 _4669 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_4643.x, int((_104.OutputHeight - 1u) - uint(_4643.y)), 0).xy, 0);
                            float _4670 = _4669.x;
                            bool _4370 = _4363.z > (((_4670 > 0.0) && (abs(_4670 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_4670 - _104.ProjA)) : 1000000000.0);
                            _5879 = _4370 ? _5817 : _4358;
                            _5880 = _4370 ? _4358 : _5816;
                        }
                        vec3 _4384 = _2884 + (_2606 * _5816);
                        float _4727 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _4717 = vec2(((_4384.x * _4727) / _4384.z) + (0.5 * float(_104.OutputWidth)), (((-_4384.y) * _4727) / _4384.z) + (0.5 * float(_104.OutputHeight)));
                        float _4743 = clamp((_4288 * _5816) / max(_4384.z, 0.0500000007450580596923828125), 0.0, 10.0);
                        float _4745 = 0.5 * _4743;
                        vec2 _4830 = clamp(_4717 / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _4856 = clamp((_4717 + vec2(_4745)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _4882 = clamp((_4717 + vec2(_4743 * (-0.5), _4745)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _4908 = clamp((_4717 + vec2(_4745, _4743 * (-0.5))) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _4934 = clamp((_4717 + vec2(_4743 * (-0.5), _4743 * (-0.5))) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _4960 = clamp((_4717 + vec2(_4743, 0.0)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _4986 = clamp((_4717 + vec2(-_4743, 0.0)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _5012 = clamp((_4717 + vec2(0.0, _4743)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _5038 = clamp((_4717 + vec2(0.0, -_4743)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _4815 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4830.x, 1.0 - _4830.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4856.x, 1.0 - _4856.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4882.x, 1.0 - _4882.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4908.x, 1.0 - _4908.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4934.x, 1.0 - _4934.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4960.x, 1.0 - _4960.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_4986.x, 1.0 - _4986.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5012.x, 1.0 - _5012.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5038.x, 1.0 - _5038.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _5847 = _4815;
                        _5830 = _4282;
                        _5825 = _4303;
                        _5821 = false;
                        _5819 = true;
                        break;
                    }
                    _4398 = _5814 * 1.07000005245208740234375;
                    _5814 = _4398;
                    _5813 = _4303;
                    _5812++;
                    _5826 = _4303;
                    continue;
                }
                else
                {
                    _5847 = vec3(0.0);
                    _5830 = _5842;
                    _5825 = _5826;
                    _5821 = false;
                    _5819 = false;
                    break;
                }
            }
            if (_5819)
            {
                _5846 = _5847;
                _5829 = _5830;
                break;
            }
            if ((!_5821) || (_5825 < 0.0))
            {
                _5846 = _5847;
                _5829 = 0.0;
                break;
            }
            vec3 _4416 = _2884 + (_2606 * _5825);
            float _5089 = (0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _5079 = vec2(((_4416.x * _5089) / _4416.z) + (0.5 * float(_104.OutputWidth)), (((-_4416.y) * _5089) / _4416.z) + (0.5 * float(_104.OutputHeight)));
            ivec2 _5104 = clamp(ivec2(_5079), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
            vec4 _5130 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_5104.x, int((_104.OutputHeight - 1u) - uint(_5104.y)), 0).xy, 0);
            float _5131 = _5130.x;
            float _5148 = ((_5131 > 0.0) && (abs(_5131 - _104.ProjA) > 9.999999717180685365747194737196e-10)) ? (_104.ProjB / (_5131 - _104.ProjA)) : 1000000000.0;
            float _5164 = clamp((_4288 * _5825) / max(_4416.z, 0.0500000007450580596923828125), 0.0, 10.0);
            float _5166 = 0.5 * _5164;
            vec2 _5251 = clamp(_5079 / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5277 = clamp((_5079 + vec2(_5166)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5303 = clamp((_5079 + vec2(_5164 * (-0.5), _5166)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5329 = clamp((_5079 + vec2(_5166, _5164 * (-0.5))) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5355 = clamp((_5079 + vec2(_5164 * (-0.5), _5164 * (-0.5))) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5381 = clamp((_5079 + vec2(_5164, 0.0)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5407 = clamp((_5079 + vec2(-_5164, 0.0)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5433 = clamp((_5079 + vec2(0.0, _5164)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _5459 = clamp((_5079 + vec2(0.0, -_5164)) / vec2(float(_104.OutputWidth), float(_104.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _5236 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5251.x, 1.0 - _5251.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5277.x, 1.0 - _5277.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5303.x, 1.0 - _5303.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5329.x, 1.0 - _5329.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5355.x, 1.0 - _5355.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5381.x, 1.0 - _5381.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5407.x, 1.0 - _5407.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5433.x, 1.0 - _5433.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_5459.x, 1.0 - _5459.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_5148 > 100000000.0)
            {
                _5846 = _5236;
                _5829 = _4282;
                break;
            }
            float _4439 = _5148 - _4416.z;
            if (_4439 <= 0.0)
            {
                _5846 = _5236;
                _5829 = _4282;
                break;
            }
            _5846 = _5236;
            _5829 = _4282 * exp((_4439 / max(_2606.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        float _5476 = _4236.y;
        vec3 _2618 = mix(mix(_104.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_104.SkyHorizon.xyz, _104.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _5476))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _5476))), _5846, vec3(_5829));
        if (_104.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_2127), vec4(_2618, 1.0));
            break;
        }
        float _2647 = 0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_5792 + _5796), _2232), 0.0, 1.0), max(_104.FresnelPower, 1.0)));
        vec3 _2657 = normalize(_104.SunDirection.xyz);
        float _2664 = max(_104.ShadeParams.w, 8.0);
        float _2700 = clamp(2.5 * ((_2169 * ((0.5 * float(_104.OutputHeight)) * max(_104.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _2815), 2.0, 48.0);
        float _2702 = 4.0 * _2169;
        float _5867;
        _5867 = 0.0;
        SPIRV_CROSS_UNROLL
        for (int _5866 = 0; _5866 < 8; )
        {
            float _2710 = float(_5866) * 0.785398185253143310546875;
            ivec2 _5519 = clamp(_2166 + ivec2(roundEven(vec2(cos(_2710), sin(_2710)) * _2700)), ivec2(0), ivec2(int(_104.OutputWidth) - 1, int(_104.OutputHeight) - 1));
            vec4 _5545 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_5519.x, int((_104.OutputHeight - 1u) - uint(_5519.y)), 0).xy, 0);
            float _5546 = _5545.x;
            _5867 += float((_5546 <= 0.0) || (_5546 > (_2815 + _2702)));
            _5866++;
            continue;
        }
        float _2746 = clamp((_5867 - 5.0) * 0.5, 0.0, 1.0) * clamp(_104.Foam, 0.0, 1.0);
        vec3 _2758 = mix(mix(_2559, _2618, vec3(clamp(_2647, 0.0, 1.0))) + ((((_104.SunColor.xyz * ((pow(clamp(dot(_5796, normalize(_2657 - _2230)), 0.0, 1.0), _2664) * (_2664 + 8.0)) * 0.0397888012230396270751953125)) * _104.Specular) * _2647) * clamp(dot(_5796, _2657) * 4.0, 0.0, 1.0)), (mix(_104.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_104.SkyHorizon.xyz, _104.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.5))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.5))) + (_104.SunColor.xyz * 0.1500000059604644775390625)) * 0.85000002384185791015625, vec3(_2746));
        imageStore(OutputTexture, ivec2(_2127), vec4(_2758, clamp(_2855.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _2746))));
        break;
    } while(false);
}

