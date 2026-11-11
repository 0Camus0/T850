#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=fdd4a59d77af6bf5a1a3b7804f84ec38554c3ccebe9e0ee3f0b0453476984c52; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1894[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _27021;
bool _27249;
uint _27361;
vec3 _27539;
float _27595;
vec2 _27761;
float _29849;

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
        uvec2 _5547 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _5547.x;
        FluidSize.y = _5547.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _5574 = uvec2(gl_GlobalInvocationID.x, (_283.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _5577 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _5582 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _5596 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _283.OutputWidth)) * 4u;
        uint _5600 = WhitewaterBits_1._data[_5596];
        float _5601 = float(_5600);
        uint _5606 = WhitewaterBits_1._data[_5596 + 1u];
        float _5607 = float(_5606);
        uint _5612 = WhitewaterBits_1._data[_5596 + 2u];
        float _5613 = float(_5612);
        uint _5618 = WhitewaterBits_1._data[_5596 + 3u];
        WhitewaterBits_1._data[_5596] = 0u;
        WhitewaterBits_1._data[_5596 + 1u] = 0u;
        WhitewaterBits_1._data[_5596 + 2u] = 0u;
        WhitewaterBits_1._data[_5596 + 3u] = 0u;
        vec3 _5636 = normalize(_283.SunDirection.xyz);
        vec3 _5638 = normalize(((_283.CameraRight.xyz * _5636.x) + (_283.CameraUp.xyz * _5636.y)) + (_283.CameraForward.xyz * _5636.z));
        vec3 _5641 = refract(-_5638, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _29787 = bvec3((dot(_5641, _5641) < 9.9999999747524270787835121154785e-07) || (_5638.y <= 0.0));
        vec3 _29788 = vec3(_29787.x ? vec3(0.0, -1.0, 0.0).x : _5641.x, _29787.y ? vec3(0.0, -1.0, 0.0).y : _5641.y, _29787.z ? vec3(0.0, -1.0, 0.0).z : _5641.z);
        float _6631 = max(EnvMaxMip - 1.0, 0.0);
        vec3 _27022;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27022 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                break;
            }
            _27022 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_6631, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27023;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27023 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27023 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6631, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27025;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27025 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27025 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6631, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27027;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27027 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27027 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_6631, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _27029;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                _27029 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _27029 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_6631, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _6653 = (((((_27022 * 2.0) + _27023) + _27025) + _27027) + _27029) * vec3(0.16666667163372039794921875);
        float _5661 = clamp(1.0 - exp(_5601 * (-0.0006103515625)), 0.0, 1.0) * clamp(_283.FoamParams.w, 0.0, 1.0);
        float _27033;
        vec4 _27034;
        do
        {
            vec2 _7006 = ((_5582 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
            ivec2 _7009 = ivec2(floor(_7006));
            vec2 _7013 = _7006 - vec2(_7009);
            ivec2 _7188 = clamp(_7009, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7224 = clamp(_7009 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7260 = clamp(_7009 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7296 = clamp(_7009 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7049 = vec4((1.0 - _7013.x) * (1.0 - _7013.y), _7013.x * (1.0 - _7013.y), (1.0 - _7013.x) * _7013.y, _7013.x * _7013.y);
            vec4 _6976 = _7049;
            vec4 _7054 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7188.x, int((FluidSize.y - 1u) - uint(_7188.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7224.x, int((FluidSize.y - 1u) - uint(_7224.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7260.x, int((FluidSize.y - 1u) - uint(_7260.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7296.x, int((FluidSize.y - 1u) - uint(_7296.y)), 0).xy, 0).x);
            vec4 _6977 = _7054;
            float _29516;
            _29516 = 1000000000.0;
            float _29786;
            SPIRV_CROSS_UNROLL
            for (int _27031 = 0; _27031 < 4; _29516 = _29786, _27031++)
            {
                if (_6977[_27031] > 0.0)
                {
                    _29786 = min(_29516, _6977[_27031]);
                }
                else
                {
                    _29786 = _29516;
                }
            }
            vec4 _6980 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _27032 = 0; _27032 < 4; )
            {
                _6980[_27032] = ((_6977[_27032] > 0.0) && (_6977[_27032] < (_29516 + max(0.25, 0.02999999932944774627685546875 * _29516)))) ? _6976[_27032] : 0.0;
                _27032++;
                continue;
            }
            float _7114 = ((_6980.x + _6980.y) + _6980.z) + _6980.w;
            if (_7114 <= 9.9999997473787516355514526367188e-05)
            {
                _27034 = vec4(0.0);
                _27033 = 0.0;
                break;
            }
            ivec2 _7332 = clamp(_7009, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7358 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7332.x, int((FluidSize.y - 1u) - uint(_7332.y)), 0).xy, 0);
            ivec2 _7367 = clamp(_7009 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7393 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7367.x, int((FluidSize.y - 1u) - uint(_7367.y)), 0).xy, 0);
            ivec2 _7402 = clamp(_7009 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7428 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7402.x, int((FluidSize.y - 1u) - uint(_7402.y)), 0).xy, 0);
            ivec2 _7437 = clamp(_7009 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7463 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7437.x, int((FluidSize.y - 1u) - uint(_7437.y)), 0).xy, 0);
            _27034 = vec4((((((_7358.xyz * _6980.x) + (_7393.xyz * _6980.y)) + (_7428.xyz * _6980.z)) + (_7463.xyz * _6980.w)) / vec3(_7114)).x, _29849, _29849, dot(_7049, vec4(_7358.w, _7393.w, _7428.w, _7463.w)));
            _27033 = dot(_6980, _7054) / _7114;
            break;
        } while(false);
        ivec2 _7525 = clamp(_5577, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
        vec4 _7476 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7525.x, int((_283.OutputHeight - 1u) - uint(_7525.y)), 0).xy, 0);
        float _7477 = _7476.x;
        float _7494 = ((_7477 > 0.0) && (abs(_7477 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_7477 - _283.ProjA)) : 1000000000.0;
        if (!(((_27033 > 0.0) && (_27034.w > 0.001000000047497451305389404296875)) && (_27033 < (_7494 - 0.00200000009499490261077880859375))))
        {
            vec2 _7570 = clamp(_5582 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _7581 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_7570.x, 1.0 - _7570.y), 0.0).xyz;
            float _29501;
            vec3 _29512;
            if ((_7494 < 100000000.0) && (_283.WetParams.w > 0.0))
            {
                float _7629 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _5696 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_5582.x - (0.5 * float(_283.OutputWidth))) * _7494) / _7629)) + (_283.CameraUp.xyz * (((-(_5582.y - (0.5 * float(_283.OutputHeight)))) * _7494) / _7629))) + (_283.CameraForward.xyz * _7494));
                ivec2 _7709 = clamp(_5577, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                vec3 _7669 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_7709.x, int((_283.OutputHeight - 1u) - uint(_7709.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _7671 = length(_7669);
                vec3 _7677 = _7669 / vec3(_7671);
                bvec3 _7678 = bvec3(_7671 > 0.001000000047497451305389404296875);
                float _29502;
                vec3 _29513;
                if ((vec3(_7678.x ? _7677.x : vec3(0.0, 1.0, 0.0).x, _7678.y ? _7677.y : vec3(0.0, 1.0, 0.0).y, _7678.z ? _7677.z : vec3(0.0, 1.0, 0.0).z).y > 0.60000002384185791015625) && (_5696.y > (_283.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _29443;
                    do
                    {
                        int _7762 = int(_283.GridParams.x);
                        int _7765 = int(_283.GridParams.y);
                        if (((_7762 < 4) || (_7765 < 4)) || (_283.WetParams.z <= 0.0))
                        {
                            _29443 = 0.0;
                            break;
                        }
                        vec2 _7787 = ((_5696.xz - _283.WetParams.xy) / vec2(_283.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_7787, vec2(-1.0))) || (_7787.x > float(_7762))) || (_7787.y > float(_7765)))
                        {
                            _29443 = 0.0;
                            break;
                        }
                        ivec2 _7807 = ivec2(floor(_7787));
                        vec2 _7811 = _7787 - vec2(_7807);
                        float _7813 = _7811.x;
                        float _7894 = _7813 * _7813;
                        float _7897 = _7894 * _7813;
                        vec4 _7747 = vec4(((1.0 - (3.0 * _7813)) + (3.0 * _7894)) - _7897, (4.0 - (6.0 * _7894)) + (3.0 * _7897), ((1.0 + (3.0 * _7813)) + (3.0 * _7894)) - (3.0 * _7897), _7897) * 0.16666667163372039794921875;
                        float _7816 = _7811.y;
                        float _7930 = _7816 * _7816;
                        float _7933 = _7930 * _7816;
                        vec4 _7749 = vec4(((1.0 - (3.0 * _7816)) + (3.0 * _7930)) - _7933, (4.0 - (6.0 * _7930)) + (3.0 * _7933), ((1.0 + (3.0 * _7816)) + (3.0 * _7930)) - (3.0 * _7933), _7933) * 0.16666667163372039794921875;
                        float _29436;
                        _29436 = 0.0;
                        float _29772;
                        SPIRV_CROSS_UNROLL
                        for (int _29435 = 0; _29435 < 4; _29436 = _29772, _29435++)
                        {
                            int _7827 = (_7807.y - 1) + _29435;
                            if ((_7827 < 0) || (_7827 >= _7765))
                            {
                                _29772 = _29436;
                                continue;
                            }
                            float _29439;
                            _29439 = 0.0;
                            float _29774;
                            SPIRV_CROSS_UNROLL
                            for (int _29437 = 0; _29437 < 4; _29439 = _29774, _29437++)
                            {
                                int _7845 = (_7807.x - 1) + _29437;
                                if ((_7845 >= 0) && (_7845 < _7762))
                                {
                                    _29774 = _29439 + (_7747[_29437] * float(Wetness_1._data[uint(_7845 + (_7827 * _7762))]));
                                }
                                else
                                {
                                    _29774 = _29439;
                                }
                            }
                            _29772 = _29436 + (_7749[_29435] * _29439);
                        }
                        _29443 = _29436 * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _5718 = clamp(((_29443 * _283.WetParams.w) - 0.00999999977648258209228515625) * 1.01010096073150634765625, 0.0, 1.0);
                    vec3 _29514;
                    if (_5718 > 0.001000000047497451305389404296875)
                    {
                        vec3 _5725 = normalize(_5696 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w));
                        vec3 _29500;
                        do
                        {
                            float _7996 = smoothstep(0.449999988079071044921875, 1.0, _5718);
                            vec3 _8000 = _7581 * mix(1.0, 0.5, clamp(_5718 * 2.0, 0.0, 1.0));
                            if (_7996 <= 0.0)
                            {
                                _29500 = _8000;
                                break;
                            }
                            vec3 _8018 = reflect(_5725, vec3(0.0, 1.0, 0.0));
                            float _8077 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec3 _8067 = vec3(((_5582.x - (0.5 * float(_283.OutputWidth))) * _7494) / _8077, ((-(_5582.y - (0.5 * float(_283.OutputHeight)))) * _7494) / _8077, _7494);
                            float _8094 = dot(_8018, _283.CameraForward.xyz);
                            vec3 _8095 = vec3(dot(_8018, _283.CameraRight.xyz), dot(_8018, _283.CameraUp.xyz), _8094);
                            float _29473;
                            vec3 _29493;
                            do
                            {
                                if (_283.ReflectionDistance <= 0.0)
                                {
                                    _29493 = vec3(0.0);
                                    _29473 = 0.0;
                                    break;
                                }
                                float _8144 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _8094);
                                if (_8144 <= 0.0)
                                {
                                    _29493 = vec3(0.0);
                                    _29473 = 0.0;
                                    break;
                                }
                                float _8341 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _8153 = max(0.0040000001899898052215576171875 * _7494, 0.00999999977648258209228515625);
                                float _8155 = 0.5 * _8153;
                                float _8164;
                                float _8166;
                                bool _29458;
                                bool _29463;
                                float _29468;
                                float _29474;
                                vec3 _29494;
                                int _29449 = 0;
                                float _29450 = _8155;
                                float _29451 = _8153;
                                float _29469 = -1.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_29449 < 80)
                                    {
                                        _8164 = _29450 + _29451;
                                        _8166 = _29451 * 1.07000005245208740234375;
                                        if (_8164 > _283.ReflectionDistance)
                                        {
                                            _29494 = vec3(0.0);
                                            _29474 = 0.0;
                                            _29468 = _29469;
                                            _29463 = false;
                                            _29458 = true;
                                            break;
                                        }
                                        vec3 _8177 = _8067 + (_8095 * _8164);
                                        if (_8177.z <= 0.0500000007450580596923828125)
                                        {
                                            _29494 = vec3(0.0);
                                            _29474 = 0.0;
                                            _29468 = _29469;
                                            _29463 = false;
                                            _29458 = true;
                                            break;
                                        }
                                        float _8381 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        float _8357 = ((_8177.x * _8381) / _8177.z) + (0.5 * float(_283.OutputWidth));
                                        float _8370 = (((-_8177.y) * _8381) / _8177.z) + (0.5 * float(_283.OutputHeight));
                                        if (!((((_8357 >= 0.0) && (_8370 >= 0.0)) && (_8357 < float(_283.OutputWidth))) && (_8370 < float(_283.OutputHeight))))
                                        {
                                            _29494 = vec3(0.0);
                                            _29474 = _27595;
                                            _29468 = _29469;
                                            _29463 = true;
                                            _29458 = false;
                                            break;
                                        }
                                        ivec2 _8465 = clamp(ivec2(vec2(_8357, _8370)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _8417 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8465.x, int((_283.OutputHeight - 1u) - uint(_8465.y)), 0).xy, 0);
                                        float _8418 = _8417.x;
                                        float _8435 = ((_8418 > 0.0) && (abs(_8418 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_8418 - _283.ProjA)) : 1000000000.0;
                                        if (((_8435 < 100000000.0) && (_8177.z > _8435)) && ((_8177.z - _8435) < max(_29451 * 2.1400001049041748046875, 0.25)))
                                        {
                                            float _29453;
                                            _29453 = _8164;
                                            float _29789;
                                            float _29790;
                                            int _29452 = 0;
                                            float _29456 = _29450;
                                            SPIRV_CROSS_UNROLL
                                            for (; _29452 < 6; _29456 = _29789, _29453 = _29790, _29452++)
                                            {
                                                float _8221 = 0.5 * (_29456 + _29453);
                                                vec3 _8226 = _8067 + (_8095 * _8221);
                                                float _8535 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                ivec2 _8596 = clamp(ivec2(vec2(((_8226.x * _8535) / _8226.z) + (0.5 * float(_283.OutputWidth)), (((-_8226.y) * _8535) / _8226.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                vec4 _8548 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8596.x, int((_283.OutputHeight - 1u) - uint(_8596.y)), 0).xy, 0);
                                                float _8549 = _8548.x;
                                                bool _8233 = _8226.z > (((_8549 > 0.0) && (abs(_8549 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_8549 - _283.ProjA)) : 1000000000.0);
                                                _29789 = _8233 ? _29456 : _8221;
                                                _29790 = _8233 ? _8221 : _29453;
                                            }
                                            vec3 _8247 = _8067 + (_8095 * _29453);
                                            float _8666 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            vec2 _8656 = vec2(((_8247.x * _8666) / _8247.z) + (0.5 * float(_283.OutputWidth)), (((-_8247.y) * _8666) / _8247.z) + (0.5 * float(_283.OutputHeight)));
                                            ivec2 _8724 = clamp(ivec2(_8656), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                            vec3 _8684 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_8724.x, int((_283.OutputHeight - 1u) - uint(_8724.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                            float _8686 = length(_8684);
                                            vec3 _8692 = _8684 / vec3(_8686);
                                            bvec3 _8693 = bvec3(_8686 > 0.001000000047497451305389404296875);
                                            vec3 _8694 = vec3(_8693.x ? _8692.x : vec3(0.0, 1.0, 0.0).x, _8693.y ? _8692.y : vec3(0.0, 1.0, 0.0).y, _8693.z ? _8692.z : vec3(0.0, 1.0, 0.0).z);
                                            if (dot(vec3(dot(_8694, _283.CameraRight.xyz), dot(_8694, _283.CameraUp.xyz), dot(_8694, _283.CameraForward.xyz)), _8095) > 0.100000001490116119384765625)
                                            {
                                                int _8274 = _29449 + 1;
                                                _29451 = _8166;
                                                _29450 = _8164;
                                                _29449 = _8274;
                                                _29469 = _8164;
                                                continue;
                                            }
                                            float _8788 = clamp(((0.039999999105930328369140625 * _29453) * _8341) / max(_8247.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                            float _8790 = 0.5 * _8788;
                                            vec2 _8875 = clamp(_8656 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8901 = clamp((_8656 + vec2(_8790)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8927 = clamp((_8656 + vec2(_8788 * (-0.5), _8790)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8953 = clamp((_8656 + vec2(_8790, _8788 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8979 = clamp((_8656 + vec2(_8788 * (-0.5), _8788 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9005 = clamp((_8656 + vec2(_8788, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9031 = clamp((_8656 + vec2(-_8788, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9057 = clamp((_8656 + vec2(0.0, _8788)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9083 = clamp((_8656 + vec2(0.0, -_8788)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec3 _8860 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8875.x, 1.0 - _8875.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8901.x, 1.0 - _8901.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8927.x, 1.0 - _8927.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8953.x, 1.0 - _8953.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8979.x, 1.0 - _8979.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9005.x, 1.0 - _9005.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9031.x, 1.0 - _9031.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9057.x, 1.0 - _9057.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9083.x, 1.0 - _9083.y), 0.0).xyz) * 0.100000001490116119384765625;
                                            _29494 = _8860;
                                            _29474 = _8144;
                                            _29468 = _8164;
                                            _29463 = false;
                                            _29458 = true;
                                            break;
                                        }
                                        int _8274 = _29449 + 1;
                                        _29451 = _8166;
                                        _29450 = _8164;
                                        _29449 = _8274;
                                        _29469 = _8164;
                                        continue;
                                    }
                                    else
                                    {
                                        _29494 = vec3(0.0);
                                        _29474 = _27595;
                                        _29468 = _29469;
                                        _29463 = false;
                                        _29458 = false;
                                        break;
                                    }
                                }
                                if (_29458)
                                {
                                    _29493 = _29494;
                                    _29473 = _29474;
                                    break;
                                }
                                if ((!_29463) || (_29468 < 0.0))
                                {
                                    _29493 = _29494;
                                    _29473 = 0.0;
                                    break;
                                }
                                vec3 _8289 = _8067 + (_8095 * _29468);
                                float _9134 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec2 _9124 = vec2(((_8289.x * _9134) / _8289.z) + (0.5 * float(_283.OutputWidth)), (((-_8289.y) * _9134) / _8289.z) + (0.5 * float(_283.OutputHeight)));
                                ivec2 _9195 = clamp(ivec2(_9124), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _9147 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_9195.x, int((_283.OutputHeight - 1u) - uint(_9195.y)), 0).xy, 0);
                                float _9148 = _9147.x;
                                float _9165 = ((_9148 > 0.0) && (abs(_9148 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_9148 - _283.ProjA)) : 1000000000.0;
                                float _9241 = clamp(((0.039999999105930328369140625 * _29468) * _8341) / max(_8289.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                float _9243 = 0.5 * _9241;
                                vec2 _9328 = clamp(_9124 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9354 = clamp((_9124 + vec2(_9243)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9380 = clamp((_9124 + vec2(_9241 * (-0.5), _9243)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9406 = clamp((_9124 + vec2(_9243, _9241 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9432 = clamp((_9124 + vec2(_9241 * (-0.5), _9241 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9458 = clamp((_9124 + vec2(_9241, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9484 = clamp((_9124 + vec2(-_9241, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9510 = clamp((_9124 + vec2(0.0, _9241)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9536 = clamp((_9124 + vec2(0.0, -_9241)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec3 _9313 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9328.x, 1.0 - _9328.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9354.x, 1.0 - _9354.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9380.x, 1.0 - _9380.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9406.x, 1.0 - _9406.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9432.x, 1.0 - _9432.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9458.x, 1.0 - _9458.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9484.x, 1.0 - _9484.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9510.x, 1.0 - _9510.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9536.x, 1.0 - _9536.y), 0.0).xyz) * 0.100000001490116119384765625;
                                if (_9165 > 100000000.0)
                                {
                                    _29493 = _9313;
                                    _29473 = _8144;
                                    break;
                                }
                                float _8314 = _9165 - _8289.z;
                                if (_8314 <= 0.0)
                                {
                                    _29493 = _9313;
                                    _29473 = _8144;
                                    break;
                                }
                                _29493 = _9313;
                                _29473 = _8144 * exp((_8314 / max(_8094, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                                break;
                            } while(false);
                            vec3 _29499;
                            do
                            {
                                if (_283.EnvParams.x <= 0.0)
                                {
                                    float _9590 = _8018.y;
                                    _29499 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _9590))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _9590)));
                                    break;
                                }
                                _29499 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_8018.x, _8018.y, -_8018.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
                                break;
                            } while(false);
                            _29500 = mix(_8000, mix(_29499, _29493, vec3(_29473)), vec3((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp((-_5725).y, 0.0, 1.0), 5.0))) * _7996));
                            break;
                        } while(false);
                        _29514 = _29500;
                    }
                    else
                    {
                        _29514 = _7581;
                    }
                    _29513 = _29514;
                    _29502 = _5718;
                }
                else
                {
                    _29513 = _7581;
                    _29502 = 0.0;
                }
                _29512 = _29513;
                _29501 = _29502;
            }
            else
            {
                _29512 = _7581;
                _29501 = 0.0;
            }
            if (_283.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_5574), vec4(_29501, _29501, _29501, 1.0));
                break;
            }
            if ((_29501 <= 0.001000000047497451305389404296875) && (_5661 <= 0.001000000047497451305389404296875))
            {
                if (_283.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_5574), vec4(0.0));
                }
                break;
            }
            vec3 _9626 = ((_283.SunColor.xyz * (clamp(_5638.y, 0.0, 1.0) * 0.3183098733425140380859375)) + _6653) * 0.85000002384185791015625;
            vec3 _9632 = mix(vec3(dot(_9626, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _9626, vec3(0.5));
            if (_29501 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_5574), vec4(mix(_29512, _9632, vec3(_5661)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_5574), vec4(_9632, _5661));
            }
            break;
        }
        float _9675 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _9651 = ((_5582.x - (0.5 * float(_283.OutputWidth))) * _27033) / _9675;
        float _9663 = ((-(_5582.y - (0.5 * float(_283.OutputHeight)))) * _27033) / _9675;
        vec3 _9665 = vec3(_9651, _9663, _27033);
        vec3 _5793 = normalize(_9665);
        vec3 _5795 = -_5793;
        int _5811 = clamp(int((_283.NormalStep * max(float(_283.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _5817 = (2.0 * max(_283.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_283.NormalStep, 1.0);
        ivec2 _5819 = ivec2(_5811, 0);
        vec3 _27041;
        do
        {
            float _27037;
            do
            {
                vec2 _9833 = (((vec2(clamp(_5577 + _5819, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _9836 = ivec2(floor(_9833));
                vec2 _9840 = _9833 - vec2(_9836);
                ivec2 _10015 = clamp(_9836, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10051 = clamp(_9836 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10087 = clamp(_9836 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10123 = clamp(_9836 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _9803 = vec4((1.0 - _9840.x) * (1.0 - _9840.y), _9840.x * (1.0 - _9840.y), (1.0 - _9840.x) * _9840.y, _9840.x * _9840.y);
                vec4 _9881 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10015.x, int((FluidSize.y - 1u) - uint(_10015.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10051.x, int((FluidSize.y - 1u) - uint(_10051.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10087.x, int((FluidSize.y - 1u) - uint(_10087.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10123.x, int((FluidSize.y - 1u) - uint(_10123.y)), 0).xy, 0).x);
                vec4 _9804 = _9881;
                float _29433;
                _29433 = 1000000000.0;
                float _29771;
                SPIRV_CROSS_UNROLL
                for (int _27035 = 0; _27035 < 4; _29433 = _29771, _27035++)
                {
                    if (_9804[_27035] > 0.0)
                    {
                        _29771 = min(_29433, _9804[_27035]);
                    }
                    else
                    {
                        _29771 = _29433;
                    }
                }
                vec4 _9807 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27036 = 0; _27036 < 4; )
                {
                    _9807[_27036] = ((_9804[_27036] > 0.0) && (_9804[_27036] < (_29433 + max(0.25, 0.02999999932944774627685546875 * _29433)))) ? _9803[_27036] : 0.0;
                    _27036++;
                    continue;
                }
                float _9941 = ((_9807.x + _9807.y) + _9807.z) + _9807.w;
                if (_9941 <= 9.9999997473787516355514526367188e-05)
                {
                    _27037 = 0.0;
                    break;
                }
                _27037 = dot(_9807, _9881) / _9941;
                break;
            } while(false);
            float _27040;
            do
            {
                vec2 _10363 = (((vec2(clamp(_5577 - _5819, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _10366 = ivec2(floor(_10363));
                vec2 _10370 = _10363 - vec2(_10366);
                ivec2 _10545 = clamp(_10366, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10581 = clamp(_10366 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10617 = clamp(_10366 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10653 = clamp(_10366 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10333 = vec4((1.0 - _10370.x) * (1.0 - _10370.y), _10370.x * (1.0 - _10370.y), (1.0 - _10370.x) * _10370.y, _10370.x * _10370.y);
                vec4 _10411 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10545.x, int((FluidSize.y - 1u) - uint(_10545.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10581.x, int((FluidSize.y - 1u) - uint(_10581.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10617.x, int((FluidSize.y - 1u) - uint(_10617.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10653.x, int((FluidSize.y - 1u) - uint(_10653.y)), 0).xy, 0).x);
                vec4 _10334 = _10411;
                float _29430;
                _29430 = 1000000000.0;
                float _29770;
                SPIRV_CROSS_UNROLL
                for (int _27038 = 0; _27038 < 4; _29430 = _29770, _27038++)
                {
                    if (_10334[_27038] > 0.0)
                    {
                        _29770 = min(_29430, _10334[_27038]);
                    }
                    else
                    {
                        _29770 = _29430;
                    }
                }
                vec4 _10337 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27039 = 0; _27039 < 4; )
                {
                    _10337[_27039] = ((_10334[_27039] > 0.0) && (_10334[_27039] < (_29430 + max(0.25, 0.02999999932944774627685546875 * _29430)))) ? _10333[_27039] : 0.0;
                    _27039++;
                    continue;
                }
                float _10471 = ((_10337.x + _10337.y) + _10337.z) + _10337.w;
                if (_10471 <= 9.9999997473787516355514526367188e-05)
                {
                    _27040 = 0.0;
                    break;
                }
                _27040 = dot(_10337, _10411) / _10471;
                break;
            } while(false);
            bool _9709 = (_27037 > 0.0) && (abs(_27037 - _27033) < _5817);
            bool _9718 = (_27040 > 0.0) && (abs(_27033 - _27040) < _5817);
            vec2 _9725 = vec2(_5577 + _5819) + vec2(0.5);
            float _10859 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9729 = vec3(((_9725.x - (0.5 * float(_283.OutputWidth))) * _27037) / _10859, ((-(_9725.y - (0.5 * float(_283.OutputHeight)))) * _27037) / _10859, _27037) - _9665;
            bvec3 _9730 = bvec3(_9709);
            vec3 _9731 = vec3(_9730.x ? _9729.x : vec3(0.0).x, _9730.y ? _9729.y : vec3(0.0).y, _9730.z ? _9729.z : vec3(0.0).z);
            vec2 _9739 = vec2(_5577 - _5819) + vec2(0.5);
            float _10898 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9742 = _9665 - vec3(((_9739.x - (0.5 * float(_283.OutputWidth))) * _27040) / _10898, ((-(_9739.y - (0.5 * float(_283.OutputHeight)))) * _27040) / _10898, _27040);
            bvec3 _9743 = bvec3(_9718);
            vec3 _9744 = vec3(_9743.x ? _9742.x : vec3(0.0).x, _9743.y ? _9742.y : vec3(0.0).y, _9743.z ? _9742.z : vec3(0.0).z);
            if (_9709 && _9718)
            {
                _27041 = (_9731 + _9744) * 0.5;
                break;
            }
            bvec3 _9757 = bvec3(_9709);
            _27041 = vec3(_9757.x ? _9731.x : _9744.x, _9757.y ? _9731.y : _9744.y, _9757.z ? _9731.z : _9744.z);
            break;
        } while(false);
        ivec2 _5826 = ivec2(0, _5811);
        vec3 _27048;
        do
        {
            float _27044;
            do
            {
                vec2 _11056 = (((vec2(clamp(_5577 + _5826, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _11059 = ivec2(floor(_11056));
                vec2 _11063 = _11056 - vec2(_11059);
                ivec2 _11238 = clamp(_11059, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11274 = clamp(_11059 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11310 = clamp(_11059 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11346 = clamp(_11059 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11026 = vec4((1.0 - _11063.x) * (1.0 - _11063.y), _11063.x * (1.0 - _11063.y), (1.0 - _11063.x) * _11063.y, _11063.x * _11063.y);
                vec4 _11104 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11238.x, int((FluidSize.y - 1u) - uint(_11238.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11274.x, int((FluidSize.y - 1u) - uint(_11274.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11310.x, int((FluidSize.y - 1u) - uint(_11310.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11346.x, int((FluidSize.y - 1u) - uint(_11346.y)), 0).xy, 0).x);
                vec4 _11027 = _11104;
                float _29427;
                _29427 = 1000000000.0;
                float _29769;
                SPIRV_CROSS_UNROLL
                for (int _27042 = 0; _27042 < 4; _29427 = _29769, _27042++)
                {
                    if (_11027[_27042] > 0.0)
                    {
                        _29769 = min(_29427, _11027[_27042]);
                    }
                    else
                    {
                        _29769 = _29427;
                    }
                }
                vec4 _11030 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27043 = 0; _27043 < 4; )
                {
                    _11030[_27043] = ((_11027[_27043] > 0.0) && (_11027[_27043] < (_29427 + max(0.25, 0.02999999932944774627685546875 * _29427)))) ? _11026[_27043] : 0.0;
                    _27043++;
                    continue;
                }
                float _11164 = ((_11030.x + _11030.y) + _11030.z) + _11030.w;
                if (_11164 <= 9.9999997473787516355514526367188e-05)
                {
                    _27044 = 0.0;
                    break;
                }
                _27044 = dot(_11030, _11104) / _11164;
                break;
            } while(false);
            float _27047;
            do
            {
                vec2 _11586 = (((vec2(clamp(_5577 - _5826, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                ivec2 _11589 = ivec2(floor(_11586));
                vec2 _11593 = _11586 - vec2(_11589);
                ivec2 _11768 = clamp(_11589, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11804 = clamp(_11589 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11840 = clamp(_11589 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11876 = clamp(_11589 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11556 = vec4((1.0 - _11593.x) * (1.0 - _11593.y), _11593.x * (1.0 - _11593.y), (1.0 - _11593.x) * _11593.y, _11593.x * _11593.y);
                vec4 _11634 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11768.x, int((FluidSize.y - 1u) - uint(_11768.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11804.x, int((FluidSize.y - 1u) - uint(_11804.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11840.x, int((FluidSize.y - 1u) - uint(_11840.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11876.x, int((FluidSize.y - 1u) - uint(_11876.y)), 0).xy, 0).x);
                vec4 _11557 = _11634;
                float _29424;
                _29424 = 1000000000.0;
                float _29768;
                SPIRV_CROSS_UNROLL
                for (int _27045 = 0; _27045 < 4; _29424 = _29768, _27045++)
                {
                    if (_11557[_27045] > 0.0)
                    {
                        _29768 = min(_29424, _11557[_27045]);
                    }
                    else
                    {
                        _29768 = _29424;
                    }
                }
                vec4 _11560 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27046 = 0; _27046 < 4; )
                {
                    _11560[_27046] = ((_11557[_27046] > 0.0) && (_11557[_27046] < (_29424 + max(0.25, 0.02999999932944774627685546875 * _29424)))) ? _11556[_27046] : 0.0;
                    _27046++;
                    continue;
                }
                float _11694 = ((_11560.x + _11560.y) + _11560.z) + _11560.w;
                if (_11694 <= 9.9999997473787516355514526367188e-05)
                {
                    _27047 = 0.0;
                    break;
                }
                _27047 = dot(_11560, _11634) / _11694;
                break;
            } while(false);
            bool _10932 = (_27044 > 0.0) && (abs(_27044 - _27033) < _5817);
            bool _10941 = (_27047 > 0.0) && (abs(_27033 - _27047) < _5817);
            vec2 _10948 = vec2(_5577 + _5826) + vec2(0.5);
            float _12082 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10952 = vec3(((_10948.x - (0.5 * float(_283.OutputWidth))) * _27044) / _12082, ((-(_10948.y - (0.5 * float(_283.OutputHeight)))) * _27044) / _12082, _27044) - _9665;
            bvec3 _10953 = bvec3(_10932);
            vec3 _10954 = vec3(_10953.x ? _10952.x : vec3(0.0).x, _10953.y ? _10952.y : vec3(0.0).y, _10953.z ? _10952.z : vec3(0.0).z);
            vec2 _10962 = vec2(_5577 - _5826) + vec2(0.5);
            float _12121 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10965 = _9665 - vec3(((_10962.x - (0.5 * float(_283.OutputWidth))) * _27047) / _12121, ((-(_10962.y - (0.5 * float(_283.OutputHeight)))) * _27047) / _12121, _27047);
            bvec3 _10966 = bvec3(_10941);
            vec3 _10967 = vec3(_10966.x ? _10965.x : vec3(0.0).x, _10966.y ? _10965.y : vec3(0.0).y, _10966.z ? _10965.z : vec3(0.0).z);
            if (_10932 && _10941)
            {
                _27048 = (_10954 + _10967) * 0.5;
                break;
            }
            bvec3 _10980 = bvec3(_10932);
            _27048 = vec3(_10980.x ? _10954.x : _10967.x, _10980.y ? _10954.y : _10967.y, _10980.z ? _10954.z : _10967.z);
            break;
        } while(false);
        vec2 _5837 = _27041.xy * max(_283.NormalScale, 0.0);
        vec3 _26148 = _27041;
        _26148.x = _5837.x;
        _26148.y = _5837.y;
        vec2 _5847 = _27048.xy * max(_283.NormalScale, 0.0);
        vec3 _26152 = _27048;
        _26152.x = _5847.x;
        _26152.y = _5847.y;
        bvec3 _29791 = bvec3(length(_26148) < 9.9999999747524270787835121154785e-07);
        bvec3 _29793 = bvec3(length(_26152) < 9.9999999747524270787835121154785e-07);
        vec3 _5865 = normalize(cross(vec3(_29791.x ? vec3(1.0, 0.0, 0.0).x : _26148.x, _29791.y ? vec3(1.0, 0.0, 0.0).y : _26148.y, _29791.z ? vec3(1.0, 0.0, 0.0).z : _26148.z), vec3(_29793.x ? vec3(0.0, -1.0, 0.0).x : _26152.x, _29793.y ? vec3(0.0, -1.0, 0.0).y : _26152.y, _29793.z ? vec3(0.0, -1.0, 0.0).z : _26152.z)));
        vec3 _27077;
        if (dot(_5865, _5795) < 0.0)
        {
            _27077 = -_5865;
        }
        else
        {
            _27077 = _5865;
        }
        vec3 _27085;
        if (_283.Mode == 2u)
        {
            vec4 _27071;
            do
            {
                float _27062;
                do
                {
                    vec2 _12319 = ((_5582 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight))) - vec2(0.5);
                    ivec2 _12322 = ivec2(floor(_12319));
                    vec2 _12326 = _12319 - vec2(_12322);
                    ivec2 _12501 = clamp(_12322, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12537 = clamp(_12322 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12573 = clamp(_12322 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12609 = clamp(_12322 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12289 = vec4((1.0 - _12326.x) * (1.0 - _12326.y), _12326.x * (1.0 - _12326.y), (1.0 - _12326.x) * _12326.y, _12326.x * _12326.y);
                    vec4 _12367 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12501.x, int((FluidSize.y - 1u) - uint(_12501.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12537.x, int((FluidSize.y - 1u) - uint(_12537.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12573.x, int((FluidSize.y - 1u) - uint(_12573.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12609.x, int((FluidSize.y - 1u) - uint(_12609.y)), 0).xy, 0).x);
                    vec4 _12290 = _12367;
                    float _29421;
                    _29421 = 1000000000.0;
                    float _29767;
                    SPIRV_CROSS_UNROLL
                    for (int _27060 = 0; _27060 < 4; _29421 = _29767, _27060++)
                    {
                        if (_12290[_27060] > 0.0)
                        {
                            _29767 = min(_29421, _12290[_27060]);
                        }
                        else
                        {
                            _29767 = _29421;
                        }
                    }
                    vec4 _12293 = vec4(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _27061 = 0; _27061 < 4; )
                    {
                        _12293[_27061] = ((_12290[_27061] > 0.0) && (_12290[_27061] < (_29421 + max(0.25, 0.02999999932944774627685546875 * _29421)))) ? _12289[_27061] : 0.0;
                        _27061++;
                        continue;
                    }
                    float _12427 = ((_12293.x + _12293.y) + _12293.z) + _12293.w;
                    if (_12427 <= 9.9999997473787516355514526367188e-05)
                    {
                        _27062 = 0.0;
                        break;
                    }
                    _27062 = dot(_12293, _12367) / _12427;
                    break;
                } while(false);
                if (_27062 <= 0.0)
                {
                    _27071 = vec4(0.0);
                    break;
                }
                float _12166 = clamp(((max(_283.ShadeParams.y, 0.001000000047497451305389404296875) * ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _27062) * _283.EnvParams.y, 1.0, 48.0);
                vec2 _12176 = vec2(FluidSize) / vec2(float(_283.OutputWidth), float(_283.OutputHeight));
                float _12179 = max(0.25, 0.02999999932944774627685546875 * _27062);
                float _27064;
                vec4 _27065;
                _27065 = vec4(0.0);
                _27064 = 0.0;
                float _29520;
                vec4 _29521;
                SPIRV_CROSS_UNROLL
                for (int _27063 = 0; _27063 < 13; _27065 = _29521, _27064 = _29520, _27063++)
                {
                    vec2 _27066;
                    if (_27063 > 0)
                    {
                        float _12203 = (float((_27063 <= 6) ? _27063 : (_27063 - 6)) * 1.0471975803375244140625) + ((_27063 <= 6) ? 0.0 : 0.52359998226165771484375);
                        _27066 = vec2(cos(_12203), sin(_12203)) * (((_27063 <= 6) ? 0.5 : 1.0) * _12166);
                    }
                    else
                    {
                        _27066 = vec2(0.0);
                    }
                    ivec2 _12220 = ivec2(floor((_5582 + _27066) * _12176));
                    ivec2 _12795 = clamp(_12220, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12821 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12795.x, int((FluidSize.y - 1u) - uint(_12795.y)), 0).xy, 0);
                    float _12822 = _12821.x;
                    if ((_12822 <= 0.0) || (abs(_12822 - _27062) > _12179))
                    {
                        _29521 = _27065;
                        _29520 = _27064;
                        continue;
                    }
                    ivec2 _12831 = clamp(_12220, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12857 = texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12831.x, int((FluidSize.y - 1u) - uint(_12831.y)), 0).xy, 0);
                    float _12249 = exp((-2.0) * (dot(_27066, _27066) / max(_12166 * _12166, 9.9999997473787516355514526367188e-05))) * _12857.w;
                    _29521 = _27065 + (_12857 * _12249);
                    _29520 = _27064 + _12249;
                }
                if (_27064 <= 9.9999997473787516355514526367188e-05)
                {
                    _27071 = vec4(0.0);
                    break;
                }
                vec4 _12269 = _27065 / vec4(_27064);
                _12269.w = min(_12269.w, 1.0);
                _27071 = _12269;
                break;
            } while(false);
            float _5882 = length(_27071.xyz);
            vec3 _27086;
            if (_5882 > 0.001000000047497451305389404296875)
            {
                vec3 _5890 = _27071.xyz / vec3(_5882);
                vec3 _27081;
                if (dot(_5890, _5795) < 0.0)
                {
                    _27081 = -_5890;
                }
                else
                {
                    _27081 = _5890;
                }
                _27086 = normalize(mix(_27077, _27081, vec3(clamp(_27071.w * 4.0, 0.0, 1.0))));
            }
            else
            {
                _27086 = _27077;
            }
            _27085 = _27086;
        }
        else
        {
            _27085 = _27077;
        }
        float _5912 = max(_27034.x, 9.9999997473787516355514526367188e-05);
        ivec2 _12952 = clamp(ivec2(floor((_5582 * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        ivec2 _12962 = clamp(_12952, ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _12973 = (uint(_12962.x) + (uint(_12962.y) * FluidSize.x)) * 4u;
        uint _12983 = AuxBits_1._data[_12973 + 1u];
        uint _12995 = AuxBits_1._data[_12973 + 3u];
        float _27083;
        _27083 = float(AuxBits_1._data[_12973]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _27082 = 0; _27082 < 4; )
        {
            ivec2 _12887 = ivec2((_27082 * 2) - 1, 0);
            ivec2 _12892 = ivec2(0, ((_27082 - 2) * 2) - 1);
            bvec2 _12893 = bvec2(_27082 < 2);
            ivec2 _13008 = clamp(_12952 + ivec2(_12893.x ? _12887.x : _12892.x, _12893.y ? _12887.y : _12892.y), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _13054 = clamp(_12952 + ivec2(((_27082 & 1) != int(0u)) ? 1 : (-1), ((_27082 & 2) != int(0u)) ? 1 : (-1)), ivec2(0), ivec2(FluidSize) - ivec2(1));
            _27083 += ((float(AuxBits_1._data[(uint(_13008.x) + (uint(_13008.y) * FluidSize.x)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_13054.x) + (uint(_13054.y) * FluidSize.x)) * 4u]) * 1.525902189314365386962890625e-05));
            _27082++;
            continue;
        }
        float _5931 = clamp((float(_12995) * 0.0009765625) / _5912, 0.0, 1.0);
        vec3 _13112 = ((_283.CameraRight.xyz * _5793.x) + (_283.CameraUp.xyz * _5793.y)) + (_283.CameraForward.xyz * _5793.z);
        vec3 _5937 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * _9651) + (_283.CameraUp.xyz * _9663)) + (_283.CameraForward.xyz * _27033));
        vec3 _13165 = ((_283.CameraRight.xyz * _27085.x) + (_283.CameraUp.xyz * _27085.y)) + (_283.CameraForward.xyz * _27085.z);
        float _5948 = (_27033 / ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_27085, _5795)), 0.07999999821186065673828125);
        float _5972 = (_283.RippleParams.x * mix(_283.RippleFlow.y, 1.0 + _283.RippleFlow.x, clamp((float(AuxBits_1._data[_12973 + 2u]) * 0.0009765625) / _5912, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _13165.y) * clamp(1.0 - (length(_9665) / max(_283.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _27091;
        vec3 _27135;
        if (_5972 > 0.0)
        {
            vec2 _5978 = _5937.xz;
            float _13195 = max(_283.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _27088;
            float _27089;
            _27089 = 0.0;
            _27088 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _27087 = 0; _27087 < 6; )
            {
                float _13207 = _13195 * _1894[_27087];
                float _13212 = _5972 * pow(0.7799999713897705078125, float(_27087));
                float _13219 = clamp((_13207 / max(_5948 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _13295 = (((uint(_27087) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _13303 = ((_13295 >> ((_13295 >> 28u) + 4u)) ^ _13295) * 277803737u;
                float _13226 = 0.60000002384185791015625 + (float(((_13303 >> 22u) ^ _13303) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _13231 = vec2(cos(_13226), sin(_13226));
                uint _13322 = (((uint(_27087) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _13330 = ((_13322 >> ((_13322 >> 28u) + 4u)) ^ _13322) * 277803737u;
                _27089 += (((0.5 * _13212) * _13212) * (1.0 - (_13219 * _13219)));
                _27088 += (_13231 * ((_13212 * _13219) * cos(((dot(_13231, _5978) * (6.283185482025146484375 / _13207)) + ((sqrt(61.63805389404296875 / _13207) * _283.RippleParams.z) * _283.ShadeParams.z)) + (float(((_13330 >> 22u) ^ _13330) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _27087++;
                continue;
            }
            vec3 _5993 = normalize(_13165 + vec3(-_27088.x, 0.0, -_27088.y));
            vec3 _5995 = normalize(vec3(dot(_5993, _283.CameraRight.xyz), dot(_5993, _283.CameraUp.xyz), dot(_5993, _283.CameraForward.xyz)));
            bvec3 _29797 = bvec3(dot(_5995, _5795) < 0.0199999995529651641845703125);
            _27135 = vec3(_29797.x ? _27085.x : _5995.x, _29797.y ? _27085.y : _5995.y, _29797.z ? _27085.z : _5995.z);
            _27091 = _27089;
        }
        else
        {
            _27135 = _27085;
            _27091 = 0.0;
        }
        float _6007 = _27091 * _283.RippleFlow.z;
        if (_283.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_5574), vec4((_27135 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_283.DebugView == 2u)
        {
            float _6030 = clamp(_27033 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5574), vec4(_6030, _6030, _6030, 1.0));
            break;
        }
        float _6050 = (_7494 < 100000000.0) ? (max(_7494 - _27033, 0.0) / max(_5793.z, 0.0500000007450580596923828125)) : 10000.0;
        float _6055 = _5912 * max(_283.ThicknessScale, 0.0);
        float _6060 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _6055 / max(_6050, 0.001000000047497451305389404296875));
        float _27133;
        do
        {
            vec3 _13386 = (_5937 + (_29788 * (max(_5937.y - _283.PoolMin.y, 0.0500000007450580596923828125) / max(-_29788.y, 0.20000000298023223876953125)))) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
            float _13452 = dot(_13386, _283.CameraForward.xyz);
            if (_13452 <= 0.100000001490116119384765625)
            {
                _27133 = 1.0;
                break;
            }
            float _13493 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _13469 = ((dot(_13386, _283.CameraRight.xyz) * _13493) / _13452) + (0.5 * float(_283.OutputWidth));
            float _13482 = (((-dot(_13386, _283.CameraUp.xyz)) * _13493) / _13452) + (0.5 * float(_283.OutputHeight));
            vec2 _13483 = vec2(_13469, _13482);
            if (!((((_13469 >= 0.0) && (_13482 >= 0.0)) && (_13469 < float(_283.OutputWidth))) && (_13482 < float(_283.OutputHeight))))
            {
                _27133 = 1.0;
                break;
            }
            ivec2 _13577 = clamp(ivec2(_13483), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _13529 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13577.x, int((_283.OutputHeight - 1u) - uint(_13577.y)), 0).xy, 0);
            float _13530 = _13529.x;
            if (abs((((_13530 > 0.0) && (abs(_13530 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_13530 - _283.ProjA)) : 1000000000.0) - _13452) > ((0.1500000059604644775390625 * _13452) + 0.100000001490116119384765625))
            {
                _27133 = 1.0;
                break;
            }
            ivec2 _13637 = clamp(ivec2(_13483), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            _27133 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_13637.x, int((_283.OutputHeight - 1u) - uint(_13637.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _6066 = refract(_5793, _27135, 0.75187969207763671875);
        bvec3 _29801 = bvec3(dot(_6066, _6066) < 9.9999999747524270787835121154785e-07);
        vec3 _6085 = normalize(mix(_5793, vec3(_29801.x ? _5793.x : _6066.x, _29801.y ? _5793.y : _6066.y, _29801.z ? _5793.z : _6066.z), vec3(clamp(_283.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _6060))));
        vec3 _13689 = ((_283.CameraRight.xyz * _6085.x) + (_283.CameraUp.xyz * _6085.y)) + (_283.CameraForward.xyz * _6085.z);
        float _27150;
        do
        {
            if ((((_5937.x < _283.PoolMin.x) || (_5937.x > _283.PoolMax.x)) || (_5937.z < _283.PoolMin.z)) || (_5937.z > _283.PoolMax.z))
            {
                _27150 = 1000.0;
                break;
            }
            float _27138;
            if (_13689.x > 9.9999997473787516355514526367188e-05)
            {
                _27138 = (_283.PoolMax.x - _5937.x) / _13689.x;
            }
            else
            {
                float _27139;
                if (_13689.x < (-9.9999997473787516355514526367188e-05))
                {
                    _27139 = (_283.PoolMin.x - _5937.x) / _13689.x;
                }
                else
                {
                    _27139 = 1000.0;
                }
                _27138 = _27139;
            }
            float _29803 = (_27138 < 1000.0) ? _27138 : 1000.0;
            float _27142;
            if (_13689.z > 9.9999997473787516355514526367188e-05)
            {
                _27142 = (_283.PoolMax.z - _5937.z) / _13689.z;
            }
            else
            {
                float _27143;
                if (_13689.z < (-9.9999997473787516355514526367188e-05))
                {
                    _27143 = (_283.PoolMin.z - _5937.z) / _13689.z;
                }
                else
                {
                    _27143 = 1000.0;
                }
                _27142 = _27143;
            }
            float _29804 = (_27142 < _29803) ? _27142 : _29803;
            float _27148;
            if (_13689.y < (-9.9999997473787516355514526367188e-05))
            {
                float _13819 = (_283.PoolMin.y - _5937.y) / _13689.y;
                _27148 = (_13819 < _29804) ? _13819 : _29804;
            }
            else
            {
                _27148 = _29804;
            }
            _27150 = max(_27148, 0.0);
            break;
        } while(false);
        float _6104 = clamp(max((_6050 * 1.2000000476837158203125) + 0.20000000298023223876953125, (_27150 < 100.0) ? ((_27150 * 1.10000002384185791015625) + 0.100000001490116119384765625) : 0.0), 0.300000011920928955078125, max(_283.MaxTravel, 0.300000011920928955078125) * 2.0);
        vec3 _27504;
        float _27560;
        float _27616;
        float _27671;
        vec2 _27726;
        do
        {
            ivec2 _14161 = clamp(ivec2(_5582), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _14113 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14161.x, int((_283.OutputHeight - 1u) - uint(_14161.y)), 0).xy, 0);
            float _14114 = _14113.x;
            vec2 _27419;
            float _27449;
            _27449 = ((_14114 > 0.0) && (abs(_14114 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14114 - _283.ProjA)) : 1000000000.0;
            _27419 = _5582;
            float _13909;
            float _29808;
            vec2 _29810;
            bool _27158;
            float _27478;
            vec3 _27505;
            float _27561;
            float _27617;
            float _27672;
            vec2 _27727;
            int _27151 = 1;
            float _27153 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_27151 <= 16)
                {
                    float _13903 = float(_27151) * 0.0625;
                    _13909 = (_6104 * _13903) * sqrt(_13903);
                    vec3 _13914 = _9665 + (_6085 * _13909);
                    if (_13914.z <= 0.0500000007450580596923828125)
                    {
                        _27727 = _27761;
                        _27672 = _27595;
                        _27617 = _27595;
                        _27561 = _27595;
                        _27505 = _27539;
                        _27478 = 0.0;
                        _27158 = false;
                        break;
                    }
                    float _14231 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _14207 = ((_13914.x * _14231) / _13914.z) + (0.5 * float(_283.OutputWidth));
                    float _14220 = (((-_13914.y) * _14231) / _13914.z) + (0.5 * float(_283.OutputHeight));
                    vec2 _14221 = vec2(_14207, _14220);
                    if (!((((_14207 >= 0.0) && (_14220 >= 0.0)) && (_14207 < float(_283.OutputWidth))) && (_14220 < float(_283.OutputHeight))))
                    {
                        _27727 = _27761;
                        _27672 = _27595;
                        _27617 = _27595;
                        _27561 = _27595;
                        _27505 = _27539;
                        _27478 = 0.0;
                        _27158 = false;
                        break;
                    }
                    ivec2 _14315 = clamp(ivec2(_14221), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _14267 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14315.x, int((_283.OutputHeight - 1u) - uint(_14315.y)), 0).xy, 0);
                    float _14268 = _14267.x;
                    float _14285 = ((_14268 > 0.0) && (abs(_14268 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14268 - _283.ProjA)) : 1000000000.0;
                    if (_13914.z >= _14285)
                    {
                        ivec2 _13936 = ivec2(_14221);
                        ivec2 _14396 = clamp(ivec2(floor(((vec2(_13936) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14405 = clamp(_14396, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _14431 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14405.x, int((FluidSize.y - 1u) - uint(_14405.y)), 0).xy, 0);
                        float _14432 = _14431.x;
                        ivec2 _14441 = clamp(_14396, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14528 = clamp(_13936, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _14480 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14528.x, int((_283.OutputHeight - 1u) - uint(_14528.y)), 0).xy, 0);
                        float _14481 = _14480.x;
                        if (!(((_14432 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14441.x, int((FluidSize.y - 1u) - uint(_14441.y)), 0).xy, 0).w > 0.5)) && ((((_14481 > 0.0) && (abs(_14481 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14481 - _283.ProjA)) : 1000000000.0) >= (_14432 + 0.0500000007450580596923828125))))
                        {
                            _27727 = _27761;
                            _27672 = _27595;
                            _27617 = _27595;
                            _27561 = _27595;
                            _27505 = _27539;
                            _27478 = 0.0;
                            _27158 = false;
                            break;
                        }
                        float _27155;
                        _27155 = _13909;
                        float _29806;
                        float _29807;
                        int _27154 = 0;
                        float _27156 = _27153;
                        SPIRV_CROSS_UNROLL
                        for (; _27154 < 5; _27156 = _29806, _27155 = _29807, _27154++)
                        {
                            float _13951 = 0.5 * (_27156 + _27155);
                            vec3 _13956 = _9665 + (_6085 * _13951);
                            float _14598 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _14659 = clamp(ivec2(vec2(((_13956.x * _14598) / _13956.z) + (0.5 * float(_283.OutputWidth)), (((-_13956.y) * _14598) / _13956.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                            vec4 _14611 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14659.x, int((_283.OutputHeight - 1u) - uint(_14659.y)), 0).xy, 0);
                            float _14612 = _14611.x;
                            bool _13963 = _13956.z >= (((_14612 > 0.0) && (abs(_14612 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14612 - _283.ProjA)) : 1000000000.0);
                            _29806 = _13963 ? _27156 : _13951;
                            _29807 = _13963 ? _13951 : _27155;
                        }
                        vec3 _13977 = _9665 + (_6085 * _27155);
                        float _14729 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _13988 = clamp(vec2(((_13977.x * _14729) / _13977.z) + (0.5 * float(_283.OutputWidth)), (((-_13977.y) * _14729) / _13977.z) + (0.5 * float(_283.OutputHeight))), vec2(0.5), vec2(float(_283.OutputWidth), float(_283.OutputHeight)) - vec2(0.5));
                        ivec2 _14790 = clamp(ivec2(_13988), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _14742 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14790.x, int((_283.OutputHeight - 1u) - uint(_14790.y)), 0).xy, 0);
                        float _14743 = _14742.x;
                        float _14760 = ((_14743 > 0.0) && (abs(_14743 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_14743 - _283.ProjA)) : 1000000000.0;
                        float _14868 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        _27727 = _13988;
                        _27672 = _14760;
                        _27617 = _27155;
                        _27561 = 1.0;
                        _27505 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_13988.x - (0.5 * float(_283.OutputWidth))) * _14760) / _14868)) + (_283.CameraUp.xyz * (((-(_13988.y - (0.5 * float(_283.OutputHeight)))) * _14760) / _14868))) + (_283.CameraForward.xyz * _14760));
                        _27478 = 1.0;
                        _27158 = true;
                        break;
                    }
                    ivec2 _14012 = ivec2(_14221 - vec2(0.5));
                    ivec2 _14941 = clamp(ivec2(floor(((vec2(_14012) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14950 = clamp(_14941, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _14976 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14950.x, int((FluidSize.y - 1u) - uint(_14950.y)), 0).xy, 0);
                    float _14977 = _14976.x;
                    ivec2 _14986 = clamp(_14941, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15073 = clamp(_14012, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _15025 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15073.x, int((_283.OutputHeight - 1u) - uint(_15073.y)), 0).xy, 0);
                    float _15026 = _15025.x;
                    ivec2 _14017 = ivec2(_14221 + vec2(0.5));
                    ivec2 _15154 = clamp(ivec2(floor(((vec2(_14017) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15163 = clamp(_15154, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _15189 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15163.x, int((FluidSize.y - 1u) - uint(_15163.y)), 0).xy, 0);
                    float _15190 = _15189.x;
                    ivec2 _15199 = clamp(_15154, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15286 = clamp(_14017, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _15238 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15286.x, int((_283.OutputHeight - 1u) - uint(_15286.y)), 0).xy, 0);
                    float _15239 = _15238.x;
                    bool _14019 = (((_14977 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14986.x, int((FluidSize.y - 1u) - uint(_14986.y)), 0).xy, 0).w > 0.5)) && ((((_15026 > 0.0) && (abs(_15026 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15026 - _283.ProjA)) : 1000000000.0) >= (_14977 + 0.0500000007450580596923828125))) && (((_15190 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15199.x, int((FluidSize.y - 1u) - uint(_15199.y)), 0).xy, 0).w > 0.5)) && ((((_15239 > 0.0) && (abs(_15239 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15239 - _283.ProjA)) : 1000000000.0) >= (_15190 + 0.0500000007450580596923828125)));
                    _29808 = _14019 ? _14285 : _27449;
                    bvec2 _29809 = bvec2(_14019);
                    _29810 = vec2(_29809.x ? _14221.x : _27419.x, _29809.y ? _14221.y : _27419.y);
                    _27151++;
                    _27449 = _29808;
                    _27419 = _29810;
                    _27153 = _13909;
                    continue;
                }
                else
                {
                    _27727 = _27761;
                    _27672 = _27595;
                    _27617 = _27595;
                    _27561 = _27595;
                    _27505 = _27539;
                    _27478 = 0.0;
                    _27158 = false;
                    break;
                }
            }
            if (_27158)
            {
                _27726 = _27727;
                _27671 = _27672;
                _27616 = _27617;
                _27560 = _27561;
                _27504 = _27505;
                break;
            }
            vec3 _15338 = ((_283.CameraRight.xyz * _6085.x) + (_283.CameraUp.xyz * _6085.y)) + (_283.CameraForward.xyz * _6085.z);
            float _27173;
            vec3 _27174;
            do
            {
                if ((((_5937.x < _283.PoolMin.x) || (_5937.x > _283.PoolMax.x)) || (_5937.z < _283.PoolMin.z)) || (_5937.z > _283.PoolMax.z))
                {
                    _27174 = vec3(0.0, 1.0, 0.0);
                    _27173 = 1000.0;
                    break;
                }
                float _27161;
                if (_15338.x > 9.9999997473787516355514526367188e-05)
                {
                    _27161 = (_283.PoolMax.x - _5937.x) / _15338.x;
                }
                else
                {
                    float _27162;
                    if (_15338.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _27162 = (_283.PoolMin.x - _5937.x) / _15338.x;
                    }
                    else
                    {
                        _27162 = 1000.0;
                    }
                    _27161 = _27162;
                }
                bool _15400 = _27161 < 1000.0;
                vec3 _27178;
                if (_15400)
                {
                    _27178 = vec3((_15338.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                }
                else
                {
                    _27178 = vec3(0.0, 1.0, 0.0);
                }
                float _29811 = _15400 ? _27161 : 1000.0;
                float _27165;
                if (_15338.z > 9.9999997473787516355514526367188e-05)
                {
                    _27165 = (_283.PoolMax.z - _5937.z) / _15338.z;
                }
                else
                {
                    float _27166;
                    if (_15338.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _27166 = (_283.PoolMin.z - _5937.z) / _15338.z;
                    }
                    else
                    {
                        _27166 = 1000.0;
                    }
                    _27165 = _27166;
                }
                bool _15438 = _27165 < _29811;
                vec3 _27176;
                if (_15438)
                {
                    _27176 = vec3(0.0, 0.0, _29849);
                }
                else
                {
                    _27176 = _27178;
                }
                float _29812 = _15438 ? _27165 : _29811;
                float _27171;
                vec3 _27175;
                if (_15338.y < (-9.9999997473787516355514526367188e-05))
                {
                    float _15458 = (_283.PoolMin.y - _5937.y) / _15338.y;
                    bool _15461 = _15458 < _29812;
                    bvec3 _29813 = bvec3(_15461);
                    _27175 = vec3(_29813.x ? vec3(0.0, 1.0, 0.0).x : _27176.x, _29813.y ? vec3(0.0, 1.0, 0.0).y : _27176.y, _29813.z ? vec3(0.0, 1.0, 0.0).z : _27176.z);
                    _27171 = _15461 ? _15458 : _29812;
                }
                else
                {
                    _27175 = _27176;
                    _27171 = _29812;
                }
                _27174 = _27175;
                _27173 = max(_27171, 0.0);
                break;
            } while(false);
            if (_27173 < 100.0)
            {
                vec3 _14044 = _5937 + (_15338 * _27173);
                uint _27321;
                vec2 _27402;
                float _27409;
                do
                {
                    bool _27183;
                    vec2 _27184;
                    float _27185;
                    do
                    {
                        vec3 _15712 = _14044 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                        float _15770 = dot(_15712, _283.CameraForward.xyz);
                        if (_15770 <= 0.0500000007450580596923828125)
                        {
                            _27185 = 1000000000.0;
                            _27184 = vec2(0.5);
                            _27183 = false;
                            break;
                        }
                        float _15811 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _15787 = ((dot(_15712, _283.CameraRight.xyz) * _15811) / _15770) + (0.5 * float(_283.OutputWidth));
                        float _15800 = (((-dot(_15712, _283.CameraUp.xyz)) * _15811) / _15770) + (0.5 * float(_283.OutputHeight));
                        if (!((((_15787 >= 0.0) && (_15800 >= 0.0)) && (_15787 < float(_283.OutputWidth))) && (_15800 < float(_283.OutputHeight))))
                        {
                            _27185 = 1000000000.0;
                            _27184 = vec2(0.5);
                            _27183 = false;
                            break;
                        }
                        vec2 _15729 = floor(vec2(_15787, _15800)) + vec2(0.5);
                        ivec2 _15895 = clamp(ivec2(_15729), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _15847 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15895.x, int((_283.OutputHeight - 1u) - uint(_15895.y)), 0).xy, 0);
                        float _15848 = _15847.x;
                        float _15865 = ((_15848 > 0.0) && (abs(_15848 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_15848 - _283.ProjA)) : 1000000000.0;
                        _27185 = _15865;
                        _27184 = _15729;
                        _27183 = abs(_15865 - _15770) < ((0.0199999995529651641845703125 * _15770) + 0.02999999932944774627685546875);
                        break;
                    } while(false);
                    if (_27183)
                    {
                        _27409 = _27185;
                        _27402 = _27184;
                        _27321 = 1u;
                        break;
                    }
                    vec3 _15518 = _14044 - vec3(_283.CameraRight.w, _27021, _283.CameraForward.w);
                    vec3 _15526 = (_283.PoolMin.xyz + _283.PoolMax.xyz) * 0.5;
                    vec3 _15531 = vec3((_15518.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15536 = vec3(0.0, 0.0, (_15518.z >= 0.0) ? 1.0 : (-1.0));
                    vec3 _15543 = vec3((_15526.x >= _14044.x) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15550 = vec3(0.0, 0.0, (_15526.z >= _14044.z) ? 1.0 : (-1.0));
                    vec3 _27186;
                    vec3 _27187;
                    vec3 _27192;
                    vec3 _27194;
                    if (abs(_27174.y) > 0.5)
                    {
                        bool _15562 = abs(_15518.z) >= abs(_15518.x);
                        bvec3 _15566 = bvec3(_15562);
                        bvec3 _15571 = bvec3(_15562);
                        bvec3 _15576 = bvec3(_15562);
                        bvec3 _15581 = bvec3(_15562);
                        _27194 = vec3(_15566.x ? _15536.x : _15531.x, _15566.y ? _15536.y : _15531.y, _15566.z ? _15536.z : _15531.z);
                        _27192 = vec3(_15576.x ? _15531.x : _15536.x, _15576.y ? _15531.y : _15536.y, _15576.z ? _15531.z : _15536.z);
                        _27187 = vec3(_15581.x ? _15550.x : _15543.x, _15581.y ? _15550.y : _15543.y, _15581.z ? _15550.z : _15543.z);
                        _27186 = vec3(_15571.x ? _15543.x : _15550.x, _15571.y ? _15543.y : _15550.y, _15571.z ? _15543.z : _15550.z);
                    }
                    else
                    {
                        bool _15587 = abs(_27174.x) > 0.5;
                        bvec3 _15591 = bvec3(_15587);
                        bvec3 _15596 = bvec3(_15587);
                        _27194 = vec3(_15591.x ? _15536.x : _15531.x, _15591.y ? _15536.y : _15531.y, _15591.z ? _15536.z : _15531.z);
                        _27192 = vec3(0.0, 1.0, 0.0);
                        _27187 = vec3(_15596.x ? _15550.x : _15543.x, _15596.y ? _15550.y : _15543.y, _15596.z ? _15550.z : _15543.z);
                        _27186 = vec3(0.0, 1.0, 0.0);
                    }
                    float _15978 = ((_27186.y > 0.5) ? (_283.PoolMin.w - _14044.y) : ((_27186.x > 0.5) ? (_283.PoolMax.x - _14044.x) : ((_27186.x < (-0.5)) ? (_14044.x - _283.PoolMin.x) : ((_27186.z > 0.5) ? (_283.PoolMax.z - _14044.z) : (_14044.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                    float _16022 = ((_27187.y > 0.5) ? (_283.PoolMin.w - _14044.y) : ((_27187.x > 0.5) ? (_283.PoolMax.x - _14044.x) : ((_27187.x < (-0.5)) ? (_14044.x - _283.PoolMin.x) : ((_27187.z > 0.5) ? (_283.PoolMax.z - _14044.z) : (_14044.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                    vec2 _27404;
                    _27404 = _27184;
                    bool _27307;
                    uint _27322;
                    vec2 _27403;
                    vec2 _27406;
                    float _27410;
                    float _27413;
                    bool _29577;
                    int _27188 = 0;
                    bool _27211;
                    float _27411 = _27185;
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_27188 < 11)
                        {
                            if (_27188 == 1)
                            {
                                vec3 _15615 = _14044 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                float _16048 = dot(_15615, _283.CameraForward.xyz);
                                vec2 _27407;
                                float _27414;
                                if (_16048 > 0.0500000007450580596923828125)
                                {
                                    float _16089 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _16065 = ((dot(_15615, _283.CameraRight.xyz) * _16089) / _16048) + (0.5 * float(_283.OutputWidth));
                                    float _16078 = (((-dot(_15615, _283.CameraUp.xyz)) * _16089) / _16048) + (0.5 * float(_283.OutputHeight));
                                    vec2 _16079 = vec2(_16065, _16078);
                                    ivec2 _15626 = ivec2(_16079);
                                    ivec2 _16163 = clamp(ivec2(floor(((vec2(_15626) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16172 = clamp(_16163, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _16198 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_16172.x, int((FluidSize.y - 1u) - uint(_16172.y)), 0).xy, 0);
                                    float _16199 = _16198.x;
                                    ivec2 _16208 = clamp(_16163, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16295 = clamp(_15626, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _16247 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16295.x, int((_283.OutputHeight - 1u) - uint(_16295.y)), 0).xy, 0);
                                    float _16248 = _16247.x;
                                    vec2 _27408;
                                    float _27415;
                                    if (((((_16065 >= 0.0) && (_16078 >= 0.0)) && (_16065 < float(_283.OutputWidth))) && (_16078 < float(_283.OutputHeight))) && (((_16199 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_16208.x, int((FluidSize.y - 1u) - uint(_16208.y)), 0).xy, 0).w > 0.5)) && ((((_16248 > 0.0) && (abs(_16248 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16248 - _283.ProjA)) : 1000000000.0) >= (_16199 + 0.0500000007450580596923828125))))
                                    {
                                        vec2 _15633 = floor(_16079) + vec2(0.5);
                                        ivec2 _16386 = clamp(ivec2(_15633), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _16338 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16386.x, int((_283.OutputHeight - 1u) - uint(_16386.y)), 0).xy, 0);
                                        float _16339 = _16338.x;
                                        float _16356 = ((_16339 > 0.0) && (abs(_16339 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16339 - _283.ProjA)) : 1000000000.0;
                                        if (_16356 < _16048)
                                        {
                                            _27410 = _16356;
                                            _27403 = _15633;
                                            _27322 = 2u;
                                            _27307 = true;
                                            break;
                                        }
                                        _27415 = _16356;
                                        _27408 = _15633;
                                    }
                                    else
                                    {
                                        _27415 = _27411;
                                        _27408 = _27404;
                                    }
                                    _27414 = _27415;
                                    _27407 = _27408;
                                }
                                else
                                {
                                    _27414 = _27411;
                                    _27407 = _27404;
                                }
                                _29577 = _27211;
                                _27413 = _27414;
                                _27406 = _27407;
                                int _15693 = _27188 + 1;
                                _27211 = _29577;
                                _27188 = _15693;
                                _27411 = _27413;
                                _27404 = _27406;
                                continue;
                            }
                            bool _15647 = _27188 == 2;
                            bool _15649 = _27188 >= 3;
                            int _15651 = _27188 & 1;
                            bool _15654 = _15647 || (_15649 && (_15651 == 0));
                            bool _15656 = _27188 < 3;
                            int _15658 = _27188 - 3;
                            float _15663 = _15656 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_15658 / 2)));
                            if (_15663 >= (_15654 ? _16022 : _15978))
                            {
                                _29577 = _27211;
                                _27413 = _27411;
                                _27406 = _27404;
                                int _15693 = _27188 + 1;
                                _27211 = _29577;
                                _27188 = _15693;
                                _27411 = _27413;
                                _27404 = _27406;
                                continue;
                            }
                            bvec3 _15676 = bvec3(_15654);
                            vec3 _15680 = _14044 + (vec3(_15676.x ? _27187.x : _27186.x, _15676.y ? _27187.y : _27186.y, _15676.z ? _27187.z : _27186.z) * _15663);
                            bvec3 _15684 = bvec3(_15654);
                            vec3 _15685 = vec3(_15684.x ? _27192.x : _27194.x, _15684.y ? _27192.y : _27194.y, _15684.z ? _27192.z : _27194.z);
                            bool _27208;
                            vec2 _27282;
                            float _27286;
                            do
                            {
                                float _16549 = ((_15685.y > 0.5) ? (_283.PoolMin.w - _15680.y) : ((_15685.x > 0.5) ? (_283.PoolMax.x - _15680.x) : ((_15685.x < (-0.5)) ? (_15680.x - _283.PoolMin.x) : ((_15685.z > 0.5) ? (_283.PoolMax.z - _15680.z) : (_15680.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _27283;
                                _27283 = vec2(0.5);
                                float _16455;
                                float _16497;
                                vec2 _27199;
                                float _27200;
                                bool _27205;
                                bool _27209;
                                int _27195 = 0;
                                float _27196 = 0.0;
                                float _27197 = 0.0599999986588954925537109375;
                                float _27287 = 1000000000.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if ((_27195 < 40) && (_27196 < _16549))
                                    {
                                        _16455 = min(_27196 + _27197, _16549);
                                        bool _27198;
                                        do
                                        {
                                            vec3 _16563 = (_15680 + (_15685 * _16455)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                            float _16621 = dot(_16563, _283.CameraForward.xyz);
                                            if (_16621 <= 0.0500000007450580596923828125)
                                            {
                                                _27200 = 1000000000.0;
                                                _27199 = vec2(0.5);
                                                _27198 = false;
                                                break;
                                            }
                                            float _16662 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            float _16638 = ((dot(_16563, _283.CameraRight.xyz) * _16662) / _16621) + (0.5 * float(_283.OutputWidth));
                                            float _16651 = (((-dot(_16563, _283.CameraUp.xyz)) * _16662) / _16621) + (0.5 * float(_283.OutputHeight));
                                            if (!((((_16638 >= 0.0) && (_16651 >= 0.0)) && (_16638 < float(_283.OutputWidth))) && (_16651 < float(_283.OutputHeight))))
                                            {
                                                _27200 = 1000000000.0;
                                                _27199 = vec2(0.5);
                                                _27198 = false;
                                                break;
                                            }
                                            vec2 _16580 = floor(vec2(_16638, _16651)) + vec2(0.5);
                                            ivec2 _16746 = clamp(ivec2(_16580), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                            vec4 _16698 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16746.x, int((_283.OutputHeight - 1u) - uint(_16746.y)), 0).xy, 0);
                                            float _16699 = _16698.x;
                                            float _16716 = ((_16699 > 0.0) && (abs(_16699 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16699 - _283.ProjA)) : 1000000000.0;
                                            _27200 = _16716;
                                            _27199 = _16580;
                                            _27198 = abs(_16716 - _16621) < ((0.0199999995529651641845703125 * _16621) + 0.02999999932944774627685546875);
                                            break;
                                        } while(false);
                                        if (_27198)
                                        {
                                            vec2 _27284;
                                            float _27288;
                                            _27288 = _27200;
                                            _27284 = _27199;
                                            float _29816;
                                            float _29817;
                                            float _29818;
                                            vec2 _29820;
                                            int _27204 = 0;
                                            float _27289 = _27196;
                                            float _27291 = _16455;
                                            SPIRV_CROSS_UNROLL
                                            for (; _27204 < 4; _27291 = _29816, _27289 = _29817, _27288 = _29818, _27284 = _29820, _27204++)
                                            {
                                                float _16473 = 0.5 * (_27289 + _27291);
                                                bool _27292;
                                                vec2 _27293;
                                                float _27294;
                                                do
                                                {
                                                    vec3 _16790 = (_15680 + (_15685 * _16473)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                    float _16848 = dot(_16790, _283.CameraForward.xyz);
                                                    if (_16848 <= 0.0500000007450580596923828125)
                                                    {
                                                        _27294 = 1000000000.0;
                                                        _27293 = vec2(0.5);
                                                        _27292 = false;
                                                        break;
                                                    }
                                                    float _16889 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                    float _16865 = ((dot(_16790, _283.CameraRight.xyz) * _16889) / _16848) + (0.5 * float(_283.OutputWidth));
                                                    float _16878 = (((-dot(_16790, _283.CameraUp.xyz)) * _16889) / _16848) + (0.5 * float(_283.OutputHeight));
                                                    if (!((((_16865 >= 0.0) && (_16878 >= 0.0)) && (_16865 < float(_283.OutputWidth))) && (_16878 < float(_283.OutputHeight))))
                                                    {
                                                        _27294 = 1000000000.0;
                                                        _27293 = vec2(0.5);
                                                        _27292 = false;
                                                        break;
                                                    }
                                                    vec2 _16807 = floor(vec2(_16865, _16878)) + vec2(0.5);
                                                    ivec2 _16973 = clamp(ivec2(_16807), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                    vec4 _16925 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16973.x, int((_283.OutputHeight - 1u) - uint(_16973.y)), 0).xy, 0);
                                                    float _16926 = _16925.x;
                                                    float _16943 = ((_16926 > 0.0) && (abs(_16926 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_16926 - _283.ProjA)) : 1000000000.0;
                                                    _27294 = _16943;
                                                    _27293 = _16807;
                                                    _27292 = abs(_16943 - _16848) < ((0.0199999995529651641845703125 * _16848) + 0.02999999932944774627685546875);
                                                    break;
                                                } while(false);
                                                _29816 = _27292 ? _16473 : _27291;
                                                _29817 = _27292 ? _27289 : _16473;
                                                _29818 = _27292 ? _27294 : _27288;
                                                bvec2 _29819 = bvec2(_27292);
                                                _29820 = vec2(_29819.x ? _27293.x : _27284.x, _29819.y ? _27293.y : _27284.y);
                                            }
                                            _27286 = _27288;
                                            _27282 = _27284;
                                            _27209 = true;
                                            _27205 = true;
                                            break;
                                        }
                                        _16497 = min(_27197 * 1.2000000476837158203125, 0.300000011920928955078125);
                                        _27197 = _16497;
                                        _27196 = _16455;
                                        _27195++;
                                        _27287 = _27200;
                                        _27283 = _27199;
                                        continue;
                                    }
                                    else
                                    {
                                        _27286 = _27287;
                                        _27282 = _27283;
                                        _27209 = _27211;
                                        _27205 = false;
                                        break;
                                    }
                                }
                                if (_27205)
                                {
                                    _27208 = _27209;
                                    break;
                                }
                                _27208 = false;
                                break;
                            } while(false);
                            if (_27208)
                            {
                                _27410 = _27286;
                                _27403 = _27282;
                                _27322 = 1u;
                                _27307 = true;
                                break;
                            }
                            _29577 = _27208;
                            _27413 = _27286;
                            _27406 = _27282;
                            int _15693 = _27188 + 1;
                            _27211 = _29577;
                            _27188 = _15693;
                            _27411 = _27413;
                            _27404 = _27406;
                            continue;
                        }
                        else
                        {
                            _27410 = _27411;
                            _27403 = _27404;
                            _27322 = _27361;
                            _27307 = false;
                            break;
                        }
                    }
                    if (_27307)
                    {
                        _27409 = _27410;
                        _27402 = _27403;
                        _27321 = _27322;
                        break;
                    }
                    _27409 = _27410;
                    _27402 = _27403;
                    _27321 = 0u;
                    break;
                } while(false);
                if (_27321 != 0u)
                {
                    float _17051 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec3 _14065 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_27402.x - (0.5 * float(_283.OutputWidth))) * _27409) / _17051)) + (_283.CameraUp.xyz * (((-(_27402.y - (0.5 * float(_283.OutputHeight)))) * _27409) / _17051))) + (_283.CameraForward.xyz * _27409));
                    bvec3 _14066 = bvec3(_27321 == 1u);
                    vec3 _14067 = vec3(_14066.x ? _14044.x : _14065.x, _14066.y ? _14044.y : _14065.y, _14066.z ? _14044.z : _14065.z);
                    _27726 = _27402;
                    _27671 = _27409;
                    _27616 = (_27321 == 1u) ? _27173 : length(_14067 - _5937);
                    _27560 = 1.0;
                    _27504 = _14067;
                    break;
                }
            }
            float _14089 = min(_27173, _6104);
            _27726 = _27419;
            _27671 = _27449;
            _27616 = _14089;
            _27560 = _27478;
            _27504 = _5937 + (_15338 * _14089);
            break;
        } while(false);
        vec3 _27826;
        vec3 _27827;
        do
        {
            vec2 _17288 = clamp(_27726 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _17299 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17288.x, 1.0 - _17288.y), 0.0).xyz;
            if ((_27560 < 0.5) || (_27671 > 100000000.0))
            {
                _27827 = vec3(0.0, 1.0, 0.5);
                _27826 = _17299;
                break;
            }
            ivec2 _17122 = ivec2(_27726);
            float _17128 = _5937.y - _27504.y;
            if (_17128 <= 0.00999999977648258209228515625)
            {
                _27827 = vec3(0.0, 1.0, 0.5);
                _27826 = _17299;
                break;
            }
            ivec2 _17329 = clamp(_17122, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            ivec2 _17417 = clamp(_17122, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec3 _17377 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_17417.x, int((_283.OutputHeight - 1u) - uint(_17417.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _17379 = length(_17377);
            vec3 _17385 = _17377 / vec3(_17379);
            bvec3 _17386 = bvec3(_17379 > 0.001000000047497451305389404296875);
            ivec2 _17477 = clamp(_17122, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            float _17174 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_17329.x, int((_283.OutputHeight - 1u) - uint(_17329.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_17386.x ? _17385.x : vec3(0.0, 1.0, 0.0).x, _17386.y ? _17385.y : vec3(0.0, 1.0, 0.0).y, _17386.z ? _17385.z : vec3(0.0, 1.0, 0.0).z), _5638), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_17477.x, int((_283.OutputHeight - 1u) - uint(_17477.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _283.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_17299, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _17178 = max(-_29788.y, 0.20000000298023223876953125);
            vec3 _17192 = mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _17128) / vec3(_17178)), vec3(_283.LightParams.x));
            float _27825;
            if ((_283.CausticParams.x > 0.0) && (_17174 > 0.0))
            {
                vec2 _17221 = _27504.xz - ((_29788.xz / vec2(_17178)) * _17128);
                vec2 _17532 = _17221 * (6.283185482025146484375 / max(_283.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _17555 = _17532 + (vec2(sin((_17532.y * 0.730000019073486328125) + ((_283.ShadeParams.z * 0.89999997615814208984375) * _283.CausticParams.z)), sin((_17532.x * 0.61000001430511474609375) - ((_283.ShadeParams.z * 1.10000002384185791015625) * _283.CausticParams.z))) * 0.89999997615814208984375);
                float _27824;
                do
                {
                    if ((_283.GridParams.z < 2u) || (_283.GridParams.w < 2u))
                    {
                        _27824 = 1.0;
                        break;
                    }
                    vec2 _17665 = clamp(((_17221 - _283.PoolMin.xz) / max(_283.PoolMax.xz - _283.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)), vec2(0.0), vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _17667 = uvec2(_17665);
                    vec2 _17671 = _17665 - vec2(_17667);
                    uint _17678 = _17667.x + (_17667.y * _283.GridParams.z);
                    _27824 = mix(mix(CausticField_1._data[_17678], CausticField_1._data[_17678 + 1u], _17671.x), mix(CausticField_1._data[_17678 + _283.GridParams.z], CausticField_1._data[(_17678 + _283.GridParams.z) + 1u], _17671.x), _17671.y);
                    break;
                } while(false);
                _27825 = min(max(1.0 + ((_283.CausticParams.x * (exp((-_17128) / max(_283.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _17128))) * ((pow(clamp((1.0 - abs(sin(_17555.x + ((_283.ShadeParams.z * 0.60000002384185791015625) * _283.CausticParams.z)) * sin((_17555.y * 1.12999999523162841796875) - ((_283.ShadeParams.z * 0.4000000059604644775390625) * _283.CausticParams.z)))) * (1.0 - abs(sin(((_17555.x + _17555.y) * 0.709999978542327880859375) - ((_283.ShadeParams.z * 0.5) * _283.CausticParams.z)) * sin(((_17555.x - _17555.y) * 0.829999983310699462890625) + ((_283.ShadeParams.z * 0.699999988079071044921875) * _283.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_27824, 0.0500000007450580596923828125), _283.LightParams.y), 2.5);
            }
            else
            {
                _27825 = 1.0;
            }
            _27827 = vec3(_17174, dot(_17192, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _27825);
            _27826 = _17299 * ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _17128) * 1.2000000476837158203125), vec3(_283.LightParams.x)) * (1.0 - _17174)) + ((_17192 * _17174) * _27825));
            break;
        } while(false);
        float _6125 = mix(_6055, max(_27616, _6055), _6060);
        vec3 _28933;
        if (_6060 < 0.5)
        {
            vec3 _17735 = ((_283.CameraRight.xyz * _6085.x) + (_283.CameraUp.xyz * _6085.y)) + (_283.CameraForward.xyz * _6085.z);
            vec3 _28934;
            if (_17735.y < (-0.0199999995529651641845703125))
            {
                float _6142 = (_283.PoolMin.w - _5937.y) / _17735.y;
                vec3 _6147 = _5937 + (_17735 * _6142);
                vec3 _28935;
                if (((_6142 > _6055) && ((((_6147.x > _283.PoolMin.x) && (_6147.x < _283.PoolMax.x)) && (_6147.z > _283.PoolMin.z)) && (_6147.z < _283.PoolMax.z))) && ((_27560 < 0.5) || (_6142 < _27616)))
                {
                    float _17791 = length(_6147 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w)) / ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _17796 = _283.RippleParams.x * _283.RippleFlow.y;
                    vec2 _17798 = _6147.xz;
                    float _17930 = max(_283.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _27832;
                    _27832 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _27831 = 0; _27831 < 6; )
                    {
                        float _17942 = _17930 * _1894[_27831];
                        uint _18030 = (((uint(_27831) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _18038 = ((_18030 >> ((_18030 >> 28u) + 4u)) ^ _18030) * 277803737u;
                        float _17961 = 0.60000002384185791015625 + (float(((_18038 >> 22u) ^ _18038) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _17966 = vec2(cos(_17961), sin(_17961));
                        uint _18057 = (((uint(_27831) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _18065 = ((_18057 >> ((_18057 >> 28u) + 4u)) ^ _18057) * 277803737u;
                        _27832 += (_17966 * (((_17796 * pow(0.7799999713897705078125, float(_27831))) * clamp((_17942 / max(_17791 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_17966, _17798) * (6.283185482025146484375 / _17942)) + ((sqrt(61.63805389404296875 / _17942) * _283.RippleParams.z) * _283.ShadeParams.z)) + (float(((_18065 >> 22u) ^ _18065) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _27831++;
                        continue;
                    }
                    vec3 _17809 = normalize(vec3(-_27832.x, 1.0, -_27832.y));
                    vec3 _17812 = _6147 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                    float _18085 = dot(_17812, _283.CameraRight.xyz);
                    float _18090 = dot(_17812, _283.CameraUp.xyz);
                    float _18095 = dot(_17812, _283.CameraForward.xyz);
                    vec3 _18096 = vec3(_18085, _18090, _18095);
                    vec3 _17816 = reflect(_17735, _17809);
                    float _18113 = dot(_17816, _283.CameraForward.xyz);
                    vec3 _18114 = vec3(dot(_17816, _283.CameraRight.xyz), dot(_17816, _283.CameraUp.xyz), _18113);
                    float _27857;
                    vec3 _27949;
                    do
                    {
                        if (_283.ReflectionDistance <= 0.0)
                        {
                            _27949 = vec3(0.0);
                            _27857 = 0.0;
                            break;
                        }
                        float _18163 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _18113);
                        if (_18163 <= 0.0)
                        {
                            _27949 = vec3(0.0);
                            _27857 = 0.0;
                            break;
                        }
                        float _18360 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _18172 = max(0.0040000001899898052215576171875 * _18095, 0.00999999977648258209228515625);
                        float _18174 = 0.5 * _18172;
                        float _18183;
                        float _18185;
                        bool _27842;
                        bool _27847;
                        float _27852;
                        float _27858;
                        vec3 _27950;
                        int _27833 = 0;
                        float _27834 = _18174;
                        float _27835 = _18172;
                        float _27853 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_27833 < 80)
                            {
                                _18183 = _27834 + _27835;
                                _18185 = _27835 * 1.07000005245208740234375;
                                if (_18183 > _283.ReflectionDistance)
                                {
                                    _27950 = vec3(0.0);
                                    _27858 = 0.0;
                                    _27852 = _27853;
                                    _27847 = false;
                                    _27842 = true;
                                    break;
                                }
                                vec3 _18196 = _18096 + (_18114 * _18183);
                                if (_18196.z <= 0.0500000007450580596923828125)
                                {
                                    _27950 = vec3(0.0);
                                    _27858 = 0.0;
                                    _27852 = _27853;
                                    _27847 = false;
                                    _27842 = true;
                                    break;
                                }
                                float _18400 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _18376 = ((_18196.x * _18400) / _18196.z) + (0.5 * float(_283.OutputWidth));
                                float _18389 = (((-_18196.y) * _18400) / _18196.z) + (0.5 * float(_283.OutputHeight));
                                if (!((((_18376 >= 0.0) && (_18389 >= 0.0)) && (_18376 < float(_283.OutputWidth))) && (_18389 < float(_283.OutputHeight))))
                                {
                                    _27950 = vec3(0.0);
                                    _27858 = _27595;
                                    _27852 = _27853;
                                    _27847 = true;
                                    _27842 = false;
                                    break;
                                }
                                ivec2 _18484 = clamp(ivec2(vec2(_18376, _18389)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _18436 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18484.x, int((_283.OutputHeight - 1u) - uint(_18484.y)), 0).xy, 0);
                                float _18437 = _18436.x;
                                float _18454 = ((_18437 > 0.0) && (abs(_18437 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18437 - _283.ProjA)) : 1000000000.0;
                                if (((_18454 < 100000000.0) && (_18196.z > _18454)) && ((_18196.z - _18454) < max(_27835 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _27837;
                                    _27837 = _18183;
                                    float _29821;
                                    float _29822;
                                    int _27836 = 0;
                                    float _27840 = _27834;
                                    SPIRV_CROSS_UNROLL
                                    for (; _27836 < 6; _27840 = _29821, _27837 = _29822, _27836++)
                                    {
                                        float _18240 = 0.5 * (_27840 + _27837);
                                        vec3 _18245 = _18096 + (_18114 * _18240);
                                        float _18554 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _18615 = clamp(ivec2(vec2(((_18245.x * _18554) / _18245.z) + (0.5 * float(_283.OutputWidth)), (((-_18245.y) * _18554) / _18245.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _18567 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18615.x, int((_283.OutputHeight - 1u) - uint(_18615.y)), 0).xy, 0);
                                        float _18568 = _18567.x;
                                        bool _18252 = _18245.z > (((_18568 > 0.0) && (abs(_18568 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_18568 - _283.ProjA)) : 1000000000.0);
                                        _29821 = _18252 ? _27840 : _18240;
                                        _29822 = _18252 ? _18240 : _27837;
                                    }
                                    vec3 _18266 = _18096 + (_18114 * _27837);
                                    float _18685 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _18675 = vec2(((_18266.x * _18685) / _18266.z) + (0.5 * float(_283.OutputWidth)), (((-_18266.y) * _18685) / _18266.z) + (0.5 * float(_283.OutputHeight)));
                                    ivec2 _18743 = clamp(ivec2(_18675), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec3 _18703 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_18743.x, int((_283.OutputHeight - 1u) - uint(_18743.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _18705 = length(_18703);
                                    vec3 _18711 = _18703 / vec3(_18705);
                                    bvec3 _18712 = bvec3(_18705 > 0.001000000047497451305389404296875);
                                    vec3 _18713 = vec3(_18712.x ? _18711.x : vec3(0.0, 1.0, 0.0).x, _18712.y ? _18711.y : vec3(0.0, 1.0, 0.0).y, _18712.z ? _18711.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_18713, _283.CameraRight.xyz), dot(_18713, _283.CameraUp.xyz), dot(_18713, _283.CameraForward.xyz)), _18114) > 0.100000001490116119384765625)
                                    {
                                        int _18293 = _27833 + 1;
                                        _27835 = _18185;
                                        _27834 = _18183;
                                        _27833 = _18293;
                                        _27853 = _18183;
                                        continue;
                                    }
                                    float _18807 = clamp(((0.0199999995529651641845703125 * _27837) * _18360) / max(_18266.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _18809 = 0.5 * _18807;
                                    vec2 _18894 = clamp(_18675 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18920 = clamp((_18675 + vec2(_18809)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18946 = clamp((_18675 + vec2(_18807 * (-0.5), _18809)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18972 = clamp((_18675 + vec2(_18809, _18807 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18998 = clamp((_18675 + vec2(_18807 * (-0.5), _18807 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19024 = clamp((_18675 + vec2(_18807, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19050 = clamp((_18675 + vec2(-_18807, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19076 = clamp((_18675 + vec2(0.0, _18807)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19102 = clamp((_18675 + vec2(0.0, -_18807)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _18879 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18894.x, 1.0 - _18894.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18920.x, 1.0 - _18920.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18946.x, 1.0 - _18946.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18972.x, 1.0 - _18972.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18998.x, 1.0 - _18998.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19024.x, 1.0 - _19024.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19050.x, 1.0 - _19050.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19076.x, 1.0 - _19076.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19102.x, 1.0 - _19102.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _27950 = _18879;
                                    _27858 = _18163;
                                    _27852 = _18183;
                                    _27847 = false;
                                    _27842 = true;
                                    break;
                                }
                                int _18293 = _27833 + 1;
                                _27835 = _18185;
                                _27834 = _18183;
                                _27833 = _18293;
                                _27853 = _18183;
                                continue;
                            }
                            else
                            {
                                _27950 = vec3(0.0);
                                _27858 = _27595;
                                _27852 = _27853;
                                _27847 = false;
                                _27842 = false;
                                break;
                            }
                        }
                        if (_27842)
                        {
                            _27949 = _27950;
                            _27857 = _27858;
                            break;
                        }
                        if ((!_27847) || (_27852 < 0.0))
                        {
                            _27949 = _27950;
                            _27857 = 0.0;
                            break;
                        }
                        vec3 _18308 = _18096 + (_18114 * _27852);
                        float _19153 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _19143 = vec2(((_18308.x * _19153) / _18308.z) + (0.5 * float(_283.OutputWidth)), (((-_18308.y) * _19153) / _18308.z) + (0.5 * float(_283.OutputHeight)));
                        ivec2 _19214 = clamp(ivec2(_19143), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _19166 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19214.x, int((_283.OutputHeight - 1u) - uint(_19214.y)), 0).xy, 0);
                        float _19167 = _19166.x;
                        float _19184 = ((_19167 > 0.0) && (abs(_19167 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_19167 - _283.ProjA)) : 1000000000.0;
                        float _19260 = clamp(((0.0199999995529651641845703125 * _27852) * _18360) / max(_18308.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _19262 = 0.5 * _19260;
                        vec2 _19347 = clamp(_19143 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19373 = clamp((_19143 + vec2(_19262)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19399 = clamp((_19143 + vec2(_19260 * (-0.5), _19262)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19425 = clamp((_19143 + vec2(_19262, _19260 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19451 = clamp((_19143 + vec2(_19260 * (-0.5), _19260 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19477 = clamp((_19143 + vec2(_19260, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19503 = clamp((_19143 + vec2(-_19260, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19529 = clamp((_19143 + vec2(0.0, _19260)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19555 = clamp((_19143 + vec2(0.0, -_19260)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _19332 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19347.x, 1.0 - _19347.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19373.x, 1.0 - _19373.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19399.x, 1.0 - _19399.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19425.x, 1.0 - _19425.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19451.x, 1.0 - _19451.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19477.x, 1.0 - _19477.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19503.x, 1.0 - _19503.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19529.x, 1.0 - _19529.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19555.x, 1.0 - _19555.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_19184 > 100000000.0)
                        {
                            _27949 = _19332;
                            _27857 = _18163;
                            break;
                        }
                        float _18333 = _19184 - _18308.z;
                        if (_18333 <= 0.0)
                        {
                            _27949 = _19332;
                            _27857 = _18163;
                            break;
                        }
                        _27949 = _19332;
                        _27857 = _18163 * exp((_18333 / max(_18113, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _27955;
                    do
                    {
                        if (_283.EnvParams.x <= 0.0)
                        {
                            float _19609 = _17816.y;
                            _27955 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _19609))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _19609)));
                            break;
                        }
                        _27955 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_17816.x, _17816.y, -_17816.z), 0.0).xyz * _283.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _17830 = refract(_17735, _17809, 0.75187969207763671875);
                    bvec3 _29823 = bvec3(dot(_17830, _17830) < 9.9999999747524270787835121154785e-07);
                    vec3 _29824 = vec3(_29823.x ? _17735.x : _17830.x, _29823.y ? _17735.y : _17830.y, _29823.z ? _17735.z : _17830.z);
                    float _19635 = dot(_29824, _283.CameraRight.xyz);
                    float _19640 = dot(_29824, _283.CameraUp.xyz);
                    float _19645 = dot(_29824, _283.CameraForward.xyz);
                    vec3 _19646 = vec3(_19635, _19640, _19645);
                    float _19686 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _19676 = vec2(((_18085 * _19686) / _18095) + (0.5 * float(_283.OutputWidth)), (((-_18090) * _19686) / _18095) + (0.5 * float(_283.OutputHeight)));
                    vec3 _28394;
                    float _28492;
                    float _28590;
                    float _28688;
                    vec2 _28786;
                    do
                    {
                        ivec2 _20017 = clamp(ivec2(_19676), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec4 _19969 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20017.x, int((_283.OutputHeight - 1u) - uint(_20017.y)), 0).xy, 0);
                        float _19970 = _19969.x;
                        vec2 _28309;
                        float _28339;
                        _28339 = ((_19970 > 0.0) && (abs(_19970 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_19970 - _283.ProjA)) : 1000000000.0;
                        _28309 = _19676;
                        float _19765;
                        float _29827;
                        vec2 _29829;
                        bool _27964;
                        float _28368;
                        vec3 _28395;
                        float _28493;
                        float _28591;
                        float _28689;
                        vec2 _28787;
                        int _27957 = 1;
                        float _27959 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_27957 <= 16)
                            {
                                float _19758 = float(_27957);
                                _19765 = (_19758 * 0.25) * sqrt(_19758 * 0.0625);
                                vec3 _19770 = _18096 + (_19646 * _19765);
                                if (_19770.z <= 0.0500000007450580596923828125)
                                {
                                    _28787 = _27761;
                                    _28689 = _27595;
                                    _28591 = _27595;
                                    _28493 = _27595;
                                    _28395 = _27539;
                                    _28368 = 0.0;
                                    _27964 = false;
                                    break;
                                }
                                float _20087 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _20063 = ((_19770.x * _20087) / _19770.z) + (0.5 * float(_283.OutputWidth));
                                float _20076 = (((-_19770.y) * _20087) / _19770.z) + (0.5 * float(_283.OutputHeight));
                                vec2 _20077 = vec2(_20063, _20076);
                                if (!((((_20063 >= 0.0) && (_20076 >= 0.0)) && (_20063 < float(_283.OutputWidth))) && (_20076 < float(_283.OutputHeight))))
                                {
                                    _28787 = _27761;
                                    _28689 = _27595;
                                    _28591 = _27595;
                                    _28493 = _27595;
                                    _28395 = _27539;
                                    _28368 = 0.0;
                                    _27964 = false;
                                    break;
                                }
                                ivec2 _20171 = clamp(ivec2(_20077), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _20123 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20171.x, int((_283.OutputHeight - 1u) - uint(_20171.y)), 0).xy, 0);
                                float _20124 = _20123.x;
                                float _20141 = ((_20124 > 0.0) && (abs(_20124 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20124 - _283.ProjA)) : 1000000000.0;
                                if (_19770.z >= _20141)
                                {
                                    ivec2 _19792 = ivec2(_20077);
                                    ivec2 _20252 = clamp(ivec2(floor(((vec2(_19792) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20261 = clamp(_20252, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _20287 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20261.x, int((FluidSize.y - 1u) - uint(_20261.y)), 0).xy, 0);
                                    float _20288 = _20287.x;
                                    ivec2 _20297 = clamp(_20252, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20384 = clamp(_19792, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _20336 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20384.x, int((_283.OutputHeight - 1u) - uint(_20384.y)), 0).xy, 0);
                                    float _20337 = _20336.x;
                                    if (!(((_20288 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20297.x, int((FluidSize.y - 1u) - uint(_20297.y)), 0).xy, 0).w > 0.5)) && ((((_20337 > 0.0) && (abs(_20337 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20337 - _283.ProjA)) : 1000000000.0) >= (_20288 + 0.0500000007450580596923828125))))
                                    {
                                        _28787 = _27761;
                                        _28689 = _27595;
                                        _28591 = _27595;
                                        _28493 = _27595;
                                        _28395 = _27539;
                                        _28368 = 0.0;
                                        _27964 = false;
                                        break;
                                    }
                                    float _27961;
                                    _27961 = _19765;
                                    float _29825;
                                    float _29826;
                                    int _27960 = 0;
                                    float _27962 = _27959;
                                    SPIRV_CROSS_UNROLL
                                    for (; _27960 < 5; _27962 = _29825, _27961 = _29826, _27960++)
                                    {
                                        float _19807 = 0.5 * (_27962 + _27961);
                                        vec3 _19812 = _18096 + (_19646 * _19807);
                                        float _20454 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _20515 = clamp(ivec2(vec2(((_19812.x * _20454) / _19812.z) + (0.5 * float(_283.OutputWidth)), (((-_19812.y) * _20454) / _19812.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                        vec4 _20467 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20515.x, int((_283.OutputHeight - 1u) - uint(_20515.y)), 0).xy, 0);
                                        float _20468 = _20467.x;
                                        bool _19819 = _19812.z >= (((_20468 > 0.0) && (abs(_20468 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20468 - _283.ProjA)) : 1000000000.0);
                                        _29825 = _19819 ? _27962 : _19807;
                                        _29826 = _19819 ? _19807 : _27961;
                                    }
                                    vec3 _19833 = _18096 + (_19646 * _27961);
                                    float _20585 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _19844 = clamp(vec2(((_19833.x * _20585) / _19833.z) + (0.5 * float(_283.OutputWidth)), (((-_19833.y) * _20585) / _19833.z) + (0.5 * float(_283.OutputHeight))), vec2(0.5), vec2(float(_283.OutputWidth), float(_283.OutputHeight)) - vec2(0.5));
                                    ivec2 _20646 = clamp(ivec2(_19844), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _20598 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20646.x, int((_283.OutputHeight - 1u) - uint(_20646.y)), 0).xy, 0);
                                    float _20599 = _20598.x;
                                    float _20616 = ((_20599 > 0.0) && (abs(_20599 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20599 - _283.ProjA)) : 1000000000.0;
                                    float _20724 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    _28787 = _19844;
                                    _28689 = _20616;
                                    _28591 = _27961;
                                    _28493 = 1.0;
                                    _28395 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_19844.x - (0.5 * float(_283.OutputWidth))) * _20616) / _20724)) + (_283.CameraUp.xyz * (((-(_19844.y - (0.5 * float(_283.OutputHeight)))) * _20616) / _20724))) + (_283.CameraForward.xyz * _20616));
                                    _28368 = 1.0;
                                    _27964 = true;
                                    break;
                                }
                                ivec2 _19868 = ivec2(_20077 - vec2(0.5));
                                ivec2 _20797 = clamp(ivec2(floor(((vec2(_19868) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20806 = clamp(_20797, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _20832 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20806.x, int((FluidSize.y - 1u) - uint(_20806.y)), 0).xy, 0);
                                float _20833 = _20832.x;
                                ivec2 _20842 = clamp(_20797, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20929 = clamp(_19868, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _20881 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20929.x, int((_283.OutputHeight - 1u) - uint(_20929.y)), 0).xy, 0);
                                float _20882 = _20881.x;
                                ivec2 _19873 = ivec2(_20077 + vec2(0.5));
                                ivec2 _21010 = clamp(ivec2(floor(((vec2(_19873) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21019 = clamp(_21010, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _21045 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_21019.x, int((FluidSize.y - 1u) - uint(_21019.y)), 0).xy, 0);
                                float _21046 = _21045.x;
                                ivec2 _21055 = clamp(_21010, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21142 = clamp(_19873, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                vec4 _21094 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21142.x, int((_283.OutputHeight - 1u) - uint(_21142.y)), 0).xy, 0);
                                float _21095 = _21094.x;
                                bool _19875 = (((_20833 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20842.x, int((FluidSize.y - 1u) - uint(_20842.y)), 0).xy, 0).w > 0.5)) && ((((_20882 > 0.0) && (abs(_20882 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_20882 - _283.ProjA)) : 1000000000.0) >= (_20833 + 0.0500000007450580596923828125))) && (((_21046 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_21055.x, int((FluidSize.y - 1u) - uint(_21055.y)), 0).xy, 0).w > 0.5)) && ((((_21095 > 0.0) && (abs(_21095 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21095 - _283.ProjA)) : 1000000000.0) >= (_21046 + 0.0500000007450580596923828125)));
                                _29827 = _19875 ? _20141 : _28339;
                                bvec2 _29828 = bvec2(_19875);
                                _29829 = vec2(_29828.x ? _20077.x : _28309.x, _29828.y ? _20077.y : _28309.y);
                                _27957++;
                                _28339 = _29827;
                                _28309 = _29829;
                                _27959 = _19765;
                                continue;
                            }
                            else
                            {
                                _28787 = _27761;
                                _28689 = _27595;
                                _28591 = _27595;
                                _28493 = _27595;
                                _28395 = _27539;
                                _28368 = 0.0;
                                _27964 = false;
                                break;
                            }
                        }
                        if (_27964)
                        {
                            _28786 = _28787;
                            _28688 = _28689;
                            _28590 = _28591;
                            _28492 = _28493;
                            _28394 = _28395;
                            break;
                        }
                        vec3 _21194 = ((_283.CameraRight.xyz * _19635) + (_283.CameraUp.xyz * _19640)) + (_283.CameraForward.xyz * _19645);
                        float _27979;
                        vec3 _27980;
                        do
                        {
                            if ((((_6147.x < _283.PoolMin.x) || (_6147.x > _283.PoolMax.x)) || (_6147.z < _283.PoolMin.z)) || (_6147.z > _283.PoolMax.z))
                            {
                                _27980 = vec3(0.0, 1.0, 0.0);
                                _27979 = 1000.0;
                                break;
                            }
                            float _27967;
                            if (_21194.x > 9.9999997473787516355514526367188e-05)
                            {
                                _27967 = (_283.PoolMax.x - _6147.x) / _21194.x;
                            }
                            else
                            {
                                float _27968;
                                if (_21194.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _27968 = (_283.PoolMin.x - _6147.x) / _21194.x;
                                }
                                else
                                {
                                    _27968 = 1000.0;
                                }
                                _27967 = _27968;
                            }
                            bool _21256 = _27967 < 1000.0;
                            vec3 _27984;
                            if (_21256)
                            {
                                _27984 = vec3((_21194.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                            }
                            else
                            {
                                _27984 = vec3(0.0, 1.0, 0.0);
                            }
                            float _29830 = _21256 ? _27967 : 1000.0;
                            float _27971;
                            if (_21194.z > 9.9999997473787516355514526367188e-05)
                            {
                                _27971 = (_283.PoolMax.z - _6147.z) / _21194.z;
                            }
                            else
                            {
                                float _27972;
                                if (_21194.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _27972 = (_283.PoolMin.z - _6147.z) / _21194.z;
                                }
                                else
                                {
                                    _27972 = 1000.0;
                                }
                                _27971 = _27972;
                            }
                            bool _21294 = _27971 < _29830;
                            vec3 _27982;
                            if (_21294)
                            {
                                _27982 = vec3(0.0, 0.0, _29849);
                            }
                            else
                            {
                                _27982 = _27984;
                            }
                            float _29831 = _21294 ? _27971 : _29830;
                            float _27977;
                            vec3 _27981;
                            if (_21194.y < (-9.9999997473787516355514526367188e-05))
                            {
                                float _21314 = (_283.PoolMin.y - _6147.y) / _21194.y;
                                bool _21317 = _21314 < _29831;
                                bvec3 _29832 = bvec3(_21317);
                                _27981 = vec3(_29832.x ? vec3(0.0, 1.0, 0.0).x : _27982.x, _29832.y ? vec3(0.0, 1.0, 0.0).y : _27982.y, _29832.z ? vec3(0.0, 1.0, 0.0).z : _27982.z);
                                _27977 = _21317 ? _21314 : _29831;
                            }
                            else
                            {
                                _27981 = _27982;
                                _27977 = _29831;
                            }
                            _27980 = _27981;
                            _27979 = max(_27977, 0.0);
                            break;
                        } while(false);
                        if (_27979 < 100.0)
                        {
                            vec3 _19900 = _6147 + (_21194 * _27979);
                            uint _28169;
                            vec2 _28292;
                            float _28299;
                            do
                            {
                                bool _27989;
                                vec2 _27990;
                                float _27991;
                                do
                                {
                                    vec3 _21568 = _19900 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                    float _21626 = dot(_21568, _283.CameraForward.xyz);
                                    if (_21626 <= 0.0500000007450580596923828125)
                                    {
                                        _27991 = 1000000000.0;
                                        _27990 = vec2(0.5);
                                        _27989 = false;
                                        break;
                                    }
                                    float _21667 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _21643 = ((dot(_21568, _283.CameraRight.xyz) * _21667) / _21626) + (0.5 * float(_283.OutputWidth));
                                    float _21656 = (((-dot(_21568, _283.CameraUp.xyz)) * _21667) / _21626) + (0.5 * float(_283.OutputHeight));
                                    if (!((((_21643 >= 0.0) && (_21656 >= 0.0)) && (_21643 < float(_283.OutputWidth))) && (_21656 < float(_283.OutputHeight))))
                                    {
                                        _27991 = 1000000000.0;
                                        _27990 = vec2(0.5);
                                        _27989 = false;
                                        break;
                                    }
                                    vec2 _21585 = floor(vec2(_21643, _21656)) + vec2(0.5);
                                    ivec2 _21751 = clamp(ivec2(_21585), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                    vec4 _21703 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21751.x, int((_283.OutputHeight - 1u) - uint(_21751.y)), 0).xy, 0);
                                    float _21704 = _21703.x;
                                    float _21721 = ((_21704 > 0.0) && (abs(_21704 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_21704 - _283.ProjA)) : 1000000000.0;
                                    _27991 = _21721;
                                    _27990 = _21585;
                                    _27989 = abs(_21721 - _21626) < ((0.0199999995529651641845703125 * _21626) + 0.02999999932944774627685546875);
                                    break;
                                } while(false);
                                if (_27989)
                                {
                                    _28299 = _27991;
                                    _28292 = _27990;
                                    _28169 = 1u;
                                    break;
                                }
                                vec3 _21374 = _19900 - vec3(_283.CameraRight.w, _27021, _283.CameraForward.w);
                                vec3 _21382 = (_283.PoolMin.xyz + _283.PoolMax.xyz) * 0.5;
                                vec3 _21387 = vec3((_21374.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21392 = vec3(0.0, 0.0, (_21374.z >= 0.0) ? 1.0 : (-1.0));
                                vec3 _21399 = vec3((_21382.x >= _19900.x) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21406 = vec3(0.0, 0.0, (_21382.z >= _19900.z) ? 1.0 : (-1.0));
                                vec3 _27992;
                                vec3 _27993;
                                vec3 _27998;
                                vec3 _28000;
                                if (abs(_27980.y) > 0.5)
                                {
                                    bool _21418 = abs(_21374.z) >= abs(_21374.x);
                                    bvec3 _21422 = bvec3(_21418);
                                    bvec3 _21427 = bvec3(_21418);
                                    bvec3 _21432 = bvec3(_21418);
                                    bvec3 _21437 = bvec3(_21418);
                                    _28000 = vec3(_21422.x ? _21392.x : _21387.x, _21422.y ? _21392.y : _21387.y, _21422.z ? _21392.z : _21387.z);
                                    _27998 = vec3(_21432.x ? _21387.x : _21392.x, _21432.y ? _21387.y : _21392.y, _21432.z ? _21387.z : _21392.z);
                                    _27993 = vec3(_21437.x ? _21406.x : _21399.x, _21437.y ? _21406.y : _21399.y, _21437.z ? _21406.z : _21399.z);
                                    _27992 = vec3(_21427.x ? _21399.x : _21406.x, _21427.y ? _21399.y : _21406.y, _21427.z ? _21399.z : _21406.z);
                                }
                                else
                                {
                                    bool _21443 = abs(_27980.x) > 0.5;
                                    bvec3 _21447 = bvec3(_21443);
                                    bvec3 _21452 = bvec3(_21443);
                                    _28000 = vec3(_21447.x ? _21392.x : _21387.x, _21447.y ? _21392.y : _21387.y, _21447.z ? _21392.z : _21387.z);
                                    _27998 = vec3(0.0, 1.0, 0.0);
                                    _27993 = vec3(_21452.x ? _21406.x : _21399.x, _21452.y ? _21406.y : _21399.y, _21452.z ? _21406.z : _21399.z);
                                    _27992 = vec3(0.0, 1.0, 0.0);
                                }
                                float _21834 = ((_27992.y > 0.5) ? (_283.PoolMin.w - _19900.y) : ((_27992.x > 0.5) ? (_283.PoolMax.x - _19900.x) : ((_27992.x < (-0.5)) ? (_19900.x - _283.PoolMin.x) : ((_27992.z > 0.5) ? (_283.PoolMax.z - _19900.z) : (_19900.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                float _21878 = ((_27993.y > 0.5) ? (_283.PoolMin.w - _19900.y) : ((_27993.x > 0.5) ? (_283.PoolMax.x - _19900.x) : ((_27993.x < (-0.5)) ? (_19900.x - _283.PoolMin.x) : ((_27993.z > 0.5) ? (_283.PoolMax.z - _19900.z) : (_19900.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _28294;
                                _28294 = _27990;
                                bool _28155;
                                uint _28170;
                                vec2 _28293;
                                vec2 _28296;
                                float _28300;
                                float _28303;
                                bool _29687;
                                int _27994 = 0;
                                bool _28017;
                                float _28301 = _27991;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_27994 < 11)
                                    {
                                        if (_27994 == 1)
                                        {
                                            vec3 _21471 = _19900 - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                            float _21904 = dot(_21471, _283.CameraForward.xyz);
                                            vec2 _28297;
                                            float _28304;
                                            if (_21904 > 0.0500000007450580596923828125)
                                            {
                                                float _21945 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                float _21921 = ((dot(_21471, _283.CameraRight.xyz) * _21945) / _21904) + (0.5 * float(_283.OutputWidth));
                                                float _21934 = (((-dot(_21471, _283.CameraUp.xyz)) * _21945) / _21904) + (0.5 * float(_283.OutputHeight));
                                                vec2 _21935 = vec2(_21921, _21934);
                                                ivec2 _21482 = ivec2(_21935);
                                                ivec2 _22019 = clamp(ivec2(floor(((vec2(_21482) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _22028 = clamp(_22019, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                vec4 _22054 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_22028.x, int((FluidSize.y - 1u) - uint(_22028.y)), 0).xy, 0);
                                                float _22055 = _22054.x;
                                                ivec2 _22064 = clamp(_22019, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _22151 = clamp(_21482, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                vec4 _22103 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22151.x, int((_283.OutputHeight - 1u) - uint(_22151.y)), 0).xy, 0);
                                                float _22104 = _22103.x;
                                                vec2 _28298;
                                                float _28305;
                                                if (((((_21921 >= 0.0) && (_21934 >= 0.0)) && (_21921 < float(_283.OutputWidth))) && (_21934 < float(_283.OutputHeight))) && (((_22055 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_22064.x, int((FluidSize.y - 1u) - uint(_22064.y)), 0).xy, 0).w > 0.5)) && ((((_22104 > 0.0) && (abs(_22104 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22104 - _283.ProjA)) : 1000000000.0) >= (_22055 + 0.0500000007450580596923828125))))
                                                {
                                                    vec2 _21489 = floor(_21935) + vec2(0.5);
                                                    ivec2 _22242 = clamp(ivec2(_21489), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                    vec4 _22194 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22242.x, int((_283.OutputHeight - 1u) - uint(_22242.y)), 0).xy, 0);
                                                    float _22195 = _22194.x;
                                                    float _22212 = ((_22195 > 0.0) && (abs(_22195 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22195 - _283.ProjA)) : 1000000000.0;
                                                    if (_22212 < _21904)
                                                    {
                                                        _28300 = _22212;
                                                        _28293 = _21489;
                                                        _28170 = 2u;
                                                        _28155 = true;
                                                        break;
                                                    }
                                                    _28305 = _22212;
                                                    _28298 = _21489;
                                                }
                                                else
                                                {
                                                    _28305 = _28301;
                                                    _28298 = _28294;
                                                }
                                                _28304 = _28305;
                                                _28297 = _28298;
                                            }
                                            else
                                            {
                                                _28304 = _28301;
                                                _28297 = _28294;
                                            }
                                            _29687 = _28017;
                                            _28303 = _28304;
                                            _28296 = _28297;
                                            int _21549 = _27994 + 1;
                                            _28017 = _29687;
                                            _27994 = _21549;
                                            _28301 = _28303;
                                            _28294 = _28296;
                                            continue;
                                        }
                                        bool _21503 = _27994 == 2;
                                        bool _21505 = _27994 >= 3;
                                        int _21507 = _27994 & 1;
                                        bool _21510 = _21503 || (_21505 && (_21507 == 0));
                                        bool _21512 = _27994 < 3;
                                        int _21514 = _27994 - 3;
                                        float _21519 = _21512 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_21514 / 2)));
                                        if (_21519 >= (_21510 ? _21878 : _21834))
                                        {
                                            _29687 = _28017;
                                            _28303 = _28301;
                                            _28296 = _28294;
                                            int _21549 = _27994 + 1;
                                            _28017 = _29687;
                                            _27994 = _21549;
                                            _28301 = _28303;
                                            _28294 = _28296;
                                            continue;
                                        }
                                        bvec3 _21532 = bvec3(_21510);
                                        vec3 _21536 = _19900 + (vec3(_21532.x ? _27993.x : _27992.x, _21532.y ? _27993.y : _27992.y, _21532.z ? _27993.z : _27992.z) * _21519);
                                        bvec3 _21540 = bvec3(_21510);
                                        vec3 _21541 = vec3(_21540.x ? _27998.x : _28000.x, _21540.y ? _27998.y : _28000.y, _21540.z ? _27998.z : _28000.z);
                                        bool _28014;
                                        vec2 _28130;
                                        float _28134;
                                        do
                                        {
                                            float _22405 = ((_21541.y > 0.5) ? (_283.PoolMin.w - _21536.y) : ((_21541.x > 0.5) ? (_283.PoolMax.x - _21536.x) : ((_21541.x < (-0.5)) ? (_21536.x - _283.PoolMin.x) : ((_21541.z > 0.5) ? (_283.PoolMax.z - _21536.z) : (_21536.z - _283.PoolMin.z))))) - 0.02999999932944774627685546875;
                                            vec2 _28131;
                                            _28131 = vec2(0.5);
                                            float _22311;
                                            float _22353;
                                            vec2 _28005;
                                            float _28006;
                                            bool _28011;
                                            bool _28015;
                                            int _28001 = 0;
                                            float _28002 = 0.0;
                                            float _28003 = 0.0599999986588954925537109375;
                                            float _28135 = 1000000000.0;
                                            SPIRV_CROSS_LOOP
                                            for (;;)
                                            {
                                                if ((_28001 < 40) && (_28002 < _22405))
                                                {
                                                    _22311 = min(_28002 + _28003, _22405);
                                                    bool _28004;
                                                    do
                                                    {
                                                        vec3 _22419 = (_21536 + (_21541 * _22311)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                        float _22477 = dot(_22419, _283.CameraForward.xyz);
                                                        if (_22477 <= 0.0500000007450580596923828125)
                                                        {
                                                            _28006 = 1000000000.0;
                                                            _28005 = vec2(0.5);
                                                            _28004 = false;
                                                            break;
                                                        }
                                                        float _22518 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                        float _22494 = ((dot(_22419, _283.CameraRight.xyz) * _22518) / _22477) + (0.5 * float(_283.OutputWidth));
                                                        float _22507 = (((-dot(_22419, _283.CameraUp.xyz)) * _22518) / _22477) + (0.5 * float(_283.OutputHeight));
                                                        if (!((((_22494 >= 0.0) && (_22507 >= 0.0)) && (_22494 < float(_283.OutputWidth))) && (_22507 < float(_283.OutputHeight))))
                                                        {
                                                            _28006 = 1000000000.0;
                                                            _28005 = vec2(0.5);
                                                            _28004 = false;
                                                            break;
                                                        }
                                                        vec2 _22436 = floor(vec2(_22494, _22507)) + vec2(0.5);
                                                        ivec2 _22602 = clamp(ivec2(_22436), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                        vec4 _22554 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22602.x, int((_283.OutputHeight - 1u) - uint(_22602.y)), 0).xy, 0);
                                                        float _22555 = _22554.x;
                                                        float _22572 = ((_22555 > 0.0) && (abs(_22555 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22555 - _283.ProjA)) : 1000000000.0;
                                                        _28006 = _22572;
                                                        _28005 = _22436;
                                                        _28004 = abs(_22572 - _22477) < ((0.0199999995529651641845703125 * _22477) + 0.02999999932944774627685546875);
                                                        break;
                                                    } while(false);
                                                    if (_28004)
                                                    {
                                                        vec2 _28132;
                                                        float _28136;
                                                        _28136 = _28006;
                                                        _28132 = _28005;
                                                        float _29835;
                                                        float _29836;
                                                        float _29837;
                                                        vec2 _29839;
                                                        int _28010 = 0;
                                                        float _28137 = _28002;
                                                        float _28139 = _22311;
                                                        SPIRV_CROSS_UNROLL
                                                        for (; _28010 < 4; _28139 = _29835, _28137 = _29836, _28136 = _29837, _28132 = _29839, _28010++)
                                                        {
                                                            float _22329 = 0.5 * (_28137 + _28139);
                                                            bool _28140;
                                                            vec2 _28141;
                                                            float _28142;
                                                            do
                                                            {
                                                                vec3 _22646 = (_21536 + (_21541 * _22329)) - vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w);
                                                                float _22704 = dot(_22646, _283.CameraForward.xyz);
                                                                if (_22704 <= 0.0500000007450580596923828125)
                                                                {
                                                                    _28142 = 1000000000.0;
                                                                    _28141 = vec2(0.5);
                                                                    _28140 = false;
                                                                    break;
                                                                }
                                                                float _22745 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                                float _22721 = ((dot(_22646, _283.CameraRight.xyz) * _22745) / _22704) + (0.5 * float(_283.OutputWidth));
                                                                float _22734 = (((-dot(_22646, _283.CameraUp.xyz)) * _22745) / _22704) + (0.5 * float(_283.OutputHeight));
                                                                if (!((((_22721 >= 0.0) && (_22734 >= 0.0)) && (_22721 < float(_283.OutputWidth))) && (_22734 < float(_283.OutputHeight))))
                                                                {
                                                                    _28142 = 1000000000.0;
                                                                    _28141 = vec2(0.5);
                                                                    _28140 = false;
                                                                    break;
                                                                }
                                                                vec2 _22663 = floor(vec2(_22721, _22734)) + vec2(0.5);
                                                                ivec2 _22829 = clamp(ivec2(_22663), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                                                                vec4 _22781 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22829.x, int((_283.OutputHeight - 1u) - uint(_22829.y)), 0).xy, 0);
                                                                float _22782 = _22781.x;
                                                                float _22799 = ((_22782 > 0.0) && (abs(_22782 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_22782 - _283.ProjA)) : 1000000000.0;
                                                                _28142 = _22799;
                                                                _28141 = _22663;
                                                                _28140 = abs(_22799 - _22704) < ((0.0199999995529651641845703125 * _22704) + 0.02999999932944774627685546875);
                                                                break;
                                                            } while(false);
                                                            _29835 = _28140 ? _22329 : _28139;
                                                            _29836 = _28140 ? _28137 : _22329;
                                                            _29837 = _28140 ? _28142 : _28136;
                                                            bvec2 _29838 = bvec2(_28140);
                                                            _29839 = vec2(_29838.x ? _28141.x : _28132.x, _29838.y ? _28141.y : _28132.y);
                                                        }
                                                        _28134 = _28136;
                                                        _28130 = _28132;
                                                        _28015 = true;
                                                        _28011 = true;
                                                        break;
                                                    }
                                                    _22353 = min(_28003 * 1.2000000476837158203125, 0.300000011920928955078125);
                                                    _28003 = _22353;
                                                    _28002 = _22311;
                                                    _28001++;
                                                    _28135 = _28006;
                                                    _28131 = _28005;
                                                    continue;
                                                }
                                                else
                                                {
                                                    _28134 = _28135;
                                                    _28130 = _28131;
                                                    _28015 = _28017;
                                                    _28011 = false;
                                                    break;
                                                }
                                            }
                                            if (_28011)
                                            {
                                                _28014 = _28015;
                                                break;
                                            }
                                            _28014 = false;
                                            break;
                                        } while(false);
                                        if (_28014)
                                        {
                                            _28300 = _28134;
                                            _28293 = _28130;
                                            _28170 = 1u;
                                            _28155 = true;
                                            break;
                                        }
                                        _29687 = _28014;
                                        _28303 = _28134;
                                        _28296 = _28130;
                                        int _21549 = _27994 + 1;
                                        _28017 = _29687;
                                        _27994 = _21549;
                                        _28301 = _28303;
                                        _28294 = _28296;
                                        continue;
                                    }
                                    else
                                    {
                                        _28300 = _28301;
                                        _28293 = _28294;
                                        _28170 = _27361;
                                        _28155 = false;
                                        break;
                                    }
                                }
                                if (_28155)
                                {
                                    _28299 = _28300;
                                    _28292 = _28293;
                                    _28169 = _28170;
                                    break;
                                }
                                _28299 = _28300;
                                _28292 = _28293;
                                _28169 = 0u;
                                break;
                            } while(false);
                            if (_28169 != 0u)
                            {
                                float _22907 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec3 _19921 = vec3(_283.CameraRight.w, _283.CameraUp.w, _283.CameraForward.w) + (((_283.CameraRight.xyz * (((_28292.x - (0.5 * float(_283.OutputWidth))) * _28299) / _22907)) + (_283.CameraUp.xyz * (((-(_28292.y - (0.5 * float(_283.OutputHeight)))) * _28299) / _22907))) + (_283.CameraForward.xyz * _28299));
                                bvec3 _19922 = bvec3(_28169 == 1u);
                                vec3 _19923 = vec3(_19922.x ? _19900.x : _19921.x, _19922.y ? _19900.y : _19921.y, _19922.z ? _19900.z : _19921.z);
                                _28786 = _28292;
                                _28688 = _28299;
                                _28590 = (_28169 == 1u) ? _27979 : length(_19923 - _6147);
                                _28492 = 1.0;
                                _28394 = _19923;
                                break;
                            }
                        }
                        float _19945 = min(_27979, 4.0);
                        _28786 = _28309;
                        _28688 = _28339;
                        _28590 = _19945;
                        _28492 = _28368;
                        _28394 = _6147 + (_21194 * _19945);
                        break;
                    } while(false);
                    vec3 _28886;
                    do
                    {
                        vec2 _23144 = clamp(_28786 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _23155 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23144.x, 1.0 - _23144.y), 0.0).xyz;
                        if ((_28492 < 0.5) || (_28688 > 100000000.0))
                        {
                            _28886 = _23155;
                            break;
                        }
                        ivec2 _22978 = ivec2(_28786);
                        float _22984 = _6147.y - _28394.y;
                        if (_22984 <= 0.00999999977648258209228515625)
                        {
                            _28886 = _23155;
                            break;
                        }
                        ivec2 _23185 = clamp(_22978, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        ivec2 _23273 = clamp(_22978, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec3 _23233 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_23273.x, int((_283.OutputHeight - 1u) - uint(_23273.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _23235 = length(_23233);
                        vec3 _23241 = _23233 / vec3(_23235);
                        bvec3 _23242 = bvec3(_23235 > 0.001000000047497451305389404296875);
                        ivec2 _23333 = clamp(_22978, ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        float _23030 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_23185.x, int((_283.OutputHeight - 1u) - uint(_23185.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_23242.x ? _23241.x : vec3(0.0, 1.0, 0.0).x, _23242.y ? _23241.y : vec3(0.0, 1.0, 0.0).y, _23242.z ? _23241.z : vec3(0.0, 1.0, 0.0).z), _5638), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_23333.x, int((_283.OutputHeight - 1u) - uint(_23333.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _283.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_23155, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _23034 = max(-_29788.y, 0.20000000298023223876953125);
                        float _28885;
                        if ((_283.CausticParams.x > 0.0) && (_23030 > 0.0))
                        {
                            vec2 _23077 = _28394.xz - ((_29788.xz / vec2(_23034)) * _22984);
                            vec2 _23388 = _23077 * (6.283185482025146484375 / max(_283.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _23411 = _23388 + (vec2(sin((_23388.y * 0.730000019073486328125) + ((_283.ShadeParams.z * 0.89999997615814208984375) * _283.CausticParams.z)), sin((_23388.x * 0.61000001430511474609375) - ((_283.ShadeParams.z * 1.10000002384185791015625) * _283.CausticParams.z))) * 0.89999997615814208984375);
                            float _28884;
                            do
                            {
                                if ((_283.GridParams.z < 2u) || (_283.GridParams.w < 2u))
                                {
                                    _28884 = 1.0;
                                    break;
                                }
                                vec2 _23521 = clamp(((_23077 - _283.PoolMin.xz) / max(_283.PoolMax.xz - _283.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)), vec2(0.0), vec2(float(_283.GridParams.z - 1u), float(_283.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _23523 = uvec2(_23521);
                                vec2 _23527 = _23521 - vec2(_23523);
                                uint _23534 = _23523.x + (_23523.y * _283.GridParams.z);
                                _28884 = mix(mix(CausticField_1._data[_23534], CausticField_1._data[_23534 + 1u], _23527.x), mix(CausticField_1._data[_23534 + _283.GridParams.z], CausticField_1._data[(_23534 + _283.GridParams.z) + 1u], _23527.x), _23527.y);
                                break;
                            } while(false);
                            _28885 = min(max(1.0 + ((_283.CausticParams.x * (exp((-_22984) / max(_283.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _22984))) * ((pow(clamp((1.0 - abs(sin(_23411.x + ((_283.ShadeParams.z * 0.60000002384185791015625) * _283.CausticParams.z)) * sin((_23411.y * 1.12999999523162841796875) - ((_283.ShadeParams.z * 0.4000000059604644775390625) * _283.CausticParams.z)))) * (1.0 - abs(sin(((_23411.x + _23411.y) * 0.709999978542327880859375) - ((_283.ShadeParams.z * 0.5) * _283.CausticParams.z)) * sin(((_23411.x - _23411.y) * 0.829999983310699462890625) + ((_283.ShadeParams.z * 0.699999988079071044921875) * _283.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_28884, 0.0500000007450580596923828125), _283.LightParams.y), 2.5);
                        }
                        else
                        {
                            _28885 = 1.0;
                        }
                        _28886 = _23155 * ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _22984) * 1.2000000476837158203125), vec3(_283.LightParams.x)) * (1.0 - _23030)) + ((mix(vec3(1.0), exp(((-_283.FluidTint.xyz) * _22984) / vec3(_23034)), vec3(_283.LightParams.x)) * _23030) * _28885));
                        break;
                    } while(false);
                    vec3 _17861 = exp((-_283.FluidTint.xyz) * max(_28590, 0.0500000007450580596923828125));
                    float _23613 = _283.ScatterParams.z * _283.ScatterParams.z;
                    _28935 = mix(_27826, mix((_28886 * _17861) + ((_283.FluidColor.xyz * ((_6653 * _283.ScatterParams.y) + (_283.SunColor.xyz * (((_283.ScatterParams.x * 0.25) * ((1.0 - _23613) / pow(max((1.0 + _23613) - ((2.0 * _283.ScatterParams.z) * dot(_29788, -_17735)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5638.y, 0.0, 1.0))))) * (vec3(1.0) - _17861)), mix(_27955, _27949, vec3(_27857)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_17809, -_17735), 0.0, 1.0), max(_283.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _6060), 0.0, 1.0)));
                }
                else
                {
                    _28935 = _27826;
                }
                _28934 = _28935;
            }
            else
            {
                _28934 = _27826;
            }
            _28933 = _28934;
        }
        else
        {
            _28933 = _27826;
        }
        float _6213 = clamp((clamp((float(_12983) * 0.0009765625) / _5912, 0.0, 1.0) * _283.ScatterParams.w) + (((_5613 * 0.000244140625) * _283.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _6225 = exp(max(_283.FluidTint.xyz, vec3(0.0)) * (-_6125)) * (1.0 - (0.85000002384185791015625 * _6213));
        vec3 _23642 = ((_283.SunColor.xyz * ((clamp(_5638.y, 0.0, 1.0) * _27133) * 0.3183098733425140380859375)) + _6653) * 0.85000002384185791015625;
        vec3 _23648 = mix(vec3(dot(_23642, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _23642, vec3(0.5));
        float _23696 = _283.ScatterParams.z * _283.ScatterParams.z;
        vec3 _6262 = ((_28933 * _6225) + ((vec3(1.0) - _6225) * mix(_283.FluidColor.xyz * ((_6653 * _283.ScatterParams.y) + (_283.SunColor.xyz * ((((_283.ScatterParams.x * 0.25) * ((1.0 - _23696) / pow(max((1.0 + _23696) - ((2.0 * _283.ScatterParams.z) * dot(_29788, -_13112)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5638.y, 0.0, 1.0)) * _27133))), _23648 * 0.800000011920928955078125, vec3(_6213)))) + (_23648 * (((float(_5618) * 2.2888532839715480804443359375e-05) + (_5613 * 3.6621095205191522836685180664062e-05)) * clamp(_283.FoamParams.z, 0.0, 1.0)));
        if (_283.DebugView == 3u)
        {
            float _6269 = clamp(_6125 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5574), vec4(_6269, _6269, _6269, 1.0));
            break;
        }
        if (_283.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_5574), vec4(_27034.www, 1.0));
            break;
        }
        if (_283.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_5574), vec4(_6262, 1.0));
            break;
        }
        if (_283.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_5574), vec4(clamp((_27083 * 0.0625) + (_5607 * 0.000244140625), 0.0, 1.0), _6213, clamp(_5931 + (_5601 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_283.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_5574), vec4(_27827, 1.0));
            break;
        }
        float _6341 = 0.014999999664723873138427734375 + (2.0 * sqrt(_6007));
        vec3 _6344 = reflect(_5793, _27135);
        vec3 _23730 = ((_283.CameraRight.xyz * _6344.x) + (_283.CameraUp.xyz * _6344.y)) + (_283.CameraForward.xyz * _6344.z);
        float _29141;
        vec3 _29278;
        do
        {
            if (_283.ReflectionDistance <= 0.0)
            {
                _29278 = vec3(0.0);
                _29141 = 0.0;
                break;
            }
            float _23779 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _6344.z);
            if (_23779 <= 0.0)
            {
                _29278 = vec3(0.0);
                _29141 = 0.0;
                break;
            }
            float _23976 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _23788 = max(0.0040000001899898052215576171875 * _27033, 0.00999999977648258209228515625);
            float _23790 = 0.5 * _23788;
            float _23799;
            float _23801;
            bool _29126;
            bool _29131;
            float _29136;
            float _29142;
            vec3 _29279;
            int _29117 = 0;
            float _29118 = _23790;
            float _29119 = _23788;
            float _29137 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_29117 < 80)
                {
                    _23799 = _29118 + _29119;
                    _23801 = _29119 * 1.07000005245208740234375;
                    if (_23799 > _283.ReflectionDistance)
                    {
                        _29279 = vec3(0.0);
                        _29142 = 0.0;
                        _29136 = _29137;
                        _29131 = false;
                        _29126 = true;
                        break;
                    }
                    vec3 _23812 = _9665 + (_6344 * _23799);
                    if (_23812.z <= 0.0500000007450580596923828125)
                    {
                        _29279 = vec3(0.0);
                        _29142 = 0.0;
                        _29136 = _29137;
                        _29131 = false;
                        _29126 = true;
                        break;
                    }
                    float _24016 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _23992 = ((_23812.x * _24016) / _23812.z) + (0.5 * float(_283.OutputWidth));
                    float _24005 = (((-_23812.y) * _24016) / _23812.z) + (0.5 * float(_283.OutputHeight));
                    if (!((((_23992 >= 0.0) && (_24005 >= 0.0)) && (_23992 < float(_283.OutputWidth))) && (_24005 < float(_283.OutputHeight))))
                    {
                        _29279 = vec3(0.0);
                        _29142 = _27595;
                        _29136 = _29137;
                        _29131 = true;
                        _29126 = false;
                        break;
                    }
                    ivec2 _24100 = clamp(ivec2(vec2(_23992, _24005)), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                    vec4 _24052 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24100.x, int((_283.OutputHeight - 1u) - uint(_24100.y)), 0).xy, 0);
                    float _24053 = _24052.x;
                    float _24070 = ((_24053 > 0.0) && (abs(_24053 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24053 - _283.ProjA)) : 1000000000.0;
                    if (((_24070 < 100000000.0) && (_23812.z > _24070)) && ((_23812.z - _24070) < max(_29119 * 2.1400001049041748046875, 0.25)))
                    {
                        float _29121;
                        _29121 = _23799;
                        float _29842;
                        float _29843;
                        int _29120 = 0;
                        float _29124 = _29118;
                        SPIRV_CROSS_UNROLL
                        for (; _29120 < 6; _29124 = _29842, _29121 = _29843, _29120++)
                        {
                            float _23856 = 0.5 * (_29124 + _29121);
                            vec3 _23861 = _9665 + (_6344 * _23856);
                            float _24170 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _24231 = clamp(ivec2(vec2(((_23861.x * _24170) / _23861.z) + (0.5 * float(_283.OutputWidth)), (((-_23861.y) * _24170) / _23861.z) + (0.5 * float(_283.OutputHeight)))), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                            vec4 _24183 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24231.x, int((_283.OutputHeight - 1u) - uint(_24231.y)), 0).xy, 0);
                            float _24184 = _24183.x;
                            bool _23868 = _23861.z > (((_24184 > 0.0) && (abs(_24184 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24184 - _283.ProjA)) : 1000000000.0);
                            _29842 = _23868 ? _29124 : _23856;
                            _29843 = _23868 ? _23856 : _29121;
                        }
                        vec3 _23882 = _9665 + (_6344 * _29121);
                        float _24301 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _24291 = vec2(((_23882.x * _24301) / _23882.z) + (0.5 * float(_283.OutputWidth)), (((-_23882.y) * _24301) / _23882.z) + (0.5 * float(_283.OutputHeight)));
                        ivec2 _24359 = clamp(ivec2(_24291), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
                        vec3 _24319 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_24359.x, int((_283.OutputHeight - 1u) - uint(_24359.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _24321 = length(_24319);
                        vec3 _24327 = _24319 / vec3(_24321);
                        bvec3 _24328 = bvec3(_24321 > 0.001000000047497451305389404296875);
                        vec3 _24329 = vec3(_24328.x ? _24327.x : vec3(0.0, 1.0, 0.0).x, _24328.y ? _24327.y : vec3(0.0, 1.0, 0.0).y, _24328.z ? _24327.z : vec3(0.0, 1.0, 0.0).z);
                        if (dot(vec3(dot(_24329, _283.CameraRight.xyz), dot(_24329, _283.CameraUp.xyz), dot(_24329, _283.CameraForward.xyz)), _6344) > 0.100000001490116119384765625)
                        {
                            int _23909 = _29117 + 1;
                            _29119 = _23801;
                            _29118 = _23799;
                            _29117 = _23909;
                            _29137 = _23799;
                            continue;
                        }
                        float _24423 = clamp(((_6341 * _29121) * _23976) / max(_23882.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _24425 = 0.5 * _24423;
                        vec2 _24510 = clamp(_24291 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24536 = clamp((_24291 + vec2(_24425)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24562 = clamp((_24291 + vec2(_24423 * (-0.5), _24425)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24588 = clamp((_24291 + vec2(_24425, _24423 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24614 = clamp((_24291 + vec2(_24423 * (-0.5), _24423 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24640 = clamp((_24291 + vec2(_24423, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24666 = clamp((_24291 + vec2(-_24423, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24692 = clamp((_24291 + vec2(0.0, _24423)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _24718 = clamp((_24291 + vec2(0.0, -_24423)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _24495 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24510.x, 1.0 - _24510.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24536.x, 1.0 - _24536.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24562.x, 1.0 - _24562.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24588.x, 1.0 - _24588.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24614.x, 1.0 - _24614.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24640.x, 1.0 - _24640.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24666.x, 1.0 - _24666.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24692.x, 1.0 - _24692.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24718.x, 1.0 - _24718.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _29279 = _24495;
                        _29142 = _23779;
                        _29136 = _23799;
                        _29131 = false;
                        _29126 = true;
                        break;
                    }
                    int _23909 = _29117 + 1;
                    _29119 = _23801;
                    _29118 = _23799;
                    _29117 = _23909;
                    _29137 = _23799;
                    continue;
                }
                else
                {
                    _29279 = vec3(0.0);
                    _29142 = _27595;
                    _29136 = _29137;
                    _29131 = false;
                    _29126 = false;
                    break;
                }
            }
            if (_29126)
            {
                _29278 = _29279;
                _29141 = _29142;
                break;
            }
            if ((!_29131) || (_29136 < 0.0))
            {
                _29278 = _29279;
                _29141 = 0.0;
                break;
            }
            vec3 _23924 = _9665 + (_6344 * _29136);
            float _24769 = (0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _24759 = vec2(((_23924.x * _24769) / _23924.z) + (0.5 * float(_283.OutputWidth)), (((-_23924.y) * _24769) / _23924.z) + (0.5 * float(_283.OutputHeight)));
            ivec2 _24830 = clamp(ivec2(_24759), ivec2(0), ivec2(int(_283.OutputWidth) - 1, int(_283.OutputHeight) - 1));
            vec4 _24782 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24830.x, int((_283.OutputHeight - 1u) - uint(_24830.y)), 0).xy, 0);
            float _24783 = _24782.x;
            float _24800 = ((_24783 > 0.0) && (abs(_24783 - _283.ProjA) > 9.999999717180685365747194737196e-10)) ? (_283.ProjB / (_24783 - _283.ProjA)) : 1000000000.0;
            float _24876 = clamp(((_6341 * _29136) * _23976) / max(_23924.z, 0.0500000007450580596923828125), 0.0, 12.0);
            float _24878 = 0.5 * _24876;
            vec2 _24963 = clamp(_24759 / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24989 = clamp((_24759 + vec2(_24878)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25015 = clamp((_24759 + vec2(_24876 * (-0.5), _24878)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25041 = clamp((_24759 + vec2(_24878, _24876 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25067 = clamp((_24759 + vec2(_24876 * (-0.5), _24876 * (-0.5))) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25093 = clamp((_24759 + vec2(_24876, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25119 = clamp((_24759 + vec2(-_24876, 0.0)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25145 = clamp((_24759 + vec2(0.0, _24876)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _25171 = clamp((_24759 + vec2(0.0, -_24876)) / vec2(float(_283.OutputWidth), float(_283.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _24948 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24963.x, 1.0 - _24963.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24989.x, 1.0 - _24989.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25015.x, 1.0 - _25015.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25041.x, 1.0 - _25041.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25067.x, 1.0 - _25067.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25093.x, 1.0 - _25093.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25119.x, 1.0 - _25119.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25145.x, 1.0 - _25145.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25171.x, 1.0 - _25171.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_24800 > 100000000.0)
            {
                _29278 = _24948;
                _29141 = _23779;
                break;
            }
            float _23949 = _24800 - _23924.z;
            if (_23949 <= 0.0)
            {
                _29278 = _24948;
                _29141 = _23779;
                break;
            }
            _29278 = _24948;
            _29141 = _23779 * exp((_23949 / max(_6344.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        vec3 _29284;
        do
        {
            if (_283.EnvParams.x <= 0.0)
            {
                float _25225 = _23730.y;
                _29284 = mix(_283.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_283.SkyHorizon.xyz, _283.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _25225))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _25225)));
                break;
            }
            _29284 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_23730.x, _23730.y, -_23730.z), clamp(clamp(log2(max(_6341 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _283.EnvParams.x;
            break;
        } while(false);
        vec3 _6364 = mix(_29284, _29278, vec3(_29141));
        if (_283.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_5574), vec4(_6364, 1.0));
            break;
        }
        float _6392 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_27085 + _27135), _5795), 0.0, 1.0), max(_283.FresnelPower, 1.0));
        float _6393 = 0.0199999995529651641845703125 + _6392;
        vec3 _6403 = normalize(_283.SunDirection.xyz);
        float _6420 = max((2.0 / ((2.0 / (max(_283.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _6007))) - 2.0, 4.0);
        vec3 _6489 = (mix(_6262, _6364, vec3(clamp(_6393, 0.0, 1.0))) + (((((_283.SunColor.xyz * ((pow(clamp(dot(_27135, normalize(_6403 - _5793)), 0.0, 1.0), _6420) * (_6420 + 8.0)) * 0.0397887341678142547607421875)) * _283.Specular) * _6393) * _27133) * clamp(dot(_27135, _6403) * 4.0, 0.0, 1.0))) + ((_283.SunColor.xyz * _283.FluidColor.xyz) * (((((_283.LightParams.w * 0.60000002384185791015625) * (((1.0 - _6060) * exp(_6055 * (-1.5))) * (1.0 - exp(_6055 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_5638, _13112)), 9.9999997473787516355514526367188e-05), 1.5))) * _27133) * (0.980000019073486328125 - _6392)));
        vec3 _6507 = mix(_6489, (_6489 * 1.25) + (_6653 * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _6050)) * _6060) * _283.LightParams.z, 0.0, 1.0)));
        float _6516 = clamp(1.0 - exp(-((_27083 * 0.07500000298023223876953125) + (_5607 * 0.0003662109375))), 0.0, 1.0);
        vec3 _29407;
        if ((_6516 > 0.0199999995529651641845703125) && (_283.FoamParams.x > 0.0))
        {
            vec2 _25286 = _5937.xz / vec2(max(_283.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _25321 = floor(_25286);
            vec2 _25324 = _25286 - _25321;
            float _29400;
            float _29401;
            _29401 = 8.0;
            _29400 = 8.0;
            float _29416;
            float _29417;
            SPIRV_CROSS_UNROLL
            for (int _29399 = -1; _29399 <= 1; _29401 = _29416, _29400 = _29417, _29399++)
            {
                _29417 = _29400;
                _29416 = _29401;
                float _29845;
                float _29765;
                SPIRV_CROSS_UNROLL
                for (int _29414 = -1; _29414 <= 1; _29417 = _29765, _29416 = _29845, _29414++)
                {
                    vec2 _25339 = vec2(float(_29414), float(_29399));
                    ivec2 _25343 = ivec2(_25321 + _25339);
                    uint _25415 = ((((uint(_25343.x + 4096) * 1973u) + (uint(_25343.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25423 = ((_25415 >> ((_25415 >> 28u) + 4u)) ^ _25415) * 277803737u;
                    uint _25427 = (_25423 >> 22u) ^ _25423;
                    float _25381 = length((_25339 + (vec2(0.5) + (sin(vec2(_283.ShadeParams.z * 0.5) + ((vec2(float(_25427 & 65535u), float(_25427 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25324);
                    bool _25384 = _25381 < _29416;
                    if (_25384)
                    {
                        _29765 = _29416;
                    }
                    else
                    {
                        _29765 = (_25381 < _29417) ? _25381 : _29417;
                    }
                    _29845 = _25384 ? _25381 : _29416;
                }
            }
            vec2 _25293 = (_25286 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _25444 = floor(_25293);
            vec2 _25447 = _25293 - _25444;
            float _29403;
            float _29404;
            _29404 = 8.0;
            _29403 = 8.0;
            float _29410;
            float _29411;
            SPIRV_CROSS_UNROLL
            for (int _29402 = -1; _29402 <= 1; _29404 = _29410, _29403 = _29411, _29402++)
            {
                _29411 = _29403;
                _29410 = _29404;
                float _29847;
                float _29759;
                SPIRV_CROSS_UNROLL
                for (int _29408 = -1; _29408 <= 1; _29411 = _29759, _29410 = _29847, _29408++)
                {
                    vec2 _25462 = vec2(float(_29408), float(_29402));
                    ivec2 _25466 = ivec2(_25444 + _25462);
                    uint _25538 = ((((uint(_25466.x + 4096) * 1973u) + (uint(_25466.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25546 = ((_25538 >> ((_25538 >> 28u) + 4u)) ^ _25538) * 277803737u;
                    uint _25550 = (_25546 >> 22u) ^ _25546;
                    float _25504 = length((_25462 + (vec2(0.5) + (sin(vec2(_283.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_25550 & 65535u), float(_25550 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25447);
                    bool _25507 = _25504 < _29410;
                    if (_25507)
                    {
                        _29759 = _29410;
                    }
                    else
                    {
                        _29759 = (_25504 < _29411) ? _25504 : _29411;
                    }
                    _29847 = _25507 ? _25504 : _29410;
                }
            }
            float _25304 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29400 - _29401), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29403 - _29404))), clamp((((_283.FoamParams.y * ((0.5 * float(_283.OutputHeight)) * max(_283.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_27033, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _29407 = mix(_6507, _23648 * mix(0.85000002384185791015625, 1.0, _25304), vec3(clamp(_6516 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_25304 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_283.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _29407 = _6507;
        }
        float _6562 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _5931) * clamp(_283.Foam * 1.60000002384185791015625, 0.0, 1.0);
        imageStore(OutputTexture, ivec2(_5574), vec4(mix(mix(_29407, _23648, vec3(_6562 * 0.89999997615814208984375)), _23648, vec3(_5661)), max(clamp(_27034.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _6562)), _5661)));
        break;
    } while(false);
}

