#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=283bb4e595e74923cd1aabd12bec0f06866a3c82e7bd47e9d805d57c7baf608a; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1923[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _26767;
bool _26978;
uint _27086;
vec3 _27260;
float _27312;
vec2 _27466;
float _29512;

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
        uvec2 _5579 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _5579.x;
        FluidSize.y = _5579.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _5606 = uvec2(gl_GlobalInvocationID.x, (_283.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _5609 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _5614 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _5628 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _283.OutputWidth)) * 4u;
        uint _5632 = WhitewaterBits_1._data[_5628];
        float _5633 = float(_5632);
        uint _5638 = WhitewaterBits_1._data[_5628 + 1u];
        float _5639 = float(_5638);
        uint _5644 = WhitewaterBits_1._data[_5628 + 2u];
        float _5645 = float(_5644);
        uint _5650 = WhitewaterBits_1._data[_5628 + 3u];
        WhitewaterBits_1._data[_5628] = 0u;
        WhitewaterBits_1._data[_5628 + 1u] = 0u;
        WhitewaterBits_1._data[_5628 + 2u] = 0u;
        WhitewaterBits_1._data[_5628 + 3u] = 0u;
        vec3 _5668 = normalize(_283.SunDirection.xyz);
        vec3 _5670 = normalize(((_283.CameraRight.xyz * _5668.x) + (_283.CameraUp.xyz * _5668.y)) + (_283.CameraForward.xyz * _5668.z));
        vec3 _5673 = refract(-_5670, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _29450 = bvec3((dot(_5673, _5673) < 9.9999999747524270787835121154785e-07) || (_5670.y <= 0.0));
        vec3 _29451 = vec3(_29450.x ? vec3(0.0, -1.0, 0.0).x : _5673.x, _29450.y ? vec3(0.0, -1.0, 0.0).y : _5673.y, _29450.z ? vec3(0.0, -1.0, 0.0).z : _5673.z);
        float _6663 = max(EnvMaxMip - 1.0, 0.0);
        vec3 _26768;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _26768 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                break;
            }
            _26768 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_6663, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _26769;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _26769 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _26769 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6663, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _26771;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _26771 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _26771 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6663, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _26773;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _26773 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _26773 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_6663, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _26775;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _26775 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _26775 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_6663, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _6685 = (((((_26768 * 2.0) + _26769) + _26771) + _26773) + _26775) * vec3(0.16666667163372039794921875);
        float _5693 = clamp(1.0 - exp(_5633 * (-0.0006103515625)), 0.0, 1.0) * clamp(_283.FoamParams.w, 0.0, 1.0);
        float _26779;
        vec4 _26780;
        do
        {
            vec2 _7038 = ((_5614 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
            ivec2 _7041 = ivec2(floor(_7038));
            vec2 _7045 = _7038 - vec2(_7041);
            ivec2 _7220 = clamp(_7041, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7256 = clamp(_7041 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7292 = clamp(_7041 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7328 = clamp(_7041 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7081 = vec4((1.0 - _7045.x) * (1.0 - _7045.y), _7045.x * (1.0 - _7045.y), (1.0 - _7045.x) * _7045.y, _7045.x * _7045.y);
            vec4 _7008 = _7081;
            vec4 _7086 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7220.x, int((FluidSize.y - 1u) - uint(_7220.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7256.x, int((FluidSize.y - 1u) - uint(_7256.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7292.x, int((FluidSize.y - 1u) - uint(_7292.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7328.x, int((FluidSize.y - 1u) - uint(_7328.y)), 0).xy, 0).x);
            vec4 _7009 = _7086;
            float _29181;
            _29181 = 1000000000.0;
            float _29449;
            SPIRV_CROSS_UNROLL
            for (int _26777 = 0; _26777 < 4; _29181 = _29449, _26777++)
            {
                if (_7009[_26777] > 0.0)
                {
                    _29449 = min(_29181, _7009[_26777]);
                }
                else
                {
                    _29449 = _29181;
                }
            }
            vec4 _7012 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _26778 = 0; _26778 < 4; )
            {
                _7012[_26778] = ((_7009[_26778] > 0.0) && (_7009[_26778] < (_29181 + max(0.25, 0.02999999932944774627685546875 * _29181)))) ? _7008[_26778] : 0.0;
                _26778++;
                continue;
            }
            float _7146 = ((_7012.x + _7012.y) + _7012.z) + _7012.w;
            if (_7146 <= 9.9999997473787516355514526367188e-05)
            {
                _26780 = vec4(0.0);
                _26779 = 0.0;
                break;
            }
            ivec2 _7364 = clamp(_7041, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7390 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7364.x, int((FluidSize.y - 1u) - uint(_7364.y)), 0).xy, 0);
            ivec2 _7399 = clamp(_7041 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7425 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7399.x, int((FluidSize.y - 1u) - uint(_7399.y)), 0).xy, 0);
            ivec2 _7434 = clamp(_7041 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7460 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7434.x, int((FluidSize.y - 1u) - uint(_7434.y)), 0).xy, 0);
            ivec2 _7469 = clamp(_7041 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7495 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7469.x, int((FluidSize.y - 1u) - uint(_7469.y)), 0).xy, 0);
            _26780 = vec4((((((_7390.xyz * _7012.x) + (_7425.xyz * _7012.y)) + (_7460.xyz * _7012.z)) + (_7495.xyz * _7012.w)) / vec3(_7146)).x, _29512, _29512, dot(_7081, vec4(_7390.w, _7425.w, _7460.w, _7495.w)));
            _26779 = dot(_7012, _7086) / _7146;
            break;
        } while(false);
        ivec2 _7557 = clamp(_5609, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
        vec4 _7508 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7557.x, int((_283.OutputHeight - 1u) - uint(_7557.y)), 0).xy, 0);
        float _7509 = _7508.x;
        float _7526 = ((_7509 > 0.0) && (abs(_7509 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_7509 - _283.ProjA)) : 1000000000.0;
        if (!(((_26779 > 0.0) && (_26780.w > 0.001000000047497451305389404296875)) && (_26779 < (_7526 - 0.00200000009499490261077880859375))))
        {
            vec2 _7602 = clamp(_5614 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _7613 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_7602.x, 1.0 - _7602.y), 0.0).xyz;
            float _29166;
            vec3 _29177;
            if ((_7526 < 100000000.0) && (_283.WetParams.w > 0.0))
            {
                float _7661 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _5728 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_5614.x - (0.5 * float(_283.OutputWidth))) * _7526) / _7661)) + (_283.CameraUp.xyz * (((-(_5614.y - (0.5 * float(_283.OutputHeight)))) * _7526) / _7661))) + (_283.CameraForward.xyz * _7526));
                ivec2 _7741 = clamp(_5609, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                vec3 _7701 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_7741.x, int((_283.OutputHeight - 1u) - uint(_7741.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _7703 = length(_7701);
                vec3 _7709 = _7701 / vec3(_7703);
                bvec3 _7710 = bvec3(_7703 > 0.001000000047497451305389404296875);
                float _29167;
                vec3 _29178;
                if ((vec3(_7710.x ? _7709.x : vec3(0.0, 1.0, 0.0).x, _7710.y ? _7709.y : vec3(0.0, 1.0, 0.0).y, _7710.z ? _7709.z : vec3(0.0, 1.0, 0.0).z).y > 0.60000002384185791015625) && (_5728.y > (_283.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _29108;
                    do
                    {
                        int _7794 = int(_283.GridParams.x);
                        int _7797 = int(_283.GridParams.y);
                        if (((_7794 < 4) || (_7797 < 4)) || (_283.WetParams.z <= 0.0))
                        {
                            _29108 = 0.0;
                            break;
                        }
                        vec2 _7819 = ((_5728.xz - _283.WetParams.xy) / vec2(_283.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_7819, vec2(-1.0))) || (_7819.x > float(_7794))) || (_7819.y > float(_7797)))
                        {
                            _29108 = 0.0;
                            break;
                        }
                        ivec2 _7839 = ivec2(floor(_7819));
                        vec2 _7843 = _7819 - vec2(_7839);
                        float _7845 = _7843.x;
                        float _7926 = _7845 * _7845;
                        float _7929 = _7926 * _7845;
                        vec4 _7779 = vec4(((1.0 - (3.0 * _7845)) + (3.0 * _7926)) - _7929, (4.0 - (6.0 * _7926)) + (3.0 * _7929), ((1.0 + (3.0 * _7845)) + (3.0 * _7926)) - (3.0 * _7929), _7929) * 0.16666667163372039794921875;
                        float _7848 = _7843.y;
                        float _7962 = _7848 * _7848;
                        float _7965 = _7962 * _7848;
                        vec4 _7781 = vec4(((1.0 - (3.0 * _7848)) + (3.0 * _7962)) - _7965, (4.0 - (6.0 * _7962)) + (3.0 * _7965), ((1.0 + (3.0 * _7848)) + (3.0 * _7962)) - (3.0 * _7965), _7965) * 0.16666667163372039794921875;
                        float _29101;
                        _29101 = 0.0;
                        float _29435;
                        SPIRV_CROSS_UNROLL
                        for (int _29100 = 0; _29100 < 4; _29101 = _29435, _29100++)
                        {
                            int _7859 = (_7839.y - 1) + _29100;
                            if ((_7859 < 0) || (_7859 >= _7797))
                            {
                                _29435 = _29101;
                                continue;
                            }
                            float _29104;
                            _29104 = 0.0;
                            float _29437;
                            SPIRV_CROSS_UNROLL
                            for (int _29102 = 0; _29102 < 4; _29104 = _29437, _29102++)
                            {
                                int _7877 = (_7839.x - 1) + _29102;
                                if ((_7877 >= 0) && (_7877 < _7794))
                                {
                                    _29437 = _29104 + (_7779[_29102] * float(Wetness_1._data[uint(_7877 + (_7859 * _7794))]));
                                }
                                else
                                {
                                    _29437 = _29104;
                                }
                            }
                            _29435 = _29101 + (_7781[_29100] * _29104);
                        }
                        _29108 = _29101 * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _5750 = clamp(((_29108 * _283.WetParams.w) - 0.00999999977648258209228515625) * 1.01010096073150634765625, 0.0, 1.0);
                    vec3 _29179;
                    if (_5750 > 0.001000000047497451305389404296875)
                    {
                        vec3 _5757 = normalize(_5728 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w));
                        vec3 _29165;
                        do
                        {
                            float _8028 = smoothstep(0.449999988079071044921875, 1.0, _5750);
                            vec3 _8032 = _7613 * mix(1.0, 0.5, clamp(_5750 * 2.0, 0.0, 1.0));
                            if (_8028 <= 0.0)
                            {
                                _29165 = _8032;
                                break;
                            }
                            vec3 _8050 = reflect(_5757, vec3(0.0, 1.0, 0.0));
                            float _8109 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec3 _8099 = vec3(((_5614.x - (0.5 * float(_283.OutputWidth))) * _7526) / _8109, ((-(_5614.y - (0.5 * float(_283.OutputHeight)))) * _7526) / _8109, _7526);
                            float _8126 = dot(_8050, _283.CameraForward.xyz);
                            vec3 _8127 = vec3(dot(_8050, _283.CameraRight.xyz), dot(_8050, _283.CameraUp.xyz), _8126);
                            float _29138;
                            vec3 _29158;
                            do
                            {
                                if (_283.ReflectionDistance <= 0.0)
                                {
                                    _29158 = vec3(0.0);
                                    _29138 = 0.0;
                                    break;
                                }
                                float _8176 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _8126);
                                if (_8176 <= 0.0)
                                {
                                    _29158 = vec3(0.0);
                                    _29138 = 0.0;
                                    break;
                                }
                                float _8373 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _8185 = max(0.0040000001899898052215576171875 * _7526, 0.00999999977648258209228515625);
                                float _8187 = 0.5 * _8185;
                                float _8196;
                                float _8198;
                                bool _29123;
                                bool _29128;
                                float _29133;
                                float _29139;
                                vec3 _29159;
                                int _29114 = 0;
                                float _29115 = _8187;
                                float _29116 = _8185;
                                float _29134 = -1.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_29114 < 80)
                                    {
                                        _8196 = _29115 + _29116;
                                        _8198 = _29116 * 1.07000005245208740234375;
                                        if (_8196 > _283.ReflectionDistance)
                                        {
                                            _29159 = vec3(0.0);
                                            _29139 = 0.0;
                                            _29133 = _29134;
                                            _29128 = false;
                                            _29123 = true;
                                            break;
                                        }
                                        vec3 _8209 = _8099 + (_8127 * _8196);
                                        if (_8209.z <= 0.0500000007450580596923828125)
                                        {
                                            _29159 = vec3(0.0);
                                            _29139 = 0.0;
                                            _29133 = _29134;
                                            _29128 = false;
                                            _29123 = true;
                                            break;
                                        }
                                        float _8413 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        float _8389 = ((_8209.x * _8413) / _8209.z) + (0.5 * float(_283.OutputWidth));
                                        float _8402 = (((-_8209.y) * _8413) / _8209.z) + (0.5 * float(_283.OutputHeight));
                                        if (!((((_8389 >= 0.0) && (_8402 >= 0.0)) && (_8389 < float(_283.OutputWidth))) && (_8402 < float(_283.OutputHeight))))
                                        {
                                            _29159 = vec3(0.0);
                                            _29139 = _27312;
                                            _29133 = _29134;
                                            _29128 = true;
                                            _29123 = false;
                                            break;
                                        }
                                        ivec2 _8497 = clamp(ivec2(vec2(_8389, _8402)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _8449 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8497.x, int((_283.OutputHeight - 1u) - uint(_8497.y)), 0).xy, 0);
                                        float _8450 = _8449.x;
                                        float _8467 = ((_8450 > 0.0) && (abs(_8450 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_8450 - _283.ProjA)) : 1000000000.0;
                                        if (((_8467 < 100000000.0) && (_8209.z > _8467)) && ((_8209.z - _8467) < max(_29116 * 2.1400001049041748046875, 0.25)))
                                        {
                                            float _29118;
                                            _29118 = _8196;
                                            float _29452;
                                            float _29453;
                                            int _29117 = 0;
                                            float _29121 = _29115;
                                            SPIRV_CROSS_UNROLL
                                            for (; _29117 < 6; _29121 = _29452, _29118 = _29453, _29117++)
                                            {
                                                float _8253 = 0.5 * (_29121 + _29118);
                                                vec3 _8258 = _8099 + (_8127 * _8253);
                                                float _8567 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                ivec2 _8628 = clamp(ivec2(vec2(((_8258.x * _8567) / _8258.z) + (0.5 * float(_283.OutputWidth)), (((-_8258.y) * _8567) / _8258.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                vec4 _8580 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8628.x, int((_283.OutputHeight - 1u) - uint(_8628.y)), 0).xy, 0);
                                                float _8581 = _8580.x;
                                                bool _8265 = _8258.z > (((_8581 > 0.0) && (abs(_8581 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_8581 - _283.ProjA)) : 1000000000.0);
                                                _29452 = _8265 ? _29121 : _8253;
                                                _29453 = _8265 ? _8253 : _29118;
                                            }
                                            vec3 _8279 = _8099 + (_8127 * _29118);
                                            float _8698 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            vec2 _8688 = vec2(((_8279.x * _8698) / _8279.z) + (0.5 * float(_283.OutputWidth)), (((-_8279.y) * _8698) / _8279.z) + (0.5 * float(_283.OutputHeight)));
                                            ivec2 _8756 = clamp(ivec2(_8688), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                            vec3 _8716 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_8756.x, int((_283.OutputHeight - 1u) - uint(_8756.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                            float _8718 = length(_8716);
                                            vec3 _8724 = _8716 / vec3(_8718);
                                            bvec3 _8725 = bvec3(_8718 > 0.001000000047497451305389404296875);
                                            vec3 _8726 = vec3(_8725.x ? _8724.x : vec3(0.0, 1.0, 0.0).x, _8725.y ? _8724.y : vec3(0.0, 1.0, 0.0).y, _8725.z ? _8724.z : vec3(0.0, 1.0, 0.0).z);
                                            if (dot(vec3(dot(_8726, _283.CameraRight.xyz), dot(_8726, _283.CameraUp.xyz), dot(_8726, _283.CameraForward.xyz)), _8127) > 0.100000001490116119384765625)
                                            {
                                                int _8306 = _29114 + 1;
                                                _29116 = _8198;
                                                _29115 = _8196;
                                                _29114 = _8306;
                                                _29134 = _8196;
                                                continue;
                                            }
                                            float _8820 = clamp(((0.039999999105930328369140625 * _29118) * _8373) / max(_8279.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                            float _8822 = 0.5 * _8820;
                                            vec2 _8907 = clamp(_8688 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8933 = clamp((_8688 + vec2(_8822)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8959 = clamp((_8688 + vec2(_8820 * (-0.5), _8822)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8985 = clamp((_8688 + vec2(_8822, _8820 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9011 = clamp((_8688 + vec2(_8820 * (-0.5), _8820 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9037 = clamp((_8688 + vec2(_8820, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9063 = clamp((_8688 + vec2(-_8820, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9089 = clamp((_8688 + vec2(0.0, _8820)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9115 = clamp((_8688 + vec2(0.0, -_8820)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec3 _8892 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8907.x, 1.0 - _8907.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8933.x, 1.0 - _8933.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8959.x, 1.0 - _8959.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8985.x, 1.0 - _8985.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9011.x, 1.0 - _9011.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9037.x, 1.0 - _9037.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9063.x, 1.0 - _9063.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9089.x, 1.0 - _9089.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9115.x, 1.0 - _9115.y), 0.0).xyz) * 0.100000001490116119384765625;
                                            _29159 = _8892;
                                            _29139 = _8176;
                                            _29133 = _8196;
                                            _29128 = false;
                                            _29123 = true;
                                            break;
                                        }
                                        int _8306 = _29114 + 1;
                                        _29116 = _8198;
                                        _29115 = _8196;
                                        _29114 = _8306;
                                        _29134 = _8196;
                                        continue;
                                    }
                                    else
                                    {
                                        _29159 = vec3(0.0);
                                        _29139 = _27312;
                                        _29133 = _29134;
                                        _29128 = false;
                                        _29123 = false;
                                        break;
                                    }
                                }
                                if (_29123)
                                {
                                    _29158 = _29159;
                                    _29138 = _29139;
                                    break;
                                }
                                if ((!_29128) || (_29133 < 0.0))
                                {
                                    _29158 = _29159;
                                    _29138 = 0.0;
                                    break;
                                }
                                vec3 _8321 = _8099 + (_8127 * _29133);
                                float _9166 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec2 _9156 = vec2(((_8321.x * _9166) / _8321.z) + (0.5 * float(_283.OutputWidth)), (((-_8321.y) * _9166) / _8321.z) + (0.5 * float(_283.OutputHeight)));
                                ivec2 _9227 = clamp(ivec2(_9156), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _9179 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_9227.x, int((_283.OutputHeight - 1u) - uint(_9227.y)), 0).xy, 0);
                                float _9180 = _9179.x;
                                float _9197 = ((_9180 > 0.0) && (abs(_9180 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_9180 - _283.ProjA)) : 1000000000.0;
                                float _9273 = clamp(((0.039999999105930328369140625 * _29133) * _8373) / max(_8321.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                float _9275 = 0.5 * _9273;
                                vec2 _9360 = clamp(_9156 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9386 = clamp((_9156 + vec2(_9275)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9412 = clamp((_9156 + vec2(_9273 * (-0.5), _9275)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9438 = clamp((_9156 + vec2(_9275, _9273 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9464 = clamp((_9156 + vec2(_9273 * (-0.5), _9273 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9490 = clamp((_9156 + vec2(_9273, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9516 = clamp((_9156 + vec2(-_9273, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9542 = clamp((_9156 + vec2(0.0, _9273)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9568 = clamp((_9156 + vec2(0.0, -_9273)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec3 _9345 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9360.x, 1.0 - _9360.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9386.x, 1.0 - _9386.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9412.x, 1.0 - _9412.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9438.x, 1.0 - _9438.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9464.x, 1.0 - _9464.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9490.x, 1.0 - _9490.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9516.x, 1.0 - _9516.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9542.x, 1.0 - _9542.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9568.x, 1.0 - _9568.y), 0.0).xyz) * 0.100000001490116119384765625;
                                if (_9197 > 100000000.0)
                                {
                                    _29158 = _9345;
                                    _29138 = _8176;
                                    break;
                                }
                                float _8346 = _9197 - _8321.z;
                                if (_8346 <= 0.0)
                                {
                                    _29158 = _9345;
                                    _29138 = _8176;
                                    break;
                                }
                                _29158 = _9345;
                                _29138 = _8176 * exp((_8346 / max(_8126, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                                break;
                            } while(false);
                            vec3 _29164;
                            do
                            {
                                if (_283.EnvParams.x <= 0.0)
                                {
                                    float _9622 = _8050.y;
                                    _29164 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _9622))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _9622)));
                                    break;
                                }
                                _29164 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_8050.x, _8050.y, -_8050.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
                                break;
                            } while(false);
                            _29165 = mix(_8032, mix(_29164, _29158, vec3(_29138)), vec3((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp((-_5757).y, 0.0, 1.0), 5.0))) * _8028));
                            break;
                        } while(false);
                        _29179 = _29165;
                    }
                    else
                    {
                        _29179 = _7613;
                    }
                    _29178 = _29179;
                    _29167 = _5750;
                }
                else
                {
                    _29178 = _7613;
                    _29167 = 0.0;
                }
                _29177 = _29178;
                _29166 = _29167;
            }
            else
            {
                _29177 = _7613;
                _29166 = 0.0;
            }
            if (_283.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_5606), vec4(_29166, _29166, _29166, 1.0));
                break;
            }
            if ((_29166 <= 0.001000000047497451305389404296875) && (_5693 <= 0.001000000047497451305389404296875))
            {
                if (_283.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_5606), vec4(0.0));
                }
                break;
            }
            vec3 _9658 = ((_283.SunColor.xyz * (clamp(_5670.y, 0.0, 1.0) * 0.3183098733425140380859375)) + _6685) * 0.85000002384185791015625;
            vec3 _9664 = mix(vec3(dot(_9658, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _9658, vec3(0.5));
            if (_29166 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_5606), vec4(mix(_29177, _9664, vec3(_5693)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_5606), vec4(_9664, _5693));
            }
            break;
        }
        float _9707 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _9683 = ((_5614.x - (0.5 * float(_283.OutputWidth))) * _26779) / _9707;
        float _9695 = ((-(_5614.y - (0.5 * float(_283.OutputHeight)))) * _26779) / _9707;
        vec3 _9697 = vec3(_9683, _9695, _26779);
        vec3 _5825 = normalize(_9697);
        vec3 _5827 = -_5825;
        int _5843 = clamp(int((_283.NormalStep * max(float(_283.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _5849 = (2.0 * max(_283.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_283.NormalStep, 1.0);
        ivec2 _5851 = ivec2(_5843, 0);
        vec3 _26787;
        do
        {
            float _26783;
            do
            {
                vec2 _9865 = (((vec2(clamp(_5609 + _5851, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _9868 = ivec2(floor(_9865));
                vec2 _9872 = _9865 - vec2(_9868);
                ivec2 _10047 = clamp(_9868, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10083 = clamp(_9868 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10119 = clamp(_9868 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10155 = clamp(_9868 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _9835 = vec4((1.0 - _9872.x) * (1.0 - _9872.y), _9872.x * (1.0 - _9872.y), (1.0 - _9872.x) * _9872.y, _9872.x * _9872.y);
                vec4 _9913 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10047.x, int((FluidSize.y - 1u) - uint(_10047.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10083.x, int((FluidSize.y - 1u) - uint(_10083.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10119.x, int((FluidSize.y - 1u) - uint(_10119.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10155.x, int((FluidSize.y - 1u) - uint(_10155.y)), 0).xy, 0).x);
                vec4 _9836 = _9913;
                float _29098;
                _29098 = 1000000000.0;
                float _29434;
                SPIRV_CROSS_UNROLL
                for (int _26781 = 0; _26781 < 4; _29098 = _29434, _26781++)
                {
                    if (_9836[_26781] > 0.0)
                    {
                        _29434 = min(_29098, _9836[_26781]);
                    }
                    else
                    {
                        _29434 = _29098;
                    }
                }
                vec4 _9839 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _26782 = 0; _26782 < 4; )
                {
                    _9839[_26782] = ((_9836[_26782] > 0.0) && (_9836[_26782] < (_29098 + max(0.25, 0.02999999932944774627685546875 * _29098)))) ? _9835[_26782] : 0.0;
                    _26782++;
                    continue;
                }
                float _9973 = ((_9839.x + _9839.y) + _9839.z) + _9839.w;
                if (_9973 <= 9.9999997473787516355514526367188e-05)
                {
                    _26783 = 0.0;
                    break;
                }
                _26783 = dot(_9839, _9913) / _9973;
                break;
            } while(false);
            float _26786;
            do
            {
                vec2 _10395 = (((vec2(clamp(_5609 - _5851, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _10398 = ivec2(floor(_10395));
                vec2 _10402 = _10395 - vec2(_10398);
                ivec2 _10577 = clamp(_10398, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10613 = clamp(_10398 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10649 = clamp(_10398 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10685 = clamp(_10398 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10365 = vec4((1.0 - _10402.x) * (1.0 - _10402.y), _10402.x * (1.0 - _10402.y), (1.0 - _10402.x) * _10402.y, _10402.x * _10402.y);
                vec4 _10443 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10577.x, int((FluidSize.y - 1u) - uint(_10577.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10613.x, int((FluidSize.y - 1u) - uint(_10613.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10649.x, int((FluidSize.y - 1u) - uint(_10649.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10685.x, int((FluidSize.y - 1u) - uint(_10685.y)), 0).xy, 0).x);
                vec4 _10366 = _10443;
                float _29095;
                _29095 = 1000000000.0;
                float _29433;
                SPIRV_CROSS_UNROLL
                for (int _26784 = 0; _26784 < 4; _29095 = _29433, _26784++)
                {
                    if (_10366[_26784] > 0.0)
                    {
                        _29433 = min(_29095, _10366[_26784]);
                    }
                    else
                    {
                        _29433 = _29095;
                    }
                }
                vec4 _10369 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _26785 = 0; _26785 < 4; )
                {
                    _10369[_26785] = ((_10366[_26785] > 0.0) && (_10366[_26785] < (_29095 + max(0.25, 0.02999999932944774627685546875 * _29095)))) ? _10365[_26785] : 0.0;
                    _26785++;
                    continue;
                }
                float _10503 = ((_10369.x + _10369.y) + _10369.z) + _10369.w;
                if (_10503 <= 9.9999997473787516355514526367188e-05)
                {
                    _26786 = 0.0;
                    break;
                }
                _26786 = dot(_10369, _10443) / _10503;
                break;
            } while(false);
            bool _9741 = (_26783 > 0.0) && (abs(_26783 - _26779) < _5849);
            bool _9750 = (_26786 > 0.0) && (abs(_26779 - _26786) < _5849);
            vec2 _9757 = vec2(_5609 + _5851) + vec2(0.5);
            float _10891 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9761 = vec3(((_9757.x - (0.5 * float(_283.OutputWidth))) * _26783) / _10891, ((-(_9757.y - (0.5 * float(_283.OutputHeight)))) * _26783) / _10891, _26783) - _9697;
            bvec3 _9762 = bvec3(_9741);
            vec3 _9763 = vec3(_9762.x ? _9761.x : vec3(0.0).x, _9762.y ? _9761.y : vec3(0.0).y, _9762.z ? _9761.z : vec3(0.0).z);
            vec2 _9771 = vec2(_5609 - _5851) + vec2(0.5);
            float _10930 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9774 = _9697 - vec3(((_9771.x - (0.5 * float(_283.OutputWidth))) * _26786) / _10930, ((-(_9771.y - (0.5 * float(_283.OutputHeight)))) * _26786) / _10930, _26786);
            bvec3 _9775 = bvec3(_9750);
            vec3 _9776 = vec3(_9775.x ? _9774.x : vec3(0.0).x, _9775.y ? _9774.y : vec3(0.0).y, _9775.z ? _9774.z : vec3(0.0).z);
            if (_9741 && _9750)
            {
                _26787 = (_9763 + _9776) * 0.5;
                break;
            }
            bvec3 _9789 = bvec3(_9741);
            _26787 = vec3(_9789.x ? _9763.x : _9776.x, _9789.y ? _9763.y : _9776.y, _9789.z ? _9763.z : _9776.z);
            break;
        } while(false);
        ivec2 _5858 = ivec2(0, _5843);
        vec3 _26794;
        do
        {
            float _26790;
            do
            {
                vec2 _11088 = (((vec2(clamp(_5609 + _5858, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _11091 = ivec2(floor(_11088));
                vec2 _11095 = _11088 - vec2(_11091);
                ivec2 _11270 = clamp(_11091, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11306 = clamp(_11091 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11342 = clamp(_11091 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11378 = clamp(_11091 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11058 = vec4((1.0 - _11095.x) * (1.0 - _11095.y), _11095.x * (1.0 - _11095.y), (1.0 - _11095.x) * _11095.y, _11095.x * _11095.y);
                vec4 _11136 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11270.x, int((FluidSize.y - 1u) - uint(_11270.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11306.x, int((FluidSize.y - 1u) - uint(_11306.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11342.x, int((FluidSize.y - 1u) - uint(_11342.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11378.x, int((FluidSize.y - 1u) - uint(_11378.y)), 0).xy, 0).x);
                vec4 _11059 = _11136;
                float _29092;
                _29092 = 1000000000.0;
                float _29432;
                SPIRV_CROSS_UNROLL
                for (int _26788 = 0; _26788 < 4; _29092 = _29432, _26788++)
                {
                    if (_11059[_26788] > 0.0)
                    {
                        _29432 = min(_29092, _11059[_26788]);
                    }
                    else
                    {
                        _29432 = _29092;
                    }
                }
                vec4 _11062 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _26789 = 0; _26789 < 4; )
                {
                    _11062[_26789] = ((_11059[_26789] > 0.0) && (_11059[_26789] < (_29092 + max(0.25, 0.02999999932944774627685546875 * _29092)))) ? _11058[_26789] : 0.0;
                    _26789++;
                    continue;
                }
                float _11196 = ((_11062.x + _11062.y) + _11062.z) + _11062.w;
                if (_11196 <= 9.9999997473787516355514526367188e-05)
                {
                    _26790 = 0.0;
                    break;
                }
                _26790 = dot(_11062, _11136) / _11196;
                break;
            } while(false);
            float _26793;
            do
            {
                vec2 _11618 = (((vec2(clamp(_5609 - _5858, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _11621 = ivec2(floor(_11618));
                vec2 _11625 = _11618 - vec2(_11621);
                ivec2 _11800 = clamp(_11621, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11836 = clamp(_11621 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11872 = clamp(_11621 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11908 = clamp(_11621 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11588 = vec4((1.0 - _11625.x) * (1.0 - _11625.y), _11625.x * (1.0 - _11625.y), (1.0 - _11625.x) * _11625.y, _11625.x * _11625.y);
                vec4 _11666 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11800.x, int((FluidSize.y - 1u) - uint(_11800.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11836.x, int((FluidSize.y - 1u) - uint(_11836.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11872.x, int((FluidSize.y - 1u) - uint(_11872.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11908.x, int((FluidSize.y - 1u) - uint(_11908.y)), 0).xy, 0).x);
                vec4 _11589 = _11666;
                float _29089;
                _29089 = 1000000000.0;
                float _29431;
                SPIRV_CROSS_UNROLL
                for (int _26791 = 0; _26791 < 4; _29089 = _29431, _26791++)
                {
                    if (_11589[_26791] > 0.0)
                    {
                        _29431 = min(_29089, _11589[_26791]);
                    }
                    else
                    {
                        _29431 = _29089;
                    }
                }
                vec4 _11592 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _26792 = 0; _26792 < 4; )
                {
                    _11592[_26792] = ((_11589[_26792] > 0.0) && (_11589[_26792] < (_29089 + max(0.25, 0.02999999932944774627685546875 * _29089)))) ? _11588[_26792] : 0.0;
                    _26792++;
                    continue;
                }
                float _11726 = ((_11592.x + _11592.y) + _11592.z) + _11592.w;
                if (_11726 <= 9.9999997473787516355514526367188e-05)
                {
                    _26793 = 0.0;
                    break;
                }
                _26793 = dot(_11592, _11666) / _11726;
                break;
            } while(false);
            bool _10964 = (_26790 > 0.0) && (abs(_26790 - _26779) < _5849);
            bool _10973 = (_26793 > 0.0) && (abs(_26779 - _26793) < _5849);
            vec2 _10980 = vec2(_5609 + _5858) + vec2(0.5);
            float _12114 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10984 = vec3(((_10980.x - (0.5 * float(_283.OutputWidth))) * _26790) / _12114, ((-(_10980.y - (0.5 * float(_283.OutputHeight)))) * _26790) / _12114, _26790) - _9697;
            bvec3 _10985 = bvec3(_10964);
            vec3 _10986 = vec3(_10985.x ? _10984.x : vec3(0.0).x, _10985.y ? _10984.y : vec3(0.0).y, _10985.z ? _10984.z : vec3(0.0).z);
            vec2 _10994 = vec2(_5609 - _5858) + vec2(0.5);
            float _12153 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10997 = _9697 - vec3(((_10994.x - (0.5 * float(_283.OutputWidth))) * _26793) / _12153, ((-(_10994.y - (0.5 * float(_283.OutputHeight)))) * _26793) / _12153, _26793);
            bvec3 _10998 = bvec3(_10973);
            vec3 _10999 = vec3(_10998.x ? _10997.x : vec3(0.0).x, _10998.y ? _10997.y : vec3(0.0).y, _10998.z ? _10997.z : vec3(0.0).z);
            if (_10964 && _10973)
            {
                _26794 = (_10986 + _10999) * 0.5;
                break;
            }
            bvec3 _11012 = bvec3(_10964);
            _26794 = vec3(_11012.x ? _10986.x : _10999.x, _11012.y ? _10986.y : _10999.y, _11012.z ? _10986.z : _10999.z);
            break;
        } while(false);
        vec2 _5869 = _26787.xy * max(_283.NormalScale, 0.0);
        vec3 _25920 = _26787;
        _25920.x = _5869.x;
        _25920.y = _5869.y;
        vec2 _5879 = _26794.xy * max(_283.NormalScale, 0.0);
        vec3 _25924 = _26794;
        _25924.x = _5879.x;
        _25924.y = _5879.y;
        bvec3 _29454 = bvec3(length(_25920) < 9.9999999747524270787835121154785e-07);
        bvec3 _29456 = bvec3(length(_25924) < 9.9999999747524270787835121154785e-07);
        vec3 _5897 = normalize(cross(vec3(_29454.x ? vec3(1.0, 0.0, 0.0).x : _25920.x, _29454.y ? vec3(1.0, 0.0, 0.0).y : _25920.y, _29454.z ? vec3(1.0, 0.0, 0.0).z : _25920.z), vec3(_29456.x ? vec3(0.0, -1.0, 0.0).x : _25924.x, _29456.y ? vec3(0.0, -1.0, 0.0).y : _25924.y, _29456.z ? vec3(0.0, -1.0, 0.0).z : _25924.z)));
        vec3 _26813;
        if (dot(_5897, _5827) < 0.0)
        {
            _26813 = -_5897;
        }
        else
        {
            _26813 = _5897;
        }
        vec3 _26818;
        if (_283.Mode == 2u)
        {
            vec4 _26808;
            do
            {
                vec2 _12198 = ((_5614 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _12201 = ivec2(floor(_12198));
                vec2 _12205 = _12198 - vec2(_12201);
                ivec2 _12354 = clamp(_12201, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12390 = clamp(_12201 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12426 = clamp(_12201 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12462 = clamp(_12201 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _12167 = vec4((1.0 - _12205.x) * (1.0 - _12205.y), _12205.x * (1.0 - _12205.y), (1.0 - _12205.x) * _12205.y, _12205.x * _12205.y);
                vec4 _12168 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12354.x, int((FluidSize.y - 1u) - uint(_12354.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12390.x, int((FluidSize.y - 1u) - uint(_12390.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12426.x, int((FluidSize.y - 1u) - uint(_12426.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12462.x, int((FluidSize.y - 1u) - uint(_12462.y)), 0).xy, 0).x);
                float _29086;
                _29086 = 1000000000.0;
                float _29430;
                SPIRV_CROSS_UNROLL
                for (int _26806 = 0; _26806 < 4; _29086 = _29430, _26806++)
                {
                    if (_12168[_26806] > 0.0)
                    {
                        _29430 = min(_29086, _12168[_26806]);
                    }
                    else
                    {
                        _29430 = _29086;
                    }
                }
                vec4 _12171 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _26807 = 0; _26807 < 4; )
                {
                    _12171[_26807] = ((_12168[_26807] > 0.0) && (_12168[_26807] < (_29086 + max(0.25, 0.02999999932944774627685546875 * _29086)))) ? _12167[_26807] : 0.0;
                    _26807++;
                    continue;
                }
                float _12306 = ((_12171.x + _12171.y) + _12171.z) + _12171.w;
                if (_12306 <= 9.9999997473787516355514526367188e-05)
                {
                    _26808 = vec4(0.0);
                    break;
                }
                ivec2 _12498 = clamp(_12201, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12533 = clamp(_12201 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12568 = clamp(_12201 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12603 = clamp(_12201 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                _26808 = ((((texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12498.x, int((FluidSize.y - 1u) - uint(_12498.y)), 0).xy, 0) * _12171.x) + (texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12533.x, int((FluidSize.y - 1u) - uint(_12533.y)), 0).xy, 0) * _12171.y)) + (texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12568.x, int((FluidSize.y - 1u) - uint(_12568.y)), 0).xy, 0) * _12171.z)) + (texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12603.x, int((FluidSize.y - 1u) - uint(_12603.y)), 0).xy, 0) * _12171.w)) / vec4(_12306);
                break;
            } while(false);
            float _5914 = length(_26808.xyz);
            vec3 _26819;
            if (_5914 > 0.001000000047497451305389404296875)
            {
                vec3 _5922 = _26808.xyz / vec3(_5914);
                vec3 _26814;
                if (dot(_5922, _5827) < 0.0)
                {
                    _26814 = -_5922;
                }
                else
                {
                    _26814 = _5922;
                }
                _26819 = normalize(mix(_26813, _26814, vec3(clamp(_26808.w * 4.0, 0.0, 1.0))));
            }
            else
            {
                _26819 = _26813;
            }
            _26818 = _26819;
        }
        else
        {
            _26818 = _26813;
        }
        float _5944 = max(_26780.x, 9.9999997473787516355514526367188e-05);
        ivec2 _12724 = clamp(ivec2(floor((_5614 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        ivec2 _12734 = clamp(_12724, ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _12745 = (uint(_12734.x) + (uint(_12734.y) * FluidSize.x)) * 4u;
        uint _12755 = AuxBits_1._data[_12745 + 1u];
        uint _12767 = AuxBits_1._data[_12745 + 3u];
        float _26816;
        _26816 = float(AuxBits_1._data[_12745]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _26815 = 0; _26815 < 4; )
        {
            ivec2 _12659 = ivec2((_26815 * 2) - 1, 0);
            ivec2 _12664 = ivec2(0, ((_26815 - 2) * 2) - 1);
            bvec2 _12665 = bvec2(_26815 < 2);
            ivec2 _12780 = clamp(_12724 + ivec2(_12665.x ? _12659.x : _12664.x, _12665.y ? _12659.y : _12664.y), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _12826 = clamp(_12724 + ivec2(((_26815 & 1) != int(0u)) ? 1 : (-1), ((_26815 & 2) != int(0u)) ? 1 : (-1)), ivec2(0), ivec2(FluidSize) - ivec2(1));
            _26816 += ((float(AuxBits_1._data[(uint(_12780.x) + (uint(_12780.y) * FluidSize.x)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_12826.x) + (uint(_12826.y) * FluidSize.x)) * 4u]) * 1.525902189314365386962890625e-05));
            _26815++;
            continue;
        }
        float _5963 = clamp((float(_12767) * 0.0009765625) / _5944, 0.0, 1.0);
        vec3 _12884 = ((_283.CameraRight.xyz * _5825.x) + (_283.CameraUp.xyz * _5825.y)) + (_283.CameraForward.xyz * _5825.z);
        vec3 _5969 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * _9683) + (_283.CameraUp.xyz * _9695)) + (_283.CameraForward.xyz * _26779));
        vec3 _12937 = ((_283.CameraRight.xyz * _26818.x) + (_283.CameraUp.xyz * _26818.y)) + (_283.CameraForward.xyz * _26818.z);
        float _5980 = (_26779 / ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_26818, _5827)), 0.07999999821186065673828125);
        float _6004 = (_283.RippleParams.x * mix(_283.RippleFlow.y, 1.0 + _283.RippleFlow.x, clamp((float(AuxBits_1._data[_12745 + 2u]) * 0.0009765625) / _5944, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _12937.y) * clamp(1.0 - (length(_9697) / max(_283.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _26824;
        vec3 _26864;
        if (_6004 > 0.0)
        {
            vec2 _6010 = _5969.xz;
            float _12967 = max(_283.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _26821;
            float _26822;
            _26822 = 0.0;
            _26821 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _26820 = 0; _26820 < 6; )
            {
                float _12979 = _12967 * _1923[_26820];
                float _12984 = _6004 * pow(0.7799999713897705078125, float(_26820));
                float _12991 = clamp((_12979 / max(_5980 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _13067 = (((uint(_26820) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _13075 = ((_13067 >> ((_13067 >> 28u) + 4u)) ^ _13067) * 277803737u;
                float _12998 = 0.60000002384185791015625 + (float(((_13075 >> 22u) ^ _13075) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _13003 = vec2(cos(_12998), sin(_12998));
                uint _13094 = (((uint(_26820) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _13102 = ((_13094 >> ((_13094 >> 28u) + 4u)) ^ _13094) * 277803737u;
                _26822 += (((0.5 * _12984) * _12984) * (1.0 - (_12991 * _12991)));
                _26821 += (_13003 * ((_12984 * _12991) * cos(((dot(_13003, _6010) * (6.283185482025146484375 / _12979)) + ((sqrt(61.63805389404296875 / _12979) * _283.RippleParams.z) * _283.ShadeParams.z)) + (float(((_13102 >> 22u) ^ _13102) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _26820++;
                continue;
            }
            vec3 _6025 = normalize(_12937 + vec3(-_26821.x, 0.0, -_26821.y));
            vec3 _6027 = normalize(vec3(dot(_6025, _283.CameraRight.xyz), dot(_6025, _283.CameraUp.xyz), dot(_6025, _283.CameraForward.xyz)));
            bvec3 _29460 = bvec3(dot(_6027, _5827) < 0.0199999995529651641845703125);
            _26864 = vec3(_29460.x ? _26818.x : _6027.x, _29460.y ? _26818.y : _6027.y, _29460.z ? _26818.z : _6027.z);
            _26824 = _26822;
        }
        else
        {
            _26864 = _26818;
            _26824 = 0.0;
        }
        float _6039 = _26824 * _283.RippleFlow.z;
        if (_283.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_5606), vec4((_26864 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_283.DebugView == 2u)
        {
            float _6062 = clamp(_26779 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5606), vec4(_6062, _6062, _6062, 1.0));
            break;
        }
        float _6082 = (_7526 < 100000000.0) ? (max(_7526 - _26779, 0.0) / max(_5825.z, 0.0500000007450580596923828125)) : 10000.0;
        float _6087 = _5944 * max(_283.ThicknessScale, 0.0);
        float _6092 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _6087 / max(_6082, 0.001000000047497451305389404296875));
        float _26862;
        do
        {
            vec3 _13158 = (_5969 + (_29451 * (max(_5969.y - _283.PoolMin.y, 0.0500000007450580596923828125) / max(-_29451.y, 0.20000000298023223876953125)))) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
            float _13224 = dot(_13158, _283.CameraForward.xyz);
            if (_13224 <= 0.100000001490116119384765625)
            {
                _26862 = 1.0;
                break;
            }
            float _13265 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _13241 = ((dot(_13158, _283.CameraRight.xyz) * _13265) / _13224) + (0.5 * float(_283.OutputWidth));
            float _13254 = (((-dot(_13158, _283.CameraUp.xyz)) * _13265) / _13224) + (0.5 * float(_283.OutputHeight));
            vec2 _13255 = vec2(_13241, _13254);
            if (!((((_13241 >= 0.0) && (_13254 >= 0.0)) && (_13241 < float(_283.OutputWidth))) && (_13254 < float(_283.OutputHeight))))
            {
                _26862 = 1.0;
                break;
            }
            ivec2 _13349 = clamp(ivec2(_13255), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _13301 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13349.x, int((_283.OutputHeight - 1u) - uint(_13349.y)), 0).xy, 0);
            float _13302 = _13301.x;
            if (abs((((_13302 > 0.0) && (abs(_13302 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_13302 - _283.ProjA)) : 1000000000.0) - _13224) > ((0.1500000059604644775390625 * _13224) + 0.100000001490116119384765625))
            {
                _26862 = 1.0;
                break;
            }
            ivec2 _13409 = clamp(ivec2(_13255), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            _26862 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_13409.x, int((_283.OutputHeight - 1u) - uint(_13409.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _6098 = refract(_5825, _26864, 0.75187969207763671875);
        bvec3 _29464 = bvec3(dot(_6098, _6098) < 9.9999999747524270787835121154785e-07);
        vec3 _6117 = normalize(mix(_5825, vec3(_29464.x ? _5825.x : _6098.x, _29464.y ? _5825.y : _6098.y, _29464.z ? _5825.z : _6098.z), vec3(clamp(_283.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _6092))));
        vec3 _13461 = ((_283.CameraRight.xyz * _6117.x) + (_283.CameraUp.xyz * _6117.y)) + (_283.CameraForward.xyz * _6117.z);
        float _26879;
        do
        {
            if ((((_5969.x < _283.PoolMin.x) || (_5969.x > _283.PoolMax.x)) || (_5969.z < _283.PoolMin.z)) || (_5969.z > _283.PoolMax.z))
            {
                _26879 = 1000.0;
                break;
            }
            float _26867;
            if (_13461.x > 9.9999997473787516355514526367188e-05)
            {
                _26867 = (_283.PoolMax.x - _5969.x) / _13461.x;
            }
            else
            {
                float _26868;
                if (_13461.x < (-9.9999997473787516355514526367188e-05))
                {
                    _26868 = (_283.PoolMin.x - _5969.x) / _13461.x;
                }
                else
                {
                    _26868 = 1000.0;
                }
                _26867 = _26868;
            }
            float _29466 = (_26867 < 1000.0) ? _26867 : 1000.0;
            float _26871;
            if (_13461.z > 9.9999997473787516355514526367188e-05)
            {
                _26871 = (_283.PoolMax.z - _5969.z) / _13461.z;
            }
            else
            {
                float _26872;
                if (_13461.z < (-9.9999997473787516355514526367188e-05))
                {
                    _26872 = (_283.PoolMin.z - _5969.z) / _13461.z;
                }
                else
                {
                    _26872 = 1000.0;
                }
                _26871 = _26872;
            }
            float _29467 = (_26871 < _29466) ? _26871 : _29466;
            float _26877;
            if (_13461.y < (-9.9999997473787516355514526367188e-05))
            {
                float _13591 = (_283.PoolMin.y - _5969.y) / _13461.y;
                _26877 = (_13591 < _29467) ? _13591 : _29467;
            }
            else
            {
                _26877 = _29467;
            }
            _26879 = max(_26877, 0.0);
            break;
        } while(false);
        float _6136 = clamp(max((_6082 * 1.2000000476837158203125) + 0.20000000298023223876953125, (_26879 < 100.0) ? ((_26879 * 1.10000002384185791015625) + 0.100000001490116119384765625) : 0.0), 0.300000011920928955078125, max(_283.MaxTravel, 0.300000011920928955078125) * 2.0);
        vec3 _27225;
        float _27277;
        float _27329;
        float _27380;
        vec2 _27431;
        do
        {
            ivec2 _13933 = clamp(ivec2(_5614), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _13885 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13933.x, int((_283.OutputHeight - 1u) - uint(_13933.y)), 0).xy, 0);
            float _13886 = _13885.x;
            vec2 _27140;
            float _27170;
            _27170 = ((_13886 > 0.0) && (abs(_13886 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_13886 - _283.ProjA)) : 1000000000.0;
            _27140 = _5614;
            float _13681;
            float _29471;
            vec2 _29473;
            bool _26887;
            float _27199;
            vec3 _27226;
            float _27278;
            float _27330;
            float _27381;
            vec2 _27432;
            int _26880 = 1;
            float _26882 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_26880 <= 16)
                {
                    float _13675 = float(_26880) * 0.0625;
                    _13681 = (_6136 * _13675) * sqrt(_13675);
                    vec3 _13686 = _9697 + (_6117 * _13681);
                    if (_13686.z <= 0.0500000007450580596923828125)
                    {
                        _27432 = _27466;
                        _27381 = _27312;
                        _27330 = _27312;
                        _27278 = _27312;
                        _27226 = _27260;
                        _27199 = 0.0;
                        _26887 = false;
                        break;
                    }
                    float _14003 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _13979 = ((_13686.x * _14003) / _13686.z) + (0.5 * float(_283.OutputWidth));
                    float _13992 = (((-_13686.y) * _14003) / _13686.z) + (0.5 * float(_283.OutputHeight));
                    vec2 _13993 = vec2(_13979, _13992);
                    if (!((((_13979 >= 0.0) && (_13992 >= 0.0)) && (_13979 < float(_283.OutputWidth))) && (_13992 < float(_283.OutputHeight))))
                    {
                        _27432 = _27466;
                        _27381 = _27312;
                        _27330 = _27312;
                        _27278 = _27312;
                        _27226 = _27260;
                        _27199 = 0.0;
                        _26887 = false;
                        break;
                    }
                    ivec2 _14087 = clamp(ivec2(_13993), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _14039 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14087.x, int((_283.OutputHeight - 1u) - uint(_14087.y)), 0).xy, 0);
                    float _14040 = _14039.x;
                    float _14057 = ((_14040 > 0.0) && (abs(_14040 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14040 - _283.ProjA)) : 1000000000.0;
                    if (_13686.z >= _14057)
                    {
                        ivec2 _13708 = ivec2(_13993);
                        ivec2 _14168 = clamp(ivec2(floor(((vec2(_13708) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14177 = clamp(_14168, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _14203 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14177.x, int((FluidSize.y - 1u) - uint(_14177.y)), 0).xy, 0);
                        float _14204 = _14203.x;
                        ivec2 _14213 = clamp(_14168, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14300 = clamp(_13708, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _14252 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14300.x, int((_283.OutputHeight - 1u) - uint(_14300.y)), 0).xy, 0);
                        float _14253 = _14252.x;
                        if (!(((_14204 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14213.x, int((FluidSize.y - 1u) - uint(_14213.y)), 0).xy, 0).w > 0.5)) && ((((_14253 > 0.0) && (abs(_14253 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14253 - _283.ProjA)) : 1000000000.0) >= (_14204 + 0.0500000007450580596923828125))))
                        {
                            _27432 = _27466;
                            _27381 = _27312;
                            _27330 = _27312;
                            _27278 = _27312;
                            _27226 = _27260;
                            _27199 = 0.0;
                            _26887 = false;
                            break;
                        }
                        float _26884;
                        _26884 = _13681;
                        float _29469;
                        float _29470;
                        int _26883 = 0;
                        float _26885 = _26882;
                        SPIRV_CROSS_UNROLL
                        for (; _26883 < 5; _26885 = _29469, _26884 = _29470, _26883++)
                        {
                            float _13723 = 0.5 * (_26885 + _26884);
                            vec3 _13728 = _9697 + (_6117 * _13723);
                            float _14370 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _14431 = clamp(ivec2(vec2(((_13728.x * _14370) / _13728.z) + (0.5 * float(_283.OutputWidth)), (((-_13728.y) * _14370) / _13728.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                            vec4 _14383 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14431.x, int((_283.OutputHeight - 1u) - uint(_14431.y)), 0).xy, 0);
                            float _14384 = _14383.x;
                            bool _13735 = _13728.z >= (((_14384 > 0.0) && (abs(_14384 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14384 - _283.ProjA)) : 1000000000.0);
                            _29469 = _13735 ? _26885 : _13723;
                            _29470 = _13735 ? _13723 : _26884;
                        }
                        vec3 _13749 = _9697 + (_6117 * _26884);
                        float _14501 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _13760 = clamp(vec2(((_13749.x * _14501) / _13749.z) + (0.5 * float(_283.OutputWidth)), (((-_13749.y) * _14501) / _13749.z) + (0.5 * float(_283.OutputHeight))), vec2(0.5), vec2(float(_283.OutputWidth), float(_283.OutputHeight)) - vec2(0.5));
                        ivec2 _14562 = clamp(ivec2(_13760), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _14514 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14562.x, int((_283.OutputHeight - 1u) - uint(_14562.y)), 0).xy, 0);
                        float _14515 = _14514.x;
                        float _14532 = ((_14515 > 0.0) && (abs(_14515 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14515 - _283.ProjA)) : 1000000000.0;
                        float _14640 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        _27432 = _13760;
                        _27381 = _14532;
                        _27330 = _26884;
                        _27278 = 1.0;
                        _27226 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_13760.x - (0.5 * float(_283.OutputWidth))) * _14532) / _14640)) + (_283.CameraUp.xyz * (((-(_13760.y - (0.5 * float(_283.OutputHeight)))) * _14532) / _14640))) + (_283.CameraForward.xyz * _14532));
                        _27199 = 1.0;
                        _26887 = true;
                        break;
                    }
                    ivec2 _13784 = ivec2(_13993 - vec2(0.5));
                    ivec2 _14713 = clamp(ivec2(floor(((vec2(_13784) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14722 = clamp(_14713, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _14748 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14722.x, int((FluidSize.y - 1u) - uint(_14722.y)), 0).xy, 0);
                    float _14749 = _14748.x;
                    ivec2 _14758 = clamp(_14713, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14845 = clamp(_13784, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _14797 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14845.x, int((_283.OutputHeight - 1u) - uint(_14845.y)), 0).xy, 0);
                    float _14798 = _14797.x;
                    ivec2 _13789 = ivec2(_13993 + vec2(0.5));
                    ivec2 _14926 = clamp(ivec2(floor(((vec2(_13789) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14935 = clamp(_14926, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _14961 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14935.x, int((FluidSize.y - 1u) - uint(_14935.y)), 0).xy, 0);
                    float _14962 = _14961.x;
                    ivec2 _14971 = clamp(_14926, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15058 = clamp(_13789, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _15010 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15058.x, int((_283.OutputHeight - 1u) - uint(_15058.y)), 0).xy, 0);
                    float _15011 = _15010.x;
                    bool _13791 = (((_14749 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14758.x, int((FluidSize.y - 1u) - uint(_14758.y)), 0).xy, 0).w > 0.5)) && ((((_14798 > 0.0) && (abs(_14798 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14798 - _283.ProjA)) : 1000000000.0) >= (_14749 + 0.0500000007450580596923828125))) && (((_14962 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14971.x, int((FluidSize.y - 1u) - uint(_14971.y)), 0).xy, 0).w > 0.5)) && ((((_15011 > 0.0) && (abs(_15011 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15011 - _283.ProjA)) : 1000000000.0) >= (_14962 + 0.0500000007450580596923828125)));
                    _29471 = _13791 ? _14057 : _27170;
                    bvec2 _29472 = bvec2(_13791);
                    _29473 = vec2(_29472.x ? _13993.x : _27140.x, _29472.y ? _13993.y : _27140.y);
                    _26880++;
                    _27170 = _29471;
                    _27140 = _29473;
                    _26882 = _13681;
                    continue;
                }
                else
                {
                    _27432 = _27466;
                    _27381 = _27312;
                    _27330 = _27312;
                    _27278 = _27312;
                    _27226 = _27260;
                    _27199 = 0.0;
                    _26887 = false;
                    break;
                }
            }
            if (_26887)
            {
                _27431 = _27432;
                _27380 = _27381;
                _27329 = _27330;
                _27277 = _27278;
                _27225 = _27226;
                break;
            }
            vec3 _15110 = ((_283.CameraRight.xyz * _6117.x) + (_283.CameraUp.xyz * _6117.y)) + (_283.CameraForward.xyz * _6117.z);
            float _26902;
            vec3 _26903;
            do
            {
                if ((((_5969.x < _283.PoolMin.x) || (_5969.x > _283.PoolMax.x)) || (_5969.z < _283.PoolMin.z)) || (_5969.z > _283.PoolMax.z))
                {
                    _26903 = vec3(0.0, 1.0, 0.0);
                    _26902 = 1000.0;
                    break;
                }
                float _26890;
                if (_15110.x > 9.9999997473787516355514526367188e-05)
                {
                    _26890 = (_283.PoolMax.x - _5969.x) / _15110.x;
                }
                else
                {
                    float _26891;
                    if (_15110.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _26891 = (_283.PoolMin.x - _5969.x) / _15110.x;
                    }
                    else
                    {
                        _26891 = 1000.0;
                    }
                    _26890 = _26891;
                }
                bool _15172 = _26890 < 1000.0;
                vec3 _26907;
                if (_15172)
                {
                    _26907 = vec3((_15110.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                }
                else
                {
                    _26907 = vec3(0.0, 1.0, 0.0);
                }
                float _29474 = _15172 ? _26890 : 1000.0;
                float _26894;
                if (_15110.z > 9.9999997473787516355514526367188e-05)
                {
                    _26894 = (_283.PoolMax.z - _5969.z) / _15110.z;
                }
                else
                {
                    float _26895;
                    if (_15110.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _26895 = (_283.PoolMin.z - _5969.z) / _15110.z;
                    }
                    else
                    {
                        _26895 = 1000.0;
                    }
                    _26894 = _26895;
                }
                bool _15210 = _26894 < _29474;
                vec3 _26905;
                if (_15210)
                {
                    _26905 = vec3(0.0, 0.0, _29512);
                }
                else
                {
                    _26905 = _26907;
                }
                float _29475 = _15210 ? _26894 : _29474;
                float _26900;
                vec3 _26904;
                if (_15110.y < (-9.9999997473787516355514526367188e-05))
                {
                    float _15230 = (_283.PoolMin.y - _5969.y) / _15110.y;
                    bool _15233 = _15230 < _29475;
                    bvec3 _29476 = bvec3(_15233);
                    _26904 = vec3(_29476.x ? vec3(0.0, 1.0, 0.0).x : _26905.x, _29476.y ? vec3(0.0, 1.0, 0.0).y : _26905.y, _29476.z ? vec3(0.0, 1.0, 0.0).z : _26905.z);
                    _26900 = _15233 ? _15230 : _29475;
                }
                else
                {
                    _26904 = _26905;
                    _26900 = _29475;
                }
                _26903 = _26904;
                _26902 = max(_26900, 0.0);
                break;
            } while(false);
            if (_26902 < 100.0)
            {
                vec3 _13816 = _5969 + (_15110 * _26902);
                uint _27046;
                vec2 _27123;
                float _27130;
                do
                {
                    bool _26912;
                    vec2 _26913;
                    float _26914;
                    do
                    {
                        vec3 _15484 = _13816 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                        float _15542 = dot(_15484, _283.CameraForward.xyz);
                        if (_15542 <= 0.0500000007450580596923828125)
                        {
                            _26914 = 1000000000.0;
                            _26913 = vec2(0.5);
                            _26912 = false;
                            break;
                        }
                        float _15583 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _15559 = ((dot(_15484, _283.CameraRight.xyz) * _15583) / _15542) + (0.5 * float(_283.OutputWidth));
                        float _15572 = (((-dot(_15484, _283.CameraUp.xyz)) * _15583) / _15542) + (0.5 * float(_283.OutputHeight));
                        if (!((((_15559 >= 0.0) && (_15572 >= 0.0)) && (_15559 < float(_283.OutputWidth))) && (_15572 < float(_283.OutputHeight))))
                        {
                            _26914 = 1000000000.0;
                            _26913 = vec2(0.5);
                            _26912 = false;
                            break;
                        }
                        vec2 _15501 = floor(vec2(_15559, _15572)) + vec2(0.5);
                        ivec2 _15667 = clamp(ivec2(_15501), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _15619 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15667.x, int((_283.OutputHeight - 1u) - uint(_15667.y)), 0).xy, 0);
                        float _15620 = _15619.x;
                        float _15637 = ((_15620 > 0.0) && (abs(_15620 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15620 - _283.ProjA)) : 1000000000.0;
                        _26914 = _15637;
                        _26913 = _15501;
                        _26912 = abs(_15637 - _15542) < ((0.0199999995529651641845703125 * _15542) + 0.02999999932944774627685546875);
                        break;
                    } while(false);
                    if (_26912)
                    {
                        _27130 = _26914;
                        _27123 = _26913;
                        _27046 = 1u;
                        break;
                    }
                    vec3 _15290 = _13816 - vec3(_283.CameraRight.w, _26767, _283.CameraForward.w);
                    vec3 _15298 = (_283.PoolMin.xyz + _283.PoolMax.xyz) * 0.5;
                    vec3 _15303 = vec3((_15290.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15308 = vec3(0.0, 0.0, (_15290.z >= 0.0) ? 1.0 : (-1.0));
                    vec3 _15315 = vec3((_15298.x >= _13816.x) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15322 = vec3(0.0, 0.0, (_15298.z >= _13816.z) ? 1.0 : (-1.0));
                    vec3 _26915;
                    vec3 _26916;
                    vec3 _26921;
                    vec3 _26923;
                    if (abs(_26903.y) > 0.5)
                    {
                        bool _15334 = abs(_15290.z) >= abs(_15290.x);
                        bvec3 _15338 = bvec3(_15334);
                        bvec3 _15343 = bvec3(_15334);
                        bvec3 _15348 = bvec3(_15334);
                        bvec3 _15353 = bvec3(_15334);
                        _26923 = vec3(_15338.x ? _15308.x : _15303.x, _15338.y ? _15308.y : _15303.y, _15338.z ? _15308.z : _15303.z);
                        _26921 = vec3(_15348.x ? _15303.x : _15308.x, _15348.y ? _15303.y : _15308.y, _15348.z ? _15303.z : _15308.z);
                        _26916 = vec3(_15353.x ? _15322.x : _15315.x, _15353.y ? _15322.y : _15315.y, _15353.z ? _15322.z : _15315.z);
                        _26915 = vec3(_15343.x ? _15315.x : _15322.x, _15343.y ? _15315.y : _15322.y, _15343.z ? _15315.z : _15322.z);
                    }
                    else
                    {
                        bool _15359 = abs(_26903.x) > 0.5;
                        bvec3 _15363 = bvec3(_15359);
                        bvec3 _15368 = bvec3(_15359);
                        _26923 = vec3(_15363.x ? _15308.x : _15303.x, _15363.y ? _15308.y : _15303.y, _15363.z ? _15308.z : _15303.z);
                        _26921 = vec3(0.0, 1.0, 0.0);
                        _26916 = vec3(_15368.x ? _15322.x : _15315.x, _15368.y ? _15322.y : _15315.y, _15368.z ? _15322.z : _15315.z);
                        _26915 = vec3(0.0, 1.0, 0.0);
                    }
                    float _15750 = ((_26915.y > 0.5) ? (_283.PoolMin.w - _13816.y) : ((_26915.x > 0.5) ? (_283.PoolMax.x - _13816.x) : ((_26915.x < (-0.5)) ? (_13816.x - _283.PoolMin.x) : ((_26915.z > 0.5) ? (_283.PoolMax.z - _13816.z) : (_13816.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                    float _15794 = ((_26916.y > 0.5) ? (_283.PoolMin.w - _13816.y) : ((_26916.x > 0.5) ? (_283.PoolMax.x - _13816.x) : ((_26916.x < (-0.5)) ? (_13816.x - _283.PoolMin.x) : ((_26916.z > 0.5) ? (_283.PoolMax.z - _13816.z) : (_13816.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                    vec2 _27125;
                    _27125 = _26913;
                    bool _27032;
                    uint _27047;
                    vec2 _27124;
                    vec2 _27127;
                    float _27131;
                    float _27134;
                    bool _29240;
                    int _26917 = 0;
                    bool _26940;
                    float _27132 = _26914;
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_26917 < 11)
                        {
                            if (_26917 == 1)
                            {
                                vec3 _15387 = _13816 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                float _15820 = dot(_15387, _283.CameraForward.xyz);
                                vec2 _27128;
                                float _27135;
                                if (_15820 > 0.0500000007450580596923828125)
                                {
                                    float _15861 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _15837 = ((dot(_15387, _283.CameraRight.xyz) * _15861) / _15820) + (0.5 * float(_283.OutputWidth));
                                    float _15850 = (((-dot(_15387, _283.CameraUp.xyz)) * _15861) / _15820) + (0.5 * float(_283.OutputHeight));
                                    vec2 _15851 = vec2(_15837, _15850);
                                    ivec2 _15398 = ivec2(_15851);
                                    ivec2 _15935 = clamp(ivec2(floor(((vec2(_15398) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _15944 = clamp(_15935, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _15970 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15944.x, int((FluidSize.y - 1u) - uint(_15944.y)), 0).xy, 0);
                                    float _15971 = _15970.x;
                                    ivec2 _15980 = clamp(_15935, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16067 = clamp(_15398, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _16019 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16067.x, int((_283.OutputHeight - 1u) - uint(_16067.y)), 0).xy, 0);
                                    float _16020 = _16019.x;
                                    vec2 _27129;
                                    float _27136;
                                    if (((((_15837 >= 0.0) && (_15850 >= 0.0)) && (_15837 < float(_283.OutputWidth))) && (_15850 < float(_283.OutputHeight))) && (((_15971 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15980.x, int((FluidSize.y - 1u) - uint(_15980.y)), 0).xy, 0).w > 0.5)) && ((((_16020 > 0.0) && (abs(_16020 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16020 - _283.ProjA)) : 1000000000.0) >= (_15971 + 0.0500000007450580596923828125))))
                                    {
                                        vec2 _15405 = floor(_15851) + vec2(0.5);
                                        ivec2 _16158 = clamp(ivec2(_15405), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _16110 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16158.x, int((_283.OutputHeight - 1u) - uint(_16158.y)), 0).xy, 0);
                                        float _16111 = _16110.x;
                                        float _16128 = ((_16111 > 0.0) && (abs(_16111 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16111 - _283.ProjA)) : 1000000000.0;
                                        if (_16128 < _15820)
                                        {
                                            _27131 = _16128;
                                            _27124 = _15405;
                                            _27047 = 2u;
                                            _27032 = true;
                                            break;
                                        }
                                        _27136 = _16128;
                                        _27129 = _15405;
                                    }
                                    else
                                    {
                                        _27136 = _27132;
                                        _27129 = _27125;
                                    }
                                    _27135 = _27136;
                                    _27128 = _27129;
                                }
                                else
                                {
                                    _27135 = _27132;
                                    _27128 = _27125;
                                }
                                _29240 = _26940;
                                _27134 = _27135;
                                _27127 = _27128;
                                int _15465 = _26917 + 1;
                                _26940 = _29240;
                                _26917 = _15465;
                                _27132 = _27134;
                                _27125 = _27127;
                                continue;
                            }
                            bool _15419 = _26917 == 2;
                            bool _15421 = _26917 >= 3;
                            int _15423 = _26917 & 1;
                            bool _15426 = _15419 || (_15421 && (_15423 == 0));
                            bool _15428 = _26917 < 3;
                            int _15430 = _26917 - 3;
                            float _15435 = _15428 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_15430 / 2)));
                            if (_15435 >= (_15426 ? _15794 : _15750))
                            {
                                _29240 = _26940;
                                _27134 = _27132;
                                _27127 = _27125;
                                int _15465 = _26917 + 1;
                                _26940 = _29240;
                                _26917 = _15465;
                                _27132 = _27134;
                                _27125 = _27127;
                                continue;
                            }
                            bvec3 _15448 = bvec3(_15426);
                            vec3 _15452 = _13816 + (vec3(_15448.x ? _26916.x : _26915.x, _15448.y ? _26916.y : _26915.y, _15448.z ? _26916.z : _26915.z) * _15435);
                            bvec3 _15456 = bvec3(_15426);
                            vec3 _15457 = vec3(_15456.x ? _26921.x : _26923.x, _15456.y ? _26921.y : _26923.y, _15456.z ? _26921.z : _26923.z);
                            bool _26937;
                            vec2 _27007;
                            float _27011;
                            do
                            {
                                float _16321 = ((_15457.y > 0.5) ? (_283.PoolMin.w - _15452.y) : ((_15457.x > 0.5) ? (_283.PoolMax.x - _15452.x) : ((_15457.x < (-0.5)) ? (_15452.x - _283.PoolMin.x) : ((_15457.z > 0.5) ? (_283.PoolMax.z - _15452.z) : (_15452.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _27008;
                                _27008 = vec2(0.5);
                                float _16227;
                                float _16269;
                                vec2 _26928;
                                float _26929;
                                bool _26934;
                                bool _26938;
                                int _26924 = 0;
                                float _26925 = 0.0;
                                float _26926 = 0.0599999986588954925537109375;
                                float _27012 = 1000000000.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if ((_26924 < 40) && (_26925 < _16321))
                                    {
                                        _16227 = min(_26925 + _26926, _16321);
                                        bool _26927;
                                        do
                                        {
                                            vec3 _16335 = (_15452 + (_15457 * _16227)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                            float _16393 = dot(_16335, _283.CameraForward.xyz);
                                            if (_16393 <= 0.0500000007450580596923828125)
                                            {
                                                _26929 = 1000000000.0;
                                                _26928 = vec2(0.5);
                                                _26927 = false;
                                                break;
                                            }
                                            float _16434 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            float _16410 = ((dot(_16335, _283.CameraRight.xyz) * _16434) / _16393) + (0.5 * float(_283.OutputWidth));
                                            float _16423 = (((-dot(_16335, _283.CameraUp.xyz)) * _16434) / _16393) + (0.5 * float(_283.OutputHeight));
                                            if (!((((_16410 >= 0.0) && (_16423 >= 0.0)) && (_16410 < float(_283.OutputWidth))) && (_16423 < float(_283.OutputHeight))))
                                            {
                                                _26929 = 1000000000.0;
                                                _26928 = vec2(0.5);
                                                _26927 = false;
                                                break;
                                            }
                                            vec2 _16352 = floor(vec2(_16410, _16423)) + vec2(0.5);
                                            ivec2 _16518 = clamp(ivec2(_16352), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                            vec4 _16470 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16518.x, int((_283.OutputHeight - 1u) - uint(_16518.y)), 0).xy, 0);
                                            float _16471 = _16470.x;
                                            float _16488 = ((_16471 > 0.0) && (abs(_16471 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16471 - _283.ProjA)) : 1000000000.0;
                                            _26929 = _16488;
                                            _26928 = _16352;
                                            _26927 = abs(_16488 - _16393) < ((0.0199999995529651641845703125 * _16393) + 0.02999999932944774627685546875);
                                            break;
                                        } while(false);
                                        if (_26927)
                                        {
                                            vec2 _27009;
                                            float _27013;
                                            _27013 = _26929;
                                            _27009 = _26928;
                                            float _29479;
                                            float _29480;
                                            float _29481;
                                            vec2 _29483;
                                            int _26933 = 0;
                                            float _27014 = _26925;
                                            float _27016 = _16227;
                                            SPIRV_CROSS_UNROLL
                                            for (; _26933 < 4; _27016 = _29479, _27014 = _29480, _27013 = _29481, _27009 = _29483, _26933++)
                                            {
                                                float _16245 = 0.5 * (_27014 + _27016);
                                                bool _27017;
                                                vec2 _27018;
                                                float _27019;
                                                do
                                                {
                                                    vec3 _16562 = (_15452 + (_15457 * _16245)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                    float _16620 = dot(_16562, _283.CameraForward.xyz);
                                                    if (_16620 <= 0.0500000007450580596923828125)
                                                    {
                                                        _27019 = 1000000000.0;
                                                        _27018 = vec2(0.5);
                                                        _27017 = false;
                                                        break;
                                                    }
                                                    float _16661 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                    float _16637 = ((dot(_16562, _283.CameraRight.xyz) * _16661) / _16620) + (0.5 * float(_283.OutputWidth));
                                                    float _16650 = (((-dot(_16562, _283.CameraUp.xyz)) * _16661) / _16620) + (0.5 * float(_283.OutputHeight));
                                                    if (!((((_16637 >= 0.0) && (_16650 >= 0.0)) && (_16637 < float(_283.OutputWidth))) && (_16650 < float(_283.OutputHeight))))
                                                    {
                                                        _27019 = 1000000000.0;
                                                        _27018 = vec2(0.5);
                                                        _27017 = false;
                                                        break;
                                                    }
                                                    vec2 _16579 = floor(vec2(_16637, _16650)) + vec2(0.5);
                                                    ivec2 _16745 = clamp(ivec2(_16579), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                    vec4 _16697 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16745.x, int((_283.OutputHeight - 1u) - uint(_16745.y)), 0).xy, 0);
                                                    float _16698 = _16697.x;
                                                    float _16715 = ((_16698 > 0.0) && (abs(_16698 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16698 - _283.ProjA)) : 1000000000.0;
                                                    _27019 = _16715;
                                                    _27018 = _16579;
                                                    _27017 = abs(_16715 - _16620) < ((0.0199999995529651641845703125 * _16620) + 0.02999999932944774627685546875);
                                                    break;
                                                } while(false);
                                                _29479 = _27017 ? _16245 : _27016;
                                                _29480 = _27017 ? _27014 : _16245;
                                                _29481 = _27017 ? _27019 : _27013;
                                                bvec2 _29482 = bvec2(_27017);
                                                _29483 = vec2(_29482.x ? _27018.x : _27009.x, _29482.y ? _27018.y : _27009.y);
                                            }
                                            _27011 = _27013;
                                            _27007 = _27009;
                                            _26938 = true;
                                            _26934 = true;
                                            break;
                                        }
                                        _16269 = min(_26926 * 1.2000000476837158203125, 0.300000011920928955078125);
                                        _26926 = _16269;
                                        _26925 = _16227;
                                        _26924++;
                                        _27012 = _26929;
                                        _27008 = _26928;
                                        continue;
                                    }
                                    else
                                    {
                                        _27011 = _27012;
                                        _27007 = _27008;
                                        _26938 = _26940;
                                        _26934 = false;
                                        break;
                                    }
                                }
                                if (_26934)
                                {
                                    _26937 = _26938;
                                    break;
                                }
                                _26937 = false;
                                break;
                            } while(false);
                            if (_26937)
                            {
                                _27131 = _27011;
                                _27124 = _27007;
                                _27047 = 1u;
                                _27032 = true;
                                break;
                            }
                            _29240 = _26937;
                            _27134 = _27011;
                            _27127 = _27007;
                            int _15465 = _26917 + 1;
                            _26940 = _29240;
                            _26917 = _15465;
                            _27132 = _27134;
                            _27125 = _27127;
                            continue;
                        }
                        else
                        {
                            _27131 = _27132;
                            _27124 = _27125;
                            _27047 = _27086;
                            _27032 = false;
                            break;
                        }
                    }
                    if (_27032)
                    {
                        _27130 = _27131;
                        _27123 = _27124;
                        _27046 = _27047;
                        break;
                    }
                    _27130 = _27131;
                    _27123 = _27124;
                    _27046 = 0u;
                    break;
                } while(false);
                if (_27046 != 0u)
                {
                    float _16823 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec3 _13837 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_27123.x - (0.5 * float(_283.OutputWidth))) * _27130) / _16823)) + (_283.CameraUp.xyz * (((-(_27123.y - (0.5 * float(_283.OutputHeight)))) * _27130) / _16823))) + (_283.CameraForward.xyz * _27130));
                    bvec3 _13838 = bvec3(_27046 == 1u);
                    vec3 _13839 = vec3(_13838.x ? _13816.x : _13837.x, _13838.y ? _13816.y : _13837.y, _13838.z ? _13816.z : _13837.z);
                    _27431 = _27123;
                    _27380 = _27130;
                    _27329 = (_27046 == 1u) ? _26902 : length(_13839 - _5969);
                    _27277 = 1.0;
                    _27225 = _13839;
                    break;
                }
            }
            float _13861 = min(_26902, _6136);
            _27431 = _27140;
            _27380 = _27170;
            _27329 = _13861;
            _27277 = _27199;
            _27225 = _5969 + (_15110 * _13861);
            break;
        } while(false);
        vec3 _27527;
        vec3 _27528;
        do
        {
            vec2 _17060 = clamp(_27431 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _17071 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17060.x, 1.0 - _17060.y), 0.0).xyz;
            if ((_27277 < 0.5) || (_27380 > 100000000.0))
            {
                _27528 = vec3(0.0, 1.0, 0.5);
                _27527 = _17071;
                break;
            }
            ivec2 _16894 = ivec2(_27431);
            float _16900 = _5969.y - _27225.y;
            if (_16900 <= 0.00999999977648258209228515625)
            {
                _27528 = vec3(0.0, 1.0, 0.5);
                _27527 = _17071;
                break;
            }
            ivec2 _17101 = clamp(_16894, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            ivec2 _17189 = clamp(_16894, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec3 _17149 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_17189.x, int((_283.OutputHeight - 1u) - uint(_17189.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _17151 = length(_17149);
            vec3 _17157 = _17149 / vec3(_17151);
            bvec3 _17158 = bvec3(_17151 > 0.001000000047497451305389404296875);
            ivec2 _17249 = clamp(_16894, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            float _16946 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_17101.x, int((_283.OutputHeight - 1u) - uint(_17101.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_17158.x ? _17157.x : vec3(0.0, 1.0, 0.0).x, _17158.y ? _17157.y : vec3(0.0, 1.0, 0.0).y, _17158.z ? _17157.z : vec3(0.0, 1.0, 0.0).z), _5670), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_17249.x, int((_283.OutputHeight - 1u) - uint(_17249.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _283.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_17071, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _16950 = max(-_29451.y, 0.20000000298023223876953125);
            vec3 _16964 = mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _16900) / vec3(_16950)), vec3(_283.LightParams.x));
            float _27526;
            if ((_283.CausticParams.x > 0.0) && (_16946 > 0.0))
            {
                vec2 _16993 = _27225.xz - ((_29451.xz / vec2(_16950)) * _16900);
                vec2 _17304 = _16993 * (6.283185482025146484375 / max(_283.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _17327 = _17304 + (vec2(sin((_17304.y * 0.730000019073486328125) + ((_283.ShadeParams.z * 0.89999997615814208984375) * _283.CausticParams.z)), sin((_17304.x * 0.61000001430511474609375) - ((_283.ShadeParams.z * 1.10000002384185791015625) * _283.CausticParams.z))) * 0.89999997615814208984375);
                float _27525;
                do
                {
                    if ((_283.GridParams.z < 2u) || (_283.GridParams.w < 2u))
                    {
                        _27525 = 1.0;
                        break;
                    }
                    vec2 _17437 = clamp(((_16993 - _283.PoolMin.xz) / max(_283.PoolMax.xz - _283.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)), vec2(0.0), vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _17439 = uvec2(_17437);
                    vec2 _17443 = _17437 - vec2(_17439);
                    uint _17450 = _17439.x + (_17439.y * _283.GridParams.z);
                    _27525 = mix(mix(CausticField_1._data[_17450], CausticField_1._data[_17450 + 1u], _17443.x), mix(CausticField_1._data[_17450 + _283.GridParams.z], CausticField_1._data[(_17450 + _283.GridParams.z) + 1u], _17443.x), _17443.y);
                    break;
                } while(false);
                _27526 = min(max(1.0 + ((_283.CausticParams.x * (exp((-_16900) / max(_283.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _16900))) * ((pow(clamp((1.0 - abs(sin(_17327.x + ((_283.ShadeParams.z * 0.60000002384185791015625) * _283.CausticParams.z)) * sin((_17327.y * 1.12999999523162841796875) - ((_283.ShadeParams.z * 0.4000000059604644775390625) * _283.CausticParams.z)))) * (1.0 - abs(sin(((_17327.x + _17327.y) * 0.709999978542327880859375) - ((_283.ShadeParams.z * 0.5) * _283.CausticParams.z)) * sin(((_17327.x - _17327.y) * 0.829999983310699462890625) + ((_283.ShadeParams.z * 0.699999988079071044921875) * _283.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_27525, 0.0500000007450580596923828125), _283.LightParams.y), 2.5);
            }
            else
            {
                _27526 = 1.0;
            }
            _27528 = vec3(_16946, dot(_16964, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _27526);
            _27527 = _17071 * ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _16900) * 1.2000000476837158203125), vec3(_283.LightParams.x)) * (1.0 - _16946)) + ((_16964 * _16946) * _27526));
            break;
        } while(false);
        float _6157 = mix(_6087, max(_27329, _6087), _6092);
        vec3 _28602;
        if (_6092 < 0.5)
        {
            vec3 _17507 = ((_283.CameraRight.xyz * _6117.x) + (_283.CameraUp.xyz * _6117.y)) + (_283.CameraForward.xyz * _6117.z);
            vec3 _28603;
            if (_17507.y < (-0.0199999995529651641845703125))
            {
                float _6174 = (_283.PoolMin.w - _5969.y) / _17507.y;
                vec3 _6179 = _5969 + (_17507 * _6174);
                vec3 _28604;
                if (((_6174 > _6087) && ((((_6179.x > _283.PoolMin.x) && (_6179.x < _283.PoolMax.x)) && (_6179.z > _283.PoolMin.z)) && (_6179.z < _283.PoolMax.z))) && ((_27277 < 0.5) || (_6174 < _27329)))
                {
                    float _17563 = length(_6179 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w)) / ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _17568 = _283.RippleParams.x * _283.RippleFlow.y;
                    vec2 _17570 = _6179.xz;
                    float _17702 = max(_283.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _27533;
                    _27533 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _27532 = 0; _27532 < 6; )
                    {
                        float _17714 = _17702 * _1923[_27532];
                        uint _17802 = (((uint(_27532) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _17810 = ((_17802 >> ((_17802 >> 28u) + 4u)) ^ _17802) * 277803737u;
                        float _17733 = 0.60000002384185791015625 + (float(((_17810 >> 22u) ^ _17810) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _17738 = vec2(cos(_17733), sin(_17733));
                        uint _17829 = (((uint(_27532) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _17837 = ((_17829 >> ((_17829 >> 28u) + 4u)) ^ _17829) * 277803737u;
                        _27533 += (_17738 * (((_17568 * pow(0.7799999713897705078125, float(_27532))) * clamp((_17714 / max(_17563 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_17738, _17570) * (6.283185482025146484375 / _17714)) + ((sqrt(61.63805389404296875 / _17714) * _283.RippleParams.z) * _283.ShadeParams.z)) + (float(((_17837 >> 22u) ^ _17837) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _27532++;
                        continue;
                    }
                    vec3 _17581 = normalize(vec3(-_27533.x, 1.0, -_27533.y));
                    vec3 _17584 = _6179 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                    float _17857 = dot(_17584, _283.CameraRight.xyz);
                    float _17862 = dot(_17584, _283.CameraUp.xyz);
                    float _17867 = dot(_17584, _283.CameraForward.xyz);
                    vec3 _17868 = vec3(_17857, _17862, _17867);
                    vec3 _17588 = reflect(_17507, _17581);
                    float _17885 = dot(_17588, _283.CameraForward.xyz);
                    vec3 _17886 = vec3(dot(_17588, _283.CameraRight.xyz), dot(_17588, _283.CameraUp.xyz), _17885);
                    float _27558;
                    vec3 _27646;
                    do
                    {
                        if (_283.ReflectionDistance <= 0.0)
                        {
                            _27646 = vec3(0.0);
                            _27558 = 0.0;
                            break;
                        }
                        float _17935 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _17885);
                        if (_17935 <= 0.0)
                        {
                            _27646 = vec3(0.0);
                            _27558 = 0.0;
                            break;
                        }
                        float _18132 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _17944 = max(0.0040000001899898052215576171875 * _17867, 0.00999999977648258209228515625);
                        float _17946 = 0.5 * _17944;
                        float _17955;
                        float _17957;
                        bool _27543;
                        bool _27548;
                        float _27553;
                        float _27559;
                        vec3 _27647;
                        int _27534 = 0;
                        float _27535 = _17946;
                        float _27536 = _17944;
                        float _27554 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_27534 < 80)
                            {
                                _17955 = _27535 + _27536;
                                _17957 = _27536 * 1.07000005245208740234375;
                                if (_17955 > _283.ReflectionDistance)
                                {
                                    _27647 = vec3(0.0);
                                    _27559 = 0.0;
                                    _27553 = _27554;
                                    _27548 = false;
                                    _27543 = true;
                                    break;
                                }
                                vec3 _17968 = _17868 + (_17886 * _17955);
                                if (_17968.z <= 0.0500000007450580596923828125)
                                {
                                    _27647 = vec3(0.0);
                                    _27559 = 0.0;
                                    _27553 = _27554;
                                    _27548 = false;
                                    _27543 = true;
                                    break;
                                }
                                float _18172 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _18148 = ((_17968.x * _18172) / _17968.z) + (0.5 * float(_283.OutputWidth));
                                float _18161 = (((-_17968.y) * _18172) / _17968.z) + (0.5 * float(_283.OutputHeight));
                                if (!((((_18148 >= 0.0) && (_18161 >= 0.0)) && (_18148 < float(_283.OutputWidth))) && (_18161 < float(_283.OutputHeight))))
                                {
                                    _27647 = vec3(0.0);
                                    _27559 = _27312;
                                    _27553 = _27554;
                                    _27548 = true;
                                    _27543 = false;
                                    break;
                                }
                                ivec2 _18256 = clamp(ivec2(vec2(_18148, _18161)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _18208 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18256.x, int((_283.OutputHeight - 1u) - uint(_18256.y)), 0).xy, 0);
                                float _18209 = _18208.x;
                                float _18226 = ((_18209 > 0.0) && (abs(_18209 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18209 - _283.ProjA)) : 1000000000.0;
                                if (((_18226 < 100000000.0) && (_17968.z > _18226)) && ((_17968.z - _18226) < max(_27536 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _27538;
                                    _27538 = _17955;
                                    float _29484;
                                    float _29485;
                                    int _27537 = 0;
                                    float _27541 = _27535;
                                    SPIRV_CROSS_UNROLL
                                    for (; _27537 < 6; _27541 = _29484, _27538 = _29485, _27537++)
                                    {
                                        float _18012 = 0.5 * (_27541 + _27538);
                                        vec3 _18017 = _17868 + (_17886 * _18012);
                                        float _18326 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _18387 = clamp(ivec2(vec2(((_18017.x * _18326) / _18017.z) + (0.5 * float(_283.OutputWidth)), (((-_18017.y) * _18326) / _18017.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _18339 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18387.x, int((_283.OutputHeight - 1u) - uint(_18387.y)), 0).xy, 0);
                                        float _18340 = _18339.x;
                                        bool _18024 = _18017.z > (((_18340 > 0.0) && (abs(_18340 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18340 - _283.ProjA)) : 1000000000.0);
                                        _29484 = _18024 ? _27541 : _18012;
                                        _29485 = _18024 ? _18012 : _27538;
                                    }
                                    vec3 _18038 = _17868 + (_17886 * _27538);
                                    float _18457 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _18447 = vec2(((_18038.x * _18457) / _18038.z) + (0.5 * float(_283.OutputWidth)), (((-_18038.y) * _18457) / _18038.z) + (0.5 * float(_283.OutputHeight)));
                                    ivec2 _18515 = clamp(ivec2(_18447), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec3 _18475 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_18515.x, int((_283.OutputHeight - 1u) - uint(_18515.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _18477 = length(_18475);
                                    vec3 _18483 = _18475 / vec3(_18477);
                                    bvec3 _18484 = bvec3(_18477 > 0.001000000047497451305389404296875);
                                    vec3 _18485 = vec3(_18484.x ? _18483.x : vec3(0.0, 1.0, 0.0).x, _18484.y ? _18483.y : vec3(0.0, 1.0, 0.0).y, _18484.z ? _18483.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_18485, _283.CameraRight.xyz), dot(_18485, _283.CameraUp.xyz), dot(_18485, _283.CameraForward.xyz)), _17886) > 0.100000001490116119384765625)
                                    {
                                        int _18065 = _27534 + 1;
                                        _27536 = _17957;
                                        _27535 = _17955;
                                        _27534 = _18065;
                                        _27554 = _17955;
                                        continue;
                                    }
                                    float _18579 = clamp(((0.0199999995529651641845703125 * _27538) * _18132) / max(_18038.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _18581 = 0.5 * _18579;
                                    vec2 _18666 = clamp(_18447 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18692 = clamp((_18447 + vec2(_18581)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18718 = clamp((_18447 + vec2(_18579 * (-0.5), _18581)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18744 = clamp((_18447 + vec2(_18581, _18579 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18770 = clamp((_18447 + vec2(_18579 * (-0.5), _18579 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18796 = clamp((_18447 + vec2(_18579, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18822 = clamp((_18447 + vec2(-_18579, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18848 = clamp((_18447 + vec2(0.0, _18579)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18874 = clamp((_18447 + vec2(0.0, -_18579)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _18651 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18666.x, 1.0 - _18666.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18692.x, 1.0 - _18692.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18718.x, 1.0 - _18718.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18744.x, 1.0 - _18744.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18770.x, 1.0 - _18770.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18796.x, 1.0 - _18796.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18822.x, 1.0 - _18822.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18848.x, 1.0 - _18848.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18874.x, 1.0 - _18874.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _27647 = _18651;
                                    _27559 = _17935;
                                    _27553 = _17955;
                                    _27548 = false;
                                    _27543 = true;
                                    break;
                                }
                                int _18065 = _27534 + 1;
                                _27536 = _17957;
                                _27535 = _17955;
                                _27534 = _18065;
                                _27554 = _17955;
                                continue;
                            }
                            else
                            {
                                _27647 = vec3(0.0);
                                _27559 = _27312;
                                _27553 = _27554;
                                _27548 = false;
                                _27543 = false;
                                break;
                            }
                        }
                        if (_27543)
                        {
                            _27646 = _27647;
                            _27558 = _27559;
                            break;
                        }
                        if ((!_27548) || (_27553 < 0.0))
                        {
                            _27646 = _27647;
                            _27558 = 0.0;
                            break;
                        }
                        vec3 _18080 = _17868 + (_17886 * _27553);
                        float _18925 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _18915 = vec2(((_18080.x * _18925) / _18080.z) + (0.5 * float(_283.OutputWidth)), (((-_18080.y) * _18925) / _18080.z) + (0.5 * float(_283.OutputHeight)));
                        ivec2 _18986 = clamp(ivec2(_18915), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _18938 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18986.x, int((_283.OutputHeight - 1u) - uint(_18986.y)), 0).xy, 0);
                        float _18939 = _18938.x;
                        float _18956 = ((_18939 > 0.0) && (abs(_18939 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18939 - _283.ProjA)) : 1000000000.0;
                        float _19032 = clamp(((0.0199999995529651641845703125 * _27553) * _18132) / max(_18080.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _19034 = 0.5 * _19032;
                        vec2 _19119 = clamp(_18915 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19145 = clamp((_18915 + vec2(_19034)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19171 = clamp((_18915 + vec2(_19032 * (-0.5), _19034)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19197 = clamp((_18915 + vec2(_19034, _19032 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19223 = clamp((_18915 + vec2(_19032 * (-0.5), _19032 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19249 = clamp((_18915 + vec2(_19032, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19275 = clamp((_18915 + vec2(-_19032, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19301 = clamp((_18915 + vec2(0.0, _19032)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19327 = clamp((_18915 + vec2(0.0, -_19032)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _19104 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19119.x, 1.0 - _19119.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19145.x, 1.0 - _19145.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19171.x, 1.0 - _19171.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19197.x, 1.0 - _19197.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19223.x, 1.0 - _19223.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19249.x, 1.0 - _19249.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19275.x, 1.0 - _19275.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19301.x, 1.0 - _19301.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19327.x, 1.0 - _19327.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_18956 > 100000000.0)
                        {
                            _27646 = _19104;
                            _27558 = _17935;
                            break;
                        }
                        float _18105 = _18956 - _18080.z;
                        if (_18105 <= 0.0)
                        {
                            _27646 = _19104;
                            _27558 = _17935;
                            break;
                        }
                        _27646 = _19104;
                        _27558 = _17935 * exp((_18105 / max(_17885, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _27652;
                    do
                    {
                        if (_283.EnvParams.x <= 0.0)
                        {
                            float _19381 = _17588.y;
                            _27652 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _19381))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _19381)));
                            break;
                        }
                        _27652 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_17588.x, _17588.y, -_17588.z), 0.0).xyz * _283.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _17602 = refract(_17507, _17581, 0.75187969207763671875);
                    bvec3 _29486 = bvec3(dot(_17602, _17602) < 9.9999999747524270787835121154785e-07);
                    vec3 _29487 = vec3(_29486.x ? _17507.x : _17602.x, _29486.y ? _17507.y : _17602.y, _29486.z ? _17507.z : _17602.z);
                    float _19407 = dot(_29487, _283.CameraRight.xyz);
                    float _19412 = dot(_29487, _283.CameraUp.xyz);
                    float _19417 = dot(_29487, _283.CameraForward.xyz);
                    vec3 _19418 = vec3(_19407, _19412, _19417);
                    float _19458 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _19448 = vec2(((_17857 * _19458) / _17867) + (0.5 * float(_283.OutputWidth)), (((-_17862) * _19458) / _17867) + (0.5 * float(_283.OutputHeight)));
                    vec3 _28083;
                    float _28177;
                    float _28271;
                    float _28365;
                    vec2 _28459;
                    do
                    {
                        ivec2 _19789 = clamp(ivec2(_19448), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _19741 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19789.x, int((_283.OutputHeight - 1u) - uint(_19789.y)), 0).xy, 0);
                        float _19742 = _19741.x;
                        vec2 _27998;
                        float _28028;
                        _28028 = ((_19742 > 0.0) && (abs(_19742 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_19742 - _283.ProjA)) : 1000000000.0;
                        _27998 = _19448;
                        float _19537;
                        float _29490;
                        vec2 _29492;
                        bool _27661;
                        float _28057;
                        vec3 _28084;
                        float _28178;
                        float _28272;
                        float _28366;
                        vec2 _28460;
                        int _27654 = 1;
                        float _27656 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_27654 <= 16)
                            {
                                float _19530 = float(_27654);
                                _19537 = (_19530 * 0.25) * sqrt(_19530 * 0.0625);
                                vec3 _19542 = _17868 + (_19418 * _19537);
                                if (_19542.z <= 0.0500000007450580596923828125)
                                {
                                    _28460 = _27466;
                                    _28366 = _27312;
                                    _28272 = _27312;
                                    _28178 = _27312;
                                    _28084 = _27260;
                                    _28057 = 0.0;
                                    _27661 = false;
                                    break;
                                }
                                float _19859 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _19835 = ((_19542.x * _19859) / _19542.z) + (0.5 * float(_283.OutputWidth));
                                float _19848 = (((-_19542.y) * _19859) / _19542.z) + (0.5 * float(_283.OutputHeight));
                                vec2 _19849 = vec2(_19835, _19848);
                                if (!((((_19835 >= 0.0) && (_19848 >= 0.0)) && (_19835 < float(_283.OutputWidth))) && (_19848 < float(_283.OutputHeight))))
                                {
                                    _28460 = _27466;
                                    _28366 = _27312;
                                    _28272 = _27312;
                                    _28178 = _27312;
                                    _28084 = _27260;
                                    _28057 = 0.0;
                                    _27661 = false;
                                    break;
                                }
                                ivec2 _19943 = clamp(ivec2(_19849), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _19895 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19943.x, int((_283.OutputHeight - 1u) - uint(_19943.y)), 0).xy, 0);
                                float _19896 = _19895.x;
                                float _19913 = ((_19896 > 0.0) && (abs(_19896 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_19896 - _283.ProjA)) : 1000000000.0;
                                if (_19542.z >= _19913)
                                {
                                    ivec2 _19564 = ivec2(_19849);
                                    ivec2 _20024 = clamp(ivec2(floor(((vec2(_19564) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20033 = clamp(_20024, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _20059 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20033.x, int((FluidSize.y - 1u) - uint(_20033.y)), 0).xy, 0);
                                    float _20060 = _20059.x;
                                    ivec2 _20069 = clamp(_20024, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20156 = clamp(_19564, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _20108 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20156.x, int((_283.OutputHeight - 1u) - uint(_20156.y)), 0).xy, 0);
                                    float _20109 = _20108.x;
                                    if (!(((_20060 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20069.x, int((FluidSize.y - 1u) - uint(_20069.y)), 0).xy, 0).w > 0.5)) && ((((_20109 > 0.0) && (abs(_20109 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20109 - _283.ProjA)) : 1000000000.0) >= (_20060 + 0.0500000007450580596923828125))))
                                    {
                                        _28460 = _27466;
                                        _28366 = _27312;
                                        _28272 = _27312;
                                        _28178 = _27312;
                                        _28084 = _27260;
                                        _28057 = 0.0;
                                        _27661 = false;
                                        break;
                                    }
                                    float _27658;
                                    _27658 = _19537;
                                    float _29488;
                                    float _29489;
                                    int _27657 = 0;
                                    float _27659 = _27656;
                                    SPIRV_CROSS_UNROLL
                                    for (; _27657 < 5; _27659 = _29488, _27658 = _29489, _27657++)
                                    {
                                        float _19579 = 0.5 * (_27659 + _27658);
                                        vec3 _19584 = _17868 + (_19418 * _19579);
                                        float _20226 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _20287 = clamp(ivec2(vec2(((_19584.x * _20226) / _19584.z) + (0.5 * float(_283.OutputWidth)), (((-_19584.y) * _20226) / _19584.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _20239 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20287.x, int((_283.OutputHeight - 1u) - uint(_20287.y)), 0).xy, 0);
                                        float _20240 = _20239.x;
                                        bool _19591 = _19584.z >= (((_20240 > 0.0) && (abs(_20240 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20240 - _283.ProjA)) : 1000000000.0);
                                        _29488 = _19591 ? _27659 : _19579;
                                        _29489 = _19591 ? _19579 : _27658;
                                    }
                                    vec3 _19605 = _17868 + (_19418 * _27658);
                                    float _20357 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _19616 = clamp(vec2(((_19605.x * _20357) / _19605.z) + (0.5 * float(_283.OutputWidth)), (((-_19605.y) * _20357) / _19605.z) + (0.5 * float(_283.OutputHeight))), vec2(0.5), vec2(float(_283.OutputWidth), float(_283.OutputHeight)) - vec2(0.5));
                                    ivec2 _20418 = clamp(ivec2(_19616), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _20370 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20418.x, int((_283.OutputHeight - 1u) - uint(_20418.y)), 0).xy, 0);
                                    float _20371 = _20370.x;
                                    float _20388 = ((_20371 > 0.0) && (abs(_20371 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20371 - _283.ProjA)) : 1000000000.0;
                                    float _20496 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    _28460 = _19616;
                                    _28366 = _20388;
                                    _28272 = _27658;
                                    _28178 = 1.0;
                                    _28084 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_19616.x - (0.5 * float(_283.OutputWidth))) * _20388) / _20496)) + (_283.CameraUp.xyz * (((-(_19616.y - (0.5 * float(_283.OutputHeight)))) * _20388) / _20496))) + (_283.CameraForward.xyz * _20388));
                                    _28057 = 1.0;
                                    _27661 = true;
                                    break;
                                }
                                ivec2 _19640 = ivec2(_19849 - vec2(0.5));
                                ivec2 _20569 = clamp(ivec2(floor(((vec2(_19640) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20578 = clamp(_20569, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _20604 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20578.x, int((FluidSize.y - 1u) - uint(_20578.y)), 0).xy, 0);
                                float _20605 = _20604.x;
                                ivec2 _20614 = clamp(_20569, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20701 = clamp(_19640, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _20653 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20701.x, int((_283.OutputHeight - 1u) - uint(_20701.y)), 0).xy, 0);
                                float _20654 = _20653.x;
                                ivec2 _19645 = ivec2(_19849 + vec2(0.5));
                                ivec2 _20782 = clamp(ivec2(floor(((vec2(_19645) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20791 = clamp(_20782, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _20817 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20791.x, int((FluidSize.y - 1u) - uint(_20791.y)), 0).xy, 0);
                                float _20818 = _20817.x;
                                ivec2 _20827 = clamp(_20782, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20914 = clamp(_19645, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _20866 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20914.x, int((_283.OutputHeight - 1u) - uint(_20914.y)), 0).xy, 0);
                                float _20867 = _20866.x;
                                bool _19647 = (((_20605 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20614.x, int((FluidSize.y - 1u) - uint(_20614.y)), 0).xy, 0).w > 0.5)) && ((((_20654 > 0.0) && (abs(_20654 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20654 - _283.ProjA)) : 1000000000.0) >= (_20605 + 0.0500000007450580596923828125))) && (((_20818 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20827.x, int((FluidSize.y - 1u) - uint(_20827.y)), 0).xy, 0).w > 0.5)) && ((((_20867 > 0.0) && (abs(_20867 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20867 - _283.ProjA)) : 1000000000.0) >= (_20818 + 0.0500000007450580596923828125)));
                                _29490 = _19647 ? _19913 : _28028;
                                bvec2 _29491 = bvec2(_19647);
                                _29492 = vec2(_29491.x ? _19849.x : _27998.x, _29491.y ? _19849.y : _27998.y);
                                _27654++;
                                _28028 = _29490;
                                _27998 = _29492;
                                _27656 = _19537;
                                continue;
                            }
                            else
                            {
                                _28460 = _27466;
                                _28366 = _27312;
                                _28272 = _27312;
                                _28178 = _27312;
                                _28084 = _27260;
                                _28057 = 0.0;
                                _27661 = false;
                                break;
                            }
                        }
                        if (_27661)
                        {
                            _28459 = _28460;
                            _28365 = _28366;
                            _28271 = _28272;
                            _28177 = _28178;
                            _28083 = _28084;
                            break;
                        }
                        vec3 _20966 = ((_283.CameraRight.xyz * _19407) + (_283.CameraUp.xyz * _19412)) + (_283.CameraForward.xyz * _19417);
                        float _27676;
                        vec3 _27677;
                        do
                        {
                            if ((((_6179.x < _283.PoolMin.x) || (_6179.x > _283.PoolMax.x)) || (_6179.z < _283.PoolMin.z)) || (_6179.z > _283.PoolMax.z))
                            {
                                _27677 = vec3(0.0, 1.0, 0.0);
                                _27676 = 1000.0;
                                break;
                            }
                            float _27664;
                            if (_20966.x > 9.9999997473787516355514526367188e-05)
                            {
                                _27664 = (_283.PoolMax.x - _6179.x) / _20966.x;
                            }
                            else
                            {
                                float _27665;
                                if (_20966.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _27665 = (_283.PoolMin.x - _6179.x) / _20966.x;
                                }
                                else
                                {
                                    _27665 = 1000.0;
                                }
                                _27664 = _27665;
                            }
                            bool _21028 = _27664 < 1000.0;
                            vec3 _27681;
                            if (_21028)
                            {
                                _27681 = vec3((_20966.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                            }
                            else
                            {
                                _27681 = vec3(0.0, 1.0, 0.0);
                            }
                            float _29493 = _21028 ? _27664 : 1000.0;
                            float _27668;
                            if (_20966.z > 9.9999997473787516355514526367188e-05)
                            {
                                _27668 = (_283.PoolMax.z - _6179.z) / _20966.z;
                            }
                            else
                            {
                                float _27669;
                                if (_20966.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _27669 = (_283.PoolMin.z - _6179.z) / _20966.z;
                                }
                                else
                                {
                                    _27669 = 1000.0;
                                }
                                _27668 = _27669;
                            }
                            bool _21066 = _27668 < _29493;
                            vec3 _27679;
                            if (_21066)
                            {
                                _27679 = vec3(0.0, 0.0, _29512);
                            }
                            else
                            {
                                _27679 = _27681;
                            }
                            float _29494 = _21066 ? _27668 : _29493;
                            float _27674;
                            vec3 _27678;
                            if (_20966.y < (-9.9999997473787516355514526367188e-05))
                            {
                                float _21086 = (_283.PoolMin.y - _6179.y) / _20966.y;
                                bool _21089 = _21086 < _29494;
                                bvec3 _29495 = bvec3(_21089);
                                _27678 = vec3(_29495.x ? vec3(0.0, 1.0, 0.0).x : _27679.x, _29495.y ? vec3(0.0, 1.0, 0.0).y : _27679.y, _29495.z ? vec3(0.0, 1.0, 0.0).z : _27679.z);
                                _27674 = _21089 ? _21086 : _29494;
                            }
                            else
                            {
                                _27678 = _27679;
                                _27674 = _29494;
                            }
                            _27677 = _27678;
                            _27676 = max(_27674, 0.0);
                            break;
                        } while(false);
                        if (_27676 < 100.0)
                        {
                            vec3 _19672 = _6179 + (_20966 * _27676);
                            uint _27862;
                            vec2 _27981;
                            float _27988;
                            do
                            {
                                bool _27686;
                                vec2 _27687;
                                float _27688;
                                do
                                {
                                    vec3 _21340 = _19672 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                    float _21398 = dot(_21340, _283.CameraForward.xyz);
                                    if (_21398 <= 0.0500000007450580596923828125)
                                    {
                                        _27688 = 1000000000.0;
                                        _27687 = vec2(0.5);
                                        _27686 = false;
                                        break;
                                    }
                                    float _21439 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _21415 = ((dot(_21340, _283.CameraRight.xyz) * _21439) / _21398) + (0.5 * float(_283.OutputWidth));
                                    float _21428 = (((-dot(_21340, _283.CameraUp.xyz)) * _21439) / _21398) + (0.5 * float(_283.OutputHeight));
                                    if (!((((_21415 >= 0.0) && (_21428 >= 0.0)) && (_21415 < float(_283.OutputWidth))) && (_21428 < float(_283.OutputHeight))))
                                    {
                                        _27688 = 1000000000.0;
                                        _27687 = vec2(0.5);
                                        _27686 = false;
                                        break;
                                    }
                                    vec2 _21357 = floor(vec2(_21415, _21428)) + vec2(0.5);
                                    ivec2 _21523 = clamp(ivec2(_21357), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _21475 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21523.x, int((_283.OutputHeight - 1u) - uint(_21523.y)), 0).xy, 0);
                                    float _21476 = _21475.x;
                                    float _21493 = ((_21476 > 0.0) && (abs(_21476 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21476 - _283.ProjA)) : 1000000000.0;
                                    _27688 = _21493;
                                    _27687 = _21357;
                                    _27686 = abs(_21493 - _21398) < ((0.0199999995529651641845703125 * _21398) + 0.02999999932944774627685546875);
                                    break;
                                } while(false);
                                if (_27686)
                                {
                                    _27988 = _27688;
                                    _27981 = _27687;
                                    _27862 = 1u;
                                    break;
                                }
                                vec3 _21146 = _19672 - vec3(_283.CameraRight.w, _26767, _283.CameraForward.w);
                                vec3 _21154 = (_283.PoolMin.xyz + _283.PoolMax.xyz) * 0.5;
                                vec3 _21159 = vec3((_21146.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21164 = vec3(0.0, 0.0, (_21146.z >= 0.0) ? 1.0 : (-1.0));
                                vec3 _21171 = vec3((_21154.x >= _19672.x) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21178 = vec3(0.0, 0.0, (_21154.z >= _19672.z) ? 1.0 : (-1.0));
                                vec3 _27689;
                                vec3 _27690;
                                vec3 _27695;
                                vec3 _27697;
                                if (abs(_27677.y) > 0.5)
                                {
                                    bool _21190 = abs(_21146.z) >= abs(_21146.x);
                                    bvec3 _21194 = bvec3(_21190);
                                    bvec3 _21199 = bvec3(_21190);
                                    bvec3 _21204 = bvec3(_21190);
                                    bvec3 _21209 = bvec3(_21190);
                                    _27697 = vec3(_21194.x ? _21164.x : _21159.x, _21194.y ? _21164.y : _21159.y, _21194.z ? _21164.z : _21159.z);
                                    _27695 = vec3(_21204.x ? _21159.x : _21164.x, _21204.y ? _21159.y : _21164.y, _21204.z ? _21159.z : _21164.z);
                                    _27690 = vec3(_21209.x ? _21178.x : _21171.x, _21209.y ? _21178.y : _21171.y, _21209.z ? _21178.z : _21171.z);
                                    _27689 = vec3(_21199.x ? _21171.x : _21178.x, _21199.y ? _21171.y : _21178.y, _21199.z ? _21171.z : _21178.z);
                                }
                                else
                                {
                                    bool _21215 = abs(_27677.x) > 0.5;
                                    bvec3 _21219 = bvec3(_21215);
                                    bvec3 _21224 = bvec3(_21215);
                                    _27697 = vec3(_21219.x ? _21164.x : _21159.x, _21219.y ? _21164.y : _21159.y, _21219.z ? _21164.z : _21159.z);
                                    _27695 = vec3(0.0, 1.0, 0.0);
                                    _27690 = vec3(_21224.x ? _21178.x : _21171.x, _21224.y ? _21178.y : _21171.y, _21224.z ? _21178.z : _21171.z);
                                    _27689 = vec3(0.0, 1.0, 0.0);
                                }
                                float _21606 = ((_27689.y > 0.5) ? (_283.PoolMin.w - _19672.y) : ((_27689.x > 0.5) ? (_283.PoolMax.x - _19672.x) : ((_27689.x < (-0.5)) ? (_19672.x - _283.PoolMin.x) : ((_27689.z > 0.5) ? (_283.PoolMax.z - _19672.z) : (_19672.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                float _21650 = ((_27690.y > 0.5) ? (_283.PoolMin.w - _19672.y) : ((_27690.x > 0.5) ? (_283.PoolMax.x - _19672.x) : ((_27690.x < (-0.5)) ? (_19672.x - _283.PoolMin.x) : ((_27690.z > 0.5) ? (_283.PoolMax.z - _19672.z) : (_19672.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _27983;
                                _27983 = _27687;
                                bool _27848;
                                uint _27863;
                                vec2 _27982;
                                vec2 _27985;
                                float _27989;
                                float _27992;
                                bool _29350;
                                int _27691 = 0;
                                bool _27714;
                                float _27990 = _27688;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_27691 < 11)
                                    {
                                        if (_27691 == 1)
                                        {
                                            vec3 _21243 = _19672 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                            float _21676 = dot(_21243, _283.CameraForward.xyz);
                                            vec2 _27986;
                                            float _27993;
                                            if (_21676 > 0.0500000007450580596923828125)
                                            {
                                                float _21717 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                float _21693 = ((dot(_21243, _283.CameraRight.xyz) * _21717) / _21676) + (0.5 * float(_283.OutputWidth));
                                                float _21706 = (((-dot(_21243, _283.CameraUp.xyz)) * _21717) / _21676) + (0.5 * float(_283.OutputHeight));
                                                vec2 _21707 = vec2(_21693, _21706);
                                                ivec2 _21254 = ivec2(_21707);
                                                ivec2 _21791 = clamp(ivec2(floor(((vec2(_21254) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _21800 = clamp(_21791, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                vec4 _21826 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_21800.x, int((FluidSize.y - 1u) - uint(_21800.y)), 0).xy, 0);
                                                float _21827 = _21826.x;
                                                ivec2 _21836 = clamp(_21791, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _21923 = clamp(_21254, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                vec4 _21875 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21923.x, int((_283.OutputHeight - 1u) - uint(_21923.y)), 0).xy, 0);
                                                float _21876 = _21875.x;
                                                vec2 _27987;
                                                float _27994;
                                                if (((((_21693 >= 0.0) && (_21706 >= 0.0)) && (_21693 < float(_283.OutputWidth))) && (_21706 < float(_283.OutputHeight))) && (((_21827 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_21836.x, int((FluidSize.y - 1u) - uint(_21836.y)), 0).xy, 0).w > 0.5)) && ((((_21876 > 0.0) && (abs(_21876 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21876 - _283.ProjA)) : 1000000000.0) >= (_21827 + 0.0500000007450580596923828125))))
                                                {
                                                    vec2 _21261 = floor(_21707) + vec2(0.5);
                                                    ivec2 _22014 = clamp(ivec2(_21261), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                    vec4 _21966 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22014.x, int((_283.OutputHeight - 1u) - uint(_22014.y)), 0).xy, 0);
                                                    float _21967 = _21966.x;
                                                    float _21984 = ((_21967 > 0.0) && (abs(_21967 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21967 - _283.ProjA)) : 1000000000.0;
                                                    if (_21984 < _21676)
                                                    {
                                                        _27989 = _21984;
                                                        _27982 = _21261;
                                                        _27863 = 2u;
                                                        _27848 = true;
                                                        break;
                                                    }
                                                    _27994 = _21984;
                                                    _27987 = _21261;
                                                }
                                                else
                                                {
                                                    _27994 = _27990;
                                                    _27987 = _27983;
                                                }
                                                _27993 = _27994;
                                                _27986 = _27987;
                                            }
                                            else
                                            {
                                                _27993 = _27990;
                                                _27986 = _27983;
                                            }
                                            _29350 = _27714;
                                            _27992 = _27993;
                                            _27985 = _27986;
                                            int _21321 = _27691 + 1;
                                            _27714 = _29350;
                                            _27691 = _21321;
                                            _27990 = _27992;
                                            _27983 = _27985;
                                            continue;
                                        }
                                        bool _21275 = _27691 == 2;
                                        bool _21277 = _27691 >= 3;
                                        int _21279 = _27691 & 1;
                                        bool _21282 = _21275 || (_21277 && (_21279 == 0));
                                        bool _21284 = _27691 < 3;
                                        int _21286 = _27691 - 3;
                                        float _21291 = _21284 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_21286 / 2)));
                                        if (_21291 >= (_21282 ? _21650 : _21606))
                                        {
                                            _29350 = _27714;
                                            _27992 = _27990;
                                            _27985 = _27983;
                                            int _21321 = _27691 + 1;
                                            _27714 = _29350;
                                            _27691 = _21321;
                                            _27990 = _27992;
                                            _27983 = _27985;
                                            continue;
                                        }
                                        bvec3 _21304 = bvec3(_21282);
                                        vec3 _21308 = _19672 + (vec3(_21304.x ? _27690.x : _27689.x, _21304.y ? _27690.y : _27689.y, _21304.z ? _27690.z : _27689.z) * _21291);
                                        bvec3 _21312 = bvec3(_21282);
                                        vec3 _21313 = vec3(_21312.x ? _27695.x : _27697.x, _21312.y ? _27695.y : _27697.y, _21312.z ? _27695.z : _27697.z);
                                        bool _27711;
                                        vec2 _27823;
                                        float _27827;
                                        do
                                        {
                                            float _22177 = ((_21313.y > 0.5) ? (_283.PoolMin.w - _21308.y) : ((_21313.x > 0.5) ? (_283.PoolMax.x - _21308.x) : ((_21313.x < (-0.5)) ? (_21308.x - _283.PoolMin.x) : ((_21313.z > 0.5) ? (_283.PoolMax.z - _21308.z) : (_21308.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                            vec2 _27824;
                                            _27824 = vec2(0.5);
                                            float _22083;
                                            float _22125;
                                            vec2 _27702;
                                            float _27703;
                                            bool _27708;
                                            bool _27712;
                                            int _27698 = 0;
                                            float _27699 = 0.0;
                                            float _27700 = 0.0599999986588954925537109375;
                                            float _27828 = 1000000000.0;
                                            SPIRV_CROSS_LOOP
                                            for (;;)
                                            {
                                                if ((_27698 < 40) && (_27699 < _22177))
                                                {
                                                    _22083 = min(_27699 + _27700, _22177);
                                                    bool _27701;
                                                    do
                                                    {
                                                        vec3 _22191 = (_21308 + (_21313 * _22083)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                        float _22249 = dot(_22191, _283.CameraForward.xyz);
                                                        if (_22249 <= 0.0500000007450580596923828125)
                                                        {
                                                            _27703 = 1000000000.0;
                                                            _27702 = vec2(0.5);
                                                            _27701 = false;
                                                            break;
                                                        }
                                                        float _22290 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                        float _22266 = ((dot(_22191, _283.CameraRight.xyz) * _22290) / _22249) + (0.5 * float(_283.OutputWidth));
                                                        float _22279 = (((-dot(_22191, _283.CameraUp.xyz)) * _22290) / _22249) + (0.5 * float(_283.OutputHeight));
                                                        if (!((((_22266 >= 0.0) && (_22279 >= 0.0)) && (_22266 < float(_283.OutputWidth))) && (_22279 < float(_283.OutputHeight))))
                                                        {
                                                            _27703 = 1000000000.0;
                                                            _27702 = vec2(0.5);
                                                            _27701 = false;
                                                            break;
                                                        }
                                                        vec2 _22208 = floor(vec2(_22266, _22279)) + vec2(0.5);
                                                        ivec2 _22374 = clamp(ivec2(_22208), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                        vec4 _22326 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22374.x, int((_283.OutputHeight - 1u) - uint(_22374.y)), 0).xy, 0);
                                                        float _22327 = _22326.x;
                                                        float _22344 = ((_22327 > 0.0) && (abs(_22327 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22327 - _283.ProjA)) : 1000000000.0;
                                                        _27703 = _22344;
                                                        _27702 = _22208;
                                                        _27701 = abs(_22344 - _22249) < ((0.0199999995529651641845703125 * _22249) + 0.02999999932944774627685546875);
                                                        break;
                                                    } while(false);
                                                    if (_27701)
                                                    {
                                                        vec2 _27825;
                                                        float _27829;
                                                        _27829 = _27703;
                                                        _27825 = _27702;
                                                        float _29498;
                                                        float _29499;
                                                        float _29500;
                                                        vec2 _29502;
                                                        int _27707 = 0;
                                                        float _27830 = _27699;
                                                        float _27832 = _22083;
                                                        SPIRV_CROSS_UNROLL
                                                        for (; _27707 < 4; _27832 = _29498, _27830 = _29499, _27829 = _29500, _27825 = _29502, _27707++)
                                                        {
                                                            float _22101 = 0.5 * (_27830 + _27832);
                                                            bool _27833;
                                                            vec2 _27834;
                                                            float _27835;
                                                            do
                                                            {
                                                                vec3 _22418 = (_21308 + (_21313 * _22101)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                                float _22476 = dot(_22418, _283.CameraForward.xyz);
                                                                if (_22476 <= 0.0500000007450580596923828125)
                                                                {
                                                                    _27835 = 1000000000.0;
                                                                    _27834 = vec2(0.5);
                                                                    _27833 = false;
                                                                    break;
                                                                }
                                                                float _22517 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                                float _22493 = ((dot(_22418, _283.CameraRight.xyz) * _22517) / _22476) + (0.5 * float(_283.OutputWidth));
                                                                float _22506 = (((-dot(_22418, _283.CameraUp.xyz)) * _22517) / _22476) + (0.5 * float(_283.OutputHeight));
                                                                if (!((((_22493 >= 0.0) && (_22506 >= 0.0)) && (_22493 < float(_283.OutputWidth))) && (_22506 < float(_283.OutputHeight))))
                                                                {
                                                                    _27835 = 1000000000.0;
                                                                    _27834 = vec2(0.5);
                                                                    _27833 = false;
                                                                    break;
                                                                }
                                                                vec2 _22435 = floor(vec2(_22493, _22506)) + vec2(0.5);
                                                                ivec2 _22601 = clamp(ivec2(_22435), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                                vec4 _22553 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22601.x, int((_283.OutputHeight - 1u) - uint(_22601.y)), 0).xy, 0);
                                                                float _22554 = _22553.x;
                                                                float _22571 = ((_22554 > 0.0) && (abs(_22554 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22554 - _283.ProjA)) : 1000000000.0;
                                                                _27835 = _22571;
                                                                _27834 = _22435;
                                                                _27833 = abs(_22571 - _22476) < ((0.0199999995529651641845703125 * _22476) + 0.02999999932944774627685546875);
                                                                break;
                                                            } while(false);
                                                            _29498 = _27833 ? _22101 : _27832;
                                                            _29499 = _27833 ? _27830 : _22101;
                                                            _29500 = _27833 ? _27835 : _27829;
                                                            bvec2 _29501 = bvec2(_27833);
                                                            _29502 = vec2(_29501.x ? _27834.x : _27825.x, _29501.y ? _27834.y : _27825.y);
                                                        }
                                                        _27827 = _27829;
                                                        _27823 = _27825;
                                                        _27712 = true;
                                                        _27708 = true;
                                                        break;
                                                    }
                                                    _22125 = min(_27700 * 1.2000000476837158203125, 0.300000011920928955078125);
                                                    _27700 = _22125;
                                                    _27699 = _22083;
                                                    _27698++;
                                                    _27828 = _27703;
                                                    _27824 = _27702;
                                                    continue;
                                                }
                                                else
                                                {
                                                    _27827 = _27828;
                                                    _27823 = _27824;
                                                    _27712 = _27714;
                                                    _27708 = false;
                                                    break;
                                                }
                                            }
                                            if (_27708)
                                            {
                                                _27711 = _27712;
                                                break;
                                            }
                                            _27711 = false;
                                            break;
                                        } while(false);
                                        if (_27711)
                                        {
                                            _27989 = _27827;
                                            _27982 = _27823;
                                            _27863 = 1u;
                                            _27848 = true;
                                            break;
                                        }
                                        _29350 = _27711;
                                        _27992 = _27827;
                                        _27985 = _27823;
                                        int _21321 = _27691 + 1;
                                        _27714 = _29350;
                                        _27691 = _21321;
                                        _27990 = _27992;
                                        _27983 = _27985;
                                        continue;
                                    }
                                    else
                                    {
                                        _27989 = _27990;
                                        _27982 = _27983;
                                        _27863 = _27086;
                                        _27848 = false;
                                        break;
                                    }
                                }
                                if (_27848)
                                {
                                    _27988 = _27989;
                                    _27981 = _27982;
                                    _27862 = _27863;
                                    break;
                                }
                                _27988 = _27989;
                                _27981 = _27982;
                                _27862 = 0u;
                                break;
                            } while(false);
                            if (_27862 != 0u)
                            {
                                float _22679 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec3 _19693 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_27981.x - (0.5 * float(_283.OutputWidth))) * _27988) / _22679)) + (_283.CameraUp.xyz * (((-(_27981.y - (0.5 * float(_283.OutputHeight)))) * _27988) / _22679))) + (_283.CameraForward.xyz * _27988));
                                bvec3 _19694 = bvec3(_27862 == 1u);
                                vec3 _19695 = vec3(_19694.x ? _19672.x : _19693.x, _19694.y ? _19672.y : _19693.y, _19694.z ? _19672.z : _19693.z);
                                _28459 = _27981;
                                _28365 = _27988;
                                _28271 = (_27862 == 1u) ? _27676 : length(_19695 - _6179);
                                _28177 = 1.0;
                                _28083 = _19695;
                                break;
                            }
                        }
                        float _19717 = min(_27676, 4.0);
                        _28459 = _27998;
                        _28365 = _28028;
                        _28271 = _19717;
                        _28177 = _28057;
                        _28083 = _6179 + (_20966 * _19717);
                        break;
                    } while(false);
                    vec3 _28555;
                    do
                    {
                        vec2 _22916 = clamp(_28459 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _22927 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_22916.x, 1.0 - _22916.y), 0.0).xyz;
                        if ((_28177 < 0.5) || (_28365 > 100000000.0))
                        {
                            _28555 = _22927;
                            break;
                        }
                        ivec2 _22750 = ivec2(_28459);
                        float _22756 = _6179.y - _28083.y;
                        if (_22756 <= 0.00999999977648258209228515625)
                        {
                            _28555 = _22927;
                            break;
                        }
                        ivec2 _22957 = clamp(_22750, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        ivec2 _23045 = clamp(_22750, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec3 _23005 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_23045.x, int((_283.OutputHeight - 1u) - uint(_23045.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _23007 = length(_23005);
                        vec3 _23013 = _23005 / vec3(_23007);
                        bvec3 _23014 = bvec3(_23007 > 0.001000000047497451305389404296875);
                        ivec2 _23105 = clamp(_22750, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        float _22802 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_22957.x, int((_283.OutputHeight - 1u) - uint(_22957.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_23014.x ? _23013.x : vec3(0.0, 1.0, 0.0).x, _23014.y ? _23013.y : vec3(0.0, 1.0, 0.0).y, _23014.z ? _23013.z : vec3(0.0, 1.0, 0.0).z), _5670), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_23105.x, int((_283.OutputHeight - 1u) - uint(_23105.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _283.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_22927, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _22806 = max(-_29451.y, 0.20000000298023223876953125);
                        float _28554;
                        if ((_283.CausticParams.x > 0.0) && (_22802 > 0.0))
                        {
                            vec2 _22849 = _28083.xz - ((_29451.xz / vec2(_22806)) * _22756);
                            vec2 _23160 = _22849 * (6.283185482025146484375 / max(_283.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _23183 = _23160 + (vec2(sin((_23160.y * 0.730000019073486328125) + ((_283.ShadeParams.z * 0.89999997615814208984375) * _283.CausticParams.z)), sin((_23160.x * 0.61000001430511474609375) - ((_283.ShadeParams.z * 1.10000002384185791015625) * _283.CausticParams.z))) * 0.89999997615814208984375);
                            float _28553;
                            do
                            {
                                if ((_283.GridParams.z < 2u) || (_283.GridParams.w < 2u))
                                {
                                    _28553 = 1.0;
                                    break;
                                }
                                vec2 _23293 = clamp(((_22849 - _283.PoolMin.xz) / max(_283.PoolMax.xz - _283.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)), vec2(0.0), vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _23295 = uvec2(_23293);
                                vec2 _23299 = _23293 - vec2(_23295);
                                uint _23306 = _23295.x + (_23295.y * _283.GridParams.z);
                                _28553 = mix(mix(CausticField_1._data[_23306], CausticField_1._data[_23306 + 1u], _23299.x), mix(CausticField_1._data[_23306 + _283.GridParams.z], CausticField_1._data[(_23306 + _283.GridParams.z) + 1u], _23299.x), _23299.y);
                                break;
                            } while(false);
                            _28554 = min(max(1.0 + ((_283.CausticParams.x * (exp((-_22756) / max(_283.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _22756))) * ((pow(clamp((1.0 - abs(sin(_23183.x + ((_283.ShadeParams.z * 0.60000002384185791015625) * _283.CausticParams.z)) * sin((_23183.y * 1.12999999523162841796875) - ((_283.ShadeParams.z * 0.4000000059604644775390625) * _283.CausticParams.z)))) * (1.0 - abs(sin(((_23183.x + _23183.y) * 0.709999978542327880859375) - ((_283.ShadeParams.z * 0.5) * _283.CausticParams.z)) * sin(((_23183.x - _23183.y) * 0.829999983310699462890625) + ((_283.ShadeParams.z * 0.699999988079071044921875) * _283.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_28553, 0.0500000007450580596923828125), _283.LightParams.y), 2.5);
                        }
                        else
                        {
                            _28554 = 1.0;
                        }
                        _28555 = _22927 * ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _22756) * 1.2000000476837158203125), vec3(_283.LightParams.x)) * (1.0 - _22802)) + ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _22756) / vec3(_22806)), vec3(_283.LightParams.x)) * _22802) * _28554));
                        break;
                    } while(false);
                    vec3 _17633 = exp((-_283.FluidTint.xyz) * max(_28271, 0.0500000007450580596923828125));
                    float _23385 = _283.ScatterParams.z * _283.ScatterParams.z;
                    _28604 = mix(_27527, mix((_28555 * _17633) + ((_283.FluidColor.xyz * ((_6685 * _283.ScatterParams.y) + (_283.SunColor.xyz * (((_283.ScatterParams.x * 0.25) * ((1.0 - _23385) / pow(max((1.0 + _23385) - ((2.0 * _283.ScatterParams.z) * dot(_29451, -_17507)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5670.y, 0.0, 1.0))))) * (vec3(1.0) - _17633)), mix(_27652, _27646, vec3(_27558)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_17581, -_17507), 0.0, 1.0), max(_283.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _6092), 0.0, 1.0)));
                }
                else
                {
                    _28604 = _27527;
                }
                _28603 = _28604;
            }
            else
            {
                _28603 = _27527;
            }
            _28602 = _28603;
        }
        else
        {
            _28602 = _27527;
        }
        float _6245 = clamp((clamp((float(_12755) * 0.0009765625) / _5944, 0.0, 1.0) * _283.ScatterParams.w) + (((_5645 * 0.000244140625) * _283.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _6257 = exp(max(_283.FluidTint.xyz, vec3(0.0)) * (-_6157)) * (1.0 - (0.85000002384185791015625 * _6245));
        vec3 _23414 = ((_283.SunColor.xyz * ((clamp(_5670.y, 0.0, 1.0) * _26862) * 0.3183098733425140380859375)) + _6685) * 0.85000002384185791015625;
        vec3 _23420 = mix(vec3(dot(_23414, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _23414, vec3(0.5));
        float _23468 = _283.ScatterParams.z * _283.ScatterParams.z;
        vec3 _6294 = ((_28602 * _6257) + ((vec3(1.0) - _6257) * mix(_283.FluidColor.xyz * ((_6685 * _283.ScatterParams.y) + (_283.SunColor.xyz * ((((_283.ScatterParams.x * 0.25) * ((1.0 - _23468) / pow(max((1.0 + _23468) - ((2.0 * _283.ScatterParams.z) * dot(_29451, -_12884)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5670.y, 0.0, 1.0)) * _26862))), _23420 * 0.800000011920928955078125, vec3(_6245)))) + (_23420 * (((float(_5650) * 2.2888532839715480804443359375e-05) + (_5645 * 3.6621095205191522836685180664062e-05)) * clamp(_283.FoamParams.z, 0.0, 1.0)));
        if (_283.DebugView == 3u)
        {
            float _6301 = clamp(_6157 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5606), vec4(_6301, _6301, _6301, 1.0));
            break;
        }
        if (_283.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_5606), vec4(_26780.www, 1.0));
            break;
        }
        if (_283.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_5606), vec4(_6294, 1.0));
            break;
        }
        if (_283.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_5606), vec4(clamp((_26816 * 0.0625) + (_5639 * 0.000244140625), 0.0, 1.0), _6245, clamp(_5963 + (_5633 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_283.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_5606), vec4(_27528, 1.0));
            break;
        }
        float _6373 = 0.014999999664723873138427734375 + (2.0 * sqrt(_6039));
        vec3 _6376 = reflect(_5825, _26864);
        vec3 _23502 = ((_283.CameraRight.xyz * _6376.x) + (_283.CameraUp.xyz * _6376.y)) + (_283.CameraForward.xyz * _6376.z);
        float _28810;
        vec3 _28943;
        do
        {
            if (_283.ReflectionDistance <= 0.0)
            {
                _28943 = vec3(0.0);
                _28810 = 0.0;
                break;
            }
            float _23551 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _6376.z);
            if (_23551 <= 0.0)
            {
                _28943 = vec3(0.0);
                _28810 = 0.0;
                break;
            }
            float _23748 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _23560 = max(0.0040000001899898052215576171875 * _26779, 0.00999999977648258209228515625);
            float _23562 = 0.5 * _23560;
            float _23571;
            float _23573;
            bool _28795;
            bool _28800;
            float _28805;
            float _28811;
            vec3 _28944;
            int _28786 = 0;
            float _28787 = _23562;
            float _28788 = _23560;
            float _28806 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_28786 < 80)
                {
                    _23571 = _28787 + _28788;
                    _23573 = _28788 * 1.07000005245208740234375;
                    if (_23571 > _283.ReflectionDistance)
                    {
                        _28944 = vec3(0.0);
                        _28811 = 0.0;
                        _28805 = _28806;
                        _28800 = false;
                        _28795 = true;
                        break;
                    }
                    vec3 _23584 = _9697 + (_6376 * _23571);
                    if (_23584.z <= 0.0500000007450580596923828125)
                    {
                        _28944 = vec3(0.0);
                        _28811 = 0.0;
                        _28805 = _28806;
                        _28800 = false;
                        _28795 = true;
                        break;
                    }
                    float _23788 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _23764 = ((_23584.x * _23788) / _23584.z) + (0.5 * float(_283.OutputWidth));
                    float _23777 = (((-_23584.y) * _23788) / _23584.z) + (0.5 * float(_283.OutputHeight));
                    if (!((((_23764 >= 0.0) && (_23777 >= 0.0)) && (_23764 < float(_283.OutputWidth))) && (_23777 < float(_283.OutputHeight))))
                    {
                        _28944 = vec3(0.0);
                        _28811 = _27312;
                        _28805 = _28806;
                        _28800 = true;
                        _28795 = false;
                        break;
                    }
                    ivec2 _23872 = clamp(ivec2(vec2(_23764, _23777)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _23824 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_23872.x, int((_283.OutputHeight - 1u) - uint(_23872.y)), 0).xy, 0);
                    float _23825 = _23824.x;
                    float _23842 = ((_23825 > 0.0) && (abs(_23825 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_23825 - _283.ProjA)) : 1000000000.0;
                    if (((_23842 < 100000000.0) && (_23584.z > _23842)) && ((_23584.z - _23842) < max(_28788 * 2.1400001049041748046875, 0.25)))
                    {
                        float _28790;
                        _28790 = _23571;
                        float _29505;
                        float _29506;
                        int _28789 = 0;
                        float _28793 = _28787;
                        SPIRV_CROSS_UNROLL
                        for (; _28789 < 6; _28793 = _29505, _28790 = _29506, _28789++)
                        {
                            float _23628 = 0.5 * (_28793 + _28790);
                            vec3 _23633 = _9697 + (_6376 * _23628);
                            float _23942 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _24003 = clamp(ivec2(vec2(((_23633.x * _23942) / _23633.z) + (0.5 * float(_283.OutputWidth)), (((-_23633.y) * _23942) / _23633.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                            vec4 _23955 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24003.x, int((_283.OutputHeight - 1u) - uint(_24003.y)), 0).xy, 0);
                            float _23956 = _23955.x;
                            bool _23640 = _23633.z > (((_23956 > 0.0) && (abs(_23956 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_23956 - _283.ProjA)) : 1000000000.0);
                            _29505 = _23640 ? _28793 : _23628;
                            _29506 = _23640 ? _23628 : _28790;
                        }
                        vec3 _23654 = _9697 + (_6376 * _28790);
                        float _24073 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _24063 = vec2(((_23654.x * _24073) / _23654.z) + (0.5 * float(_283.OutputWidth)), (((-_23654.y) * _24073) / _23654.z) + (0.5 * float(_283.OutputHeight)));
                        ivec2 _24131 = clamp(ivec2(_24063), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec3 _24091 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_24131.x, int((_283.OutputHeight - 1u) - uint(_24131.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _24093 = length(_24091);
                        vec3 _24099 = _24091 / vec3(_24093);
                        bvec3 _24100 = bvec3(_24093 > 0.001000000047497451305389404296875);
                        vec3 _24101 = vec3(_24100.x ? _24099.x : vec3(0.0, 1.0, 0.0).x, _24100.y ? _24099.y : vec3(0.0, 1.0, 0.0).y, _24100.z ? _24099.z : vec3(0.0, 1.0, 0.0).z);
                        if (dot(vec3(dot(_24101, _283.CameraRight.xyz), dot(_24101, _283.CameraUp.xyz), dot(_24101, _283.CameraForward.xyz)), _6376) > 0.100000001490116119384765625)
                        {
                            int _23681 = _28786 + 1;
                            _28788 = _23573;
                            _28787 = _23571;
                            _28786 = _23681;
                            _28806 = _23571;
                            continue;
                        }
                        float _24195 = clamp(((_6373 * _28790) * _23748) / max(_23654.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _24197 = 0.5 * _24195;
                        vec2 _24282 = clamp(_24063 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24308 = clamp((_24063 + vec2(_24197)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24334 = clamp((_24063 + vec2(_24195 * (-0.5), _24197)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24360 = clamp((_24063 + vec2(_24197, _24195 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24386 = clamp((_24063 + vec2(_24195 * (-0.5), _24195 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24412 = clamp((_24063 + vec2(_24195, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24438 = clamp((_24063 + vec2(-_24195, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24464 = clamp((_24063 + vec2(0.0, _24195)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24490 = clamp((_24063 + vec2(0.0, -_24195)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _24267 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24282.x, 1.0 - _24282.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24308.x, 1.0 - _24308.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24334.x, 1.0 - _24334.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24360.x, 1.0 - _24360.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24386.x, 1.0 - _24386.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24412.x, 1.0 - _24412.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24438.x, 1.0 - _24438.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24464.x, 1.0 - _24464.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24490.x, 1.0 - _24490.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _28944 = _24267;
                        _28811 = _23551;
                        _28805 = _23571;
                        _28800 = false;
                        _28795 = true;
                        break;
                    }
                    int _23681 = _28786 + 1;
                    _28788 = _23573;
                    _28787 = _23571;
                    _28786 = _23681;
                    _28806 = _23571;
                    continue;
                }
                else
                {
                    _28944 = vec3(0.0);
                    _28811 = _27312;
                    _28805 = _28806;
                    _28800 = false;
                    _28795 = false;
                    break;
                }
            }
            if (_28795)
            {
                _28943 = _28944;
                _28810 = _28811;
                break;
            }
            if ((!_28800) || (_28805 < 0.0))
            {
                _28943 = _28944;
                _28810 = 0.0;
                break;
            }
            vec3 _23696 = _9697 + (_6376 * _28805);
            float _24541 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _24531 = vec2(((_23696.x * _24541) / _23696.z) + (0.5 * float(_283.OutputWidth)), (((-_23696.y) * _24541) / _23696.z) + (0.5 * float(_283.OutputHeight)));
            ivec2 _24602 = clamp(ivec2(_24531), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _24554 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24602.x, int((_283.OutputHeight - 1u) - uint(_24602.y)), 0).xy, 0);
            float _24555 = _24554.x;
            float _24572 = ((_24555 > 0.0) && (abs(_24555 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24555 - _283.ProjA)) : 1000000000.0;
            float _24648 = clamp(((_6373 * _28805) * _23748) / max(_23696.z, 0.0500000007450580596923828125), 0.0, 12.0);
            float _24650 = 0.5 * _24648;
            vec2 _24735 = clamp(_24531 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24761 = clamp((_24531 + vec2(_24650)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24787 = clamp((_24531 + vec2(_24648 * (-0.5), _24650)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24813 = clamp((_24531 + vec2(_24650, _24648 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24839 = clamp((_24531 + vec2(_24648 * (-0.5), _24648 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24865 = clamp((_24531 + vec2(_24648, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24891 = clamp((_24531 + vec2(-_24648, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24917 = clamp((_24531 + vec2(0.0, _24648)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24943 = clamp((_24531 + vec2(0.0, -_24648)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _24720 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24735.x, 1.0 - _24735.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24761.x, 1.0 - _24761.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24787.x, 1.0 - _24787.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24813.x, 1.0 - _24813.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24839.x, 1.0 - _24839.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24865.x, 1.0 - _24865.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24891.x, 1.0 - _24891.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24917.x, 1.0 - _24917.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24943.x, 1.0 - _24943.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_24572 > 100000000.0)
            {
                _28943 = _24720;
                _28810 = _23551;
                break;
            }
            float _23721 = _24572 - _23696.z;
            if (_23721 <= 0.0)
            {
                _28943 = _24720;
                _28810 = _23551;
                break;
            }
            _28943 = _24720;
            _28810 = _23551 * exp((_23721 / max(_6376.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        vec3 _28949;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                float _24997 = _23502.y;
                _28949 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _24997))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _24997)));
                break;
            }
            _28949 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_23502.x, _23502.y, -_23502.z), clamp(clamp(log2(max(_6373 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _6396 = mix(_28949, _28943, vec3(_28810));
        if (_283.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_5606), vec4(_6396, 1.0));
            break;
        }
        float _6424 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_26818 + _26864), _5827), 0.0, 1.0), max(_283.FresnelPower, 1.0));
        float _6425 = 0.0199999995529651641845703125 + _6424;
        vec3 _6435 = normalize(_283.SunDirection.xyz);
        float _6452 = max((2.0 / ((2.0 / (max(_283.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _6039))) - 2.0, 4.0);
        vec3 _6521 = (mix(_6294, _6396, vec3(clamp(_6425, 0.0, 1.0))) + (((((_283.SunColor.xyz * ((pow(clamp(dot(_26864, normalize(_6435 - _5825)), 0.0, 1.0), _6452) * (_6452 + 8.0)) * 0.0397887341678142547607421875)) * _283.Specular) * _6425) * _26862) * clamp(dot(_26864, _6435) * 4.0, 0.0, 1.0))) + ((_283.SunColor.xyz * _283.FluidColor.xyz) * (((((_283.LightParams.w * 0.60000002384185791015625) * (((1.0 - _6092) * exp(_6087 * (-1.5))) * (1.0 - exp(_6087 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_5670, _12884)), 9.9999997473787516355514526367188e-05), 1.5))) * _26862) * (0.980000019073486328125 - _6424)));
        vec3 _6539 = mix(_6521, (_6521 * 1.25) + (_6685 * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _6082)) * _6092) * _283.LightParams.z, 0.0, 1.0)));
        float _6548 = clamp(1.0 - exp(-((_26816 * 0.07500000298023223876953125) + (_5639 * 0.0003662109375))), 0.0, 1.0);
        vec3 _29072;
        if ((_6548 > 0.0199999995529651641845703125) && (_283.FoamParams.x > 0.0))
        {
            vec2 _25058 = _5969.xz / vec2(max(_283.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _25093 = floor(_25058);
            vec2 _25096 = _25058 - _25093;
            float _29065;
            float _29066;
            _29066 = 8.0;
            _29065 = 8.0;
            float _29081;
            float _29082;
            SPIRV_CROSS_UNROLL
            for (int _29064 = -1; _29064 <= 1; _29066 = _29081, _29065 = _29082, _29064++)
            {
                _29082 = _29065;
                _29081 = _29066;
                float _29508;
                float _29428;
                SPIRV_CROSS_UNROLL
                for (int _29079 = -1; _29079 <= 1; _29082 = _29428, _29081 = _29508, _29079++)
                {
                    vec2 _25111 = vec2(float(_29079), float(_29064));
                    ivec2 _25115 = ivec2(_25093 + _25111);
                    uint _25187 = ((((uint(_25115.x + 4096) * 1973u) + (uint(_25115.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25195 = ((_25187 >> ((_25187 >> 28u) + 4u)) ^ _25187) * 277803737u;
                    uint _25199 = (_25195 >> 22u) ^ _25195;
                    float _25153 = length((_25111 + (vec2(0.5) + (sin(vec2(_283.ShadeParams.z * 0.5) + ((vec2(float(_25199 & 65535u), float(_25199 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25096);
                    bool _25156 = _25153 < _29081;
                    if (_25156)
                    {
                        _29428 = _29081;
                    }
                    else
                    {
                        _29428 = (_25153 < _29082) ? _25153 : _29082;
                    }
                    _29508 = _25156 ? _25153 : _29081;
                }
            }
            vec2 _25065 = (_25058 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _25216 = floor(_25065);
            vec2 _25219 = _25065 - _25216;
            float _29068;
            float _29069;
            _29069 = 8.0;
            _29068 = 8.0;
            float _29075;
            float _29076;
            SPIRV_CROSS_UNROLL
            for (int _29067 = -1; _29067 <= 1; _29069 = _29075, _29068 = _29076, _29067++)
            {
                _29076 = _29068;
                _29075 = _29069;
                float _29510;
                float _29422;
                SPIRV_CROSS_UNROLL
                for (int _29073 = -1; _29073 <= 1; _29076 = _29422, _29075 = _29510, _29073++)
                {
                    vec2 _25234 = vec2(float(_29073), float(_29067));
                    ivec2 _25238 = ivec2(_25216 + _25234);
                    uint _25310 = ((((uint(_25238.x + 4096) * 1973u) + (uint(_25238.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25318 = ((_25310 >> ((_25310 >> 28u) + 4u)) ^ _25310) * 277803737u;
                    uint _25322 = (_25318 >> 22u) ^ _25318;
                    float _25276 = length((_25234 + (vec2(0.5) + (sin(vec2(_283.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_25322 & 65535u), float(_25322 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25219);
                    bool _25279 = _25276 < _29075;
                    if (_25279)
                    {
                        _29422 = _29075;
                    }
                    else
                    {
                        _29422 = (_25276 < _29076) ? _25276 : _29076;
                    }
                    _29510 = _25279 ? _25276 : _29075;
                }
            }
            float _25076 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29065 - _29066), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29068 - _29069))), clamp((((_283.FoamParams.y * ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_26779, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _29072 = mix(_6539, _23420 * mix(0.85000002384185791015625, 1.0, _25076), vec3(clamp(_6548 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_25076 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_283.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _29072 = _6539;
        }
        float _6594 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _5963) * clamp(_283.Foam * 1.60000002384185791015625, 0.0, 1.0);
        imageStore(OutputTexture, ivec2(_5606), vec4(mix(mix(_29072, _23420, vec3(_6594 * 0.89999997615814208984375)), _23420, vec3(_5693)), max(clamp(_26780.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _6594)), _5693)));
        break;
    } while(false);
}

