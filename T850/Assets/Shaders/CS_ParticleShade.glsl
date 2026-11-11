#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=1046724ef924584d00a5d25125da711d4f124e8ac1efa2408070edb0701a9436; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1922[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _27130;
bool _27358;
uint _27470;
vec3 _27648;
float _27704;
vec2 _27870;
float _29958;

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
    vec4 EnvParams;
    vec4 PoolMin;
    vec4 PoolMax;
    vec4 LightParams;
    vec4 ScatterParams;
    vec4 FoamParams;
    vec4 WetParams;
    uvec4 GridParams;
    vec4 RippleFlow;
    uint ThicknessShift;
    float ThicknessPad0;
    float ThicknessPad1;
    float ThicknessPad2;
} _283;

layout(binding = 11, std430) readonly buffer AuxBits
{
    uint _data[];
} AuxBits_1;

layout(binding = 12, std430) readonly buffer CausticField
{
    float _data[];
} CausticField_1;

layout(binding = 13, std430) readonly buffer Wetness
{
    uint _data[];
} Wetness_1;

layout(binding = 14, std430) buffer WhitewaterBits
{
    uint _data[];
} WhitewaterBits_1;

layout(binding = 5, rgba16f) uniform writeonly image2D OutputTexture;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler;
layout(binding = 7) uniform samplerCube SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler;
layout(binding = 7) uniform samplerCube SPIRV_Cross_CombinedEnvironmentMapLinearSampler;
layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler;
layout(binding = 6) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;
layout(binding = 3) uniform sampler2D SPIRV_Cross_CombinedSceneColorLinearSampler;
layout(binding = 9) uniform sampler2D SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler;
layout(binding = 15) uniform sampler2D SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler;
layout(binding = 10) uniform sampler2D SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler;
layout(binding = 8) uniform sampler2D SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler;

uvec2 FluidSize;
float EnvMaxMip;

void main()
{
    do
    {
        if (_283.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _283.OutputWidth) || (gl_GlobalInvocationID.y >= _283.OutputHeight))
        {
            break;
        }
        uvec2 _5575 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _5575.x;
        FluidSize.y = _5575.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _5602 = uvec2(gl_GlobalInvocationID.x, (_283.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _5605 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _5610 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _5624 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _283.OutputWidth)) * 4u;
        uint _5628 = WhitewaterBits_1._data[_5624];
        float _5629 = float(_5628);
        uint _5634 = WhitewaterBits_1._data[_5624 + 1u];
        float _5635 = float(_5634);
        uint _5640 = WhitewaterBits_1._data[_5624 + 2u];
        float _5641 = float(_5640);
        uint _5646 = WhitewaterBits_1._data[_5624 + 3u];
        WhitewaterBits_1._data[_5624] = 0u;
        WhitewaterBits_1._data[_5624 + 1u] = 0u;
        WhitewaterBits_1._data[_5624 + 2u] = 0u;
        WhitewaterBits_1._data[_5624 + 3u] = 0u;
        vec3 _5664 = normalize(_283.SunDirection.xyz);
        vec3 _5666 = normalize(((_283.CameraRight.xyz * _5664.x) + (_283.CameraUp.xyz * _5664.y)) + (_283.CameraForward.xyz * _5664.z));
        vec3 _5669 = refract(-_5666, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _29896 = bvec3((dot(_5669, _5669) < 9.9999999747524270787835121154785e-07) || (_5666.y <= 0.0));
        vec3 _29897 = vec3(_29896.x ? vec3(0.0, -1.0, 0.0).x : _5669.x, _29896.y ? vec3(0.0, -1.0, 0.0).y : _5669.y, _29896.z ? vec3(0.0, -1.0, 0.0).z : _5669.z);
        float _6659 = max(EnvMaxMip - 1.0, 0.0);
        vec3 _27131;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27131 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                break;
            }
            _27131 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_6659, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27132;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27132 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27132 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6659, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27134;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27134 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27134 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6659, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27136;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27136 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27136 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_6659, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27138;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27138 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27138 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_6659, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _6681 = (((((_27131 * 2.0) + _27132) + _27134) + _27136) + _27138) * vec3(0.16666667163372039794921875);
        float _5689 = clamp(1.0 - exp(_5629 * (-0.0006103515625)), 0.0, 1.0) * clamp(_283.FoamParams.w, 0.0, 1.0);
        float _27142;
        vec4 _27143;
        do
        {
            vec2 _7034 = ((_5610 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
            ivec2 _7037 = ivec2(floor(_7034));
            vec2 _7041 = _7034 - vec2(_7037);
            ivec2 _7216 = clamp(_7037, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7252 = clamp(_7037 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7288 = clamp(_7037 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7324 = clamp(_7037 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7077 = vec4((1.0 - _7041.x) * (1.0 - _7041.y), _7041.x * (1.0 - _7041.y), (1.0 - _7041.x) * _7041.y, _7041.x * _7041.y);
            vec4 _7004 = _7077;
            vec4 _7082 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7216.x, int((FluidSize.y - 1u) - uint(_7216.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7252.x, int((FluidSize.y - 1u) - uint(_7252.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7288.x, int((FluidSize.y - 1u) - uint(_7288.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7324.x, int((FluidSize.y - 1u) - uint(_7324.y)), 0).xy, 0).x);
            vec4 _7005 = _7082;
            float _29625;
            _29625 = 1000000000.0;
            float _29895;
            SPIRV_CROSS_UNROLL
            for (int _27140 = 0; _27140 < 4; _29625 = _29895, _27140++)
            {
                if (_7005[_27140] > 0.0)
                {
                    _29895 = min(_29625, _7005[_27140]);
                }
                else
                {
                    _29895 = _29625;
                }
            }
            vec4 _7008 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _27141 = 0; _27141 < 4; )
            {
                _7008[_27141] = ((_7005[_27141] > 0.0) && (_7005[_27141] < (_29625 + max(0.25, 0.02999999932944774627685546875 * _29625)))) ? _7004[_27141] : 0.0;
                _27141++;
                continue;
            }
            float _7142 = ((_7008.x + _7008.y) + _7008.z) + _7008.w;
            if (_7142 <= 9.9999997473787516355514526367188e-05)
            {
                _27143 = vec4(0.0);
                _27142 = 0.0;
                break;
            }
            ivec2 _7360 = clamp(_7037, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7386 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7360.x, int((FluidSize.y - 1u) - uint(_7360.y)), 0).xy, 0);
            ivec2 _7395 = clamp(_7037 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7421 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7395.x, int((FluidSize.y - 1u) - uint(_7395.y)), 0).xy, 0);
            ivec2 _7430 = clamp(_7037 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7456 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7430.x, int((FluidSize.y - 1u) - uint(_7430.y)), 0).xy, 0);
            ivec2 _7465 = clamp(_7037 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7491 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7465.x, int((FluidSize.y - 1u) - uint(_7465.y)), 0).xy, 0);
            _27143 = vec4((((((_7386.xyz * _7008.x) + (_7421.xyz * _7008.y)) + (_7456.xyz * _7008.z)) + (_7491.xyz * _7008.w)) / vec3(_7142)).x, _29958, _29958, dot(_7077, vec4(_7386.w, _7421.w, _7456.w, _7491.w)));
            _27142 = dot(_7008, _7082) / _7142;
            break;
        } while(false);
        ivec2 _7553 = clamp(_5605, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
        vec4 _7504 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7553.x, int((_283.OutputHeight - 1u) - uint(_7553.y)), 0).xy, 0);
        float _7505 = _7504.x;
        float _7522 = ((_7505 > 0.0) && (abs(_7505 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_7505 - _283.ProjA)) : 1000000000.0;
        if (!(((_27142 > 0.0) && (_27143.w > 0.001000000047497451305389404296875)) && (_27142 < (_7522 - 0.00200000009499490261077880859375))))
        {
            vec2 _7598 = clamp(_5610 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _7609 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_7598.x, 1.0 - _7598.y), 0.0).xyz;
            float _29610;
            vec3 _29621;
            if ((_7522 < 100000000.0) && (_283.WetParams.w > 0.0))
            {
                float _7657 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _5724 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_5610.x - (0.5 * float(_283.OutputWidth))) * _7522) / _7657)) + (_283.CameraUp.xyz * (((-(_5610.y - (0.5 * float(_283.OutputHeight)))) * _7522) / _7657))) + (_283.CameraForward.xyz * _7522));
                ivec2 _7737 = clamp(_5605, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                vec3 _7697 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_7737.x, int((_283.OutputHeight - 1u) - uint(_7737.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _7699 = length(_7697);
                vec3 _7705 = _7697 / vec3(_7699);
                bvec3 _7706 = bvec3(_7699 > 0.001000000047497451305389404296875);
                float _29611;
                vec3 _29622;
                if ((vec3(_7706.x ? _7705.x : vec3(0.0, 1.0, 0.0).x, _7706.y ? _7705.y : vec3(0.0, 1.0, 0.0).y, _7706.z ? _7705.z : vec3(0.0, 1.0, 0.0).z).y > 0.60000002384185791015625) && (_5724.y > (_283.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _29552;
                    do
                    {
                        int _7790 = int(_283.GridParams.x);
                        int _7793 = int(_283.GridParams.y);
                        if (((_7790 < 4) || (_7793 < 4)) || (_283.WetParams.z <= 0.0))
                        {
                            _29552 = 0.0;
                            break;
                        }
                        vec2 _7815 = ((_5724.xz - _283.WetParams.xy) / vec2(_283.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_7815, vec2(-1.0))) || (_7815.x > float(_7790))) || (_7815.y > float(_7793)))
                        {
                            _29552 = 0.0;
                            break;
                        }
                        ivec2 _7835 = ivec2(floor(_7815));
                        vec2 _7839 = _7815 - vec2(_7835);
                        float _7841 = _7839.x;
                        float _7922 = _7841 * _7841;
                        float _7925 = _7922 * _7841;
                        vec4 _7775 = vec4(((1.0 - (3.0 * _7841)) + (3.0 * _7922)) - _7925, (4.0 - (6.0 * _7922)) + (3.0 * _7925), ((1.0 + (3.0 * _7841)) + (3.0 * _7922)) - (3.0 * _7925), _7925) * 0.16666667163372039794921875;
                        float _7844 = _7839.y;
                        float _7958 = _7844 * _7844;
                        float _7961 = _7958 * _7844;
                        vec4 _7777 = vec4(((1.0 - (3.0 * _7844)) + (3.0 * _7958)) - _7961, (4.0 - (6.0 * _7958)) + (3.0 * _7961), ((1.0 + (3.0 * _7844)) + (3.0 * _7958)) - (3.0 * _7961), _7961) * 0.16666667163372039794921875;
                        float _29545;
                        _29545 = 0.0;
                        float _29881;
                        SPIRV_CROSS_UNROLL
                        for (int _29544 = 0; _29544 < 4; _29545 = _29881, _29544++)
                        {
                            int _7855 = (_7835.y - 1) + _29544;
                            if ((_7855 < 0) || (_7855 >= _7793))
                            {
                                _29881 = _29545;
                                continue;
                            }
                            float _29548;
                            _29548 = 0.0;
                            float _29883;
                            SPIRV_CROSS_UNROLL
                            for (int _29546 = 0; _29546 < 4; _29548 = _29883, _29546++)
                            {
                                int _7873 = (_7835.x - 1) + _29546;
                                if ((_7873 >= 0) && (_7873 < _7790))
                                {
                                    _29883 = _29548 + (_7775[_29546] * float(Wetness_1._data[uint(_7873 + (_7855 * _7790))]));
                                }
                                else
                                {
                                    _29883 = _29548;
                                }
                            }
                            _29881 = _29545 + (_7777[_29544] * _29548);
                        }
                        _29552 = _29545 * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _5746 = clamp(((_29552 * _283.WetParams.w) - 0.00999999977648258209228515625) * 1.01010096073150634765625, 0.0, 1.0);
                    vec3 _29623;
                    if (_5746 > 0.001000000047497451305389404296875)
                    {
                        vec3 _5753 = normalize(_5724 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w));
                        vec3 _29609;
                        do
                        {
                            float _8024 = smoothstep(0.449999988079071044921875, 1.0, _5746);
                            vec3 _8028 = _7609 * mix(1.0, 0.5, clamp(_5746 * 2.0, 0.0, 1.0));
                            if (_8024 <= 0.0)
                            {
                                _29609 = _8028;
                                break;
                            }
                            vec3 _8046 = reflect(_5753, vec3(0.0, 1.0, 0.0));
                            float _8105 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec3 _8095 = vec3(((_5610.x - (0.5 * float(_283.OutputWidth))) * _7522) / _8105, ((-(_5610.y - (0.5 * float(_283.OutputHeight)))) * _7522) / _8105, _7522);
                            float _8122 = dot(_8046, _283.CameraForward.xyz);
                            vec3 _8123 = vec3(dot(_8046, _283.CameraRight.xyz), dot(_8046, _283.CameraUp.xyz), _8122);
                            float _29582;
                            vec3 _29602;
                            do
                            {
                                if (_283.ReflectionDistance <= 0.0)
                                {
                                    _29602 = vec3(0.0);
                                    _29582 = 0.0;
                                    break;
                                }
                                float _8172 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _8122);
                                if (_8172 <= 0.0)
                                {
                                    _29602 = vec3(0.0);
                                    _29582 = 0.0;
                                    break;
                                }
                                float _8369 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _8181 = max(0.0040000001899898052215576171875 * _7522, 0.00999999977648258209228515625);
                                float _8183 = 0.5 * _8181;
                                float _8192;
                                float _8194;
                                bool _29567;
                                bool _29572;
                                float _29577;
                                float _29583;
                                vec3 _29603;
                                int _29558 = 0;
                                float _29559 = _8183;
                                float _29560 = _8181;
                                float _29578 = -1.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_29558 < 80)
                                    {
                                        _8192 = _29559 + _29560;
                                        _8194 = _29560 * 1.07000005245208740234375;
                                        if (_8192 > _283.ReflectionDistance)
                                        {
                                            _29603 = vec3(0.0);
                                            _29583 = 0.0;
                                            _29577 = _29578;
                                            _29572 = false;
                                            _29567 = true;
                                            break;
                                        }
                                        vec3 _8205 = _8095 + (_8123 * _8192);
                                        if (_8205.z <= 0.0500000007450580596923828125)
                                        {
                                            _29603 = vec3(0.0);
                                            _29583 = 0.0;
                                            _29577 = _29578;
                                            _29572 = false;
                                            _29567 = true;
                                            break;
                                        }
                                        float _8409 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        float _8385 = ((_8205.x * _8409) / _8205.z) + (0.5 * float(_283.OutputWidth));
                                        float _8398 = (((-_8205.y) * _8409) / _8205.z) + (0.5 * float(_283.OutputHeight));
                                        if (!((((_8385 >= 0.0) && (_8398 >= 0.0)) && (_8385 < float(_283.OutputWidth))) && (_8398 < float(_283.OutputHeight))))
                                        {
                                            _29603 = vec3(0.0);
                                            _29583 = _27704;
                                            _29577 = _29578;
                                            _29572 = true;
                                            _29567 = false;
                                            break;
                                        }
                                        ivec2 _8493 = clamp(ivec2(vec2(_8385, _8398)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _8445 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8493.x, int((_283.OutputHeight - 1u) - uint(_8493.y)), 0).xy, 0);
                                        float _8446 = _8445.x;
                                        float _8463 = ((_8446 > 0.0) && (abs(_8446 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_8446 - _283.ProjA)) : 1000000000.0;
                                        if (((_8463 < 100000000.0) && (_8205.z > _8463)) && ((_8205.z - _8463) < max(_29560 * 2.1400001049041748046875, 0.25)))
                                        {
                                            float _29562;
                                            _29562 = _8192;
                                            float _29898;
                                            float _29899;
                                            int _29561 = 0;
                                            float _29565 = _29559;
                                            SPIRV_CROSS_UNROLL
                                            for (; _29561 < 6; _29565 = _29898, _29562 = _29899, _29561++)
                                            {
                                                float _8249 = 0.5 * (_29565 + _29562);
                                                vec3 _8254 = _8095 + (_8123 * _8249);
                                                float _8563 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                ivec2 _8624 = clamp(ivec2(vec2(((_8254.x * _8563) / _8254.z) + (0.5 * float(_283.OutputWidth)), (((-_8254.y) * _8563) / _8254.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                vec4 _8576 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8624.x, int((_283.OutputHeight - 1u) - uint(_8624.y)), 0).xy, 0);
                                                float _8577 = _8576.x;
                                                bool _8261 = _8254.z > (((_8577 > 0.0) && (abs(_8577 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_8577 - _283.ProjA)) : 1000000000.0);
                                                _29898 = _8261 ? _29565 : _8249;
                                                _29899 = _8261 ? _8249 : _29562;
                                            }
                                            vec3 _8275 = _8095 + (_8123 * _29562);
                                            float _8694 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            vec2 _8684 = vec2(((_8275.x * _8694) / _8275.z) + (0.5 * float(_283.OutputWidth)), (((-_8275.y) * _8694) / _8275.z) + (0.5 * float(_283.OutputHeight)));
                                            ivec2 _8752 = clamp(ivec2(_8684), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                            vec3 _8712 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_8752.x, int((_283.OutputHeight - 1u) - uint(_8752.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                            float _8714 = length(_8712);
                                            vec3 _8720 = _8712 / vec3(_8714);
                                            bvec3 _8721 = bvec3(_8714 > 0.001000000047497451305389404296875);
                                            vec3 _8722 = vec3(_8721.x ? _8720.x : vec3(0.0, 1.0, 0.0).x, _8721.y ? _8720.y : vec3(0.0, 1.0, 0.0).y, _8721.z ? _8720.z : vec3(0.0, 1.0, 0.0).z);
                                            if (dot(vec3(dot(_8722, _283.CameraRight.xyz), dot(_8722, _283.CameraUp.xyz), dot(_8722, _283.CameraForward.xyz)), _8123) > 0.100000001490116119384765625)
                                            {
                                                int _8302 = _29558 + 1;
                                                _29560 = _8194;
                                                _29559 = _8192;
                                                _29558 = _8302;
                                                _29578 = _8192;
                                                continue;
                                            }
                                            float _8816 = clamp(((0.039999999105930328369140625 * _29562) * _8369) / max(_8275.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                            float _8818 = 0.5 * _8816;
                                            vec2 _8903 = clamp(_8684 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8929 = clamp((_8684 + vec2(_8818)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8955 = clamp((_8684 + vec2(_8816 * (-0.5), _8818)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8981 = clamp((_8684 + vec2(_8818, _8816 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9007 = clamp((_8684 + vec2(_8816 * (-0.5), _8816 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9033 = clamp((_8684 + vec2(_8816, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9059 = clamp((_8684 + vec2(-_8816, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9085 = clamp((_8684 + vec2(0.0, _8816)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9111 = clamp((_8684 + vec2(0.0, -_8816)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec3 _8888 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8903.x, 1.0 - _8903.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8929.x, 1.0 - _8929.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8955.x, 1.0 - _8955.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8981.x, 1.0 - _8981.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9007.x, 1.0 - _9007.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9033.x, 1.0 - _9033.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9059.x, 1.0 - _9059.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9085.x, 1.0 - _9085.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9111.x, 1.0 - _9111.y), 0.0).xyz) * 0.100000001490116119384765625;
                                            _29603 = _8888;
                                            _29583 = _8172;
                                            _29577 = _8192;
                                            _29572 = false;
                                            _29567 = true;
                                            break;
                                        }
                                        int _8302 = _29558 + 1;
                                        _29560 = _8194;
                                        _29559 = _8192;
                                        _29558 = _8302;
                                        _29578 = _8192;
                                        continue;
                                    }
                                    else
                                    {
                                        _29603 = vec3(0.0);
                                        _29583 = _27704;
                                        _29577 = _29578;
                                        _29572 = false;
                                        _29567 = false;
                                        break;
                                    }
                                }
                                if (_29567)
                                {
                                    _29602 = _29603;
                                    _29582 = _29583;
                                    break;
                                }
                                if ((!_29572) || (_29577 < 0.0))
                                {
                                    _29602 = _29603;
                                    _29582 = 0.0;
                                    break;
                                }
                                vec3 _8317 = _8095 + (_8123 * _29577);
                                float _9162 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec2 _9152 = vec2(((_8317.x * _9162) / _8317.z) + (0.5 * float(_283.OutputWidth)), (((-_8317.y) * _9162) / _8317.z) + (0.5 * float(_283.OutputHeight)));
                                ivec2 _9223 = clamp(ivec2(_9152), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _9175 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_9223.x, int((_283.OutputHeight - 1u) - uint(_9223.y)), 0).xy, 0);
                                float _9176 = _9175.x;
                                float _9193 = ((_9176 > 0.0) && (abs(_9176 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_9176 - _283.ProjA)) : 1000000000.0;
                                float _9269 = clamp(((0.039999999105930328369140625 * _29577) * _8369) / max(_8317.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                float _9271 = 0.5 * _9269;
                                vec2 _9356 = clamp(_9152 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9382 = clamp((_9152 + vec2(_9271)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9408 = clamp((_9152 + vec2(_9269 * (-0.5), _9271)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9434 = clamp((_9152 + vec2(_9271, _9269 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9460 = clamp((_9152 + vec2(_9269 * (-0.5), _9269 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9486 = clamp((_9152 + vec2(_9269, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9512 = clamp((_9152 + vec2(-_9269, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9538 = clamp((_9152 + vec2(0.0, _9269)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9564 = clamp((_9152 + vec2(0.0, -_9269)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec3 _9341 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9356.x, 1.0 - _9356.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9382.x, 1.0 - _9382.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9408.x, 1.0 - _9408.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9434.x, 1.0 - _9434.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9460.x, 1.0 - _9460.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9486.x, 1.0 - _9486.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9512.x, 1.0 - _9512.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9538.x, 1.0 - _9538.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9564.x, 1.0 - _9564.y), 0.0).xyz) * 0.100000001490116119384765625;
                                if (_9193 > 100000000.0)
                                {
                                    _29602 = _9341;
                                    _29582 = _8172;
                                    break;
                                }
                                float _8342 = _9193 - _8317.z;
                                if (_8342 <= 0.0)
                                {
                                    _29602 = _9341;
                                    _29582 = _8172;
                                    break;
                                }
                                _29602 = _9341;
                                _29582 = _8172 * exp((_8342 / max(_8122, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                                break;
                            } while(false);
                            vec3 _29608;
                            do
                            {
                                if (_283.EnvParams.x <= 0.0)
                                {
                                    float _9618 = _8046.y;
                                    _29608 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _9618))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _9618)));
                                    break;
                                }
                                _29608 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_8046.x, _8046.y, -_8046.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
                                break;
                            } while(false);
                            _29609 = mix(_8028, mix(_29608, _29602, vec3(_29582)), vec3((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp((-_5753).y, 0.0, 1.0), 5.0))) * _8024));
                            break;
                        } while(false);
                        _29623 = _29609;
                    }
                    else
                    {
                        _29623 = _7609;
                    }
                    _29622 = _29623;
                    _29611 = _5746;
                }
                else
                {
                    _29622 = _7609;
                    _29611 = 0.0;
                }
                _29621 = _29622;
                _29610 = _29611;
            }
            else
            {
                _29621 = _7609;
                _29610 = 0.0;
            }
            if (_283.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_5602), vec4(_29610, _29610, _29610, 1.0));
                break;
            }
            if ((_29610 <= 0.001000000047497451305389404296875) && (_5689 <= 0.001000000047497451305389404296875))
            {
                if (_283.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_5602), vec4(0.0));
                }
                break;
            }
            vec3 _9654 = ((_283.SunColor.xyz * (clamp(_5666.y, 0.0, 1.0) * 0.3183098733425140380859375)) + _6681) * 0.85000002384185791015625;
            vec3 _9660 = mix(vec3(dot(_9654, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _9654, vec3(0.5));
            if (_29610 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_5602), vec4(mix(_29621, _9660, vec3(_5689)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_5602), vec4(_9660, _5689));
            }
            break;
        }
        float _9703 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _9679 = ((_5610.x - (0.5 * float(_283.OutputWidth))) * _27142) / _9703;
        float _9691 = ((-(_5610.y - (0.5 * float(_283.OutputHeight)))) * _27142) / _9703;
        vec3 _9693 = vec3(_9679, _9691, _27142);
        vec3 _5821 = normalize(_9693);
        vec3 _5823 = -_5821;
        int _5839 = clamp(int((_283.NormalStep * max(float(_283.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _5845 = (2.0 * max(_283.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_283.NormalStep, 1.0);
        ivec2 _5847 = ivec2(_5839, 0);
        vec3 _27150;
        do
        {
            float _27146;
            do
            {
                vec2 _9861 = (((vec2(clamp(_5605 + _5847, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _9864 = ivec2(floor(_9861));
                vec2 _9868 = _9861 - vec2(_9864);
                ivec2 _10043 = clamp(_9864, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10079 = clamp(_9864 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10115 = clamp(_9864 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10151 = clamp(_9864 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _9831 = vec4((1.0 - _9868.x) * (1.0 - _9868.y), _9868.x * (1.0 - _9868.y), (1.0 - _9868.x) * _9868.y, _9868.x * _9868.y);
                vec4 _9909 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10043.x, int((FluidSize.y - 1u) - uint(_10043.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10079.x, int((FluidSize.y - 1u) - uint(_10079.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10115.x, int((FluidSize.y - 1u) - uint(_10115.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10151.x, int((FluidSize.y - 1u) - uint(_10151.y)), 0).xy, 0).x);
                vec4 _9832 = _9909;
                float _29542;
                _29542 = 1000000000.0;
                float _29880;
                SPIRV_CROSS_UNROLL
                for (int _27144 = 0; _27144 < 4; _29542 = _29880, _27144++)
                {
                    if (_9832[_27144] > 0.0)
                    {
                        _29880 = min(_29542, _9832[_27144]);
                    }
                    else
                    {
                        _29880 = _29542;
                    }
                }
                vec4 _9835 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27145 = 0; _27145 < 4; )
                {
                    _9835[_27145] = ((_9832[_27145] > 0.0) && (_9832[_27145] < (_29542 + max(0.25, 0.02999999932944774627685546875 * _29542)))) ? _9831[_27145] : 0.0;
                    _27145++;
                    continue;
                }
                float _9969 = ((_9835.x + _9835.y) + _9835.z) + _9835.w;
                if (_9969 <= 9.9999997473787516355514526367188e-05)
                {
                    _27146 = 0.0;
                    break;
                }
                _27146 = dot(_9835, _9909) / _9969;
                break;
            } while(false);
            float _27149;
            do
            {
                vec2 _10391 = (((vec2(clamp(_5605 - _5847, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _10394 = ivec2(floor(_10391));
                vec2 _10398 = _10391 - vec2(_10394);
                ivec2 _10573 = clamp(_10394, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10609 = clamp(_10394 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10645 = clamp(_10394 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10681 = clamp(_10394 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10361 = vec4((1.0 - _10398.x) * (1.0 - _10398.y), _10398.x * (1.0 - _10398.y), (1.0 - _10398.x) * _10398.y, _10398.x * _10398.y);
                vec4 _10439 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10573.x, int((FluidSize.y - 1u) - uint(_10573.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10609.x, int((FluidSize.y - 1u) - uint(_10609.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10645.x, int((FluidSize.y - 1u) - uint(_10645.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10681.x, int((FluidSize.y - 1u) - uint(_10681.y)), 0).xy, 0).x);
                vec4 _10362 = _10439;
                float _29539;
                _29539 = 1000000000.0;
                float _29879;
                SPIRV_CROSS_UNROLL
                for (int _27147 = 0; _27147 < 4; _29539 = _29879, _27147++)
                {
                    if (_10362[_27147] > 0.0)
                    {
                        _29879 = min(_29539, _10362[_27147]);
                    }
                    else
                    {
                        _29879 = _29539;
                    }
                }
                vec4 _10365 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27148 = 0; _27148 < 4; )
                {
                    _10365[_27148] = ((_10362[_27148] > 0.0) && (_10362[_27148] < (_29539 + max(0.25, 0.02999999932944774627685546875 * _29539)))) ? _10361[_27148] : 0.0;
                    _27148++;
                    continue;
                }
                float _10499 = ((_10365.x + _10365.y) + _10365.z) + _10365.w;
                if (_10499 <= 9.9999997473787516355514526367188e-05)
                {
                    _27149 = 0.0;
                    break;
                }
                _27149 = dot(_10365, _10439) / _10499;
                break;
            } while(false);
            bool _9737 = (_27146 > 0.0) && (abs(_27146 - _27142) < _5845);
            bool _9746 = (_27149 > 0.0) && (abs(_27142 - _27149) < _5845);
            vec2 _9753 = vec2(_5605 + _5847) + vec2(0.5);
            float _10887 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9757 = vec3(((_9753.x - (0.5 * float(_283.OutputWidth))) * _27146) / _10887, ((-(_9753.y - (0.5 * float(_283.OutputHeight)))) * _27146) / _10887, _27146) - _9693;
            bvec3 _9758 = bvec3(_9737);
            vec3 _9759 = vec3(_9758.x ? _9757.x : vec3(0.0).x, _9758.y ? _9757.y : vec3(0.0).y, _9758.z ? _9757.z : vec3(0.0).z);
            vec2 _9767 = vec2(_5605 - _5847) + vec2(0.5);
            float _10926 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9770 = _9693 - vec3(((_9767.x - (0.5 * float(_283.OutputWidth))) * _27149) / _10926, ((-(_9767.y - (0.5 * float(_283.OutputHeight)))) * _27149) / _10926, _27149);
            bvec3 _9771 = bvec3(_9746);
            vec3 _9772 = vec3(_9771.x ? _9770.x : vec3(0.0).x, _9771.y ? _9770.y : vec3(0.0).y, _9771.z ? _9770.z : vec3(0.0).z);
            if (_9737 && _9746)
            {
                _27150 = (_9759 + _9772) * 0.5;
                break;
            }
            bvec3 _9785 = bvec3(_9737);
            _27150 = vec3(_9785.x ? _9759.x : _9772.x, _9785.y ? _9759.y : _9772.y, _9785.z ? _9759.z : _9772.z);
            break;
        } while(false);
        ivec2 _5854 = ivec2(0, _5839);
        vec3 _27157;
        do
        {
            float _27153;
            do
            {
                vec2 _11084 = (((vec2(clamp(_5605 + _5854, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _11087 = ivec2(floor(_11084));
                vec2 _11091 = _11084 - vec2(_11087);
                ivec2 _11266 = clamp(_11087, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11302 = clamp(_11087 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11338 = clamp(_11087 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11374 = clamp(_11087 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11054 = vec4((1.0 - _11091.x) * (1.0 - _11091.y), _11091.x * (1.0 - _11091.y), (1.0 - _11091.x) * _11091.y, _11091.x * _11091.y);
                vec4 _11132 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11266.x, int((FluidSize.y - 1u) - uint(_11266.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11302.x, int((FluidSize.y - 1u) - uint(_11302.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11338.x, int((FluidSize.y - 1u) - uint(_11338.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11374.x, int((FluidSize.y - 1u) - uint(_11374.y)), 0).xy, 0).x);
                vec4 _11055 = _11132;
                float _29536;
                _29536 = 1000000000.0;
                float _29878;
                SPIRV_CROSS_UNROLL
                for (int _27151 = 0; _27151 < 4; _29536 = _29878, _27151++)
                {
                    if (_11055[_27151] > 0.0)
                    {
                        _29878 = min(_29536, _11055[_27151]);
                    }
                    else
                    {
                        _29878 = _29536;
                    }
                }
                vec4 _11058 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27152 = 0; _27152 < 4; )
                {
                    _11058[_27152] = ((_11055[_27152] > 0.0) && (_11055[_27152] < (_29536 + max(0.25, 0.02999999932944774627685546875 * _29536)))) ? _11054[_27152] : 0.0;
                    _27152++;
                    continue;
                }
                float _11192 = ((_11058.x + _11058.y) + _11058.z) + _11058.w;
                if (_11192 <= 9.9999997473787516355514526367188e-05)
                {
                    _27153 = 0.0;
                    break;
                }
                _27153 = dot(_11058, _11132) / _11192;
                break;
            } while(false);
            float _27156;
            do
            {
                vec2 _11614 = (((vec2(clamp(_5605 - _5854, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _11617 = ivec2(floor(_11614));
                vec2 _11621 = _11614 - vec2(_11617);
                ivec2 _11796 = clamp(_11617, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11832 = clamp(_11617 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11868 = clamp(_11617 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11904 = clamp(_11617 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11584 = vec4((1.0 - _11621.x) * (1.0 - _11621.y), _11621.x * (1.0 - _11621.y), (1.0 - _11621.x) * _11621.y, _11621.x * _11621.y);
                vec4 _11662 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11796.x, int((FluidSize.y - 1u) - uint(_11796.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11832.x, int((FluidSize.y - 1u) - uint(_11832.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11868.x, int((FluidSize.y - 1u) - uint(_11868.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11904.x, int((FluidSize.y - 1u) - uint(_11904.y)), 0).xy, 0).x);
                vec4 _11585 = _11662;
                float _29533;
                _29533 = 1000000000.0;
                float _29877;
                SPIRV_CROSS_UNROLL
                for (int _27154 = 0; _27154 < 4; _29533 = _29877, _27154++)
                {
                    if (_11585[_27154] > 0.0)
                    {
                        _29877 = min(_29533, _11585[_27154]);
                    }
                    else
                    {
                        _29877 = _29533;
                    }
                }
                vec4 _11588 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27155 = 0; _27155 < 4; )
                {
                    _11588[_27155] = ((_11585[_27155] > 0.0) && (_11585[_27155] < (_29533 + max(0.25, 0.02999999932944774627685546875 * _29533)))) ? _11584[_27155] : 0.0;
                    _27155++;
                    continue;
                }
                float _11722 = ((_11588.x + _11588.y) + _11588.z) + _11588.w;
                if (_11722 <= 9.9999997473787516355514526367188e-05)
                {
                    _27156 = 0.0;
                    break;
                }
                _27156 = dot(_11588, _11662) / _11722;
                break;
            } while(false);
            bool _10960 = (_27153 > 0.0) && (abs(_27153 - _27142) < _5845);
            bool _10969 = (_27156 > 0.0) && (abs(_27142 - _27156) < _5845);
            vec2 _10976 = vec2(_5605 + _5854) + vec2(0.5);
            float _12110 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10980 = vec3(((_10976.x - (0.5 * float(_283.OutputWidth))) * _27153) / _12110, ((-(_10976.y - (0.5 * float(_283.OutputHeight)))) * _27153) / _12110, _27153) - _9693;
            bvec3 _10981 = bvec3(_10960);
            vec3 _10982 = vec3(_10981.x ? _10980.x : vec3(0.0).x, _10981.y ? _10980.y : vec3(0.0).y, _10981.z ? _10980.z : vec3(0.0).z);
            vec2 _10990 = vec2(_5605 - _5854) + vec2(0.5);
            float _12149 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10993 = _9693 - vec3(((_10990.x - (0.5 * float(_283.OutputWidth))) * _27156) / _12149, ((-(_10990.y - (0.5 * float(_283.OutputHeight)))) * _27156) / _12149, _27156);
            bvec3 _10994 = bvec3(_10969);
            vec3 _10995 = vec3(_10994.x ? _10993.x : vec3(0.0).x, _10994.y ? _10993.y : vec3(0.0).y, _10994.z ? _10993.z : vec3(0.0).z);
            if (_10960 && _10969)
            {
                _27157 = (_10982 + _10995) * 0.5;
                break;
            }
            bvec3 _11008 = bvec3(_10960);
            _27157 = vec3(_11008.x ? _10982.x : _10995.x, _11008.y ? _10982.y : _10995.y, _11008.z ? _10982.z : _10995.z);
            break;
        } while(false);
        vec2 _5865 = _27150.xy * max(_283.NormalScale, 0.0);
        vec3 _26257 = _27150;
        _26257.x = _5865.x;
        _26257.y = _5865.y;
        vec2 _5875 = _27157.xy * max(_283.NormalScale, 0.0);
        vec3 _26261 = _27157;
        _26261.x = _5875.x;
        _26261.y = _5875.y;
        bvec3 _29900 = bvec3(length(_26257) < 9.9999999747524270787835121154785e-07);
        bvec3 _29902 = bvec3(length(_26261) < 9.9999999747524270787835121154785e-07);
        vec3 _5893 = normalize(cross(vec3(_29900.x ? vec3(1.0, 0.0, 0.0).x : _26257.x, _29900.y ? vec3(1.0, 0.0, 0.0).y : _26257.y, _29900.z ? vec3(1.0, 0.0, 0.0).z : _26257.z), vec3(_29902.x ? vec3(0.0, -1.0, 0.0).x : _26261.x, _29902.y ? vec3(0.0, -1.0, 0.0).y : _26261.y, _29902.z ? vec3(0.0, -1.0, 0.0).z : _26261.z)));
        vec3 _27186;
        if (dot(_5893, _5823) < 0.0)
        {
            _27186 = -_5893;
        }
        else
        {
            _27186 = _5893;
        }
        vec3 _27194;
        if (_283.Mode == 2u)
        {
            vec4 _27180;
            do
            {
                float _27171;
                do
                {
                    vec2 _12347 = ((_5610 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                    ivec2 _12350 = ivec2(floor(_12347));
                    vec2 _12354 = _12347 - vec2(_12350);
                    ivec2 _12529 = clamp(_12350, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12565 = clamp(_12350 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12601 = clamp(_12350 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12637 = clamp(_12350 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12317 = vec4((1.0 - _12354.x) * (1.0 - _12354.y), _12354.x * (1.0 - _12354.y), (1.0 - _12354.x) * _12354.y, _12354.x * _12354.y);
                    vec4 _12395 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12529.x, int((FluidSize.y - 1u) - uint(_12529.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12565.x, int((FluidSize.y - 1u) - uint(_12565.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12601.x, int((FluidSize.y - 1u) - uint(_12601.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12637.x, int((FluidSize.y - 1u) - uint(_12637.y)), 0).xy, 0).x);
                    vec4 _12318 = _12395;
                    float _29530;
                    _29530 = 1000000000.0;
                    float _29876;
                    SPIRV_CROSS_UNROLL
                    for (int _27169 = 0; _27169 < 4; _29530 = _29876, _27169++)
                    {
                        if (_12318[_27169] > 0.0)
                        {
                            _29876 = min(_29530, _12318[_27169]);
                        }
                        else
                        {
                            _29876 = _29530;
                        }
                    }
                    vec4 _12321 = vec4(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _27170 = 0; _27170 < 4; )
                    {
                        _12321[_27170] = ((_12318[_27170] > 0.0) && (_12318[_27170] < (_29530 + max(0.25, 0.02999999932944774627685546875 * _29530)))) ? _12317[_27170] : 0.0;
                        _27170++;
                        continue;
                    }
                    float _12455 = ((_12321.x + _12321.y) + _12321.z) + _12321.w;
                    if (_12455 <= 9.9999997473787516355514526367188e-05)
                    {
                        _27171 = 0.0;
                        break;
                    }
                    _27171 = dot(_12321, _12395) / _12455;
                    break;
                } while(false);
                if (_27171 <= 0.0)
                {
                    _27180 = vec4(0.0);
                    break;
                }
                float _12194 = clamp(((max(_283.ShadeParams.y, 0.001000000047497451305389404296875) * ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _27171) * _283.EnvParams.y, 1.0, 48.0);
                vec2 _12204 = vec2(FluidSize) / vec2(float(_283.OutputWidth), float(_283.OutputHeight));
                float _12207 = max(0.25, 0.02999999932944774627685546875 * _27171);
                float _27173;
                vec4 _27174;
                _27174 = vec4(0.0);
                _27173 = 0.0;
                float _29629;
                vec4 _29630;
                SPIRV_CROSS_UNROLL
                for (int _27172 = 0; _27172 < 13; _27174 = _29630, _27173 = _29629, _27172++)
                {
                    vec2 _27175;
                    if (_27172 > 0)
                    {
                        float _12231 = (float((_27172 <= 6) ? _27172 : (_27172 - 6)) * 1.0471975803375244140625) + ((_27172 <= 6) ? 0.0 : 0.52359998226165771484375);
                        _27175 = vec2(cos(_12231), sin(_12231)) * (((_27172 <= 6) ? 0.5 : 1.0) * _12194);
                    }
                    else
                    {
                        _27175 = vec2(0.0);
                    }
                    ivec2 _12248 = ivec2(floor((_5610 + _27175) * _12204));
                    ivec2 _12823 = clamp(_12248, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12849 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12823.x, int((FluidSize.y - 1u) - uint(_12823.y)), 0).xy, 0);
                    float _12850 = _12849.x;
                    if ((_12850 <= 0.0) || (abs(_12850 - _27171) > _12207))
                    {
                        _29630 = _27174;
                        _29629 = _27173;
                        continue;
                    }
                    ivec2 _12859 = clamp(_12248, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12885 = texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12859.x, int((FluidSize.y - 1u) - uint(_12859.y)), 0).xy, 0);
                    float _12277 = exp((-2.0) * (dot(_27175, _27175) / max(_12194 * _12194, 9.9999997473787516355514526367188e-05))) * _12885.w;
                    _29630 = _27174 + (_12885 * _12277);
                    _29629 = _27173 + _12277;
                }
                if (_27173 <= 9.9999997473787516355514526367188e-05)
                {
                    _27180 = vec4(0.0);
                    break;
                }
                vec4 _12297 = _27174 / vec4(_27173);
                _12297.w = min(_12297.w, 1.0);
                _27180 = _12297;
                break;
            } while(false);
            float _5910 = length(_27180.xyz);
            vec3 _27195;
            if (_5910 > 0.001000000047497451305389404296875)
            {
                vec3 _5918 = _27180.xyz / vec3(_5910);
                vec3 _27190;
                if (dot(_5918, _5823) < 0.0)
                {
                    _27190 = -_5918;
                }
                else
                {
                    _27190 = _5918;
                }
                _27195 = normalize(mix(_27186, _27190, vec3(clamp(_27180.w * 4.0, 0.0, 1.0))));
            }
            else
            {
                _27195 = _27186;
            }
            _27194 = _27195;
        }
        else
        {
            _27194 = _27186;
        }
        float _5940 = max(_27143.x, 9.9999997473787516355514526367188e-05);
        ivec2 _12980 = clamp(ivec2(floor((_5610 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _12990 = min(_283.ThicknessShift, 1u);
        uint _12998 = ((FluidSize.x + (1u << _12990)) - 1u) >> _12990;
        ivec2 _13018 = clamp(_12980 >> ivec2(uvec2(_12990)), ivec2(0), ivec2(int(_12998), int(((FluidSize.y + (1u << _12990)) - 1u) >> _12990)) - ivec2(1));
        uint _13028 = (uint(_13018.x) + (uint(_13018.y) * _12998)) * 4u;
        uint _13038 = AuxBits_1._data[_13028 + 1u];
        uint _13050 = AuxBits_1._data[_13028 + 3u];
        float _27192;
        _27192 = float(AuxBits_1._data[_13028]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _27191 = 0; _27191 < 4; )
        {
            ivec2 _12915 = ivec2((_27191 * 2) - 1, 0);
            ivec2 _12920 = ivec2(0, ((_27191 - 2) * 2) - 1);
            bvec2 _12921 = bvec2(_27191 < 2);
            uint _13063 = min(_283.ThicknessShift, 1u);
            uint _13071 = ((FluidSize.x + (1u << _13063)) - 1u) >> _13063;
            ivec2 _13091 = clamp((_12980 + ivec2(_12921.x ? _12915.x : _12920.x, _12921.y ? _12915.y : _12920.y)) >> ivec2(uvec2(_13063)), ivec2(0), ivec2(int(_13071), int(((FluidSize.y + (1u << _13063)) - 1u) >> _13063)) - ivec2(1));
            uint _13136 = min(_283.ThicknessShift, 1u);
            uint _13144 = ((FluidSize.x + (1u << _13136)) - 1u) >> _13136;
            ivec2 _13164 = clamp((_12980 + ivec2(((_27191 & 1) != int(0u)) ? 1 : (-1), ((_27191 & 2) != int(0u)) ? 1 : (-1))) >> ivec2(uvec2(_13136)), ivec2(0), ivec2(int(_13144), int(((FluidSize.y + (1u << _13136)) - 1u) >> _13136)) - ivec2(1));
            _27192 += ((float(AuxBits_1._data[(uint(_13091.x) + (uint(_13091.y) * _13071)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_13164.x) + (uint(_13164.y) * _13144)) * 4u]) * 1.525902189314365386962890625e-05));
            _27191++;
            continue;
        }
        float _5959 = clamp((float(_13050) * 0.0009765625) / _5940, 0.0, 1.0);
        vec3 _13221 = ((_283.CameraRight.xyz * _5821.x) + (_283.CameraUp.xyz * _5821.y)) + (_283.CameraForward.xyz * _5821.z);
        vec3 _5965 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * _9679) + (_283.CameraUp.xyz * _9691)) + (_283.CameraForward.xyz * _27142));
        vec3 _13274 = ((_283.CameraRight.xyz * _27194.x) + (_283.CameraUp.xyz * _27194.y)) + (_283.CameraForward.xyz * _27194.z);
        float _5976 = (_27142 / ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_27194, _5823)), 0.07999999821186065673828125);
        float _6000 = (_283.RippleParams.x * mix(_283.RippleFlow.y, 1.0 + _283.RippleFlow.x, clamp((float(AuxBits_1._data[_13028 + 2u]) * 0.0009765625) / _5940, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _13274.y) * clamp(1.0 - (length(_9693) / max(_283.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _27200;
        vec3 _27244;
        if (_6000 > 0.0)
        {
            vec2 _6006 = _5965.xz;
            float _13304 = max(_283.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _27197;
            float _27198;
            _27198 = 0.0;
            _27197 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _27196 = 0; _27196 < 6; )
            {
                float _13316 = _13304 * _1922[_27196];
                float _13321 = _6000 * pow(0.7799999713897705078125, float(_27196));
                float _13328 = clamp((_13316 / max(_5976 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _13404 = (((uint(_27196) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _13412 = ((_13404 >> ((_13404 >> 28u) + 4u)) ^ _13404) * 277803737u;
                float _13335 = 0.60000002384185791015625 + (float(((_13412 >> 22u) ^ _13412) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _13340 = vec2(cos(_13335), sin(_13335));
                uint _13431 = (((uint(_27196) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _13439 = ((_13431 >> ((_13431 >> 28u) + 4u)) ^ _13431) * 277803737u;
                _27198 += (((0.5 * _13321) * _13321) * (1.0 - (_13328 * _13328)));
                _27197 += (_13340 * ((_13321 * _13328) * cos(((dot(_13340, _6006) * (6.283185482025146484375 / _13316)) + ((sqrt(61.63805389404296875 / _13316) * _283.RippleParams.z) * _283.ShadeParams.z)) + (float(((_13439 >> 22u) ^ _13439) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _27196++;
                continue;
            }
            vec3 _6021 = normalize(_13274 + vec3(-_27197.x, 0.0, -_27197.y));
            vec3 _6023 = normalize(vec3(dot(_6021, _283.CameraRight.xyz), dot(_6021, _283.CameraUp.xyz), dot(_6021, _283.CameraForward.xyz)));
            bvec3 _29906 = bvec3(dot(_6023, _5823) < 0.0199999995529651641845703125);
            _27244 = vec3(_29906.x ? _27194.x : _6023.x, _29906.y ? _27194.y : _6023.y, _29906.z ? _27194.z : _6023.z);
            _27200 = _27198;
        }
        else
        {
            _27244 = _27194;
            _27200 = 0.0;
        }
        float _6035 = _27200 * _283.RippleFlow.z;
        if (_283.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_5602), vec4((_27244 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_283.DebugView == 2u)
        {
            float _6058 = clamp(_27142 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5602), vec4(_6058, _6058, _6058, 1.0));
            break;
        }
        float _6078 = (_7522 < 100000000.0) ? (max(_7522 - _27142, 0.0) / max(_5821.z, 0.0500000007450580596923828125)) : 10000.0;
        float _6083 = _5940 * max(_283.ThicknessScale, 0.0);
        float _6088 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _6083 / max(_6078, 0.001000000047497451305389404296875));
        float _27242;
        do
        {
            vec3 _13495 = (_5965 + (_29897 * (max(_5965.y - _283.PoolMin.y, 0.0500000007450580596923828125) / max(-_29897.y, 0.20000000298023223876953125)))) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
            float _13561 = dot(_13495, _283.CameraForward.xyz);
            if (_13561 <= 0.100000001490116119384765625)
            {
                _27242 = 1.0;
                break;
            }
            float _13602 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _13578 = ((dot(_13495, _283.CameraRight.xyz) * _13602) / _13561) + (0.5 * float(_283.OutputWidth));
            float _13591 = (((-dot(_13495, _283.CameraUp.xyz)) * _13602) / _13561) + (0.5 * float(_283.OutputHeight));
            vec2 _13592 = vec2(_13578, _13591);
            if (!((((_13578 >= 0.0) && (_13591 >= 0.0)) && (_13578 < float(_283.OutputWidth))) && (_13591 < float(_283.OutputHeight))))
            {
                _27242 = 1.0;
                break;
            }
            ivec2 _13686 = clamp(ivec2(_13592), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _13638 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13686.x, int((_283.OutputHeight - 1u) - uint(_13686.y)), 0).xy, 0);
            float _13639 = _13638.x;
            if (abs((((_13639 > 0.0) && (abs(_13639 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_13639 - _283.ProjA)) : 1000000000.0) - _13561) > ((0.1500000059604644775390625 * _13561) + 0.100000001490116119384765625))
            {
                _27242 = 1.0;
                break;
            }
            ivec2 _13746 = clamp(ivec2(_13592), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            _27242 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_13746.x, int((_283.OutputHeight - 1u) - uint(_13746.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _6094 = refract(_5821, _27244, 0.75187969207763671875);
        bvec3 _29910 = bvec3(dot(_6094, _6094) < 9.9999999747524270787835121154785e-07);
        vec3 _6113 = normalize(mix(_5821, vec3(_29910.x ? _5821.x : _6094.x, _29910.y ? _5821.y : _6094.y, _29910.z ? _5821.z : _6094.z), vec3(clamp(_283.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _6088))));
        vec3 _13798 = ((_283.CameraRight.xyz * _6113.x) + (_283.CameraUp.xyz * _6113.y)) + (_283.CameraForward.xyz * _6113.z);
        float _27259;
        do
        {
            if ((((_5965.x < _283.PoolMin.x) || (_5965.x > _283.PoolMax.x)) || (_5965.z < _283.PoolMin.z)) || (_5965.z > _283.PoolMax.z))
            {
                _27259 = 1000.0;
                break;
            }
            float _27247;
            if (_13798.x > 9.9999997473787516355514526367188e-05)
            {
                _27247 = (_283.PoolMax.x - _5965.x) / _13798.x;
            }
            else
            {
                float _27248;
                if (_13798.x < (-9.9999997473787516355514526367188e-05))
                {
                    _27248 = (_283.PoolMin.x - _5965.x) / _13798.x;
                }
                else
                {
                    _27248 = 1000.0;
                }
                _27247 = _27248;
            }
            float _29912 = (_27247 < 1000.0) ? _27247 : 1000.0;
            float _27251;
            if (_13798.z > 9.9999997473787516355514526367188e-05)
            {
                _27251 = (_283.PoolMax.z - _5965.z) / _13798.z;
            }
            else
            {
                float _27252;
                if (_13798.z < (-9.9999997473787516355514526367188e-05))
                {
                    _27252 = (_283.PoolMin.z - _5965.z) / _13798.z;
                }
                else
                {
                    _27252 = 1000.0;
                }
                _27251 = _27252;
            }
            float _29913 = (_27251 < _29912) ? _27251 : _29912;
            float _27257;
            if (_13798.y < (-9.9999997473787516355514526367188e-05))
            {
                float _13928 = (_283.PoolMin.y - _5965.y) / _13798.y;
                _27257 = (_13928 < _29913) ? _13928 : _29913;
            }
            else
            {
                _27257 = _29913;
            }
            _27259 = max(_27257, 0.0);
            break;
        } while(false);
        float _6132 = clamp(max((_6078 * 1.2000000476837158203125) + 0.20000000298023223876953125, (_27259 < 100.0) ? ((_27259 * 1.10000002384185791015625) + 0.100000001490116119384765625) : 0.0), 0.300000011920928955078125, max(_283.MaxTravel, 0.300000011920928955078125) * 2.0);
        vec3 _27613;
        float _27669;
        float _27725;
        float _27780;
        vec2 _27835;
        do
        {
            ivec2 _14270 = clamp(ivec2(_5610), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _14222 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14270.x, int((_283.OutputHeight - 1u) - uint(_14270.y)), 0).xy, 0);
            float _14223 = _14222.x;
            vec2 _27528;
            float _27558;
            _27558 = ((_14223 > 0.0) && (abs(_14223 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14223 - _283.ProjA)) : 1000000000.0;
            _27528 = _5610;
            float _14018;
            float _29917;
            vec2 _29919;
            bool _27267;
            float _27587;
            vec3 _27614;
            float _27670;
            float _27726;
            float _27781;
            vec2 _27836;
            int _27260 = 1;
            float _27262 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_27260 <= 16)
                {
                    float _14012 = float(_27260) * 0.0625;
                    _14018 = (_6132 * _14012) * sqrt(_14012);
                    vec3 _14023 = _9693 + (_6113 * _14018);
                    if (_14023.z <= 0.0500000007450580596923828125)
                    {
                        _27836 = _27870;
                        _27781 = _27704;
                        _27726 = _27704;
                        _27670 = _27704;
                        _27614 = _27648;
                        _27587 = 0.0;
                        _27267 = false;
                        break;
                    }
                    float _14340 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _14316 = ((_14023.x * _14340) / _14023.z) + (0.5 * float(_283.OutputWidth));
                    float _14329 = (((-_14023.y) * _14340) / _14023.z) + (0.5 * float(_283.OutputHeight));
                    vec2 _14330 = vec2(_14316, _14329);
                    if (!((((_14316 >= 0.0) && (_14329 >= 0.0)) && (_14316 < float(_283.OutputWidth))) && (_14329 < float(_283.OutputHeight))))
                    {
                        _27836 = _27870;
                        _27781 = _27704;
                        _27726 = _27704;
                        _27670 = _27704;
                        _27614 = _27648;
                        _27587 = 0.0;
                        _27267 = false;
                        break;
                    }
                    ivec2 _14424 = clamp(ivec2(_14330), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _14376 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14424.x, int((_283.OutputHeight - 1u) - uint(_14424.y)), 0).xy, 0);
                    float _14377 = _14376.x;
                    float _14394 = ((_14377 > 0.0) && (abs(_14377 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14377 - _283.ProjA)) : 1000000000.0;
                    if (_14023.z >= _14394)
                    {
                        ivec2 _14045 = ivec2(_14330);
                        ivec2 _14505 = clamp(ivec2(floor(((vec2(_14045) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14514 = clamp(_14505, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _14540 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14514.x, int((FluidSize.y - 1u) - uint(_14514.y)), 0).xy, 0);
                        float _14541 = _14540.x;
                        ivec2 _14550 = clamp(_14505, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14637 = clamp(_14045, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _14589 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14637.x, int((_283.OutputHeight - 1u) - uint(_14637.y)), 0).xy, 0);
                        float _14590 = _14589.x;
                        if (!(((_14541 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14550.x, int((FluidSize.y - 1u) - uint(_14550.y)), 0).xy, 0).w > 0.5)) && ((((_14590 > 0.0) && (abs(_14590 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14590 - _283.ProjA)) : 1000000000.0) >= (_14541 + 0.0500000007450580596923828125))))
                        {
                            _27836 = _27870;
                            _27781 = _27704;
                            _27726 = _27704;
                            _27670 = _27704;
                            _27614 = _27648;
                            _27587 = 0.0;
                            _27267 = false;
                            break;
                        }
                        float _27264;
                        _27264 = _14018;
                        float _29915;
                        float _29916;
                        int _27263 = 0;
                        float _27265 = _27262;
                        SPIRV_CROSS_UNROLL
                        for (; _27263 < 5; _27265 = _29915, _27264 = _29916, _27263++)
                        {
                            float _14060 = 0.5 * (_27265 + _27264);
                            vec3 _14065 = _9693 + (_6113 * _14060);
                            float _14707 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _14768 = clamp(ivec2(vec2(((_14065.x * _14707) / _14065.z) + (0.5 * float(_283.OutputWidth)), (((-_14065.y) * _14707) / _14065.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                            vec4 _14720 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14768.x, int((_283.OutputHeight - 1u) - uint(_14768.y)), 0).xy, 0);
                            float _14721 = _14720.x;
                            bool _14072 = _14065.z >= (((_14721 > 0.0) && (abs(_14721 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14721 - _283.ProjA)) : 1000000000.0);
                            _29915 = _14072 ? _27265 : _14060;
                            _29916 = _14072 ? _14060 : _27264;
                        }
                        vec3 _14086 = _9693 + (_6113 * _27264);
                        float _14838 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _14097 = clamp(vec2(((_14086.x * _14838) / _14086.z) + (0.5 * float(_283.OutputWidth)), (((-_14086.y) * _14838) / _14086.z) + (0.5 * float(_283.OutputHeight))), vec2(0.5), vec2(float(_283.OutputWidth), float(_283.OutputHeight)) - vec2(0.5));
                        ivec2 _14899 = clamp(ivec2(_14097), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _14851 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14899.x, int((_283.OutputHeight - 1u) - uint(_14899.y)), 0).xy, 0);
                        float _14852 = _14851.x;
                        float _14869 = ((_14852 > 0.0) && (abs(_14852 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14852 - _283.ProjA)) : 1000000000.0;
                        float _14977 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        _27836 = _14097;
                        _27781 = _14869;
                        _27726 = _27264;
                        _27670 = 1.0;
                        _27614 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_14097.x - (0.5 * float(_283.OutputWidth))) * _14869) / _14977)) + (_283.CameraUp.xyz * (((-(_14097.y - (0.5 * float(_283.OutputHeight)))) * _14869) / _14977))) + (_283.CameraForward.xyz * _14869));
                        _27587 = 1.0;
                        _27267 = true;
                        break;
                    }
                    ivec2 _14121 = ivec2(_14330 - vec2(0.5));
                    ivec2 _15050 = clamp(ivec2(floor(((vec2(_14121) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15059 = clamp(_15050, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _15085 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15059.x, int((FluidSize.y - 1u) - uint(_15059.y)), 0).xy, 0);
                    float _15086 = _15085.x;
                    ivec2 _15095 = clamp(_15050, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15182 = clamp(_14121, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _15134 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15182.x, int((_283.OutputHeight - 1u) - uint(_15182.y)), 0).xy, 0);
                    float _15135 = _15134.x;
                    ivec2 _14126 = ivec2(_14330 + vec2(0.5));
                    ivec2 _15263 = clamp(ivec2(floor(((vec2(_14126) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15272 = clamp(_15263, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _15298 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15272.x, int((FluidSize.y - 1u) - uint(_15272.y)), 0).xy, 0);
                    float _15299 = _15298.x;
                    ivec2 _15308 = clamp(_15263, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15395 = clamp(_14126, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _15347 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15395.x, int((_283.OutputHeight - 1u) - uint(_15395.y)), 0).xy, 0);
                    float _15348 = _15347.x;
                    bool _14128 = (((_15086 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15095.x, int((FluidSize.y - 1u) - uint(_15095.y)), 0).xy, 0).w > 0.5)) && ((((_15135 > 0.0) && (abs(_15135 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15135 - _283.ProjA)) : 1000000000.0) >= (_15086 + 0.0500000007450580596923828125))) && (((_15299 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15308.x, int((FluidSize.y - 1u) - uint(_15308.y)), 0).xy, 0).w > 0.5)) && ((((_15348 > 0.0) && (abs(_15348 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15348 - _283.ProjA)) : 1000000000.0) >= (_15299 + 0.0500000007450580596923828125)));
                    _29917 = _14128 ? _14394 : _27558;
                    bvec2 _29918 = bvec2(_14128);
                    _29919 = vec2(_29918.x ? _14330.x : _27528.x, _29918.y ? _14330.y : _27528.y);
                    _27260++;
                    _27558 = _29917;
                    _27528 = _29919;
                    _27262 = _14018;
                    continue;
                }
                else
                {
                    _27836 = _27870;
                    _27781 = _27704;
                    _27726 = _27704;
                    _27670 = _27704;
                    _27614 = _27648;
                    _27587 = 0.0;
                    _27267 = false;
                    break;
                }
            }
            if (_27267)
            {
                _27835 = _27836;
                _27780 = _27781;
                _27725 = _27726;
                _27669 = _27670;
                _27613 = _27614;
                break;
            }
            vec3 _15447 = ((_283.CameraRight.xyz * _6113.x) + (_283.CameraUp.xyz * _6113.y)) + (_283.CameraForward.xyz * _6113.z);
            float _27282;
            vec3 _27283;
            do
            {
                if ((((_5965.x < _283.PoolMin.x) || (_5965.x > _283.PoolMax.x)) || (_5965.z < _283.PoolMin.z)) || (_5965.z > _283.PoolMax.z))
                {
                    _27283 = vec3(0.0, 1.0, 0.0);
                    _27282 = 1000.0;
                    break;
                }
                float _27270;
                if (_15447.x > 9.9999997473787516355514526367188e-05)
                {
                    _27270 = (_283.PoolMax.x - _5965.x) / _15447.x;
                }
                else
                {
                    float _27271;
                    if (_15447.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _27271 = (_283.PoolMin.x - _5965.x) / _15447.x;
                    }
                    else
                    {
                        _27271 = 1000.0;
                    }
                    _27270 = _27271;
                }
                bool _15509 = _27270 < 1000.0;
                vec3 _27287;
                if (_15509)
                {
                    _27287 = vec3((_15447.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                }
                else
                {
                    _27287 = vec3(0.0, 1.0, 0.0);
                }
                float _29920 = _15509 ? _27270 : 1000.0;
                float _27274;
                if (_15447.z > 9.9999997473787516355514526367188e-05)
                {
                    _27274 = (_283.PoolMax.z - _5965.z) / _15447.z;
                }
                else
                {
                    float _27275;
                    if (_15447.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _27275 = (_283.PoolMin.z - _5965.z) / _15447.z;
                    }
                    else
                    {
                        _27275 = 1000.0;
                    }
                    _27274 = _27275;
                }
                bool _15547 = _27274 < _29920;
                vec3 _27285;
                if (_15547)
                {
                    _27285 = vec3(0.0, 0.0, _29958);
                }
                else
                {
                    _27285 = _27287;
                }
                float _29921 = _15547 ? _27274 : _29920;
                float _27280;
                vec3 _27284;
                if (_15447.y < (-9.9999997473787516355514526367188e-05))
                {
                    float _15567 = (_283.PoolMin.y - _5965.y) / _15447.y;
                    bool _15570 = _15567 < _29921;
                    bvec3 _29922 = bvec3(_15570);
                    _27284 = vec3(_29922.x ? vec3(0.0, 1.0, 0.0).x : _27285.x, _29922.y ? vec3(0.0, 1.0, 0.0).y : _27285.y, _29922.z ? vec3(0.0, 1.0, 0.0).z : _27285.z);
                    _27280 = _15570 ? _15567 : _29921;
                }
                else
                {
                    _27284 = _27285;
                    _27280 = _29921;
                }
                _27283 = _27284;
                _27282 = max(_27280, 0.0);
                break;
            } while(false);
            if (_27282 < 100.0)
            {
                vec3 _14153 = _5965 + (_15447 * _27282);
                uint _27430;
                vec2 _27511;
                float _27518;
                do
                {
                    bool _27292;
                    vec2 _27293;
                    float _27294;
                    do
                    {
                        vec3 _15821 = _14153 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                        float _15879 = dot(_15821, _283.CameraForward.xyz);
                        if (_15879 <= 0.0500000007450580596923828125)
                        {
                            _27294 = 1000000000.0;
                            _27293 = vec2(0.5);
                            _27292 = false;
                            break;
                        }
                        float _15920 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _15896 = ((dot(_15821, _283.CameraRight.xyz) * _15920) / _15879) + (0.5 * float(_283.OutputWidth));
                        float _15909 = (((-dot(_15821, _283.CameraUp.xyz)) * _15920) / _15879) + (0.5 * float(_283.OutputHeight));
                        if (!((((_15896 >= 0.0) && (_15909 >= 0.0)) && (_15896 < float(_283.OutputWidth))) && (_15909 < float(_283.OutputHeight))))
                        {
                            _27294 = 1000000000.0;
                            _27293 = vec2(0.5);
                            _27292 = false;
                            break;
                        }
                        vec2 _15838 = floor(vec2(_15896, _15909)) + vec2(0.5);
                        ivec2 _16004 = clamp(ivec2(_15838), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _15956 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16004.x, int((_283.OutputHeight - 1u) - uint(_16004.y)), 0).xy, 0);
                        float _15957 = _15956.x;
                        float _15974 = ((_15957 > 0.0) && (abs(_15957 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15957 - _283.ProjA)) : 1000000000.0;
                        _27294 = _15974;
                        _27293 = _15838;
                        _27292 = abs(_15974 - _15879) < ((0.0199999995529651641845703125 * _15879) + 0.02999999932944774627685546875);
                        break;
                    } while(false);
                    if (_27292)
                    {
                        _27518 = _27294;
                        _27511 = _27293;
                        _27430 = 1u;
                        break;
                    }
                    vec3 _15627 = _14153 - vec3(_283.CameraRight.w, _27130, _283.CameraForward.w);
                    vec3 _15635 = (_283.PoolMin.xyz + _283.PoolMax.xyz) * 0.5;
                    vec3 _15640 = vec3((_15627.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15645 = vec3(0.0, 0.0, (_15627.z >= 0.0) ? 1.0 : (-1.0));
                    vec3 _15652 = vec3((_15635.x >= _14153.x) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15659 = vec3(0.0, 0.0, (_15635.z >= _14153.z) ? 1.0 : (-1.0));
                    vec3 _27295;
                    vec3 _27296;
                    vec3 _27301;
                    vec3 _27303;
                    if (abs(_27283.y) > 0.5)
                    {
                        bool _15671 = abs(_15627.z) >= abs(_15627.x);
                        bvec3 _15675 = bvec3(_15671);
                        bvec3 _15680 = bvec3(_15671);
                        bvec3 _15685 = bvec3(_15671);
                        bvec3 _15690 = bvec3(_15671);
                        _27303 = vec3(_15675.x ? _15645.x : _15640.x, _15675.y ? _15645.y : _15640.y, _15675.z ? _15645.z : _15640.z);
                        _27301 = vec3(_15685.x ? _15640.x : _15645.x, _15685.y ? _15640.y : _15645.y, _15685.z ? _15640.z : _15645.z);
                        _27296 = vec3(_15690.x ? _15659.x : _15652.x, _15690.y ? _15659.y : _15652.y, _15690.z ? _15659.z : _15652.z);
                        _27295 = vec3(_15680.x ? _15652.x : _15659.x, _15680.y ? _15652.y : _15659.y, _15680.z ? _15652.z : _15659.z);
                    }
                    else
                    {
                        bool _15696 = abs(_27283.x) > 0.5;
                        bvec3 _15700 = bvec3(_15696);
                        bvec3 _15705 = bvec3(_15696);
                        _27303 = vec3(_15700.x ? _15645.x : _15640.x, _15700.y ? _15645.y : _15640.y, _15700.z ? _15645.z : _15640.z);
                        _27301 = vec3(0.0, 1.0, 0.0);
                        _27296 = vec3(_15705.x ? _15659.x : _15652.x, _15705.y ? _15659.y : _15652.y, _15705.z ? _15659.z : _15652.z);
                        _27295 = vec3(0.0, 1.0, 0.0);
                    }
                    float _16087 = ((_27295.y > 0.5) ? (_283.PoolMin.w - _14153.y) : ((_27295.x > 0.5) ? (_283.PoolMax.x - _14153.x) : ((_27295.x < (-0.5)) ? (_14153.x - _283.PoolMin.x) : ((_27295.z > 0.5) ? (_283.PoolMax.z - _14153.z) : (_14153.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                    float _16131 = ((_27296.y > 0.5) ? (_283.PoolMin.w - _14153.y) : ((_27296.x > 0.5) ? (_283.PoolMax.x - _14153.x) : ((_27296.x < (-0.5)) ? (_14153.x - _283.PoolMin.x) : ((_27296.z > 0.5) ? (_283.PoolMax.z - _14153.z) : (_14153.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                    vec2 _27513;
                    _27513 = _27293;
                    bool _27416;
                    uint _27431;
                    vec2 _27512;
                    vec2 _27515;
                    float _27519;
                    float _27522;
                    bool _29686;
                    int _27297 = 0;
                    bool _27320;
                    float _27520 = _27294;
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_27297 < 11)
                        {
                            if (_27297 == 1)
                            {
                                vec3 _15724 = _14153 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                float _16157 = dot(_15724, _283.CameraForward.xyz);
                                vec2 _27516;
                                float _27523;
                                if (_16157 > 0.0500000007450580596923828125)
                                {
                                    float _16198 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _16174 = ((dot(_15724, _283.CameraRight.xyz) * _16198) / _16157) + (0.5 * float(_283.OutputWidth));
                                    float _16187 = (((-dot(_15724, _283.CameraUp.xyz)) * _16198) / _16157) + (0.5 * float(_283.OutputHeight));
                                    vec2 _16188 = vec2(_16174, _16187);
                                    ivec2 _15735 = ivec2(_16188);
                                    ivec2 _16272 = clamp(ivec2(floor(((vec2(_15735) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16281 = clamp(_16272, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _16307 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_16281.x, int((FluidSize.y - 1u) - uint(_16281.y)), 0).xy, 0);
                                    float _16308 = _16307.x;
                                    ivec2 _16317 = clamp(_16272, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16404 = clamp(_15735, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _16356 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16404.x, int((_283.OutputHeight - 1u) - uint(_16404.y)), 0).xy, 0);
                                    float _16357 = _16356.x;
                                    vec2 _27517;
                                    float _27524;
                                    if (((((_16174 >= 0.0) && (_16187 >= 0.0)) && (_16174 < float(_283.OutputWidth))) && (_16187 < float(_283.OutputHeight))) && (((_16308 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_16317.x, int((FluidSize.y - 1u) - uint(_16317.y)), 0).xy, 0).w > 0.5)) && ((((_16357 > 0.0) && (abs(_16357 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16357 - _283.ProjA)) : 1000000000.0) >= (_16308 + 0.0500000007450580596923828125))))
                                    {
                                        vec2 _15742 = floor(_16188) + vec2(0.5);
                                        ivec2 _16495 = clamp(ivec2(_15742), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _16447 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16495.x, int((_283.OutputHeight - 1u) - uint(_16495.y)), 0).xy, 0);
                                        float _16448 = _16447.x;
                                        float _16465 = ((_16448 > 0.0) && (abs(_16448 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16448 - _283.ProjA)) : 1000000000.0;
                                        if (_16465 < _16157)
                                        {
                                            _27519 = _16465;
                                            _27512 = _15742;
                                            _27431 = 2u;
                                            _27416 = true;
                                            break;
                                        }
                                        _27524 = _16465;
                                        _27517 = _15742;
                                    }
                                    else
                                    {
                                        _27524 = _27520;
                                        _27517 = _27513;
                                    }
                                    _27523 = _27524;
                                    _27516 = _27517;
                                }
                                else
                                {
                                    _27523 = _27520;
                                    _27516 = _27513;
                                }
                                _29686 = _27320;
                                _27522 = _27523;
                                _27515 = _27516;
                                int _15802 = _27297 + 1;
                                _27320 = _29686;
                                _27297 = _15802;
                                _27520 = _27522;
                                _27513 = _27515;
                                continue;
                            }
                            bool _15756 = _27297 == 2;
                            bool _15758 = _27297 >= 3;
                            int _15760 = _27297 & 1;
                            bool _15763 = _15756 || (_15758 && (_15760 == 0));
                            bool _15765 = _27297 < 3;
                            int _15767 = _27297 - 3;
                            float _15772 = _15765 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_15767 / 2)));
                            if (_15772 >= (_15763 ? _16131 : _16087))
                            {
                                _29686 = _27320;
                                _27522 = _27520;
                                _27515 = _27513;
                                int _15802 = _27297 + 1;
                                _27320 = _29686;
                                _27297 = _15802;
                                _27520 = _27522;
                                _27513 = _27515;
                                continue;
                            }
                            bvec3 _15785 = bvec3(_15763);
                            vec3 _15789 = _14153 + (vec3(_15785.x ? _27296.x : _27295.x, _15785.y ? _27296.y : _27295.y, _15785.z ? _27296.z : _27295.z) * _15772);
                            bvec3 _15793 = bvec3(_15763);
                            vec3 _15794 = vec3(_15793.x ? _27301.x : _27303.x, _15793.y ? _27301.y : _27303.y, _15793.z ? _27301.z : _27303.z);
                            bool _27317;
                            vec2 _27391;
                            float _27395;
                            do
                            {
                                float _16658 = ((_15794.y > 0.5) ? (_283.PoolMin.w - _15789.y) : ((_15794.x > 0.5) ? (_283.PoolMax.x - _15789.x) : ((_15794.x < (-0.5)) ? (_15789.x - _283.PoolMin.x) : ((_15794.z > 0.5) ? (_283.PoolMax.z - _15789.z) : (_15789.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _27392;
                                _27392 = vec2(0.5);
                                float _16564;
                                float _16606;
                                vec2 _27308;
                                float _27309;
                                bool _27314;
                                bool _27318;
                                int _27304 = 0;
                                float _27305 = 0.0;
                                float _27306 = 0.0599999986588954925537109375;
                                float _27396 = 1000000000.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if ((_27304 < 40) && (_27305 < _16658))
                                    {
                                        _16564 = min(_27305 + _27306, _16658);
                                        bool _27307;
                                        do
                                        {
                                            vec3 _16672 = (_15789 + (_15794 * _16564)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                            float _16730 = dot(_16672, _283.CameraForward.xyz);
                                            if (_16730 <= 0.0500000007450580596923828125)
                                            {
                                                _27309 = 1000000000.0;
                                                _27308 = vec2(0.5);
                                                _27307 = false;
                                                break;
                                            }
                                            float _16771 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            float _16747 = ((dot(_16672, _283.CameraRight.xyz) * _16771) / _16730) + (0.5 * float(_283.OutputWidth));
                                            float _16760 = (((-dot(_16672, _283.CameraUp.xyz)) * _16771) / _16730) + (0.5 * float(_283.OutputHeight));
                                            if (!((((_16747 >= 0.0) && (_16760 >= 0.0)) && (_16747 < float(_283.OutputWidth))) && (_16760 < float(_283.OutputHeight))))
                                            {
                                                _27309 = 1000000000.0;
                                                _27308 = vec2(0.5);
                                                _27307 = false;
                                                break;
                                            }
                                            vec2 _16689 = floor(vec2(_16747, _16760)) + vec2(0.5);
                                            ivec2 _16855 = clamp(ivec2(_16689), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                            vec4 _16807 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16855.x, int((_283.OutputHeight - 1u) - uint(_16855.y)), 0).xy, 0);
                                            float _16808 = _16807.x;
                                            float _16825 = ((_16808 > 0.0) && (abs(_16808 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16808 - _283.ProjA)) : 1000000000.0;
                                            _27309 = _16825;
                                            _27308 = _16689;
                                            _27307 = abs(_16825 - _16730) < ((0.0199999995529651641845703125 * _16730) + 0.02999999932944774627685546875);
                                            break;
                                        } while(false);
                                        if (_27307)
                                        {
                                            vec2 _27393;
                                            float _27397;
                                            _27397 = _27309;
                                            _27393 = _27308;
                                            float _29925;
                                            float _29926;
                                            float _29927;
                                            vec2 _29929;
                                            int _27313 = 0;
                                            float _27398 = _27305;
                                            float _27400 = _16564;
                                            SPIRV_CROSS_UNROLL
                                            for (; _27313 < 4; _27400 = _29925, _27398 = _29926, _27397 = _29927, _27393 = _29929, _27313++)
                                            {
                                                float _16582 = 0.5 * (_27398 + _27400);
                                                bool _27401;
                                                vec2 _27402;
                                                float _27403;
                                                do
                                                {
                                                    vec3 _16899 = (_15789 + (_15794 * _16582)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                    float _16957 = dot(_16899, _283.CameraForward.xyz);
                                                    if (_16957 <= 0.0500000007450580596923828125)
                                                    {
                                                        _27403 = 1000000000.0;
                                                        _27402 = vec2(0.5);
                                                        _27401 = false;
                                                        break;
                                                    }
                                                    float _16998 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                    float _16974 = ((dot(_16899, _283.CameraRight.xyz) * _16998) / _16957) + (0.5 * float(_283.OutputWidth));
                                                    float _16987 = (((-dot(_16899, _283.CameraUp.xyz)) * _16998) / _16957) + (0.5 * float(_283.OutputHeight));
                                                    if (!((((_16974 >= 0.0) && (_16987 >= 0.0)) && (_16974 < float(_283.OutputWidth))) && (_16987 < float(_283.OutputHeight))))
                                                    {
                                                        _27403 = 1000000000.0;
                                                        _27402 = vec2(0.5);
                                                        _27401 = false;
                                                        break;
                                                    }
                                                    vec2 _16916 = floor(vec2(_16974, _16987)) + vec2(0.5);
                                                    ivec2 _17082 = clamp(ivec2(_16916), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                    vec4 _17034 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17082.x, int((_283.OutputHeight - 1u) - uint(_17082.y)), 0).xy, 0);
                                                    float _17035 = _17034.x;
                                                    float _17052 = ((_17035 > 0.0) && (abs(_17035 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_17035 - _283.ProjA)) : 1000000000.0;
                                                    _27403 = _17052;
                                                    _27402 = _16916;
                                                    _27401 = abs(_17052 - _16957) < ((0.0199999995529651641845703125 * _16957) + 0.02999999932944774627685546875);
                                                    break;
                                                } while(false);
                                                _29925 = _27401 ? _16582 : _27400;
                                                _29926 = _27401 ? _27398 : _16582;
                                                _29927 = _27401 ? _27403 : _27397;
                                                bvec2 _29928 = bvec2(_27401);
                                                _29929 = vec2(_29928.x ? _27402.x : _27393.x, _29928.y ? _27402.y : _27393.y);
                                            }
                                            _27395 = _27397;
                                            _27391 = _27393;
                                            _27318 = true;
                                            _27314 = true;
                                            break;
                                        }
                                        _16606 = min(_27306 * 1.2000000476837158203125, 0.300000011920928955078125);
                                        _27306 = _16606;
                                        _27305 = _16564;
                                        _27304++;
                                        _27396 = _27309;
                                        _27392 = _27308;
                                        continue;
                                    }
                                    else
                                    {
                                        _27395 = _27396;
                                        _27391 = _27392;
                                        _27318 = _27320;
                                        _27314 = false;
                                        break;
                                    }
                                }
                                if (_27314)
                                {
                                    _27317 = _27318;
                                    break;
                                }
                                _27317 = false;
                                break;
                            } while(false);
                            if (_27317)
                            {
                                _27519 = _27395;
                                _27512 = _27391;
                                _27431 = 1u;
                                _27416 = true;
                                break;
                            }
                            _29686 = _27317;
                            _27522 = _27395;
                            _27515 = _27391;
                            int _15802 = _27297 + 1;
                            _27320 = _29686;
                            _27297 = _15802;
                            _27520 = _27522;
                            _27513 = _27515;
                            continue;
                        }
                        else
                        {
                            _27519 = _27520;
                            _27512 = _27513;
                            _27431 = _27470;
                            _27416 = false;
                            break;
                        }
                    }
                    if (_27416)
                    {
                        _27518 = _27519;
                        _27511 = _27512;
                        _27430 = _27431;
                        break;
                    }
                    _27518 = _27519;
                    _27511 = _27512;
                    _27430 = 0u;
                    break;
                } while(false);
                if (_27430 != 0u)
                {
                    float _17160 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec3 _14174 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_27511.x - (0.5 * float(_283.OutputWidth))) * _27518) / _17160)) + (_283.CameraUp.xyz * (((-(_27511.y - (0.5 * float(_283.OutputHeight)))) * _27518) / _17160))) + (_283.CameraForward.xyz * _27518));
                    bvec3 _14175 = bvec3(_27430 == 1u);
                    vec3 _14176 = vec3(_14175.x ? _14153.x : _14174.x, _14175.y ? _14153.y : _14174.y, _14175.z ? _14153.z : _14174.z);
                    _27835 = _27511;
                    _27780 = _27518;
                    _27725 = (_27430 == 1u) ? _27282 : length(_14176 - _5965);
                    _27669 = 1.0;
                    _27613 = _14176;
                    break;
                }
            }
            float _14198 = min(_27282, _6132);
            _27835 = _27528;
            _27780 = _27558;
            _27725 = _14198;
            _27669 = _27587;
            _27613 = _5965 + (_15447 * _14198);
            break;
        } while(false);
        vec3 _27935;
        vec3 _27936;
        do
        {
            vec2 _17397 = clamp(_27835 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _17408 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17397.x, 1.0 - _17397.y), 0.0).xyz;
            if ((_27669 < 0.5) || (_27780 > 100000000.0))
            {
                _27936 = vec3(0.0, 1.0, 0.5);
                _27935 = _17408;
                break;
            }
            ivec2 _17231 = ivec2(_27835);
            float _17237 = _5965.y - _27613.y;
            if (_17237 <= 0.00999999977648258209228515625)
            {
                _27936 = vec3(0.0, 1.0, 0.5);
                _27935 = _17408;
                break;
            }
            ivec2 _17438 = clamp(_17231, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            ivec2 _17526 = clamp(_17231, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec3 _17486 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_17526.x, int((_283.OutputHeight - 1u) - uint(_17526.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _17488 = length(_17486);
            vec3 _17494 = _17486 / vec3(_17488);
            bvec3 _17495 = bvec3(_17488 > 0.001000000047497451305389404296875);
            ivec2 _17586 = clamp(_17231, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            float _17283 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_17438.x, int((_283.OutputHeight - 1u) - uint(_17438.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_17495.x ? _17494.x : vec3(0.0, 1.0, 0.0).x, _17495.y ? _17494.y : vec3(0.0, 1.0, 0.0).y, _17495.z ? _17494.z : vec3(0.0, 1.0, 0.0).z), _5666), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_17586.x, int((_283.OutputHeight - 1u) - uint(_17586.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _283.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_17408, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _17287 = max(-_29897.y, 0.20000000298023223876953125);
            vec3 _17301 = mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _17237) / vec3(_17287)), vec3(_283.LightParams.x));
            float _27934;
            if ((_283.CausticParams.x > 0.0) && (_17283 > 0.0))
            {
                vec2 _17330 = _27613.xz - ((_29897.xz / vec2(_17287)) * _17237);
                vec2 _17641 = _17330 * (6.283185482025146484375 / max(_283.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _17664 = _17641 + (vec2(sin((_17641.y * 0.730000019073486328125) + ((_283.ShadeParams.z * 0.89999997615814208984375) * _283.CausticParams.z)), sin((_17641.x * 0.61000001430511474609375) - ((_283.ShadeParams.z * 1.10000002384185791015625) * _283.CausticParams.z))) * 0.89999997615814208984375);
                float _27933;
                do
                {
                    if ((_283.GridParams.z < 2u) || (_283.GridParams.w < 2u))
                    {
                        _27933 = 1.0;
                        break;
                    }
                    vec2 _17774 = clamp(((_17330 - _283.PoolMin.xz) / max(_283.PoolMax.xz - _283.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)), vec2(0.0), vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _17776 = uvec2(_17774);
                    vec2 _17780 = _17774 - vec2(_17776);
                    uint _17787 = _17776.x + (_17776.y * _283.GridParams.z);
                    _27933 = mix(mix(CausticField_1._data[_17787], CausticField_1._data[_17787 + 1u], _17780.x), mix(CausticField_1._data[_17787 + _283.GridParams.z], CausticField_1._data[(_17787 + _283.GridParams.z) + 1u], _17780.x), _17780.y);
                    break;
                } while(false);
                _27934 = min(max(1.0 + ((_283.CausticParams.x * (exp((-_17237) / max(_283.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _17237))) * ((pow(clamp((1.0 - abs(sin(_17664.x + ((_283.ShadeParams.z * 0.60000002384185791015625) * _283.CausticParams.z)) * sin((_17664.y * 1.12999999523162841796875) - ((_283.ShadeParams.z * 0.4000000059604644775390625) * _283.CausticParams.z)))) * (1.0 - abs(sin(((_17664.x + _17664.y) * 0.709999978542327880859375) - ((_283.ShadeParams.z * 0.5) * _283.CausticParams.z)) * sin(((_17664.x - _17664.y) * 0.829999983310699462890625) + ((_283.ShadeParams.z * 0.699999988079071044921875) * _283.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_27933, 0.0500000007450580596923828125), _283.LightParams.y), 2.5);
            }
            else
            {
                _27934 = 1.0;
            }
            _27936 = vec3(_17283, dot(_17301, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _27934);
            _27935 = _17408 * ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _17237) * 1.2000000476837158203125), vec3(_283.LightParams.x)) * (1.0 - _17283)) + ((_17301 * _17283) * _27934));
            break;
        } while(false);
        float _6153 = mix(_6083, max(_27725, _6083), _6088);
        vec3 _29042;
        if (_6088 < 0.5)
        {
            vec3 _17844 = ((_283.CameraRight.xyz * _6113.x) + (_283.CameraUp.xyz * _6113.y)) + (_283.CameraForward.xyz * _6113.z);
            vec3 _29043;
            if (_17844.y < (-0.0199999995529651641845703125))
            {
                float _6170 = (_283.PoolMin.w - _5965.y) / _17844.y;
                vec3 _6175 = _5965 + (_17844 * _6170);
                vec3 _29044;
                if (((_6170 > _6083) && ((((_6175.x > _283.PoolMin.x) && (_6175.x < _283.PoolMax.x)) && (_6175.z > _283.PoolMin.z)) && (_6175.z < _283.PoolMax.z))) && ((_27669 < 0.5) || (_6170 < _27725)))
                {
                    float _17900 = length(_6175 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w)) / ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _17905 = _283.RippleParams.x * _283.RippleFlow.y;
                    vec2 _17907 = _6175.xz;
                    float _18039 = max(_283.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _27941;
                    _27941 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _27940 = 0; _27940 < 6; )
                    {
                        float _18051 = _18039 * _1922[_27940];
                        uint _18139 = (((uint(_27940) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _18147 = ((_18139 >> ((_18139 >> 28u) + 4u)) ^ _18139) * 277803737u;
                        float _18070 = 0.60000002384185791015625 + (float(((_18147 >> 22u) ^ _18147) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _18075 = vec2(cos(_18070), sin(_18070));
                        uint _18166 = (((uint(_27940) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _18174 = ((_18166 >> ((_18166 >> 28u) + 4u)) ^ _18166) * 277803737u;
                        _27941 += (_18075 * (((_17905 * pow(0.7799999713897705078125, float(_27940))) * clamp((_18051 / max(_17900 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_18075, _17907) * (6.283185482025146484375 / _18051)) + ((sqrt(61.63805389404296875 / _18051) * _283.RippleParams.z) * _283.ShadeParams.z)) + (float(((_18174 >> 22u) ^ _18174) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _27940++;
                        continue;
                    }
                    vec3 _17918 = normalize(vec3(-_27941.x, 1.0, -_27941.y));
                    vec3 _17921 = _6175 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                    float _18194 = dot(_17921, _283.CameraRight.xyz);
                    float _18199 = dot(_17921, _283.CameraUp.xyz);
                    float _18204 = dot(_17921, _283.CameraForward.xyz);
                    vec3 _18205 = vec3(_18194, _18199, _18204);
                    vec3 _17925 = reflect(_17844, _17918);
                    float _18222 = dot(_17925, _283.CameraForward.xyz);
                    vec3 _18223 = vec3(dot(_17925, _283.CameraRight.xyz), dot(_17925, _283.CameraUp.xyz), _18222);
                    float _27966;
                    vec3 _28058;
                    do
                    {
                        if (_283.ReflectionDistance <= 0.0)
                        {
                            _28058 = vec3(0.0);
                            _27966 = 0.0;
                            break;
                        }
                        float _18272 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _18222);
                        if (_18272 <= 0.0)
                        {
                            _28058 = vec3(0.0);
                            _27966 = 0.0;
                            break;
                        }
                        float _18469 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _18281 = max(0.0040000001899898052215576171875 * _18204, 0.00999999977648258209228515625);
                        float _18283 = 0.5 * _18281;
                        float _18292;
                        float _18294;
                        bool _27951;
                        bool _27956;
                        float _27961;
                        float _27967;
                        vec3 _28059;
                        int _27942 = 0;
                        float _27943 = _18283;
                        float _27944 = _18281;
                        float _27962 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_27942 < 80)
                            {
                                _18292 = _27943 + _27944;
                                _18294 = _27944 * 1.07000005245208740234375;
                                if (_18292 > _283.ReflectionDistance)
                                {
                                    _28059 = vec3(0.0);
                                    _27967 = 0.0;
                                    _27961 = _27962;
                                    _27956 = false;
                                    _27951 = true;
                                    break;
                                }
                                vec3 _18305 = _18205 + (_18223 * _18292);
                                if (_18305.z <= 0.0500000007450580596923828125)
                                {
                                    _28059 = vec3(0.0);
                                    _27967 = 0.0;
                                    _27961 = _27962;
                                    _27956 = false;
                                    _27951 = true;
                                    break;
                                }
                                float _18509 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _18485 = ((_18305.x * _18509) / _18305.z) + (0.5 * float(_283.OutputWidth));
                                float _18498 = (((-_18305.y) * _18509) / _18305.z) + (0.5 * float(_283.OutputHeight));
                                if (!((((_18485 >= 0.0) && (_18498 >= 0.0)) && (_18485 < float(_283.OutputWidth))) && (_18498 < float(_283.OutputHeight))))
                                {
                                    _28059 = vec3(0.0);
                                    _27967 = _27704;
                                    _27961 = _27962;
                                    _27956 = true;
                                    _27951 = false;
                                    break;
                                }
                                ivec2 _18593 = clamp(ivec2(vec2(_18485, _18498)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _18545 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18593.x, int((_283.OutputHeight - 1u) - uint(_18593.y)), 0).xy, 0);
                                float _18546 = _18545.x;
                                float _18563 = ((_18546 > 0.0) && (abs(_18546 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18546 - _283.ProjA)) : 1000000000.0;
                                if (((_18563 < 100000000.0) && (_18305.z > _18563)) && ((_18305.z - _18563) < max(_27944 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _27946;
                                    _27946 = _18292;
                                    float _29930;
                                    float _29931;
                                    int _27945 = 0;
                                    float _27949 = _27943;
                                    SPIRV_CROSS_UNROLL
                                    for (; _27945 < 6; _27949 = _29930, _27946 = _29931, _27945++)
                                    {
                                        float _18349 = 0.5 * (_27949 + _27946);
                                        vec3 _18354 = _18205 + (_18223 * _18349);
                                        float _18663 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _18724 = clamp(ivec2(vec2(((_18354.x * _18663) / _18354.z) + (0.5 * float(_283.OutputWidth)), (((-_18354.y) * _18663) / _18354.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _18676 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18724.x, int((_283.OutputHeight - 1u) - uint(_18724.y)), 0).xy, 0);
                                        float _18677 = _18676.x;
                                        bool _18361 = _18354.z > (((_18677 > 0.0) && (abs(_18677 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18677 - _283.ProjA)) : 1000000000.0);
                                        _29930 = _18361 ? _27949 : _18349;
                                        _29931 = _18361 ? _18349 : _27946;
                                    }
                                    vec3 _18375 = _18205 + (_18223 * _27946);
                                    float _18794 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _18784 = vec2(((_18375.x * _18794) / _18375.z) + (0.5 * float(_283.OutputWidth)), (((-_18375.y) * _18794) / _18375.z) + (0.5 * float(_283.OutputHeight)));
                                    ivec2 _18852 = clamp(ivec2(_18784), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec3 _18812 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_18852.x, int((_283.OutputHeight - 1u) - uint(_18852.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _18814 = length(_18812);
                                    vec3 _18820 = _18812 / vec3(_18814);
                                    bvec3 _18821 = bvec3(_18814 > 0.001000000047497451305389404296875);
                                    vec3 _18822 = vec3(_18821.x ? _18820.x : vec3(0.0, 1.0, 0.0).x, _18821.y ? _18820.y : vec3(0.0, 1.0, 0.0).y, _18821.z ? _18820.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_18822, _283.CameraRight.xyz), dot(_18822, _283.CameraUp.xyz), dot(_18822, _283.CameraForward.xyz)), _18223) > 0.100000001490116119384765625)
                                    {
                                        int _18402 = _27942 + 1;
                                        _27944 = _18294;
                                        _27943 = _18292;
                                        _27942 = _18402;
                                        _27962 = _18292;
                                        continue;
                                    }
                                    float _18916 = clamp(((0.0199999995529651641845703125 * _27946) * _18469) / max(_18375.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _18918 = 0.5 * _18916;
                                    vec2 _19003 = clamp(_18784 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19029 = clamp((_18784 + vec2(_18918)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19055 = clamp((_18784 + vec2(_18916 * (-0.5), _18918)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19081 = clamp((_18784 + vec2(_18918, _18916 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19107 = clamp((_18784 + vec2(_18916 * (-0.5), _18916 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19133 = clamp((_18784 + vec2(_18916, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19159 = clamp((_18784 + vec2(-_18916, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19185 = clamp((_18784 + vec2(0.0, _18916)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19211 = clamp((_18784 + vec2(0.0, -_18916)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _18988 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19003.x, 1.0 - _19003.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19029.x, 1.0 - _19029.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19055.x, 1.0 - _19055.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19081.x, 1.0 - _19081.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19107.x, 1.0 - _19107.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19133.x, 1.0 - _19133.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19159.x, 1.0 - _19159.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19185.x, 1.0 - _19185.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19211.x, 1.0 - _19211.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _28059 = _18988;
                                    _27967 = _18272;
                                    _27961 = _18292;
                                    _27956 = false;
                                    _27951 = true;
                                    break;
                                }
                                int _18402 = _27942 + 1;
                                _27944 = _18294;
                                _27943 = _18292;
                                _27942 = _18402;
                                _27962 = _18292;
                                continue;
                            }
                            else
                            {
                                _28059 = vec3(0.0);
                                _27967 = _27704;
                                _27961 = _27962;
                                _27956 = false;
                                _27951 = false;
                                break;
                            }
                        }
                        if (_27951)
                        {
                            _28058 = _28059;
                            _27966 = _27967;
                            break;
                        }
                        if ((!_27956) || (_27961 < 0.0))
                        {
                            _28058 = _28059;
                            _27966 = 0.0;
                            break;
                        }
                        vec3 _18417 = _18205 + (_18223 * _27961);
                        float _19262 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _19252 = vec2(((_18417.x * _19262) / _18417.z) + (0.5 * float(_283.OutputWidth)), (((-_18417.y) * _19262) / _18417.z) + (0.5 * float(_283.OutputHeight)));
                        ivec2 _19323 = clamp(ivec2(_19252), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _19275 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19323.x, int((_283.OutputHeight - 1u) - uint(_19323.y)), 0).xy, 0);
                        float _19276 = _19275.x;
                        float _19293 = ((_19276 > 0.0) && (abs(_19276 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_19276 - _283.ProjA)) : 1000000000.0;
                        float _19369 = clamp(((0.0199999995529651641845703125 * _27961) * _18469) / max(_18417.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _19371 = 0.5 * _19369;
                        vec2 _19456 = clamp(_19252 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19482 = clamp((_19252 + vec2(_19371)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19508 = clamp((_19252 + vec2(_19369 * (-0.5), _19371)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19534 = clamp((_19252 + vec2(_19371, _19369 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19560 = clamp((_19252 + vec2(_19369 * (-0.5), _19369 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19586 = clamp((_19252 + vec2(_19369, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19612 = clamp((_19252 + vec2(-_19369, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19638 = clamp((_19252 + vec2(0.0, _19369)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19664 = clamp((_19252 + vec2(0.0, -_19369)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _19441 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19456.x, 1.0 - _19456.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19482.x, 1.0 - _19482.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19508.x, 1.0 - _19508.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19534.x, 1.0 - _19534.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19560.x, 1.0 - _19560.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19586.x, 1.0 - _19586.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19612.x, 1.0 - _19612.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19638.x, 1.0 - _19638.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19664.x, 1.0 - _19664.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_19293 > 100000000.0)
                        {
                            _28058 = _19441;
                            _27966 = _18272;
                            break;
                        }
                        float _18442 = _19293 - _18417.z;
                        if (_18442 <= 0.0)
                        {
                            _28058 = _19441;
                            _27966 = _18272;
                            break;
                        }
                        _28058 = _19441;
                        _27966 = _18272 * exp((_18442 / max(_18222, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _28064;
                    do
                    {
                        if (_283.EnvParams.x <= 0.0)
                        {
                            float _19718 = _17925.y;
                            _28064 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _19718))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _19718)));
                            break;
                        }
                        _28064 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_17925.x, _17925.y, -_17925.z), 0.0).xyz * _283.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _17939 = refract(_17844, _17918, 0.75187969207763671875);
                    bvec3 _29932 = bvec3(dot(_17939, _17939) < 9.9999999747524270787835121154785e-07);
                    vec3 _29933 = vec3(_29932.x ? _17844.x : _17939.x, _29932.y ? _17844.y : _17939.y, _29932.z ? _17844.z : _17939.z);
                    float _19744 = dot(_29933, _283.CameraRight.xyz);
                    float _19749 = dot(_29933, _283.CameraUp.xyz);
                    float _19754 = dot(_29933, _283.CameraForward.xyz);
                    vec3 _19755 = vec3(_19744, _19749, _19754);
                    float _19795 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _19785 = vec2(((_18194 * _19795) / _18204) + (0.5 * float(_283.OutputWidth)), (((-_18199) * _19795) / _18204) + (0.5 * float(_283.OutputHeight)));
                    vec3 _28503;
                    float _28601;
                    float _28699;
                    float _28797;
                    vec2 _28895;
                    do
                    {
                        ivec2 _20126 = clamp(ivec2(_19785), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _20078 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20126.x, int((_283.OutputHeight - 1u) - uint(_20126.y)), 0).xy, 0);
                        float _20079 = _20078.x;
                        vec2 _28418;
                        float _28448;
                        _28448 = ((_20079 > 0.0) && (abs(_20079 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20079 - _283.ProjA)) : 1000000000.0;
                        _28418 = _19785;
                        float _19874;
                        float _29936;
                        vec2 _29938;
                        bool _28073;
                        float _28477;
                        vec3 _28504;
                        float _28602;
                        float _28700;
                        float _28798;
                        vec2 _28896;
                        int _28066 = 1;
                        float _28068 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_28066 <= 16)
                            {
                                float _19867 = float(_28066);
                                _19874 = (_19867 * 0.25) * sqrt(_19867 * 0.0625);
                                vec3 _19879 = _18205 + (_19755 * _19874);
                                if (_19879.z <= 0.0500000007450580596923828125)
                                {
                                    _28896 = _27870;
                                    _28798 = _27704;
                                    _28700 = _27704;
                                    _28602 = _27704;
                                    _28504 = _27648;
                                    _28477 = 0.0;
                                    _28073 = false;
                                    break;
                                }
                                float _20196 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _20172 = ((_19879.x * _20196) / _19879.z) + (0.5 * float(_283.OutputWidth));
                                float _20185 = (((-_19879.y) * _20196) / _19879.z) + (0.5 * float(_283.OutputHeight));
                                vec2 _20186 = vec2(_20172, _20185);
                                if (!((((_20172 >= 0.0) && (_20185 >= 0.0)) && (_20172 < float(_283.OutputWidth))) && (_20185 < float(_283.OutputHeight))))
                                {
                                    _28896 = _27870;
                                    _28798 = _27704;
                                    _28700 = _27704;
                                    _28602 = _27704;
                                    _28504 = _27648;
                                    _28477 = 0.0;
                                    _28073 = false;
                                    break;
                                }
                                ivec2 _20280 = clamp(ivec2(_20186), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _20232 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20280.x, int((_283.OutputHeight - 1u) - uint(_20280.y)), 0).xy, 0);
                                float _20233 = _20232.x;
                                float _20250 = ((_20233 > 0.0) && (abs(_20233 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20233 - _283.ProjA)) : 1000000000.0;
                                if (_19879.z >= _20250)
                                {
                                    ivec2 _19901 = ivec2(_20186);
                                    ivec2 _20361 = clamp(ivec2(floor(((vec2(_19901) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20370 = clamp(_20361, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _20396 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20370.x, int((FluidSize.y - 1u) - uint(_20370.y)), 0).xy, 0);
                                    float _20397 = _20396.x;
                                    ivec2 _20406 = clamp(_20361, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20493 = clamp(_19901, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _20445 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20493.x, int((_283.OutputHeight - 1u) - uint(_20493.y)), 0).xy, 0);
                                    float _20446 = _20445.x;
                                    if (!(((_20397 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20406.x, int((FluidSize.y - 1u) - uint(_20406.y)), 0).xy, 0).w > 0.5)) && ((((_20446 > 0.0) && (abs(_20446 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20446 - _283.ProjA)) : 1000000000.0) >= (_20397 + 0.0500000007450580596923828125))))
                                    {
                                        _28896 = _27870;
                                        _28798 = _27704;
                                        _28700 = _27704;
                                        _28602 = _27704;
                                        _28504 = _27648;
                                        _28477 = 0.0;
                                        _28073 = false;
                                        break;
                                    }
                                    float _28070;
                                    _28070 = _19874;
                                    float _29934;
                                    float _29935;
                                    int _28069 = 0;
                                    float _28071 = _28068;
                                    SPIRV_CROSS_UNROLL
                                    for (; _28069 < 5; _28071 = _29934, _28070 = _29935, _28069++)
                                    {
                                        float _19916 = 0.5 * (_28071 + _28070);
                                        vec3 _19921 = _18205 + (_19755 * _19916);
                                        float _20563 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _20624 = clamp(ivec2(vec2(((_19921.x * _20563) / _19921.z) + (0.5 * float(_283.OutputWidth)), (((-_19921.y) * _20563) / _19921.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _20576 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20624.x, int((_283.OutputHeight - 1u) - uint(_20624.y)), 0).xy, 0);
                                        float _20577 = _20576.x;
                                        bool _19928 = _19921.z >= (((_20577 > 0.0) && (abs(_20577 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20577 - _283.ProjA)) : 1000000000.0);
                                        _29934 = _19928 ? _28071 : _19916;
                                        _29935 = _19928 ? _19916 : _28070;
                                    }
                                    vec3 _19942 = _18205 + (_19755 * _28070);
                                    float _20694 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _19953 = clamp(vec2(((_19942.x * _20694) / _19942.z) + (0.5 * float(_283.OutputWidth)), (((-_19942.y) * _20694) / _19942.z) + (0.5 * float(_283.OutputHeight))), vec2(0.5), vec2(float(_283.OutputWidth), float(_283.OutputHeight)) - vec2(0.5));
                                    ivec2 _20755 = clamp(ivec2(_19953), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _20707 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20755.x, int((_283.OutputHeight - 1u) - uint(_20755.y)), 0).xy, 0);
                                    float _20708 = _20707.x;
                                    float _20725 = ((_20708 > 0.0) && (abs(_20708 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20708 - _283.ProjA)) : 1000000000.0;
                                    float _20833 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    _28896 = _19953;
                                    _28798 = _20725;
                                    _28700 = _28070;
                                    _28602 = 1.0;
                                    _28504 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_19953.x - (0.5 * float(_283.OutputWidth))) * _20725) / _20833)) + (_283.CameraUp.xyz * (((-(_19953.y - (0.5 * float(_283.OutputHeight)))) * _20725) / _20833))) + (_283.CameraForward.xyz * _20725));
                                    _28477 = 1.0;
                                    _28073 = true;
                                    break;
                                }
                                ivec2 _19977 = ivec2(_20186 - vec2(0.5));
                                ivec2 _20906 = clamp(ivec2(floor(((vec2(_19977) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20915 = clamp(_20906, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _20941 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20915.x, int((FluidSize.y - 1u) - uint(_20915.y)), 0).xy, 0);
                                float _20942 = _20941.x;
                                ivec2 _20951 = clamp(_20906, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21038 = clamp(_19977, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _20990 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21038.x, int((_283.OutputHeight - 1u) - uint(_21038.y)), 0).xy, 0);
                                float _20991 = _20990.x;
                                ivec2 _19982 = ivec2(_20186 + vec2(0.5));
                                ivec2 _21119 = clamp(ivec2(floor(((vec2(_19982) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21128 = clamp(_21119, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _21154 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_21128.x, int((FluidSize.y - 1u) - uint(_21128.y)), 0).xy, 0);
                                float _21155 = _21154.x;
                                ivec2 _21164 = clamp(_21119, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21251 = clamp(_19982, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _21203 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21251.x, int((_283.OutputHeight - 1u) - uint(_21251.y)), 0).xy, 0);
                                float _21204 = _21203.x;
                                bool _19984 = (((_20942 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20951.x, int((FluidSize.y - 1u) - uint(_20951.y)), 0).xy, 0).w > 0.5)) && ((((_20991 > 0.0) && (abs(_20991 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20991 - _283.ProjA)) : 1000000000.0) >= (_20942 + 0.0500000007450580596923828125))) && (((_21155 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_21164.x, int((FluidSize.y - 1u) - uint(_21164.y)), 0).xy, 0).w > 0.5)) && ((((_21204 > 0.0) && (abs(_21204 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21204 - _283.ProjA)) : 1000000000.0) >= (_21155 + 0.0500000007450580596923828125)));
                                _29936 = _19984 ? _20250 : _28448;
                                bvec2 _29937 = bvec2(_19984);
                                _29938 = vec2(_29937.x ? _20186.x : _28418.x, _29937.y ? _20186.y : _28418.y);
                                _28066++;
                                _28448 = _29936;
                                _28418 = _29938;
                                _28068 = _19874;
                                continue;
                            }
                            else
                            {
                                _28896 = _27870;
                                _28798 = _27704;
                                _28700 = _27704;
                                _28602 = _27704;
                                _28504 = _27648;
                                _28477 = 0.0;
                                _28073 = false;
                                break;
                            }
                        }
                        if (_28073)
                        {
                            _28895 = _28896;
                            _28797 = _28798;
                            _28699 = _28700;
                            _28601 = _28602;
                            _28503 = _28504;
                            break;
                        }
                        vec3 _21303 = ((_283.CameraRight.xyz * _19744) + (_283.CameraUp.xyz * _19749)) + (_283.CameraForward.xyz * _19754);
                        float _28088;
                        vec3 _28089;
                        do
                        {
                            if ((((_6175.x < _283.PoolMin.x) || (_6175.x > _283.PoolMax.x)) || (_6175.z < _283.PoolMin.z)) || (_6175.z > _283.PoolMax.z))
                            {
                                _28089 = vec3(0.0, 1.0, 0.0);
                                _28088 = 1000.0;
                                break;
                            }
                            float _28076;
                            if (_21303.x > 9.9999997473787516355514526367188e-05)
                            {
                                _28076 = (_283.PoolMax.x - _6175.x) / _21303.x;
                            }
                            else
                            {
                                float _28077;
                                if (_21303.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _28077 = (_283.PoolMin.x - _6175.x) / _21303.x;
                                }
                                else
                                {
                                    _28077 = 1000.0;
                                }
                                _28076 = _28077;
                            }
                            bool _21365 = _28076 < 1000.0;
                            vec3 _28093;
                            if (_21365)
                            {
                                _28093 = vec3((_21303.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                            }
                            else
                            {
                                _28093 = vec3(0.0, 1.0, 0.0);
                            }
                            float _29939 = _21365 ? _28076 : 1000.0;
                            float _28080;
                            if (_21303.z > 9.9999997473787516355514526367188e-05)
                            {
                                _28080 = (_283.PoolMax.z - _6175.z) / _21303.z;
                            }
                            else
                            {
                                float _28081;
                                if (_21303.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _28081 = (_283.PoolMin.z - _6175.z) / _21303.z;
                                }
                                else
                                {
                                    _28081 = 1000.0;
                                }
                                _28080 = _28081;
                            }
                            bool _21403 = _28080 < _29939;
                            vec3 _28091;
                            if (_21403)
                            {
                                _28091 = vec3(0.0, 0.0, _29958);
                            }
                            else
                            {
                                _28091 = _28093;
                            }
                            float _29940 = _21403 ? _28080 : _29939;
                            float _28086;
                            vec3 _28090;
                            if (_21303.y < (-9.9999997473787516355514526367188e-05))
                            {
                                float _21423 = (_283.PoolMin.y - _6175.y) / _21303.y;
                                bool _21426 = _21423 < _29940;
                                bvec3 _29941 = bvec3(_21426);
                                _28090 = vec3(_29941.x ? vec3(0.0, 1.0, 0.0).x : _28091.x, _29941.y ? vec3(0.0, 1.0, 0.0).y : _28091.y, _29941.z ? vec3(0.0, 1.0, 0.0).z : _28091.z);
                                _28086 = _21426 ? _21423 : _29940;
                            }
                            else
                            {
                                _28090 = _28091;
                                _28086 = _29940;
                            }
                            _28089 = _28090;
                            _28088 = max(_28086, 0.0);
                            break;
                        } while(false);
                        if (_28088 < 100.0)
                        {
                            vec3 _20009 = _6175 + (_21303 * _28088);
                            uint _28278;
                            vec2 _28401;
                            float _28408;
                            do
                            {
                                bool _28098;
                                vec2 _28099;
                                float _28100;
                                do
                                {
                                    vec3 _21677 = _20009 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                    float _21735 = dot(_21677, _283.CameraForward.xyz);
                                    if (_21735 <= 0.0500000007450580596923828125)
                                    {
                                        _28100 = 1000000000.0;
                                        _28099 = vec2(0.5);
                                        _28098 = false;
                                        break;
                                    }
                                    float _21776 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _21752 = ((dot(_21677, _283.CameraRight.xyz) * _21776) / _21735) + (0.5 * float(_283.OutputWidth));
                                    float _21765 = (((-dot(_21677, _283.CameraUp.xyz)) * _21776) / _21735) + (0.5 * float(_283.OutputHeight));
                                    if (!((((_21752 >= 0.0) && (_21765 >= 0.0)) && (_21752 < float(_283.OutputWidth))) && (_21765 < float(_283.OutputHeight))))
                                    {
                                        _28100 = 1000000000.0;
                                        _28099 = vec2(0.5);
                                        _28098 = false;
                                        break;
                                    }
                                    vec2 _21694 = floor(vec2(_21752, _21765)) + vec2(0.5);
                                    ivec2 _21860 = clamp(ivec2(_21694), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _21812 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21860.x, int((_283.OutputHeight - 1u) - uint(_21860.y)), 0).xy, 0);
                                    float _21813 = _21812.x;
                                    float _21830 = ((_21813 > 0.0) && (abs(_21813 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21813 - _283.ProjA)) : 1000000000.0;
                                    _28100 = _21830;
                                    _28099 = _21694;
                                    _28098 = abs(_21830 - _21735) < ((0.0199999995529651641845703125 * _21735) + 0.02999999932944774627685546875);
                                    break;
                                } while(false);
                                if (_28098)
                                {
                                    _28408 = _28100;
                                    _28401 = _28099;
                                    _28278 = 1u;
                                    break;
                                }
                                vec3 _21483 = _20009 - vec3(_283.CameraRight.w, _27130, _283.CameraForward.w);
                                vec3 _21491 = (_283.PoolMin.xyz + _283.PoolMax.xyz) * 0.5;
                                vec3 _21496 = vec3((_21483.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21501 = vec3(0.0, 0.0, (_21483.z >= 0.0) ? 1.0 : (-1.0));
                                vec3 _21508 = vec3((_21491.x >= _20009.x) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21515 = vec3(0.0, 0.0, (_21491.z >= _20009.z) ? 1.0 : (-1.0));
                                vec3 _28101;
                                vec3 _28102;
                                vec3 _28107;
                                vec3 _28109;
                                if (abs(_28089.y) > 0.5)
                                {
                                    bool _21527 = abs(_21483.z) >= abs(_21483.x);
                                    bvec3 _21531 = bvec3(_21527);
                                    bvec3 _21536 = bvec3(_21527);
                                    bvec3 _21541 = bvec3(_21527);
                                    bvec3 _21546 = bvec3(_21527);
                                    _28109 = vec3(_21531.x ? _21501.x : _21496.x, _21531.y ? _21501.y : _21496.y, _21531.z ? _21501.z : _21496.z);
                                    _28107 = vec3(_21541.x ? _21496.x : _21501.x, _21541.y ? _21496.y : _21501.y, _21541.z ? _21496.z : _21501.z);
                                    _28102 = vec3(_21546.x ? _21515.x : _21508.x, _21546.y ? _21515.y : _21508.y, _21546.z ? _21515.z : _21508.z);
                                    _28101 = vec3(_21536.x ? _21508.x : _21515.x, _21536.y ? _21508.y : _21515.y, _21536.z ? _21508.z : _21515.z);
                                }
                                else
                                {
                                    bool _21552 = abs(_28089.x) > 0.5;
                                    bvec3 _21556 = bvec3(_21552);
                                    bvec3 _21561 = bvec3(_21552);
                                    _28109 = vec3(_21556.x ? _21501.x : _21496.x, _21556.y ? _21501.y : _21496.y, _21556.z ? _21501.z : _21496.z);
                                    _28107 = vec3(0.0, 1.0, 0.0);
                                    _28102 = vec3(_21561.x ? _21515.x : _21508.x, _21561.y ? _21515.y : _21508.y, _21561.z ? _21515.z : _21508.z);
                                    _28101 = vec3(0.0, 1.0, 0.0);
                                }
                                float _21943 = ((_28101.y > 0.5) ? (_283.PoolMin.w - _20009.y) : ((_28101.x > 0.5) ? (_283.PoolMax.x - _20009.x) : ((_28101.x < (-0.5)) ? (_20009.x - _283.PoolMin.x) : ((_28101.z > 0.5) ? (_283.PoolMax.z - _20009.z) : (_20009.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                float _21987 = ((_28102.y > 0.5) ? (_283.PoolMin.w - _20009.y) : ((_28102.x > 0.5) ? (_283.PoolMax.x - _20009.x) : ((_28102.x < (-0.5)) ? (_20009.x - _283.PoolMin.x) : ((_28102.z > 0.5) ? (_283.PoolMax.z - _20009.z) : (_20009.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _28403;
                                _28403 = _28099;
                                bool _28264;
                                uint _28279;
                                vec2 _28402;
                                vec2 _28405;
                                float _28409;
                                float _28412;
                                bool _29796;
                                int _28103 = 0;
                                bool _28126;
                                float _28410 = _28100;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_28103 < 11)
                                    {
                                        if (_28103 == 1)
                                        {
                                            vec3 _21580 = _20009 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                            float _22013 = dot(_21580, _283.CameraForward.xyz);
                                            vec2 _28406;
                                            float _28413;
                                            if (_22013 > 0.0500000007450580596923828125)
                                            {
                                                float _22054 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                float _22030 = ((dot(_21580, _283.CameraRight.xyz) * _22054) / _22013) + (0.5 * float(_283.OutputWidth));
                                                float _22043 = (((-dot(_21580, _283.CameraUp.xyz)) * _22054) / _22013) + (0.5 * float(_283.OutputHeight));
                                                vec2 _22044 = vec2(_22030, _22043);
                                                ivec2 _21591 = ivec2(_22044);
                                                ivec2 _22128 = clamp(ivec2(floor(((vec2(_21591) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _22137 = clamp(_22128, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                vec4 _22163 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_22137.x, int((FluidSize.y - 1u) - uint(_22137.y)), 0).xy, 0);
                                                float _22164 = _22163.x;
                                                ivec2 _22173 = clamp(_22128, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _22260 = clamp(_21591, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                vec4 _22212 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22260.x, int((_283.OutputHeight - 1u) - uint(_22260.y)), 0).xy, 0);
                                                float _22213 = _22212.x;
                                                vec2 _28407;
                                                float _28414;
                                                if (((((_22030 >= 0.0) && (_22043 >= 0.0)) && (_22030 < float(_283.OutputWidth))) && (_22043 < float(_283.OutputHeight))) && (((_22164 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_22173.x, int((FluidSize.y - 1u) - uint(_22173.y)), 0).xy, 0).w > 0.5)) && ((((_22213 > 0.0) && (abs(_22213 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22213 - _283.ProjA)) : 1000000000.0) >= (_22164 + 0.0500000007450580596923828125))))
                                                {
                                                    vec2 _21598 = floor(_22044) + vec2(0.5);
                                                    ivec2 _22351 = clamp(ivec2(_21598), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                    vec4 _22303 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22351.x, int((_283.OutputHeight - 1u) - uint(_22351.y)), 0).xy, 0);
                                                    float _22304 = _22303.x;
                                                    float _22321 = ((_22304 > 0.0) && (abs(_22304 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22304 - _283.ProjA)) : 1000000000.0;
                                                    if (_22321 < _22013)
                                                    {
                                                        _28409 = _22321;
                                                        _28402 = _21598;
                                                        _28279 = 2u;
                                                        _28264 = true;
                                                        break;
                                                    }
                                                    _28414 = _22321;
                                                    _28407 = _21598;
                                                }
                                                else
                                                {
                                                    _28414 = _28410;
                                                    _28407 = _28403;
                                                }
                                                _28413 = _28414;
                                                _28406 = _28407;
                                            }
                                            else
                                            {
                                                _28413 = _28410;
                                                _28406 = _28403;
                                            }
                                            _29796 = _28126;
                                            _28412 = _28413;
                                            _28405 = _28406;
                                            int _21658 = _28103 + 1;
                                            _28126 = _29796;
                                            _28103 = _21658;
                                            _28410 = _28412;
                                            _28403 = _28405;
                                            continue;
                                        }
                                        bool _21612 = _28103 == 2;
                                        bool _21614 = _28103 >= 3;
                                        int _21616 = _28103 & 1;
                                        bool _21619 = _21612 || (_21614 && (_21616 == 0));
                                        bool _21621 = _28103 < 3;
                                        int _21623 = _28103 - 3;
                                        float _21628 = _21621 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_21623 / 2)));
                                        if (_21628 >= (_21619 ? _21987 : _21943))
                                        {
                                            _29796 = _28126;
                                            _28412 = _28410;
                                            _28405 = _28403;
                                            int _21658 = _28103 + 1;
                                            _28126 = _29796;
                                            _28103 = _21658;
                                            _28410 = _28412;
                                            _28403 = _28405;
                                            continue;
                                        }
                                        bvec3 _21641 = bvec3(_21619);
                                        vec3 _21645 = _20009 + (vec3(_21641.x ? _28102.x : _28101.x, _21641.y ? _28102.y : _28101.y, _21641.z ? _28102.z : _28101.z) * _21628);
                                        bvec3 _21649 = bvec3(_21619);
                                        vec3 _21650 = vec3(_21649.x ? _28107.x : _28109.x, _21649.y ? _28107.y : _28109.y, _21649.z ? _28107.z : _28109.z);
                                        bool _28123;
                                        vec2 _28239;
                                        float _28243;
                                        do
                                        {
                                            float _22514 = ((_21650.y > 0.5) ? (_283.PoolMin.w - _21645.y) : ((_21650.x > 0.5) ? (_283.PoolMax.x - _21645.x) : ((_21650.x < (-0.5)) ? (_21645.x - _283.PoolMin.x) : ((_21650.z > 0.5) ? (_283.PoolMax.z - _21645.z) : (_21645.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                            vec2 _28240;
                                            _28240 = vec2(0.5);
                                            float _22420;
                                            float _22462;
                                            vec2 _28114;
                                            float _28115;
                                            bool _28120;
                                            bool _28124;
                                            int _28110 = 0;
                                            float _28111 = 0.0;
                                            float _28112 = 0.0599999986588954925537109375;
                                            float _28244 = 1000000000.0;
                                            SPIRV_CROSS_LOOP
                                            for (;;)
                                            {
                                                if ((_28110 < 40) && (_28111 < _22514))
                                                {
                                                    _22420 = min(_28111 + _28112, _22514);
                                                    bool _28113;
                                                    do
                                                    {
                                                        vec3 _22528 = (_21645 + (_21650 * _22420)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                        float _22586 = dot(_22528, _283.CameraForward.xyz);
                                                        if (_22586 <= 0.0500000007450580596923828125)
                                                        {
                                                            _28115 = 1000000000.0;
                                                            _28114 = vec2(0.5);
                                                            _28113 = false;
                                                            break;
                                                        }
                                                        float _22627 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                        float _22603 = ((dot(_22528, _283.CameraRight.xyz) * _22627) / _22586) + (0.5 * float(_283.OutputWidth));
                                                        float _22616 = (((-dot(_22528, _283.CameraUp.xyz)) * _22627) / _22586) + (0.5 * float(_283.OutputHeight));
                                                        if (!((((_22603 >= 0.0) && (_22616 >= 0.0)) && (_22603 < float(_283.OutputWidth))) && (_22616 < float(_283.OutputHeight))))
                                                        {
                                                            _28115 = 1000000000.0;
                                                            _28114 = vec2(0.5);
                                                            _28113 = false;
                                                            break;
                                                        }
                                                        vec2 _22545 = floor(vec2(_22603, _22616)) + vec2(0.5);
                                                        ivec2 _22711 = clamp(ivec2(_22545), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                        vec4 _22663 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22711.x, int((_283.OutputHeight - 1u) - uint(_22711.y)), 0).xy, 0);
                                                        float _22664 = _22663.x;
                                                        float _22681 = ((_22664 > 0.0) && (abs(_22664 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22664 - _283.ProjA)) : 1000000000.0;
                                                        _28115 = _22681;
                                                        _28114 = _22545;
                                                        _28113 = abs(_22681 - _22586) < ((0.0199999995529651641845703125 * _22586) + 0.02999999932944774627685546875);
                                                        break;
                                                    } while(false);
                                                    if (_28113)
                                                    {
                                                        vec2 _28241;
                                                        float _28245;
                                                        _28245 = _28115;
                                                        _28241 = _28114;
                                                        float _29944;
                                                        float _29945;
                                                        float _29946;
                                                        vec2 _29948;
                                                        int _28119 = 0;
                                                        float _28246 = _28111;
                                                        float _28248 = _22420;
                                                        SPIRV_CROSS_UNROLL
                                                        for (; _28119 < 4; _28248 = _29944, _28246 = _29945, _28245 = _29946, _28241 = _29948, _28119++)
                                                        {
                                                            float _22438 = 0.5 * (_28246 + _28248);
                                                            bool _28249;
                                                            vec2 _28250;
                                                            float _28251;
                                                            do
                                                            {
                                                                vec3 _22755 = (_21645 + (_21650 * _22438)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                                float _22813 = dot(_22755, _283.CameraForward.xyz);
                                                                if (_22813 <= 0.0500000007450580596923828125)
                                                                {
                                                                    _28251 = 1000000000.0;
                                                                    _28250 = vec2(0.5);
                                                                    _28249 = false;
                                                                    break;
                                                                }
                                                                float _22854 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                                float _22830 = ((dot(_22755, _283.CameraRight.xyz) * _22854) / _22813) + (0.5 * float(_283.OutputWidth));
                                                                float _22843 = (((-dot(_22755, _283.CameraUp.xyz)) * _22854) / _22813) + (0.5 * float(_283.OutputHeight));
                                                                if (!((((_22830 >= 0.0) && (_22843 >= 0.0)) && (_22830 < float(_283.OutputWidth))) && (_22843 < float(_283.OutputHeight))))
                                                                {
                                                                    _28251 = 1000000000.0;
                                                                    _28250 = vec2(0.5);
                                                                    _28249 = false;
                                                                    break;
                                                                }
                                                                vec2 _22772 = floor(vec2(_22830, _22843)) + vec2(0.5);
                                                                ivec2 _22938 = clamp(ivec2(_22772), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                                vec4 _22890 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22938.x, int((_283.OutputHeight - 1u) - uint(_22938.y)), 0).xy, 0);
                                                                float _22891 = _22890.x;
                                                                float _22908 = ((_22891 > 0.0) && (abs(_22891 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22891 - _283.ProjA)) : 1000000000.0;
                                                                _28251 = _22908;
                                                                _28250 = _22772;
                                                                _28249 = abs(_22908 - _22813) < ((0.0199999995529651641845703125 * _22813) + 0.02999999932944774627685546875);
                                                                break;
                                                            } while(false);
                                                            _29944 = _28249 ? _22438 : _28248;
                                                            _29945 = _28249 ? _28246 : _22438;
                                                            _29946 = _28249 ? _28251 : _28245;
                                                            bvec2 _29947 = bvec2(_28249);
                                                            _29948 = vec2(_29947.x ? _28250.x : _28241.x, _29947.y ? _28250.y : _28241.y);
                                                        }
                                                        _28243 = _28245;
                                                        _28239 = _28241;
                                                        _28124 = true;
                                                        _28120 = true;
                                                        break;
                                                    }
                                                    _22462 = min(_28112 * 1.2000000476837158203125, 0.300000011920928955078125);
                                                    _28112 = _22462;
                                                    _28111 = _22420;
                                                    _28110++;
                                                    _28244 = _28115;
                                                    _28240 = _28114;
                                                    continue;
                                                }
                                                else
                                                {
                                                    _28243 = _28244;
                                                    _28239 = _28240;
                                                    _28124 = _28126;
                                                    _28120 = false;
                                                    break;
                                                }
                                            }
                                            if (_28120)
                                            {
                                                _28123 = _28124;
                                                break;
                                            }
                                            _28123 = false;
                                            break;
                                        } while(false);
                                        if (_28123)
                                        {
                                            _28409 = _28243;
                                            _28402 = _28239;
                                            _28279 = 1u;
                                            _28264 = true;
                                            break;
                                        }
                                        _29796 = _28123;
                                        _28412 = _28243;
                                        _28405 = _28239;
                                        int _21658 = _28103 + 1;
                                        _28126 = _29796;
                                        _28103 = _21658;
                                        _28410 = _28412;
                                        _28403 = _28405;
                                        continue;
                                    }
                                    else
                                    {
                                        _28409 = _28410;
                                        _28402 = _28403;
                                        _28279 = _27470;
                                        _28264 = false;
                                        break;
                                    }
                                }
                                if (_28264)
                                {
                                    _28408 = _28409;
                                    _28401 = _28402;
                                    _28278 = _28279;
                                    break;
                                }
                                _28408 = _28409;
                                _28401 = _28402;
                                _28278 = 0u;
                                break;
                            } while(false);
                            if (_28278 != 0u)
                            {
                                float _23016 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec3 _20030 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_28401.x - (0.5 * float(_283.OutputWidth))) * _28408) / _23016)) + (_283.CameraUp.xyz * (((-(_28401.y - (0.5 * float(_283.OutputHeight)))) * _28408) / _23016))) + (_283.CameraForward.xyz * _28408));
                                bvec3 _20031 = bvec3(_28278 == 1u);
                                vec3 _20032 = vec3(_20031.x ? _20009.x : _20030.x, _20031.y ? _20009.y : _20030.y, _20031.z ? _20009.z : _20030.z);
                                _28895 = _28401;
                                _28797 = _28408;
                                _28699 = (_28278 == 1u) ? _28088 : length(_20032 - _6175);
                                _28601 = 1.0;
                                _28503 = _20032;
                                break;
                            }
                        }
                        float _20054 = min(_28088, 4.0);
                        _28895 = _28418;
                        _28797 = _28448;
                        _28699 = _20054;
                        _28601 = _28477;
                        _28503 = _6175 + (_21303 * _20054);
                        break;
                    } while(false);
                    vec3 _28995;
                    do
                    {
                        vec2 _23253 = clamp(_28895 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _23264 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23253.x, 1.0 - _23253.y), 0.0).xyz;
                        if ((_28601 < 0.5) || (_28797 > 100000000.0))
                        {
                            _28995 = _23264;
                            break;
                        }
                        ivec2 _23087 = ivec2(_28895);
                        float _23093 = _6175.y - _28503.y;
                        if (_23093 <= 0.00999999977648258209228515625)
                        {
                            _28995 = _23264;
                            break;
                        }
                        ivec2 _23294 = clamp(_23087, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        ivec2 _23382 = clamp(_23087, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec3 _23342 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_23382.x, int((_283.OutputHeight - 1u) - uint(_23382.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _23344 = length(_23342);
                        vec3 _23350 = _23342 / vec3(_23344);
                        bvec3 _23351 = bvec3(_23344 > 0.001000000047497451305389404296875);
                        ivec2 _23442 = clamp(_23087, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        float _23139 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_23294.x, int((_283.OutputHeight - 1u) - uint(_23294.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_23351.x ? _23350.x : vec3(0.0, 1.0, 0.0).x, _23351.y ? _23350.y : vec3(0.0, 1.0, 0.0).y, _23351.z ? _23350.z : vec3(0.0, 1.0, 0.0).z), _5666), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_23442.x, int((_283.OutputHeight - 1u) - uint(_23442.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _283.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_23264, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _23143 = max(-_29897.y, 0.20000000298023223876953125);
                        float _28994;
                        if ((_283.CausticParams.x > 0.0) && (_23139 > 0.0))
                        {
                            vec2 _23186 = _28503.xz - ((_29897.xz / vec2(_23143)) * _23093);
                            vec2 _23497 = _23186 * (6.283185482025146484375 / max(_283.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _23520 = _23497 + (vec2(sin((_23497.y * 0.730000019073486328125) + ((_283.ShadeParams.z * 0.89999997615814208984375) * _283.CausticParams.z)), sin((_23497.x * 0.61000001430511474609375) - ((_283.ShadeParams.z * 1.10000002384185791015625) * _283.CausticParams.z))) * 0.89999997615814208984375);
                            float _28993;
                            do
                            {
                                if ((_283.GridParams.z < 2u) || (_283.GridParams.w < 2u))
                                {
                                    _28993 = 1.0;
                                    break;
                                }
                                vec2 _23630 = clamp(((_23186 - _283.PoolMin.xz) / max(_283.PoolMax.xz - _283.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)), vec2(0.0), vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _23632 = uvec2(_23630);
                                vec2 _23636 = _23630 - vec2(_23632);
                                uint _23643 = _23632.x + (_23632.y * _283.GridParams.z);
                                _28993 = mix(mix(CausticField_1._data[_23643], CausticField_1._data[_23643 + 1u], _23636.x), mix(CausticField_1._data[_23643 + _283.GridParams.z], CausticField_1._data[(_23643 + _283.GridParams.z) + 1u], _23636.x), _23636.y);
                                break;
                            } while(false);
                            _28994 = min(max(1.0 + ((_283.CausticParams.x * (exp((-_23093) / max(_283.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _23093))) * ((pow(clamp((1.0 - abs(sin(_23520.x + ((_283.ShadeParams.z * 0.60000002384185791015625) * _283.CausticParams.z)) * sin((_23520.y * 1.12999999523162841796875) - ((_283.ShadeParams.z * 0.4000000059604644775390625) * _283.CausticParams.z)))) * (1.0 - abs(sin(((_23520.x + _23520.y) * 0.709999978542327880859375) - ((_283.ShadeParams.z * 0.5) * _283.CausticParams.z)) * sin(((_23520.x - _23520.y) * 0.829999983310699462890625) + ((_283.ShadeParams.z * 0.699999988079071044921875) * _283.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_28993, 0.0500000007450580596923828125), _283.LightParams.y), 2.5);
                        }
                        else
                        {
                            _28994 = 1.0;
                        }
                        _28995 = _23264 * ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _23093) * 1.2000000476837158203125), vec3(_283.LightParams.x)) * (1.0 - _23139)) + ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _23093) / vec3(_23143)), vec3(_283.LightParams.x)) * _23139) * _28994));
                        break;
                    } while(false);
                    vec3 _17970 = exp((-_283.FluidTint.xyz) * max(_28699, 0.0500000007450580596923828125));
                    float _23722 = _283.ScatterParams.z * _283.ScatterParams.z;
                    _29044 = mix(_27935, mix((_28995 * _17970) + ((_283.FluidColor.xyz * ((_6681 * _283.ScatterParams.y) + (_283.SunColor.xyz * (((_283.ScatterParams.x * 0.25) * ((1.0 - _23722) / pow(max((1.0 + _23722) - ((2.0 * _283.ScatterParams.z) * dot(_29897, -_17844)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5666.y, 0.0, 1.0))))) * (vec3(1.0) - _17970)), mix(_28064, _28058, vec3(_27966)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_17918, -_17844), 0.0, 1.0), max(_283.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _6088), 0.0, 1.0)));
                }
                else
                {
                    _29044 = _27935;
                }
                _29043 = _29044;
            }
            else
            {
                _29043 = _27935;
            }
            _29042 = _29043;
        }
        else
        {
            _29042 = _27935;
        }
        float _6241 = clamp((clamp((float(_13038) * 0.0009765625) / _5940, 0.0, 1.0) * _283.ScatterParams.w) + (((_5641 * 0.000244140625) * _283.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _6253 = exp(max(_283.FluidTint.xyz, vec3(0.0)) * (-_6153)) * (1.0 - (0.85000002384185791015625 * _6241));
        vec3 _23751 = ((_283.SunColor.xyz * ((clamp(_5666.y, 0.0, 1.0) * _27242) * 0.3183098733425140380859375)) + _6681) * 0.85000002384185791015625;
        vec3 _23757 = mix(vec3(dot(_23751, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _23751, vec3(0.5));
        float _23805 = _283.ScatterParams.z * _283.ScatterParams.z;
        vec3 _6290 = ((_29042 * _6253) + ((vec3(1.0) - _6253) * mix(_283.FluidColor.xyz * ((_6681 * _283.ScatterParams.y) + (_283.SunColor.xyz * ((((_283.ScatterParams.x * 0.25) * ((1.0 - _23805) / pow(max((1.0 + _23805) - ((2.0 * _283.ScatterParams.z) * dot(_29897, -_13221)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5666.y, 0.0, 1.0)) * _27242))), _23757 * 0.800000011920928955078125, vec3(_6241)))) + (_23757 * (((float(_5646) * 2.2888532839715480804443359375e-05) + (_5641 * 3.6621095205191522836685180664062e-05)) * clamp(_283.FoamParams.z, 0.0, 1.0)));
        if (_283.DebugView == 3u)
        {
            float _6297 = clamp(_6153 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5602), vec4(_6297, _6297, _6297, 1.0));
            break;
        }
        if (_283.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_5602), vec4(_27143.www, 1.0));
            break;
        }
        if (_283.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_5602), vec4(_6290, 1.0));
            break;
        }
        if (_283.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_5602), vec4(clamp((_27192 * 0.0625) + (_5635 * 0.000244140625), 0.0, 1.0), _6241, clamp(_5959 + (_5629 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_283.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_5602), vec4(_27936, 1.0));
            break;
        }
        float _6369 = 0.014999999664723873138427734375 + (2.0 * sqrt(_6035));
        vec3 _6372 = reflect(_5821, _27244);
        vec3 _23839 = ((_283.CameraRight.xyz * _6372.x) + (_283.CameraUp.xyz * _6372.y)) + (_283.CameraForward.xyz * _6372.z);
        float _29250;
        vec3 _29387;
        do
        {
            if (_283.ReflectionDistance <= 0.0)
            {
                _29387 = vec3(0.0);
                _29250 = 0.0;
                break;
            }
            float _23888 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _6372.z);
            if (_23888 <= 0.0)
            {
                _29387 = vec3(0.0);
                _29250 = 0.0;
                break;
            }
            float _24085 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _23897 = max(0.0040000001899898052215576171875 * _27142, 0.00999999977648258209228515625);
            float _23899 = 0.5 * _23897;
            float _23908;
            float _23910;
            bool _29235;
            bool _29240;
            float _29245;
            float _29251;
            vec3 _29388;
            int _29226 = 0;
            float _29227 = _23899;
            float _29228 = _23897;
            float _29246 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_29226 < 80)
                {
                    _23908 = _29227 + _29228;
                    _23910 = _29228 * 1.07000005245208740234375;
                    if (_23908 > _283.ReflectionDistance)
                    {
                        _29388 = vec3(0.0);
                        _29251 = 0.0;
                        _29245 = _29246;
                        _29240 = false;
                        _29235 = true;
                        break;
                    }
                    vec3 _23921 = _9693 + (_6372 * _23908);
                    if (_23921.z <= 0.0500000007450580596923828125)
                    {
                        _29388 = vec3(0.0);
                        _29251 = 0.0;
                        _29245 = _29246;
                        _29240 = false;
                        _29235 = true;
                        break;
                    }
                    float _24125 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _24101 = ((_23921.x * _24125) / _23921.z) + (0.5 * float(_283.OutputWidth));
                    float _24114 = (((-_23921.y) * _24125) / _23921.z) + (0.5 * float(_283.OutputHeight));
                    if (!((((_24101 >= 0.0) && (_24114 >= 0.0)) && (_24101 < float(_283.OutputWidth))) && (_24114 < float(_283.OutputHeight))))
                    {
                        _29388 = vec3(0.0);
                        _29251 = _27704;
                        _29245 = _29246;
                        _29240 = true;
                        _29235 = false;
                        break;
                    }
                    ivec2 _24209 = clamp(ivec2(vec2(_24101, _24114)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _24161 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24209.x, int((_283.OutputHeight - 1u) - uint(_24209.y)), 0).xy, 0);
                    float _24162 = _24161.x;
                    float _24179 = ((_24162 > 0.0) && (abs(_24162 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24162 - _283.ProjA)) : 1000000000.0;
                    if (((_24179 < 100000000.0) && (_23921.z > _24179)) && ((_23921.z - _24179) < max(_29228 * 2.1400001049041748046875, 0.25)))
                    {
                        float _29230;
                        _29230 = _23908;
                        float _29951;
                        float _29952;
                        int _29229 = 0;
                        float _29233 = _29227;
                        SPIRV_CROSS_UNROLL
                        for (; _29229 < 6; _29233 = _29951, _29230 = _29952, _29229++)
                        {
                            float _23965 = 0.5 * (_29233 + _29230);
                            vec3 _23970 = _9693 + (_6372 * _23965);
                            float _24279 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _24340 = clamp(ivec2(vec2(((_23970.x * _24279) / _23970.z) + (0.5 * float(_283.OutputWidth)), (((-_23970.y) * _24279) / _23970.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                            vec4 _24292 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24340.x, int((_283.OutputHeight - 1u) - uint(_24340.y)), 0).xy, 0);
                            float _24293 = _24292.x;
                            bool _23977 = _23970.z > (((_24293 > 0.0) && (abs(_24293 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24293 - _283.ProjA)) : 1000000000.0);
                            _29951 = _23977 ? _29233 : _23965;
                            _29952 = _23977 ? _23965 : _29230;
                        }
                        vec3 _23991 = _9693 + (_6372 * _29230);
                        float _24410 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _24400 = vec2(((_23991.x * _24410) / _23991.z) + (0.5 * float(_283.OutputWidth)), (((-_23991.y) * _24410) / _23991.z) + (0.5 * float(_283.OutputHeight)));
                        ivec2 _24468 = clamp(ivec2(_24400), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec3 _24428 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_24468.x, int((_283.OutputHeight - 1u) - uint(_24468.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _24430 = length(_24428);
                        vec3 _24436 = _24428 / vec3(_24430);
                        bvec3 _24437 = bvec3(_24430 > 0.001000000047497451305389404296875);
                        vec3 _24438 = vec3(_24437.x ? _24436.x : vec3(0.0, 1.0, 0.0).x, _24437.y ? _24436.y : vec3(0.0, 1.0, 0.0).y, _24437.z ? _24436.z : vec3(0.0, 1.0, 0.0).z);
                        if (dot(vec3(dot(_24438, _283.CameraRight.xyz), dot(_24438, _283.CameraUp.xyz), dot(_24438, _283.CameraForward.xyz)), _6372) > 0.100000001490116119384765625)
                        {
                            int _24018 = _29226 + 1;
                            _29228 = _23910;
                            _29227 = _23908;
                            _29226 = _24018;
                            _29246 = _23908;
                            continue;
                        }
                        float _24532 = clamp(((_6369 * _29230) * _24085) / max(_23991.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _24534 = 0.5 * _24532;
                        vec2 _24619 = clamp(_24400 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24645 = clamp((_24400 + vec2(_24534)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24671 = clamp((_24400 + vec2(_24532 * (-0.5), _24534)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24697 = clamp((_24400 + vec2(_24534, _24532 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24723 = clamp((_24400 + vec2(_24532 * (-0.5), _24532 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24749 = clamp((_24400 + vec2(_24532, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24775 = clamp((_24400 + vec2(-_24532, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24801 = clamp((_24400 + vec2(0.0, _24532)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24827 = clamp((_24400 + vec2(0.0, -_24532)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _24604 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24619.x, 1.0 - _24619.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24645.x, 1.0 - _24645.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24671.x, 1.0 - _24671.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24697.x, 1.0 - _24697.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24723.x, 1.0 - _24723.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24749.x, 1.0 - _24749.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24775.x, 1.0 - _24775.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24801.x, 1.0 - _24801.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24827.x, 1.0 - _24827.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _29388 = _24604;
                        _29251 = _23888;
                        _29245 = _23908;
                        _29240 = false;
                        _29235 = true;
                        break;
                    }
                    int _24018 = _29226 + 1;
                    _29228 = _23910;
                    _29227 = _23908;
                    _29226 = _24018;
                    _29246 = _23908;
                    continue;
                }
                else
                {
                    _29388 = vec3(0.0);
                    _29251 = _27704;
                    _29245 = _29246;
                    _29240 = false;
                    _29235 = false;
                    break;
                }
            }
            if (_29235)
            {
                _29387 = _29388;
                _29250 = _29251;
                break;
            }
            if ((!_29240) || (_29245 < 0.0))
            {
                _29387 = _29388;
                _29250 = 0.0;
                break;
            }
            vec3 _24033 = _9693 + (_6372 * _29245);
            float _24878 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _24868 = vec2(((_24033.x * _24878) / _24033.z) + (0.5 * float(_283.OutputWidth)), (((-_24033.y) * _24878) / _24033.z) + (0.5 * float(_283.OutputHeight)));
            ivec2 _24939 = clamp(ivec2(_24868), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _24891 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24939.x, int((_283.OutputHeight - 1u) - uint(_24939.y)), 0).xy, 0);
            float _24892 = _24891.x;
            float _24909 = ((_24892 > 0.0) && (abs(_24892 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24892 - _283.ProjA)) : 1000000000.0;
            float _24985 = clamp(((_6369 * _29245) * _24085) / max(_24033.z, 0.0500000007450580596923828125), 0.0, 12.0);
            float _24987 = 0.5 * _24985;
            vec2 _25072 = clamp(_24868 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25098 = clamp((_24868 + vec2(_24987)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25124 = clamp((_24868 + vec2(_24985 * (-0.5), _24987)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25150 = clamp((_24868 + vec2(_24987, _24985 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25176 = clamp((_24868 + vec2(_24985 * (-0.5), _24985 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25202 = clamp((_24868 + vec2(_24985, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25228 = clamp((_24868 + vec2(-_24985, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25254 = clamp((_24868 + vec2(0.0, _24985)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25280 = clamp((_24868 + vec2(0.0, -_24985)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _25057 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25072.x, 1.0 - _25072.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25098.x, 1.0 - _25098.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25124.x, 1.0 - _25124.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25150.x, 1.0 - _25150.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25176.x, 1.0 - _25176.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25202.x, 1.0 - _25202.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25228.x, 1.0 - _25228.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25254.x, 1.0 - _25254.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25280.x, 1.0 - _25280.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_24909 > 100000000.0)
            {
                _29387 = _25057;
                _29250 = _23888;
                break;
            }
            float _24058 = _24909 - _24033.z;
            if (_24058 <= 0.0)
            {
                _29387 = _25057;
                _29250 = _23888;
                break;
            }
            _29387 = _25057;
            _29250 = _23888 * exp((_24058 / max(_6372.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        vec3 _29393;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                float _25334 = _23839.y;
                _29393 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _25334))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _25334)));
                break;
            }
            _29393 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_23839.x, _23839.y, -_23839.z), clamp(clamp(log2(max(_6369 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _6392 = mix(_29393, _29387, vec3(_29250));
        if (_283.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_5602), vec4(_6392, 1.0));
            break;
        }
        float _6420 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_27194 + _27244), _5823), 0.0, 1.0), max(_283.FresnelPower, 1.0));
        float _6421 = 0.0199999995529651641845703125 + _6420;
        vec3 _6431 = normalize(_283.SunDirection.xyz);
        float _6448 = max((2.0 / ((2.0 / (max(_283.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _6035))) - 2.0, 4.0);
        vec3 _6517 = (mix(_6290, _6392, vec3(clamp(_6421, 0.0, 1.0))) + (((((_283.SunColor.xyz * ((pow(clamp(dot(_27244, normalize(_6431 - _5821)), 0.0, 1.0), _6448) * (_6448 + 8.0)) * 0.0397887341678142547607421875)) * _283.Specular) * _6421) * _27242) * clamp(dot(_27244, _6431) * 4.0, 0.0, 1.0))) + ((_283.SunColor.xyz * _283.FluidColor.xyz) * (((((_283.LightParams.w * 0.60000002384185791015625) * (((1.0 - _6088) * exp(_6083 * (-1.5))) * (1.0 - exp(_6083 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_5666, _13221)), 9.9999997473787516355514526367188e-05), 1.5))) * _27242) * (0.980000019073486328125 - _6420)));
        vec3 _6535 = mix(_6517, (_6517 * 1.25) + (_6681 * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _6078)) * _6088) * _283.LightParams.z, 0.0, 1.0)));
        float _6544 = clamp(1.0 - exp(-((_27192 * 0.07500000298023223876953125) + (_5635 * 0.0003662109375))), 0.0, 1.0);
        vec3 _29516;
        if ((_6544 > 0.0199999995529651641845703125) && (_283.FoamParams.x > 0.0))
        {
            vec2 _25395 = _5965.xz / vec2(max(_283.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _25430 = floor(_25395);
            vec2 _25433 = _25395 - _25430;
            float _29509;
            float _29510;
            _29510 = 8.0;
            _29509 = 8.0;
            float _29525;
            float _29526;
            SPIRV_CROSS_UNROLL
            for (int _29508 = -1; _29508 <= 1; _29510 = _29525, _29509 = _29526, _29508++)
            {
                _29526 = _29509;
                _29525 = _29510;
                float _29954;
                float _29874;
                SPIRV_CROSS_UNROLL
                for (int _29523 = -1; _29523 <= 1; _29526 = _29874, _29525 = _29954, _29523++)
                {
                    vec2 _25448 = vec2(float(_29523), float(_29508));
                    ivec2 _25452 = ivec2(_25430 + _25448);
                    uint _25524 = ((((uint(_25452.x + 4096) * 1973u) + (uint(_25452.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25532 = ((_25524 >> ((_25524 >> 28u) + 4u)) ^ _25524) * 277803737u;
                    uint _25536 = (_25532 >> 22u) ^ _25532;
                    float _25490 = length((_25448 + (vec2(0.5) + (sin(vec2(_283.ShadeParams.z * 0.5) + ((vec2(float(_25536 & 65535u), float(_25536 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25433);
                    bool _25493 = _25490 < _29525;
                    if (_25493)
                    {
                        _29874 = _29525;
                    }
                    else
                    {
                        _29874 = (_25490 < _29526) ? _25490 : _29526;
                    }
                    _29954 = _25493 ? _25490 : _29525;
                }
            }
            vec2 _25402 = (_25395 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _25553 = floor(_25402);
            vec2 _25556 = _25402 - _25553;
            float _29512;
            float _29513;
            _29513 = 8.0;
            _29512 = 8.0;
            float _29519;
            float _29520;
            SPIRV_CROSS_UNROLL
            for (int _29511 = -1; _29511 <= 1; _29513 = _29519, _29512 = _29520, _29511++)
            {
                _29520 = _29512;
                _29519 = _29513;
                float _29956;
                float _29868;
                SPIRV_CROSS_UNROLL
                for (int _29517 = -1; _29517 <= 1; _29520 = _29868, _29519 = _29956, _29517++)
                {
                    vec2 _25571 = vec2(float(_29517), float(_29511));
                    ivec2 _25575 = ivec2(_25553 + _25571);
                    uint _25647 = ((((uint(_25575.x + 4096) * 1973u) + (uint(_25575.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25655 = ((_25647 >> ((_25647 >> 28u) + 4u)) ^ _25647) * 277803737u;
                    uint _25659 = (_25655 >> 22u) ^ _25655;
                    float _25613 = length((_25571 + (vec2(0.5) + (sin(vec2(_283.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_25659 & 65535u), float(_25659 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25556);
                    bool _25616 = _25613 < _29519;
                    if (_25616)
                    {
                        _29868 = _29519;
                    }
                    else
                    {
                        _29868 = (_25613 < _29520) ? _25613 : _29520;
                    }
                    _29956 = _25616 ? _25613 : _29519;
                }
            }
            float _25413 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29509 - _29510), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29512 - _29513))), clamp((((_283.FoamParams.y * ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_27142, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _29516 = mix(_6535, _23757 * mix(0.85000002384185791015625, 1.0, _25413), vec3(clamp(_6544 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_25413 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_283.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _29516 = _6535;
        }
        float _6590 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _5959) * clamp(_283.Foam * 1.60000002384185791015625, 0.0, 1.0);
        vec3 _6601 = mix(mix(_29516, _23757, vec3(_6590 * 0.89999997615814208984375)), _23757, vec3(_5689));
        imageStore(OutputTexture, ivec2(_5602), vec4(_6601, max(clamp(_27143.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _6590)), _5689)));
        break;
    } while(false);
}

