#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=4c3a3bf5cb7d46d5f647586cbe7ecf439ccebd76c6e932eb49cf4f09087372ec; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1663[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _21646;
vec2 _21755;
float _22722;

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
} _247;

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
layout(binding = 10) uniform sampler2D SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler;
layout(binding = 8) uniform sampler2D SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler;

uvec2 FluidSize;
float EnvMaxMip;

void main()
{
    do
    {
        if (_247.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _247.OutputWidth) || (gl_GlobalInvocationID.y >= _247.OutputHeight))
        {
            break;
        }
        uvec2 _4725 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _4725.x;
        FluidSize.y = _4725.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _4752 = uvec2(gl_GlobalInvocationID.x, (_247.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _4755 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _4760 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _4774 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _247.OutputWidth)) * 4u;
        uint _4778 = WhitewaterBits_1._data[_4774];
        float _4779 = float(_4778);
        uint _4784 = WhitewaterBits_1._data[_4774 + 1u];
        float _4785 = float(_4784);
        uint _4790 = WhitewaterBits_1._data[_4774 + 2u];
        float _4791 = float(_4790);
        uint _4796 = WhitewaterBits_1._data[_4774 + 3u];
        WhitewaterBits_1._data[_4774] = 0u;
        WhitewaterBits_1._data[_4774 + 1u] = 0u;
        WhitewaterBits_1._data[_4774 + 2u] = 0u;
        WhitewaterBits_1._data[_4774 + 3u] = 0u;
        vec3 _4814 = normalize(_247.SunDirection.xyz);
        vec3 _4816 = normalize(((_247.CameraRight.xyz * _4814.x) + (_247.CameraUp.xyz * _4814.y)) + (_247.CameraForward.xyz * _4814.z));
        vec3 _4819 = refract(-_4816, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _22685 = bvec3((dot(_4819, _4819) < 9.9999999747524270787835121154785e-07) || (_4816.y <= 0.0));
        vec3 _22686 = vec3(_22685.x ? vec3(0.0, -1.0, 0.0).x : _4819.x, _22685.y ? vec3(0.0, -1.0, 0.0).y : _4819.y, _22685.z ? vec3(0.0, -1.0, 0.0).z : _4819.z);
        float _5762 = max(EnvMaxMip - 1.0, 0.0);
        vec3 _21483;
        do
        {
            if (_247.EnvParams.x <= 0.0)
            {
                _21483 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                break;
            }
            _21483 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_5762, 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
            break;
        } while(false);
        vec3 _21484;
        do
        {
            if (_247.EnvParams.x <= 0.0)
            {
                _21484 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _21484 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_5762, 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
            break;
        } while(false);
        vec3 _21486;
        do
        {
            if (_247.EnvParams.x <= 0.0)
            {
                _21486 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _21486 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_5762, 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
            break;
        } while(false);
        vec3 _21488;
        do
        {
            if (_247.EnvParams.x <= 0.0)
            {
                _21488 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _21488 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_5762, 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
            break;
        } while(false);
        vec3 _21490;
        do
        {
            if (_247.EnvParams.x <= 0.0)
            {
                _21490 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _21490 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_5762, 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
            break;
        } while(false);
        vec3 _5784 = (((((_21483 * 2.0) + _21484) + _21486) + _21488) + _21490) * vec3(0.16666667163372039794921875);
        float _4839 = clamp(1.0 - exp(_4779 * (-0.0006103515625)), 0.0, 1.0) * clamp(_247.FoamParams.w, 0.0, 1.0);
        float _21494;
        vec4 _21495;
        do
        {
            vec2 _6137 = ((_4760 * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight))) - vec2(0.5);
            ivec2 _6140 = ivec2(floor(_6137));
            vec2 _6144 = _6137 - vec2(_6140);
            ivec2 _6319 = clamp(_6140, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6355 = clamp(_6140 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6391 = clamp(_6140 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6427 = clamp(_6140 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6180 = vec4((1.0 - _6144.x) * (1.0 - _6144.y), _6144.x * (1.0 - _6144.y), (1.0 - _6144.x) * _6144.y, _6144.x * _6144.y);
            vec4 _6107 = _6180;
            vec4 _6185 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6319.x, int((FluidSize.y - 1u) - uint(_6319.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6355.x, int((FluidSize.y - 1u) - uint(_6355.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6391.x, int((FluidSize.y - 1u) - uint(_6391.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6427.x, int((FluidSize.y - 1u) - uint(_6427.y)), 0).xy, 0).x);
            vec4 _6108 = _6185;
            float _22567;
            _22567 = 1000000000.0;
            float _22684;
            SPIRV_CROSS_UNROLL
            for (int _21492 = 0; _21492 < 4; _22567 = _22684, _21492++)
            {
                if (_6108[_21492] > 0.0)
                {
                    _22684 = min(_22567, _6108[_21492]);
                }
                else
                {
                    _22684 = _22567;
                }
            }
            vec4 _6111 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _21493 = 0; _21493 < 4; )
            {
                _6111[_21493] = ((_6108[_21493] > 0.0) && (_6108[_21493] < (_22567 + max(0.25, 0.02999999932944774627685546875 * _22567)))) ? _6107[_21493] : 0.0;
                _21493++;
                continue;
            }
            float _6245 = ((_6111.x + _6111.y) + _6111.z) + _6111.w;
            if (_6245 <= 9.9999997473787516355514526367188e-05)
            {
                _21495 = vec4(0.0);
                _21494 = 0.0;
                break;
            }
            ivec2 _6463 = clamp(_6140, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6489 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6463.x, int((FluidSize.y - 1u) - uint(_6463.y)), 0).xy, 0);
            ivec2 _6498 = clamp(_6140 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6524 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6498.x, int((FluidSize.y - 1u) - uint(_6498.y)), 0).xy, 0);
            ivec2 _6533 = clamp(_6140 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6559 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6533.x, int((FluidSize.y - 1u) - uint(_6533.y)), 0).xy, 0);
            ivec2 _6568 = clamp(_6140 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6594 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6568.x, int((FluidSize.y - 1u) - uint(_6568.y)), 0).xy, 0);
            _21495 = vec4((((((_6489.xyz * _6111.x) + (_6524.xyz * _6111.y)) + (_6559.xyz * _6111.z)) + (_6594.xyz * _6111.w)) / vec3(_6245)).x, _22722, _22722, dot(_6180, vec4(_6489.w, _6524.w, _6559.w, _6594.w)));
            _21494 = dot(_6111, _6185) / _6245;
            break;
        } while(false);
        ivec2 _6656 = clamp(_4755, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
        vec4 _6607 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_6656.x, int((_247.OutputHeight - 1u) - uint(_6656.y)), 0).xy, 0);
        float _6608 = _6607.x;
        float _6625 = ((_6608 > 0.0) && (abs(_6608 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_6608 - _247.ProjA)) : 1000000000.0;
        if (!(((_21494 > 0.0) && (_21495.w > 0.001000000047497451305389404296875)) && (_21494 < (_6625 - 0.00200000009499490261077880859375))))
        {
            vec2 _6701 = clamp(_4760 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _6712 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_6701.x, 1.0 - _6701.y), 0.0).xyz;
            float _22552;
            vec3 _22563;
            if ((_6625 < 100000000.0) && (_247.WetParams.w > 0.0))
            {
                float _6760 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _4874 = vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w) + (((_247.CameraRight.xyz * (((_4760.x - (0.5 * float(_247.OutputWidth))) * _6625) / _6760)) + (_247.CameraUp.xyz * (((-(_4760.y - (0.5 * float(_247.OutputHeight)))) * _6625) / _6760))) + (_247.CameraForward.xyz * _6625));
                ivec2 _6840 = clamp(_4755, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                vec3 _6800 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_6840.x, int((_247.OutputHeight - 1u) - uint(_6840.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _6802 = length(_6800);
                vec3 _6808 = _6800 / vec3(_6802);
                bvec3 _6809 = bvec3(_6802 > 0.001000000047497451305389404296875);
                float _22553;
                vec3 _22564;
                if ((vec3(_6809.x ? _6808.x : vec3(0.0, 1.0, 0.0).x, _6809.y ? _6808.y : vec3(0.0, 1.0, 0.0).y, _6809.z ? _6808.z : vec3(0.0, 1.0, 0.0).z).y > 0.60000002384185791015625) && (_4874.y > (_247.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _22494;
                    do
                    {
                        int _6893 = int(_247.GridParams.x);
                        int _6896 = int(_247.GridParams.y);
                        if (((_6893 < 4) || (_6896 < 4)) || (_247.WetParams.z <= 0.0))
                        {
                            _22494 = 0.0;
                            break;
                        }
                        vec2 _6918 = ((_4874.xz - _247.WetParams.xy) / vec2(_247.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_6918, vec2(-1.0))) || (_6918.x > float(_6893))) || (_6918.y > float(_6896)))
                        {
                            _22494 = 0.0;
                            break;
                        }
                        ivec2 _6938 = ivec2(floor(_6918));
                        vec2 _6942 = _6918 - vec2(_6938);
                        float _6944 = _6942.x;
                        float _7025 = _6944 * _6944;
                        float _7028 = _7025 * _6944;
                        vec4 _6878 = vec4(((1.0 - (3.0 * _6944)) + (3.0 * _7025)) - _7028, (4.0 - (6.0 * _7025)) + (3.0 * _7028), ((1.0 + (3.0 * _6944)) + (3.0 * _7025)) - (3.0 * _7028), _7028) * 0.16666667163372039794921875;
                        float _6947 = _6942.y;
                        float _7061 = _6947 * _6947;
                        float _7064 = _7061 * _6947;
                        vec4 _6880 = vec4(((1.0 - (3.0 * _6947)) + (3.0 * _7061)) - _7064, (4.0 - (6.0 * _7061)) + (3.0 * _7064), ((1.0 + (3.0 * _6947)) + (3.0 * _7061)) - (3.0 * _7064), _7064) * 0.16666667163372039794921875;
                        float _22487;
                        _22487 = 0.0;
                        float _22670;
                        SPIRV_CROSS_UNROLL
                        for (int _22486 = 0; _22486 < 4; _22487 = _22670, _22486++)
                        {
                            int _6958 = (_6938.y - 1) + _22486;
                            if ((_6958 < 0) || (_6958 >= _6896))
                            {
                                _22670 = _22487;
                                continue;
                            }
                            float _22490;
                            _22490 = 0.0;
                            float _22672;
                            SPIRV_CROSS_UNROLL
                            for (int _22488 = 0; _22488 < 4; _22490 = _22672, _22488++)
                            {
                                int _6976 = (_6938.x - 1) + _22488;
                                if ((_6976 >= 0) && (_6976 < _6893))
                                {
                                    _22672 = _22490 + (_6878[_22488] * float(Wetness_1._data[uint(_6976 + (_6958 * _6893))]));
                                }
                                else
                                {
                                    _22672 = _22490;
                                }
                            }
                            _22670 = _22487 + (_6880[_22486] * _22490);
                        }
                        _22494 = _22487 * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _4896 = clamp(((_22494 * _247.WetParams.w) - 0.00999999977648258209228515625) * 1.01010096073150634765625, 0.0, 1.0);
                    vec3 _22565;
                    if (_4896 > 0.001000000047497451305389404296875)
                    {
                        vec3 _4903 = normalize(_4874 - vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w));
                        vec3 _22551;
                        do
                        {
                            float _7127 = smoothstep(0.449999988079071044921875, 1.0, _4896);
                            vec3 _7131 = _6712 * mix(1.0, 0.5, clamp(_4896 * 2.0, 0.0, 1.0));
                            if (_7127 <= 0.0)
                            {
                                _22551 = _7131;
                                break;
                            }
                            vec3 _7149 = reflect(_4903, vec3(0.0, 1.0, 0.0));
                            float _7208 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec3 _7198 = vec3(((_4760.x - (0.5 * float(_247.OutputWidth))) * _6625) / _7208, ((-(_4760.y - (0.5 * float(_247.OutputHeight)))) * _6625) / _7208, _6625);
                            float _7225 = dot(_7149, _247.CameraForward.xyz);
                            vec3 _7226 = vec3(dot(_7149, _247.CameraRight.xyz), dot(_7149, _247.CameraUp.xyz), _7225);
                            float _22524;
                            vec3 _22544;
                            do
                            {
                                if (_247.ReflectionDistance <= 0.0)
                                {
                                    _22544 = vec3(0.0);
                                    _22524 = 0.0;
                                    break;
                                }
                                float _7275 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _7225);
                                if (_7275 <= 0.0)
                                {
                                    _22544 = vec3(0.0);
                                    _22524 = 0.0;
                                    break;
                                }
                                float _7472 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _7284 = max(0.0040000001899898052215576171875 * _6625, 0.00999999977648258209228515625);
                                float _7286 = 0.5 * _7284;
                                float _7295;
                                float _7297;
                                bool _22509;
                                bool _22514;
                                float _22519;
                                float _22525;
                                vec3 _22545;
                                int _22500 = 0;
                                float _22501 = _7286;
                                float _22502 = _7284;
                                float _22520 = -1.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_22500 < 80)
                                    {
                                        _7295 = _22501 + _22502;
                                        _7297 = _22502 * 1.07000005245208740234375;
                                        if (_7295 > _247.ReflectionDistance)
                                        {
                                            _22545 = vec3(0.0);
                                            _22525 = 0.0;
                                            _22519 = _22520;
                                            _22514 = false;
                                            _22509 = true;
                                            break;
                                        }
                                        vec3 _7308 = _7198 + (_7226 * _7295);
                                        if (_7308.z <= 0.0500000007450580596923828125)
                                        {
                                            _22545 = vec3(0.0);
                                            _22525 = 0.0;
                                            _22519 = _22520;
                                            _22514 = false;
                                            _22509 = true;
                                            break;
                                        }
                                        float _7512 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        float _7488 = ((_7308.x * _7512) / _7308.z) + (0.5 * float(_247.OutputWidth));
                                        float _7501 = (((-_7308.y) * _7512) / _7308.z) + (0.5 * float(_247.OutputHeight));
                                        if (!((((_7488 >= 0.0) && (_7501 >= 0.0)) && (_7488 < float(_247.OutputWidth))) && (_7501 < float(_247.OutputHeight))))
                                        {
                                            _22545 = vec3(0.0);
                                            _22525 = _21646;
                                            _22519 = _22520;
                                            _22514 = true;
                                            _22509 = false;
                                            break;
                                        }
                                        ivec2 _7596 = clamp(ivec2(vec2(_7488, _7501)), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                        vec4 _7548 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7596.x, int((_247.OutputHeight - 1u) - uint(_7596.y)), 0).xy, 0);
                                        float _7549 = _7548.x;
                                        float _7566 = ((_7549 > 0.0) && (abs(_7549 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_7549 - _247.ProjA)) : 1000000000.0;
                                        if (((_7566 < 100000000.0) && (_7308.z > _7566)) && ((_7308.z - _7566) < max(_22502 * 2.1400001049041748046875, 0.25)))
                                        {
                                            float _22504;
                                            _22504 = _7295;
                                            float _22687;
                                            float _22688;
                                            int _22503 = 0;
                                            float _22507 = _22501;
                                            SPIRV_CROSS_UNROLL
                                            for (; _22503 < 6; _22507 = _22687, _22504 = _22688, _22503++)
                                            {
                                                float _7352 = 0.5 * (_22507 + _22504);
                                                vec3 _7357 = _7198 + (_7226 * _7352);
                                                float _7666 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                ivec2 _7727 = clamp(ivec2(vec2(((_7357.x * _7666) / _7357.z) + (0.5 * float(_247.OutputWidth)), (((-_7357.y) * _7666) / _7357.z) + (0.5 * float(_247.OutputHeight)))), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                                vec4 _7679 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7727.x, int((_247.OutputHeight - 1u) - uint(_7727.y)), 0).xy, 0);
                                                float _7680 = _7679.x;
                                                bool _7364 = _7357.z > (((_7680 > 0.0) && (abs(_7680 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_7680 - _247.ProjA)) : 1000000000.0);
                                                _22687 = _7364 ? _22507 : _7352;
                                                _22688 = _7364 ? _7352 : _22504;
                                            }
                                            vec3 _7378 = _7198 + (_7226 * _22504);
                                            float _7797 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            vec2 _7787 = vec2(((_7378.x * _7797) / _7378.z) + (0.5 * float(_247.OutputWidth)), (((-_7378.y) * _7797) / _7378.z) + (0.5 * float(_247.OutputHeight)));
                                            ivec2 _7855 = clamp(ivec2(_7787), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                            vec3 _7815 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_7855.x, int((_247.OutputHeight - 1u) - uint(_7855.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                            float _7817 = length(_7815);
                                            vec3 _7823 = _7815 / vec3(_7817);
                                            bvec3 _7824 = bvec3(_7817 > 0.001000000047497451305389404296875);
                                            vec3 _7825 = vec3(_7824.x ? _7823.x : vec3(0.0, 1.0, 0.0).x, _7824.y ? _7823.y : vec3(0.0, 1.0, 0.0).y, _7824.z ? _7823.z : vec3(0.0, 1.0, 0.0).z);
                                            if (dot(vec3(dot(_7825, _247.CameraRight.xyz), dot(_7825, _247.CameraUp.xyz), dot(_7825, _247.CameraForward.xyz)), _7226) > 0.100000001490116119384765625)
                                            {
                                                int _7405 = _22500 + 1;
                                                _22502 = _7297;
                                                _22501 = _7295;
                                                _22500 = _7405;
                                                _22520 = _7295;
                                                continue;
                                            }
                                            float _7919 = clamp(((0.039999999105930328369140625 * _22504) * _7472) / max(_7378.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                            float _7921 = 0.5 * _7919;
                                            vec2 _8006 = clamp(_7787 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8032 = clamp((_7787 + vec2(_7921)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8058 = clamp((_7787 + vec2(_7919 * (-0.5), _7921)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8084 = clamp((_7787 + vec2(_7921, _7919 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8110 = clamp((_7787 + vec2(_7919 * (-0.5), _7919 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8136 = clamp((_7787 + vec2(_7919, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8162 = clamp((_7787 + vec2(-_7919, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8188 = clamp((_7787 + vec2(0.0, _7919)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8214 = clamp((_7787 + vec2(0.0, -_7919)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec3 _7991 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8006.x, 1.0 - _8006.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8032.x, 1.0 - _8032.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8058.x, 1.0 - _8058.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8084.x, 1.0 - _8084.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8110.x, 1.0 - _8110.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8136.x, 1.0 - _8136.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8162.x, 1.0 - _8162.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8188.x, 1.0 - _8188.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8214.x, 1.0 - _8214.y), 0.0).xyz) * 0.100000001490116119384765625;
                                            _22545 = _7991;
                                            _22525 = _7275;
                                            _22519 = _7295;
                                            _22514 = false;
                                            _22509 = true;
                                            break;
                                        }
                                        int _7405 = _22500 + 1;
                                        _22502 = _7297;
                                        _22501 = _7295;
                                        _22500 = _7405;
                                        _22520 = _7295;
                                        continue;
                                    }
                                    else
                                    {
                                        _22545 = vec3(0.0);
                                        _22525 = _21646;
                                        _22519 = _22520;
                                        _22514 = false;
                                        _22509 = false;
                                        break;
                                    }
                                }
                                if (_22509)
                                {
                                    _22544 = _22545;
                                    _22524 = _22525;
                                    break;
                                }
                                if ((!_22514) || (_22519 < 0.0))
                                {
                                    _22544 = _22545;
                                    _22524 = 0.0;
                                    break;
                                }
                                vec3 _7420 = _7198 + (_7226 * _22519);
                                float _8265 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec2 _8255 = vec2(((_7420.x * _8265) / _7420.z) + (0.5 * float(_247.OutputWidth)), (((-_7420.y) * _8265) / _7420.z) + (0.5 * float(_247.OutputHeight)));
                                ivec2 _8326 = clamp(ivec2(_8255), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                vec4 _8278 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8326.x, int((_247.OutputHeight - 1u) - uint(_8326.y)), 0).xy, 0);
                                float _8279 = _8278.x;
                                float _8296 = ((_8279 > 0.0) && (abs(_8279 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_8279 - _247.ProjA)) : 1000000000.0;
                                float _8372 = clamp(((0.039999999105930328369140625 * _22519) * _7472) / max(_7420.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                float _8374 = 0.5 * _8372;
                                vec2 _8459 = clamp(_8255 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8485 = clamp((_8255 + vec2(_8374)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8511 = clamp((_8255 + vec2(_8372 * (-0.5), _8374)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8537 = clamp((_8255 + vec2(_8374, _8372 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8563 = clamp((_8255 + vec2(_8372 * (-0.5), _8372 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8589 = clamp((_8255 + vec2(_8372, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8615 = clamp((_8255 + vec2(-_8372, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8641 = clamp((_8255 + vec2(0.0, _8372)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _8667 = clamp((_8255 + vec2(0.0, -_8372)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec3 _8444 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8459.x, 1.0 - _8459.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8485.x, 1.0 - _8485.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8511.x, 1.0 - _8511.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8537.x, 1.0 - _8537.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8563.x, 1.0 - _8563.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8589.x, 1.0 - _8589.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8615.x, 1.0 - _8615.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8641.x, 1.0 - _8641.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8667.x, 1.0 - _8667.y), 0.0).xyz) * 0.100000001490116119384765625;
                                if (_8296 > 100000000.0)
                                {
                                    _22544 = _8444;
                                    _22524 = _7275;
                                    break;
                                }
                                float _7445 = _8296 - _7420.z;
                                if (_7445 <= 0.0)
                                {
                                    _22544 = _8444;
                                    _22524 = _7275;
                                    break;
                                }
                                _22544 = _8444;
                                _22524 = _7275 * exp((_7445 / max(_7225, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                                break;
                            } while(false);
                            vec3 _22550;
                            do
                            {
                                if (_247.EnvParams.x <= 0.0)
                                {
                                    float _8721 = _7149.y;
                                    _22550 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _8721))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _8721)));
                                    break;
                                }
                                _22550 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_7149.x, _7149.y, -_7149.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
                                break;
                            } while(false);
                            _22551 = mix(_7131, mix(_22550, _22544, vec3(_22524)), vec3((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp((-_4903).y, 0.0, 1.0), 5.0))) * _7127));
                            break;
                        } while(false);
                        _22565 = _22551;
                    }
                    else
                    {
                        _22565 = _6712;
                    }
                    _22564 = _22565;
                    _22553 = _4896;
                }
                else
                {
                    _22564 = _6712;
                    _22553 = 0.0;
                }
                _22563 = _22564;
                _22552 = _22553;
            }
            else
            {
                _22563 = _6712;
                _22552 = 0.0;
            }
            if (_247.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_4752), vec4(_22552, _22552, _22552, 1.0));
                break;
            }
            if ((_22552 <= 0.001000000047497451305389404296875) && (_4839 <= 0.001000000047497451305389404296875))
            {
                if (_247.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_4752), vec4(0.0));
                }
                break;
            }
            vec3 _8757 = ((_247.SunColor.xyz * (clamp(_4816.y, 0.0, 1.0) * 0.3183098733425140380859375)) + _5784) * 0.85000002384185791015625;
            vec3 _8763 = mix(vec3(dot(_8757, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _8757, vec3(0.5));
            if (_22552 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_4752), vec4(mix(_22563, _8763, vec3(_4839)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_4752), vec4(_8763, _4839));
            }
            break;
        }
        float _8806 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _8782 = ((_4760.x - (0.5 * float(_247.OutputWidth))) * _21494) / _8806;
        float _8794 = ((-(_4760.y - (0.5 * float(_247.OutputHeight)))) * _21494) / _8806;
        vec3 _8796 = vec3(_8782, _8794, _21494);
        vec3 _4971 = normalize(_8796);
        vec3 _4973 = -_4971;
        int _4989 = clamp(int((_247.NormalStep * max(float(_247.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _4995 = (2.0 * max(_247.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_247.NormalStep, 1.0);
        ivec2 _4997 = ivec2(_4989, 0);
        vec3 _21502;
        do
        {
            float _21498;
            do
            {
                vec2 _8964 = (((vec2(clamp(_4755 + _4997, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight))) - vec2(0.5);
                ivec2 _8967 = ivec2(floor(_8964));
                vec2 _8971 = _8964 - vec2(_8967);
                ivec2 _9146 = clamp(_8967, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9182 = clamp(_8967 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9218 = clamp(_8967 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9254 = clamp(_8967 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _8934 = vec4((1.0 - _8971.x) * (1.0 - _8971.y), _8971.x * (1.0 - _8971.y), (1.0 - _8971.x) * _8971.y, _8971.x * _8971.y);
                vec4 _9012 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9146.x, int((FluidSize.y - 1u) - uint(_9146.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9182.x, int((FluidSize.y - 1u) - uint(_9182.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9218.x, int((FluidSize.y - 1u) - uint(_9218.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9254.x, int((FluidSize.y - 1u) - uint(_9254.y)), 0).xy, 0).x);
                vec4 _8935 = _9012;
                float _22484;
                _22484 = 1000000000.0;
                float _22669;
                SPIRV_CROSS_UNROLL
                for (int _21496 = 0; _21496 < 4; _22484 = _22669, _21496++)
                {
                    if (_8935[_21496] > 0.0)
                    {
                        _22669 = min(_22484, _8935[_21496]);
                    }
                    else
                    {
                        _22669 = _22484;
                    }
                }
                vec4 _8938 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _21497 = 0; _21497 < 4; )
                {
                    _8938[_21497] = ((_8935[_21497] > 0.0) && (_8935[_21497] < (_22484 + max(0.25, 0.02999999932944774627685546875 * _22484)))) ? _8934[_21497] : 0.0;
                    _21497++;
                    continue;
                }
                float _9072 = ((_8938.x + _8938.y) + _8938.z) + _8938.w;
                if (_9072 <= 9.9999997473787516355514526367188e-05)
                {
                    _21498 = 0.0;
                    break;
                }
                _21498 = dot(_8938, _9012) / _9072;
                break;
            } while(false);
            float _21501;
            do
            {
                vec2 _9494 = (((vec2(clamp(_4755 - _4997, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight))) - vec2(0.5);
                ivec2 _9497 = ivec2(floor(_9494));
                vec2 _9501 = _9494 - vec2(_9497);
                ivec2 _9676 = clamp(_9497, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9712 = clamp(_9497 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9748 = clamp(_9497 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9784 = clamp(_9497 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _9464 = vec4((1.0 - _9501.x) * (1.0 - _9501.y), _9501.x * (1.0 - _9501.y), (1.0 - _9501.x) * _9501.y, _9501.x * _9501.y);
                vec4 _9542 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9676.x, int((FluidSize.y - 1u) - uint(_9676.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9712.x, int((FluidSize.y - 1u) - uint(_9712.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9748.x, int((FluidSize.y - 1u) - uint(_9748.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9784.x, int((FluidSize.y - 1u) - uint(_9784.y)), 0).xy, 0).x);
                vec4 _9465 = _9542;
                float _22481;
                _22481 = 1000000000.0;
                float _22668;
                SPIRV_CROSS_UNROLL
                for (int _21499 = 0; _21499 < 4; _22481 = _22668, _21499++)
                {
                    if (_9465[_21499] > 0.0)
                    {
                        _22668 = min(_22481, _9465[_21499]);
                    }
                    else
                    {
                        _22668 = _22481;
                    }
                }
                vec4 _9468 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _21500 = 0; _21500 < 4; )
                {
                    _9468[_21500] = ((_9465[_21500] > 0.0) && (_9465[_21500] < (_22481 + max(0.25, 0.02999999932944774627685546875 * _22481)))) ? _9464[_21500] : 0.0;
                    _21500++;
                    continue;
                }
                float _9602 = ((_9468.x + _9468.y) + _9468.z) + _9468.w;
                if (_9602 <= 9.9999997473787516355514526367188e-05)
                {
                    _21501 = 0.0;
                    break;
                }
                _21501 = dot(_9468, _9542) / _9602;
                break;
            } while(false);
            bool _8840 = (_21498 > 0.0) && (abs(_21498 - _21494) < _4995);
            bool _8849 = (_21501 > 0.0) && (abs(_21494 - _21501) < _4995);
            vec2 _8856 = vec2(_4755 + _4997) + vec2(0.5);
            float _9990 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _8860 = vec3(((_8856.x - (0.5 * float(_247.OutputWidth))) * _21498) / _9990, ((-(_8856.y - (0.5 * float(_247.OutputHeight)))) * _21498) / _9990, _21498) - _8796;
            bvec3 _8861 = bvec3(_8840);
            vec3 _8862 = vec3(_8861.x ? _8860.x : vec3(0.0).x, _8861.y ? _8860.y : vec3(0.0).y, _8861.z ? _8860.z : vec3(0.0).z);
            vec2 _8870 = vec2(_4755 - _4997) + vec2(0.5);
            float _10029 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _8873 = _8796 - vec3(((_8870.x - (0.5 * float(_247.OutputWidth))) * _21501) / _10029, ((-(_8870.y - (0.5 * float(_247.OutputHeight)))) * _21501) / _10029, _21501);
            bvec3 _8874 = bvec3(_8849);
            vec3 _8875 = vec3(_8874.x ? _8873.x : vec3(0.0).x, _8874.y ? _8873.y : vec3(0.0).y, _8874.z ? _8873.z : vec3(0.0).z);
            if (_8840 && _8849)
            {
                _21502 = (_8862 + _8875) * 0.5;
                break;
            }
            bvec3 _8888 = bvec3(_8840);
            _21502 = vec3(_8888.x ? _8862.x : _8875.x, _8888.y ? _8862.y : _8875.y, _8888.z ? _8862.z : _8875.z);
            break;
        } while(false);
        ivec2 _5004 = ivec2(0, _4989);
        vec3 _21509;
        do
        {
            float _21505;
            do
            {
                vec2 _10187 = (((vec2(clamp(_4755 + _5004, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight))) - vec2(0.5);
                ivec2 _10190 = ivec2(floor(_10187));
                vec2 _10194 = _10187 - vec2(_10190);
                ivec2 _10369 = clamp(_10190, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10405 = clamp(_10190 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10441 = clamp(_10190 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10477 = clamp(_10190 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10157 = vec4((1.0 - _10194.x) * (1.0 - _10194.y), _10194.x * (1.0 - _10194.y), (1.0 - _10194.x) * _10194.y, _10194.x * _10194.y);
                vec4 _10235 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10369.x, int((FluidSize.y - 1u) - uint(_10369.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10405.x, int((FluidSize.y - 1u) - uint(_10405.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10441.x, int((FluidSize.y - 1u) - uint(_10441.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10477.x, int((FluidSize.y - 1u) - uint(_10477.y)), 0).xy, 0).x);
                vec4 _10158 = _10235;
                float _22478;
                _22478 = 1000000000.0;
                float _22667;
                SPIRV_CROSS_UNROLL
                for (int _21503 = 0; _21503 < 4; _22478 = _22667, _21503++)
                {
                    if (_10158[_21503] > 0.0)
                    {
                        _22667 = min(_22478, _10158[_21503]);
                    }
                    else
                    {
                        _22667 = _22478;
                    }
                }
                vec4 _10161 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _21504 = 0; _21504 < 4; )
                {
                    _10161[_21504] = ((_10158[_21504] > 0.0) && (_10158[_21504] < (_22478 + max(0.25, 0.02999999932944774627685546875 * _22478)))) ? _10157[_21504] : 0.0;
                    _21504++;
                    continue;
                }
                float _10295 = ((_10161.x + _10161.y) + _10161.z) + _10161.w;
                if (_10295 <= 9.9999997473787516355514526367188e-05)
                {
                    _21505 = 0.0;
                    break;
                }
                _21505 = dot(_10161, _10235) / _10295;
                break;
            } while(false);
            float _21508;
            do
            {
                vec2 _10717 = (((vec2(clamp(_4755 - _5004, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight))) - vec2(0.5);
                ivec2 _10720 = ivec2(floor(_10717));
                vec2 _10724 = _10717 - vec2(_10720);
                ivec2 _10899 = clamp(_10720, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10935 = clamp(_10720 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10971 = clamp(_10720 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11007 = clamp(_10720 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10687 = vec4((1.0 - _10724.x) * (1.0 - _10724.y), _10724.x * (1.0 - _10724.y), (1.0 - _10724.x) * _10724.y, _10724.x * _10724.y);
                vec4 _10765 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10899.x, int((FluidSize.y - 1u) - uint(_10899.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10935.x, int((FluidSize.y - 1u) - uint(_10935.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10971.x, int((FluidSize.y - 1u) - uint(_10971.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11007.x, int((FluidSize.y - 1u) - uint(_11007.y)), 0).xy, 0).x);
                vec4 _10688 = _10765;
                float _22475;
                _22475 = 1000000000.0;
                float _22666;
                SPIRV_CROSS_UNROLL
                for (int _21506 = 0; _21506 < 4; _22475 = _22666, _21506++)
                {
                    if (_10688[_21506] > 0.0)
                    {
                        _22666 = min(_22475, _10688[_21506]);
                    }
                    else
                    {
                        _22666 = _22475;
                    }
                }
                vec4 _10691 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _21507 = 0; _21507 < 4; )
                {
                    _10691[_21507] = ((_10688[_21507] > 0.0) && (_10688[_21507] < (_22475 + max(0.25, 0.02999999932944774627685546875 * _22475)))) ? _10687[_21507] : 0.0;
                    _21507++;
                    continue;
                }
                float _10825 = ((_10691.x + _10691.y) + _10691.z) + _10691.w;
                if (_10825 <= 9.9999997473787516355514526367188e-05)
                {
                    _21508 = 0.0;
                    break;
                }
                _21508 = dot(_10691, _10765) / _10825;
                break;
            } while(false);
            bool _10063 = (_21505 > 0.0) && (abs(_21505 - _21494) < _4995);
            bool _10072 = (_21508 > 0.0) && (abs(_21494 - _21508) < _4995);
            vec2 _10079 = vec2(_4755 + _5004) + vec2(0.5);
            float _11213 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10083 = vec3(((_10079.x - (0.5 * float(_247.OutputWidth))) * _21505) / _11213, ((-(_10079.y - (0.5 * float(_247.OutputHeight)))) * _21505) / _11213, _21505) - _8796;
            bvec3 _10084 = bvec3(_10063);
            vec3 _10085 = vec3(_10084.x ? _10083.x : vec3(0.0).x, _10084.y ? _10083.y : vec3(0.0).y, _10084.z ? _10083.z : vec3(0.0).z);
            vec2 _10093 = vec2(_4755 - _5004) + vec2(0.5);
            float _11252 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10096 = _8796 - vec3(((_10093.x - (0.5 * float(_247.OutputWidth))) * _21508) / _11252, ((-(_10093.y - (0.5 * float(_247.OutputHeight)))) * _21508) / _11252, _21508);
            bvec3 _10097 = bvec3(_10072);
            vec3 _10098 = vec3(_10097.x ? _10096.x : vec3(0.0).x, _10097.y ? _10096.y : vec3(0.0).y, _10097.z ? _10096.z : vec3(0.0).z);
            if (_10063 && _10072)
            {
                _21509 = (_10085 + _10098) * 0.5;
                break;
            }
            bvec3 _10111 = bvec3(_10063);
            _21509 = vec3(_10111.x ? _10085.x : _10098.x, _10111.y ? _10085.y : _10098.y, _10111.z ? _10085.z : _10098.z);
            break;
        } while(false);
        vec2 _5015 = _21502.xy * max(_247.NormalScale, 0.0);
        vec3 _20930 = _21502;
        _20930.x = _5015.x;
        _20930.y = _5015.y;
        vec2 _5025 = _21509.xy * max(_247.NormalScale, 0.0);
        vec3 _20934 = _21509;
        _20934.x = _5025.x;
        _20934.y = _5025.y;
        bvec3 _22689 = bvec3(length(_20930) < 9.9999999747524270787835121154785e-07);
        bvec3 _22691 = bvec3(length(_20934) < 9.9999999747524270787835121154785e-07);
        vec3 _5043 = normalize(cross(vec3(_22689.x ? vec3(1.0, 0.0, 0.0).x : _20930.x, _22689.y ? vec3(1.0, 0.0, 0.0).y : _20930.y, _22689.z ? vec3(1.0, 0.0, 0.0).z : _20930.z), vec3(_22691.x ? vec3(0.0, -1.0, 0.0).x : _20934.x, _22691.y ? vec3(0.0, -1.0, 0.0).y : _20934.y, _22691.z ? vec3(0.0, -1.0, 0.0).z : _20934.z)));
        vec3 _21524;
        if (dot(_5043, _4973) < 0.0)
        {
            _21524 = -_5043;
        }
        else
        {
            _21524 = _5043;
        }
        float _5054 = max(_21495.x, 9.9999997473787516355514526367188e-05);
        ivec2 _11347 = clamp(ivec2(floor((_4760 * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        ivec2 _11357 = clamp(_11347, ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _11368 = (uint(_11357.x) + (uint(_11357.y) * FluidSize.x)) * 4u;
        uint _11378 = AuxBits_1._data[_11368 + 1u];
        uint _11390 = AuxBits_1._data[_11368 + 3u];
        float _21522;
        _21522 = float(AuxBits_1._data[_11368]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _21521 = 0; _21521 < 4; )
        {
            ivec2 _11282 = ivec2((_21521 * 2) - 1, 0);
            ivec2 _11287 = ivec2(0, ((_21521 - 2) * 2) - 1);
            bvec2 _11288 = bvec2(_21521 < 2);
            ivec2 _11403 = clamp(_11347 + ivec2(_11288.x ? _11282.x : _11287.x, _11288.y ? _11282.y : _11287.y), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _11449 = clamp(_11347 + ivec2(((_21521 & 1) != int(0u)) ? 1 : (-1), ((_21521 & 2) != int(0u)) ? 1 : (-1)), ivec2(0), ivec2(FluidSize) - ivec2(1));
            _21522 += ((float(AuxBits_1._data[(uint(_11403.x) + (uint(_11403.y) * FluidSize.x)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_11449.x) + (uint(_11449.y) * FluidSize.x)) * 4u]) * 1.525902189314365386962890625e-05));
            _21521++;
            continue;
        }
        float _5073 = clamp((float(_11390) * 0.0009765625) / _5054, 0.0, 1.0);
        vec3 _11507 = ((_247.CameraRight.xyz * _4971.x) + (_247.CameraUp.xyz * _4971.y)) + (_247.CameraForward.xyz * _4971.z);
        vec3 _5079 = vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w) + (((_247.CameraRight.xyz * _8782) + (_247.CameraUp.xyz * _8794)) + (_247.CameraForward.xyz * _21494));
        vec3 _11560 = ((_247.CameraRight.xyz * _21524.x) + (_247.CameraUp.xyz * _21524.y)) + (_247.CameraForward.xyz * _21524.z);
        float _5090 = (_21494 / ((0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_21524, _4973)), 0.07999999821186065673828125);
        float _5114 = (_247.RippleParams.x * mix(_247.RippleFlow.y, 1.0 + _247.RippleFlow.x, clamp((float(AuxBits_1._data[_11368 + 2u]) * 0.0009765625) / _5054, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _11560.y) * clamp(1.0 - (length(_8796) / max(_247.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _21529;
        vec3 _21563;
        if (_5114 > 0.0)
        {
            vec2 _5120 = _5079.xz;
            float _11590 = max(_247.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _21526;
            float _21527;
            _21527 = 0.0;
            _21526 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _21525 = 0; _21525 < 6; )
            {
                float _11602 = _11590 * _1663[_21525];
                float _11607 = _5114 * pow(0.7799999713897705078125, float(_21525));
                float _11614 = clamp((_11602 / max(_5090 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _11690 = (((uint(_21525) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _11698 = ((_11690 >> ((_11690 >> 28u) + 4u)) ^ _11690) * 277803737u;
                float _11621 = 0.60000002384185791015625 + (float(((_11698 >> 22u) ^ _11698) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _11626 = vec2(cos(_11621), sin(_11621));
                uint _11717 = (((uint(_21525) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _11725 = ((_11717 >> ((_11717 >> 28u) + 4u)) ^ _11717) * 277803737u;
                _21527 += (((0.5 * _11607) * _11607) * (1.0 - (_11614 * _11614)));
                _21526 += (_11626 * ((_11607 * _11614) * cos(((dot(_11626, _5120) * (6.283185482025146484375 / _11602)) + ((sqrt(61.63805389404296875 / _11602) * _247.RippleParams.z) * _247.ShadeParams.z)) + (float(((_11725 >> 22u) ^ _11725) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _21525++;
                continue;
            }
            vec3 _5135 = normalize(_11560 + vec3(-_21526.x, 0.0, -_21526.y));
            vec3 _5137 = normalize(vec3(dot(_5135, _247.CameraRight.xyz), dot(_5135, _247.CameraUp.xyz), dot(_5135, _247.CameraForward.xyz)));
            bvec3 _22693 = bvec3(dot(_5137, _4973) < 0.0199999995529651641845703125);
            _21563 = vec3(_22693.x ? _21524.x : _5137.x, _22693.y ? _21524.y : _5137.y, _22693.z ? _21524.z : _5137.z);
            _21529 = _21527;
        }
        else
        {
            _21563 = _21524;
            _21529 = 0.0;
        }
        float _5149 = _21529 * _247.RippleFlow.z;
        if (_247.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_4752), vec4((_21563 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_247.DebugView == 2u)
        {
            float _5172 = clamp(_21494 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_4752), vec4(_5172, _5172, _5172, 1.0));
            break;
        }
        float _5192 = (_6625 < 100000000.0) ? (max(_6625 - _21494, 0.0) / max(_4971.z, 0.0500000007450580596923828125)) : 10000.0;
        float _5197 = _5054 * max(_247.ThicknessScale, 0.0);
        float _5202 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _5197 / max(_5192, 0.001000000047497451305389404296875));
        float _21561;
        do
        {
            vec3 _11781 = (_5079 + (_22686 * (max(_5079.y - _247.PoolMin.y, 0.0500000007450580596923828125) / max(-_22686.y, 0.20000000298023223876953125)))) - vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w);
            float _11847 = dot(_11781, _247.CameraForward.xyz);
            if (_11847 <= 0.100000001490116119384765625)
            {
                _21561 = 1.0;
                break;
            }
            float _11888 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _11864 = ((dot(_11781, _247.CameraRight.xyz) * _11888) / _11847) + (0.5 * float(_247.OutputWidth));
            float _11877 = (((-dot(_11781, _247.CameraUp.xyz)) * _11888) / _11847) + (0.5 * float(_247.OutputHeight));
            vec2 _11878 = vec2(_11864, _11877);
            if (!((((_11864 >= 0.0) && (_11877 >= 0.0)) && (_11864 < float(_247.OutputWidth))) && (_11877 < float(_247.OutputHeight))))
            {
                _21561 = 1.0;
                break;
            }
            ivec2 _11972 = clamp(ivec2(_11878), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            vec4 _11924 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_11972.x, int((_247.OutputHeight - 1u) - uint(_11972.y)), 0).xy, 0);
            float _11925 = _11924.x;
            if (abs((((_11925 > 0.0) && (abs(_11925 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_11925 - _247.ProjA)) : 1000000000.0) - _11847) > ((0.1500000059604644775390625 * _11847) + 0.100000001490116119384765625))
            {
                _21561 = 1.0;
                break;
            }
            ivec2 _12032 = clamp(ivec2(_11878), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            _21561 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_12032.x, int((_247.OutputHeight - 1u) - uint(_12032.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _5208 = refract(_4971, _21563, 0.75187969207763671875);
        bvec3 _22697 = bvec3(dot(_5208, _5208) < 9.9999999747524270787835121154785e-07);
        vec3 _5227 = normalize(mix(_4971, vec3(_22697.x ? _4971.x : _5208.x, _22697.y ? _4971.y : _5208.y, _22697.z ? _4971.z : _5208.z), vec3(clamp(_247.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _5202))));
        float _5235 = clamp((_5192 * 1.2000000476837158203125) + 0.20000000298023223876953125, 0.300000011920928955078125, max(_247.MaxTravel, 0.300000011920928955078125) * 2.0);
        float _21613;
        float _21650;
        float _21686;
        vec2 _21722;
        do
        {
            ivec2 _12308 = clamp(ivec2(_4760), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            vec4 _12260 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_12308.x, int((_247.OutputHeight - 1u) - uint(_12308.y)), 0).xy, 0);
            float _12261 = _12260.x;
            vec2 _21577;
            float _21582;
            _21582 = ((_12261 > 0.0) && (abs(_12261 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_12261 - _247.ProjA)) : 1000000000.0;
            _21577 = _4760;
            float _12121;
            float _22701;
            vec2 _22703;
            bool _21573;
            float _21593;
            float _21614;
            float _21651;
            float _21687;
            vec2 _21723;
            int _21566 = 1;
            float _21568 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_21566 <= 16)
                {
                    float _12115 = float(_21566) * 0.0625;
                    _12121 = (_5235 * _12115) * sqrt(_12115);
                    vec3 _12126 = _8796 + (_5227 * _12121);
                    if (_12126.z <= 0.0500000007450580596923828125)
                    {
                        _21723 = _21755;
                        _21687 = _21646;
                        _21651 = _21646;
                        _21614 = _21646;
                        _21593 = 0.0;
                        _21573 = false;
                        break;
                    }
                    float _12378 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _12354 = ((_12126.x * _12378) / _12126.z) + (0.5 * float(_247.OutputWidth));
                    float _12367 = (((-_12126.y) * _12378) / _12126.z) + (0.5 * float(_247.OutputHeight));
                    vec2 _12368 = vec2(_12354, _12367);
                    if (!((((_12354 >= 0.0) && (_12367 >= 0.0)) && (_12354 < float(_247.OutputWidth))) && (_12367 < float(_247.OutputHeight))))
                    {
                        _21723 = _21755;
                        _21687 = _21646;
                        _21651 = _21646;
                        _21614 = _21646;
                        _21593 = 0.0;
                        _21573 = false;
                        break;
                    }
                    ivec2 _12462 = clamp(ivec2(_12368), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                    vec4 _12414 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_12462.x, int((_247.OutputHeight - 1u) - uint(_12462.y)), 0).xy, 0);
                    float _12415 = _12414.x;
                    float _12432 = ((_12415 > 0.0) && (abs(_12415 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_12415 - _247.ProjA)) : 1000000000.0;
                    if (_12126.z >= _12432)
                    {
                        ivec2 _12148 = ivec2(_12368);
                        ivec2 _12543 = clamp(ivec2(floor(((vec2(_12148) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _12552 = clamp(_12543, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _12578 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12552.x, int((FluidSize.y - 1u) - uint(_12552.y)), 0).xy, 0);
                        float _12579 = _12578.x;
                        ivec2 _12588 = clamp(_12543, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _12675 = clamp(_12148, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        vec4 _12627 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_12675.x, int((_247.OutputHeight - 1u) - uint(_12675.y)), 0).xy, 0);
                        float _12628 = _12627.x;
                        if (!(((_12579 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_12588.x, int((FluidSize.y - 1u) - uint(_12588.y)), 0).xy, 0).w > 0.5)) && ((((_12628 > 0.0) && (abs(_12628 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_12628 - _247.ProjA)) : 1000000000.0) >= (_12579 + 0.0500000007450580596923828125))))
                        {
                            _21723 = _21755;
                            _21687 = _21646;
                            _21651 = _21646;
                            _21614 = _21646;
                            _21593 = 0.0;
                            _21573 = false;
                            break;
                        }
                        float _21570;
                        _21570 = _12121;
                        float _22699;
                        float _22700;
                        int _21569 = 0;
                        float _21571 = _21568;
                        SPIRV_CROSS_UNROLL
                        for (; _21569 < 5; _21571 = _22699, _21570 = _22700, _21569++)
                        {
                            float _12163 = 0.5 * (_21571 + _21570);
                            vec3 _12168 = _8796 + (_5227 * _12163);
                            float _12745 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _12806 = clamp(ivec2(vec2(((_12168.x * _12745) / _12168.z) + (0.5 * float(_247.OutputWidth)), (((-_12168.y) * _12745) / _12168.z) + (0.5 * float(_247.OutputHeight)))), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                            vec4 _12758 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_12806.x, int((_247.OutputHeight - 1u) - uint(_12806.y)), 0).xy, 0);
                            float _12759 = _12758.x;
                            bool _12175 = _12168.z >= (((_12759 > 0.0) && (abs(_12759 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_12759 - _247.ProjA)) : 1000000000.0);
                            _22699 = _12175 ? _21571 : _12163;
                            _22700 = _12175 ? _12163 : _21570;
                        }
                        vec3 _12189 = _8796 + (_5227 * _21570);
                        float _12876 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _12200 = clamp(vec2(((_12189.x * _12876) / _12189.z) + (0.5 * float(_247.OutputWidth)), (((-_12189.y) * _12876) / _12189.z) + (0.5 * float(_247.OutputHeight))), vec2(0.5), vec2(float(_247.OutputWidth), float(_247.OutputHeight)) - vec2(0.5));
                        ivec2 _12937 = clamp(ivec2(_12200), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        vec4 _12889 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_12937.x, int((_247.OutputHeight - 1u) - uint(_12937.y)), 0).xy, 0);
                        float _12890 = _12889.x;
                        _21723 = _12200;
                        _21687 = ((_12890 > 0.0) && (abs(_12890 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_12890 - _247.ProjA)) : 1000000000.0;
                        _21651 = _21570;
                        _21614 = 1.0;
                        _21593 = 1.0;
                        _21573 = true;
                        break;
                    }
                    ivec2 _12215 = ivec2(_12368 - vec2(0.5));
                    ivec2 _13018 = clamp(ivec2(floor(((vec2(_12215) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _13027 = clamp(_13018, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _13053 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_13027.x, int((FluidSize.y - 1u) - uint(_13027.y)), 0).xy, 0);
                    float _13054 = _13053.x;
                    ivec2 _13063 = clamp(_13018, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _13150 = clamp(_12215, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                    vec4 _13102 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13150.x, int((_247.OutputHeight - 1u) - uint(_13150.y)), 0).xy, 0);
                    float _13103 = _13102.x;
                    ivec2 _12220 = ivec2(_12368 + vec2(0.5));
                    ivec2 _13231 = clamp(ivec2(floor(((vec2(_12220) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _13240 = clamp(_13231, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _13266 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_13240.x, int((FluidSize.y - 1u) - uint(_13240.y)), 0).xy, 0);
                    float _13267 = _13266.x;
                    ivec2 _13276 = clamp(_13231, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _13363 = clamp(_12220, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                    vec4 _13315 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13363.x, int((_247.OutputHeight - 1u) - uint(_13363.y)), 0).xy, 0);
                    float _13316 = _13315.x;
                    bool _12222 = (((_13054 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_13063.x, int((FluidSize.y - 1u) - uint(_13063.y)), 0).xy, 0).w > 0.5)) && ((((_13103 > 0.0) && (abs(_13103 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_13103 - _247.ProjA)) : 1000000000.0) >= (_13054 + 0.0500000007450580596923828125))) && (((_13267 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_13276.x, int((FluidSize.y - 1u) - uint(_13276.y)), 0).xy, 0).w > 0.5)) && ((((_13316 > 0.0) && (abs(_13316 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_13316 - _247.ProjA)) : 1000000000.0) >= (_13267 + 0.0500000007450580596923828125)));
                    _22701 = _12222 ? _12432 : _21582;
                    bvec2 _22702 = bvec2(_12222);
                    _22703 = vec2(_22702.x ? _12368.x : _21577.x, _22702.y ? _12368.y : _21577.y);
                    _21566++;
                    _21582 = _22701;
                    _21577 = _22703;
                    _21568 = _12121;
                    continue;
                }
                else
                {
                    _21723 = _21755;
                    _21687 = _21646;
                    _21651 = _21646;
                    _21614 = _21646;
                    _21593 = 0.0;
                    _21573 = false;
                    break;
                }
            }
            if (_21573)
            {
                _21722 = _21723;
                _21686 = _21687;
                _21650 = _21651;
                _21613 = _21614;
                break;
            }
            vec3 _13415 = ((_247.CameraRight.xyz * _5227.x) + (_247.CameraUp.xyz * _5227.y)) + (_247.CameraForward.xyz * _5227.z);
            float _21591;
            do
            {
                if ((((_5079.x < _247.PoolMin.x) || (_5079.x > _247.PoolMax.x)) || (_5079.z < _247.PoolMin.z)) || (_5079.z > _247.PoolMax.z))
                {
                    _21591 = 1000.0;
                    break;
                }
                float _21586;
                if (_13415.x > 9.9999997473787516355514526367188e-05)
                {
                    _21586 = min(1000.0, (_247.PoolMax.x - _5079.x) / _13415.x);
                }
                else
                {
                    float _21587;
                    if (_13415.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _21587 = min(1000.0, (_247.PoolMin.x - _5079.x) / _13415.x);
                    }
                    else
                    {
                        _21587 = 1000.0;
                    }
                    _21586 = _21587;
                }
                float _21588;
                if (_13415.z > 9.9999997473787516355514526367188e-05)
                {
                    _21588 = min(_21586, (_247.PoolMax.z - _5079.z) / _13415.z);
                }
                else
                {
                    float _21589;
                    if (_13415.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _21589 = min(_21586, (_247.PoolMin.z - _5079.z) / _13415.z);
                    }
                    else
                    {
                        _21589 = _21586;
                    }
                    _21588 = _21589;
                }
                float _21590;
                if (_13415.y < (-9.9999997473787516355514526367188e-05))
                {
                    _21590 = min(_21588, (_247.PoolMin.y - _5079.y) / _13415.y);
                }
                else
                {
                    _21590 = _21588;
                }
                _21591 = max(_21590, 0.0);
                break;
            } while(false);
            _21722 = _21577;
            _21686 = _21582;
            _21650 = min(_21591, _5235);
            _21613 = _21593;
            break;
        } while(false);
        vec3 _21775;
        vec3 _21776;
        do
        {
            vec2 _13751 = clamp(_21722 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _13762 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13751.x, 1.0 - _13751.y), 0.0).xyz;
            if ((_21613 < 0.5) || (_21686 > 100000000.0))
            {
                _21776 = vec3(0.0, 1.0, 0.5);
                _21775 = _13762;
                break;
            }
            ivec2 _13579 = ivec2(_21722);
            float _13810 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _13587 = vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w) + (((_247.CameraRight.xyz * (((_21722.x - (0.5 * float(_247.OutputWidth))) * _21686) / _13810)) + (_247.CameraUp.xyz * (((-(_21722.y - (0.5 * float(_247.OutputHeight)))) * _21686) / _13810))) + (_247.CameraForward.xyz * _21686));
            float _13591 = _5079.y - _13587.y;
            if (_13591 <= 0.00999999977648258209228515625)
            {
                _21776 = vec3(0.0, 1.0, 0.5);
                _21775 = _13762;
                break;
            }
            ivec2 _13862 = clamp(_13579, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            ivec2 _13950 = clamp(_13579, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            vec3 _13910 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_13950.x, int((_247.OutputHeight - 1u) - uint(_13950.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _13912 = length(_13910);
            vec3 _13918 = _13910 / vec3(_13912);
            bvec3 _13919 = bvec3(_13912 > 0.001000000047497451305389404296875);
            ivec2 _14010 = clamp(_13579, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            float _13637 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_13862.x, int((_247.OutputHeight - 1u) - uint(_13862.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_13919.x ? _13918.x : vec3(0.0, 1.0, 0.0).x, _13919.y ? _13918.y : vec3(0.0, 1.0, 0.0).y, _13919.z ? _13918.z : vec3(0.0, 1.0, 0.0).z), _4816), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_14010.x, int((_247.OutputHeight - 1u) - uint(_14010.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _247.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_13762, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _13641 = max(-_22686.y, 0.20000000298023223876953125);
            vec3 _13655 = mix(vec3(1.0), exp(((-_247.FluidTint.xyz) * _13591) / vec3(_13641)), vec3(_247.LightParams.x));
            float _21774;
            if ((_247.CausticParams.x > 0.0) && (_13637 > 0.0))
            {
                vec2 _13684 = _13587.xz - ((_22686.xz / vec2(_13641)) * _13591);
                vec2 _14065 = _13684 * (6.283185482025146484375 / max(_247.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _14088 = _14065 + (vec2(sin((_14065.y * 0.730000019073486328125) + ((_247.ShadeParams.z * 0.89999997615814208984375) * _247.CausticParams.z)), sin((_14065.x * 0.61000001430511474609375) - ((_247.ShadeParams.z * 1.10000002384185791015625) * _247.CausticParams.z))) * 0.89999997615814208984375);
                float _21773;
                do
                {
                    if ((_247.GridParams.z < 2u) || (_247.GridParams.w < 2u))
                    {
                        _21773 = 1.0;
                        break;
                    }
                    vec2 _14198 = clamp(((_13684 - _247.PoolMin.xz) / max(_247.PoolMax.xz - _247.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_247.GridParams.z - 1u), float(_247.GridParams.w - 1u)), vec2(0.0), vec2(float(_247.GridParams.z - 1u), float(_247.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _14200 = uvec2(_14198);
                    vec2 _14204 = _14198 - vec2(_14200);
                    uint _14211 = _14200.x + (_14200.y * _247.GridParams.z);
                    _21773 = mix(mix(CausticField_1._data[_14211], CausticField_1._data[_14211 + 1u], _14204.x), mix(CausticField_1._data[_14211 + _247.GridParams.z], CausticField_1._data[(_14211 + _247.GridParams.z) + 1u], _14204.x), _14204.y);
                    break;
                } while(false);
                _21774 = min(max(1.0 + ((_247.CausticParams.x * (exp((-_13591) / max(_247.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _13591))) * ((pow(clamp((1.0 - abs(sin(_14088.x + ((_247.ShadeParams.z * 0.60000002384185791015625) * _247.CausticParams.z)) * sin((_14088.y * 1.12999999523162841796875) - ((_247.ShadeParams.z * 0.4000000059604644775390625) * _247.CausticParams.z)))) * (1.0 - abs(sin(((_14088.x + _14088.y) * 0.709999978542327880859375) - ((_247.ShadeParams.z * 0.5) * _247.CausticParams.z)) * sin(((_14088.x - _14088.y) * 0.829999983310699462890625) + ((_247.ShadeParams.z * 0.699999988079071044921875) * _247.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_21773, 0.0500000007450580596923828125), _247.LightParams.y), 2.5);
            }
            else
            {
                _21774 = 1.0;
            }
            _21776 = vec3(_13637, dot(_13655, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _21774);
            _21775 = _13762 * ((mix(vec3(1.0), exp(((-_247.FluidTint.xyz) * _13591) * 1.2000000476837158203125), vec3(_247.LightParams.x)) * (1.0 - _13637)) + ((_13655 * _13637) * _21774));
            break;
        } while(false);
        float _5256 = mix(_5197, max(_21650, _5197), _5202);
        vec3 _22185;
        if (_5202 < 0.5)
        {
            vec3 _14268 = ((_247.CameraRight.xyz * _5227.x) + (_247.CameraUp.xyz * _5227.y)) + (_247.CameraForward.xyz * _5227.z);
            vec3 _22186;
            if (_14268.y < (-0.0199999995529651641845703125))
            {
                float _5273 = (_247.PoolMin.w - _5079.y) / _14268.y;
                vec3 _5278 = _5079 + (_14268 * _5273);
                vec3 _22187;
                if (((_5273 > _5197) && ((((_5278.x > _247.PoolMin.x) && (_5278.x < _247.PoolMax.x)) && (_5278.z > _247.PoolMin.z)) && (_5278.z < _247.PoolMax.z))) && ((_21613 < 0.5) || (_5273 < _21650)))
                {
                    float _14324 = length(_5278 - vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w)) / ((0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _14329 = _247.RippleParams.x * _247.RippleFlow.y;
                    vec2 _14331 = _5278.xz;
                    float _14463 = max(_247.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _21781;
                    _21781 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _21780 = 0; _21780 < 6; )
                    {
                        float _14475 = _14463 * _1663[_21780];
                        uint _14563 = (((uint(_21780) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _14571 = ((_14563 >> ((_14563 >> 28u) + 4u)) ^ _14563) * 277803737u;
                        float _14494 = 0.60000002384185791015625 + (float(((_14571 >> 22u) ^ _14571) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _14499 = vec2(cos(_14494), sin(_14494));
                        uint _14590 = (((uint(_21780) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _14598 = ((_14590 >> ((_14590 >> 28u) + 4u)) ^ _14590) * 277803737u;
                        _21781 += (_14499 * (((_14329 * pow(0.7799999713897705078125, float(_21780))) * clamp((_14475 / max(_14324 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_14499, _14331) * (6.283185482025146484375 / _14475)) + ((sqrt(61.63805389404296875 / _14475) * _247.RippleParams.z) * _247.ShadeParams.z)) + (float(((_14598 >> 22u) ^ _14598) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _21780++;
                        continue;
                    }
                    vec3 _14342 = normalize(vec3(-_21781.x, 1.0, -_21781.y));
                    vec3 _14345 = _5278 - vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w);
                    float _14618 = dot(_14345, _247.CameraRight.xyz);
                    float _14623 = dot(_14345, _247.CameraUp.xyz);
                    float _14628 = dot(_14345, _247.CameraForward.xyz);
                    vec3 _14629 = vec3(_14618, _14623, _14628);
                    vec3 _14349 = reflect(_14268, _14342);
                    float _14646 = dot(_14349, _247.CameraForward.xyz);
                    vec3 _14647 = vec3(dot(_14349, _247.CameraRight.xyz), dot(_14349, _247.CameraUp.xyz), _14646);
                    float _21806;
                    vec3 _21860;
                    do
                    {
                        if (_247.ReflectionDistance <= 0.0)
                        {
                            _21860 = vec3(0.0);
                            _21806 = 0.0;
                            break;
                        }
                        float _14696 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _14646);
                        if (_14696 <= 0.0)
                        {
                            _21860 = vec3(0.0);
                            _21806 = 0.0;
                            break;
                        }
                        float _14893 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _14705 = max(0.0040000001899898052215576171875 * _14628, 0.00999999977648258209228515625);
                        float _14707 = 0.5 * _14705;
                        float _14716;
                        float _14718;
                        bool _21791;
                        bool _21796;
                        float _21801;
                        float _21807;
                        vec3 _21861;
                        int _21782 = 0;
                        float _21783 = _14707;
                        float _21784 = _14705;
                        float _21802 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_21782 < 80)
                            {
                                _14716 = _21783 + _21784;
                                _14718 = _21784 * 1.07000005245208740234375;
                                if (_14716 > _247.ReflectionDistance)
                                {
                                    _21861 = vec3(0.0);
                                    _21807 = 0.0;
                                    _21801 = _21802;
                                    _21796 = false;
                                    _21791 = true;
                                    break;
                                }
                                vec3 _14729 = _14629 + (_14647 * _14716);
                                if (_14729.z <= 0.0500000007450580596923828125)
                                {
                                    _21861 = vec3(0.0);
                                    _21807 = 0.0;
                                    _21801 = _21802;
                                    _21796 = false;
                                    _21791 = true;
                                    break;
                                }
                                float _14933 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _14909 = ((_14729.x * _14933) / _14729.z) + (0.5 * float(_247.OutputWidth));
                                float _14922 = (((-_14729.y) * _14933) / _14729.z) + (0.5 * float(_247.OutputHeight));
                                if (!((((_14909 >= 0.0) && (_14922 >= 0.0)) && (_14909 < float(_247.OutputWidth))) && (_14922 < float(_247.OutputHeight))))
                                {
                                    _21861 = vec3(0.0);
                                    _21807 = _21646;
                                    _21801 = _21802;
                                    _21796 = true;
                                    _21791 = false;
                                    break;
                                }
                                ivec2 _15017 = clamp(ivec2(vec2(_14909, _14922)), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                vec4 _14969 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15017.x, int((_247.OutputHeight - 1u) - uint(_15017.y)), 0).xy, 0);
                                float _14970 = _14969.x;
                                float _14987 = ((_14970 > 0.0) && (abs(_14970 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_14970 - _247.ProjA)) : 1000000000.0;
                                if (((_14987 < 100000000.0) && (_14729.z > _14987)) && ((_14729.z - _14987) < max(_21784 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _21786;
                                    _21786 = _14716;
                                    float _22704;
                                    float _22705;
                                    int _21785 = 0;
                                    float _21789 = _21783;
                                    SPIRV_CROSS_UNROLL
                                    for (; _21785 < 6; _21789 = _22704, _21786 = _22705, _21785++)
                                    {
                                        float _14773 = 0.5 * (_21789 + _21786);
                                        vec3 _14778 = _14629 + (_14647 * _14773);
                                        float _15087 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _15148 = clamp(ivec2(vec2(((_14778.x * _15087) / _14778.z) + (0.5 * float(_247.OutputWidth)), (((-_14778.y) * _15087) / _14778.z) + (0.5 * float(_247.OutputHeight)))), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                        vec4 _15100 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15148.x, int((_247.OutputHeight - 1u) - uint(_15148.y)), 0).xy, 0);
                                        float _15101 = _15100.x;
                                        bool _14785 = _14778.z > (((_15101 > 0.0) && (abs(_15101 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_15101 - _247.ProjA)) : 1000000000.0);
                                        _22704 = _14785 ? _21789 : _14773;
                                        _22705 = _14785 ? _14773 : _21786;
                                    }
                                    vec3 _14799 = _14629 + (_14647 * _21786);
                                    float _15218 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _15208 = vec2(((_14799.x * _15218) / _14799.z) + (0.5 * float(_247.OutputWidth)), (((-_14799.y) * _15218) / _14799.z) + (0.5 * float(_247.OutputHeight)));
                                    ivec2 _15276 = clamp(ivec2(_15208), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                    vec3 _15236 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_15276.x, int((_247.OutputHeight - 1u) - uint(_15276.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _15238 = length(_15236);
                                    vec3 _15244 = _15236 / vec3(_15238);
                                    bvec3 _15245 = bvec3(_15238 > 0.001000000047497451305389404296875);
                                    vec3 _15246 = vec3(_15245.x ? _15244.x : vec3(0.0, 1.0, 0.0).x, _15245.y ? _15244.y : vec3(0.0, 1.0, 0.0).y, _15245.z ? _15244.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_15246, _247.CameraRight.xyz), dot(_15246, _247.CameraUp.xyz), dot(_15246, _247.CameraForward.xyz)), _14647) > 0.100000001490116119384765625)
                                    {
                                        int _14826 = _21782 + 1;
                                        _21784 = _14718;
                                        _21783 = _14716;
                                        _21782 = _14826;
                                        _21802 = _14716;
                                        continue;
                                    }
                                    float _15340 = clamp(((0.0199999995529651641845703125 * _21786) * _14893) / max(_14799.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _15342 = 0.5 * _15340;
                                    vec2 _15427 = clamp(_15208 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15453 = clamp((_15208 + vec2(_15342)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15479 = clamp((_15208 + vec2(_15340 * (-0.5), _15342)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15505 = clamp((_15208 + vec2(_15342, _15340 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15531 = clamp((_15208 + vec2(_15340 * (-0.5), _15340 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15557 = clamp((_15208 + vec2(_15340, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15583 = clamp((_15208 + vec2(-_15340, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15609 = clamp((_15208 + vec2(0.0, _15340)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _15635 = clamp((_15208 + vec2(0.0, -_15340)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _15412 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15427.x, 1.0 - _15427.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15453.x, 1.0 - _15453.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15479.x, 1.0 - _15479.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15505.x, 1.0 - _15505.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15531.x, 1.0 - _15531.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15557.x, 1.0 - _15557.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15583.x, 1.0 - _15583.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15609.x, 1.0 - _15609.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15635.x, 1.0 - _15635.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _21861 = _15412;
                                    _21807 = _14696;
                                    _21801 = _14716;
                                    _21796 = false;
                                    _21791 = true;
                                    break;
                                }
                                int _14826 = _21782 + 1;
                                _21784 = _14718;
                                _21783 = _14716;
                                _21782 = _14826;
                                _21802 = _14716;
                                continue;
                            }
                            else
                            {
                                _21861 = vec3(0.0);
                                _21807 = _21646;
                                _21801 = _21802;
                                _21796 = false;
                                _21791 = false;
                                break;
                            }
                        }
                        if (_21791)
                        {
                            _21860 = _21861;
                            _21806 = _21807;
                            break;
                        }
                        if ((!_21796) || (_21801 < 0.0))
                        {
                            _21860 = _21861;
                            _21806 = 0.0;
                            break;
                        }
                        vec3 _14841 = _14629 + (_14647 * _21801);
                        float _15686 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _15676 = vec2(((_14841.x * _15686) / _14841.z) + (0.5 * float(_247.OutputWidth)), (((-_14841.y) * _15686) / _14841.z) + (0.5 * float(_247.OutputHeight)));
                        ivec2 _15747 = clamp(ivec2(_15676), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        vec4 _15699 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15747.x, int((_247.OutputHeight - 1u) - uint(_15747.y)), 0).xy, 0);
                        float _15700 = _15699.x;
                        float _15717 = ((_15700 > 0.0) && (abs(_15700 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_15700 - _247.ProjA)) : 1000000000.0;
                        float _15793 = clamp(((0.0199999995529651641845703125 * _21801) * _14893) / max(_14841.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _15795 = 0.5 * _15793;
                        vec2 _15880 = clamp(_15676 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _15906 = clamp((_15676 + vec2(_15795)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _15932 = clamp((_15676 + vec2(_15793 * (-0.5), _15795)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _15958 = clamp((_15676 + vec2(_15795, _15793 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _15984 = clamp((_15676 + vec2(_15793 * (-0.5), _15793 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _16010 = clamp((_15676 + vec2(_15793, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _16036 = clamp((_15676 + vec2(-_15793, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _16062 = clamp((_15676 + vec2(0.0, _15793)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _16088 = clamp((_15676 + vec2(0.0, -_15793)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _15865 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15880.x, 1.0 - _15880.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15906.x, 1.0 - _15906.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15932.x, 1.0 - _15932.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15958.x, 1.0 - _15958.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_15984.x, 1.0 - _15984.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_16010.x, 1.0 - _16010.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_16036.x, 1.0 - _16036.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_16062.x, 1.0 - _16062.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_16088.x, 1.0 - _16088.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_15717 > 100000000.0)
                        {
                            _21860 = _15865;
                            _21806 = _14696;
                            break;
                        }
                        float _14866 = _15717 - _14841.z;
                        if (_14866 <= 0.0)
                        {
                            _21860 = _15865;
                            _21806 = _14696;
                            break;
                        }
                        _21860 = _15865;
                        _21806 = _14696 * exp((_14866 / max(_14646, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _21866;
                    do
                    {
                        if (_247.EnvParams.x <= 0.0)
                        {
                            float _16142 = _14349.y;
                            _21866 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _16142))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _16142)));
                            break;
                        }
                        _21866 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_14349.x, _14349.y, -_14349.z), 0.0).xyz * _247.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _14363 = refract(_14268, _14342, 0.75187969207763671875);
                    bvec3 _22706 = bvec3(dot(_14363, _14363) < 9.9999999747524270787835121154785e-07);
                    vec3 _22707 = vec3(_22706.x ? _14268.x : _14363.x, _22706.y ? _14268.y : _14363.y, _22706.z ? _14268.z : _14363.z);
                    float _16168 = dot(_22707, _247.CameraRight.xyz);
                    float _16173 = dot(_22707, _247.CameraUp.xyz);
                    float _16178 = dot(_22707, _247.CameraForward.xyz);
                    vec3 _16179 = vec3(_16168, _16173, _16178);
                    float _16219 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _16209 = vec2(((_14618 * _16219) / _14628) + (0.5 * float(_247.OutputWidth)), (((-_14623) * _16219) / _14628) + (0.5 * float(_247.OutputHeight)));
                    float _21915;
                    float _21975;
                    float _22035;
                    vec2 _22095;
                    do
                    {
                        ivec2 _16465 = clamp(ivec2(_16209), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        vec4 _16417 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16465.x, int((_247.OutputHeight - 1u) - uint(_16465.y)), 0).xy, 0);
                        float _16418 = _16417.x;
                        vec2 _21879;
                        float _21884;
                        _21884 = ((_16418 > 0.0) && (abs(_16418 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_16418 - _247.ProjA)) : 1000000000.0;
                        _21879 = _16209;
                        float _16278;
                        float _22710;
                        vec2 _22712;
                        bool _21875;
                        float _21895;
                        float _21916;
                        float _21976;
                        float _22036;
                        vec2 _22096;
                        int _21868 = 1;
                        float _21870 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_21868 <= 16)
                            {
                                float _16271 = float(_21868);
                                _16278 = (_16271 * 0.25) * sqrt(_16271 * 0.0625);
                                vec3 _16283 = _14629 + (_16179 * _16278);
                                if (_16283.z <= 0.0500000007450580596923828125)
                                {
                                    _22096 = _21755;
                                    _22036 = _21646;
                                    _21976 = _21646;
                                    _21916 = _21646;
                                    _21895 = 0.0;
                                    _21875 = false;
                                    break;
                                }
                                float _16535 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _16511 = ((_16283.x * _16535) / _16283.z) + (0.5 * float(_247.OutputWidth));
                                float _16524 = (((-_16283.y) * _16535) / _16283.z) + (0.5 * float(_247.OutputHeight));
                                vec2 _16525 = vec2(_16511, _16524);
                                if (!((((_16511 >= 0.0) && (_16524 >= 0.0)) && (_16511 < float(_247.OutputWidth))) && (_16524 < float(_247.OutputHeight))))
                                {
                                    _22096 = _21755;
                                    _22036 = _21646;
                                    _21976 = _21646;
                                    _21916 = _21646;
                                    _21895 = 0.0;
                                    _21875 = false;
                                    break;
                                }
                                ivec2 _16619 = clamp(ivec2(_16525), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                vec4 _16571 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16619.x, int((_247.OutputHeight - 1u) - uint(_16619.y)), 0).xy, 0);
                                float _16572 = _16571.x;
                                float _16589 = ((_16572 > 0.0) && (abs(_16572 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_16572 - _247.ProjA)) : 1000000000.0;
                                if (_16283.z >= _16589)
                                {
                                    ivec2 _16305 = ivec2(_16525);
                                    ivec2 _16700 = clamp(ivec2(floor(((vec2(_16305) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16709 = clamp(_16700, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _16735 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_16709.x, int((FluidSize.y - 1u) - uint(_16709.y)), 0).xy, 0);
                                    float _16736 = _16735.x;
                                    ivec2 _16745 = clamp(_16700, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16832 = clamp(_16305, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                    vec4 _16784 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16832.x, int((_247.OutputHeight - 1u) - uint(_16832.y)), 0).xy, 0);
                                    float _16785 = _16784.x;
                                    if (!(((_16736 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_16745.x, int((FluidSize.y - 1u) - uint(_16745.y)), 0).xy, 0).w > 0.5)) && ((((_16785 > 0.0) && (abs(_16785 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_16785 - _247.ProjA)) : 1000000000.0) >= (_16736 + 0.0500000007450580596923828125))))
                                    {
                                        _22096 = _21755;
                                        _22036 = _21646;
                                        _21976 = _21646;
                                        _21916 = _21646;
                                        _21895 = 0.0;
                                        _21875 = false;
                                        break;
                                    }
                                    float _21872;
                                    _21872 = _16278;
                                    float _22708;
                                    float _22709;
                                    int _21871 = 0;
                                    float _21873 = _21870;
                                    SPIRV_CROSS_UNROLL
                                    for (; _21871 < 5; _21873 = _22708, _21872 = _22709, _21871++)
                                    {
                                        float _16320 = 0.5 * (_21873 + _21872);
                                        vec3 _16325 = _14629 + (_16179 * _16320);
                                        float _16902 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _16963 = clamp(ivec2(vec2(((_16325.x * _16902) / _16325.z) + (0.5 * float(_247.OutputWidth)), (((-_16325.y) * _16902) / _16325.z) + (0.5 * float(_247.OutputHeight)))), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                        vec4 _16915 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16963.x, int((_247.OutputHeight - 1u) - uint(_16963.y)), 0).xy, 0);
                                        float _16916 = _16915.x;
                                        bool _16332 = _16325.z >= (((_16916 > 0.0) && (abs(_16916 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_16916 - _247.ProjA)) : 1000000000.0);
                                        _22708 = _16332 ? _21873 : _16320;
                                        _22709 = _16332 ? _16320 : _21872;
                                    }
                                    vec3 _16346 = _14629 + (_16179 * _21872);
                                    float _17033 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _16357 = clamp(vec2(((_16346.x * _17033) / _16346.z) + (0.5 * float(_247.OutputWidth)), (((-_16346.y) * _17033) / _16346.z) + (0.5 * float(_247.OutputHeight))), vec2(0.5), vec2(float(_247.OutputWidth), float(_247.OutputHeight)) - vec2(0.5));
                                    ivec2 _17094 = clamp(ivec2(_16357), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                    vec4 _17046 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17094.x, int((_247.OutputHeight - 1u) - uint(_17094.y)), 0).xy, 0);
                                    float _17047 = _17046.x;
                                    _22096 = _16357;
                                    _22036 = ((_17047 > 0.0) && (abs(_17047 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_17047 - _247.ProjA)) : 1000000000.0;
                                    _21976 = _21872;
                                    _21916 = 1.0;
                                    _21895 = 1.0;
                                    _21875 = true;
                                    break;
                                }
                                ivec2 _16372 = ivec2(_16525 - vec2(0.5));
                                ivec2 _17175 = clamp(ivec2(floor(((vec2(_16372) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _17184 = clamp(_17175, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _17210 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_17184.x, int((FluidSize.y - 1u) - uint(_17184.y)), 0).xy, 0);
                                float _17211 = _17210.x;
                                ivec2 _17220 = clamp(_17175, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _17307 = clamp(_16372, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                vec4 _17259 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17307.x, int((_247.OutputHeight - 1u) - uint(_17307.y)), 0).xy, 0);
                                float _17260 = _17259.x;
                                ivec2 _16377 = ivec2(_16525 + vec2(0.5));
                                ivec2 _17388 = clamp(ivec2(floor(((vec2(_16377) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _17397 = clamp(_17388, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _17423 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_17397.x, int((FluidSize.y - 1u) - uint(_17397.y)), 0).xy, 0);
                                float _17424 = _17423.x;
                                ivec2 _17433 = clamp(_17388, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _17520 = clamp(_16377, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                                vec4 _17472 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17520.x, int((_247.OutputHeight - 1u) - uint(_17520.y)), 0).xy, 0);
                                float _17473 = _17472.x;
                                bool _16379 = (((_17211 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_17220.x, int((FluidSize.y - 1u) - uint(_17220.y)), 0).xy, 0).w > 0.5)) && ((((_17260 > 0.0) && (abs(_17260 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_17260 - _247.ProjA)) : 1000000000.0) >= (_17211 + 0.0500000007450580596923828125))) && (((_17424 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_17433.x, int((FluidSize.y - 1u) - uint(_17433.y)), 0).xy, 0).w > 0.5)) && ((((_17473 > 0.0) && (abs(_17473 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_17473 - _247.ProjA)) : 1000000000.0) >= (_17424 + 0.0500000007450580596923828125)));
                                _22710 = _16379 ? _16589 : _21884;
                                bvec2 _22711 = bvec2(_16379);
                                _22712 = vec2(_22711.x ? _16525.x : _21879.x, _22711.y ? _16525.y : _21879.y);
                                _21868++;
                                _21884 = _22710;
                                _21879 = _22712;
                                _21870 = _16278;
                                continue;
                            }
                            else
                            {
                                _22096 = _21755;
                                _22036 = _21646;
                                _21976 = _21646;
                                _21916 = _21646;
                                _21895 = 0.0;
                                _21875 = false;
                                break;
                            }
                        }
                        if (_21875)
                        {
                            _22095 = _22096;
                            _22035 = _22036;
                            _21975 = _21976;
                            _21915 = _21916;
                            break;
                        }
                        vec3 _17572 = ((_247.CameraRight.xyz * _16168) + (_247.CameraUp.xyz * _16173)) + (_247.CameraForward.xyz * _16178);
                        float _21893;
                        do
                        {
                            if ((((_5278.x < _247.PoolMin.x) || (_5278.x > _247.PoolMax.x)) || (_5278.z < _247.PoolMin.z)) || (_5278.z > _247.PoolMax.z))
                            {
                                _21893 = 1000.0;
                                break;
                            }
                            float _21888;
                            if (_17572.x > 9.9999997473787516355514526367188e-05)
                            {
                                _21888 = min(1000.0, (_247.PoolMax.x - _5278.x) / _17572.x);
                            }
                            else
                            {
                                float _21889;
                                if (_17572.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _21889 = min(1000.0, (_247.PoolMin.x - _5278.x) / _17572.x);
                                }
                                else
                                {
                                    _21889 = 1000.0;
                                }
                                _21888 = _21889;
                            }
                            float _21890;
                            if (_17572.z > 9.9999997473787516355514526367188e-05)
                            {
                                _21890 = min(_21888, (_247.PoolMax.z - _5278.z) / _17572.z);
                            }
                            else
                            {
                                float _21891;
                                if (_17572.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _21891 = min(_21888, (_247.PoolMin.z - _5278.z) / _17572.z);
                                }
                                else
                                {
                                    _21891 = _21888;
                                }
                                _21890 = _21891;
                            }
                            float _21892;
                            if (_17572.y < (-9.9999997473787516355514526367188e-05))
                            {
                                _21892 = min(_21890, (_247.PoolMin.y - _5278.y) / _17572.y);
                            }
                            else
                            {
                                _21892 = _21890;
                            }
                            _21893 = max(_21892, 0.0);
                            break;
                        } while(false);
                        _22095 = _21879;
                        _22035 = _21884;
                        _21975 = min(_21893, 4.0);
                        _21915 = _21895;
                        break;
                    } while(false);
                    vec3 _22157;
                    do
                    {
                        vec2 _17908 = clamp(_22095 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _17919 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17908.x, 1.0 - _17908.y), 0.0).xyz;
                        if ((_21915 < 0.5) || (_22035 > 100000000.0))
                        {
                            _22157 = _17919;
                            break;
                        }
                        ivec2 _17736 = ivec2(_22095);
                        float _17967 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec3 _17744 = vec3(_247.CameraRight.w, _247.CameraUp.w, _247.CameraForward.w) + (((_247.CameraRight.xyz * (((_22095.x - (0.5 * float(_247.OutputWidth))) * _22035) / _17967)) + (_247.CameraUp.xyz * (((-(_22095.y - (0.5 * float(_247.OutputHeight)))) * _22035) / _17967))) + (_247.CameraForward.xyz * _22035));
                        float _17748 = _5278.y - _17744.y;
                        if (_17748 <= 0.00999999977648258209228515625)
                        {
                            _22157 = _17919;
                            break;
                        }
                        ivec2 _18019 = clamp(_17736, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        ivec2 _18107 = clamp(_17736, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        vec3 _18067 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_18107.x, int((_247.OutputHeight - 1u) - uint(_18107.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _18069 = length(_18067);
                        vec3 _18075 = _18067 / vec3(_18069);
                        bvec3 _18076 = bvec3(_18069 > 0.001000000047497451305389404296875);
                        ivec2 _18167 = clamp(_17736, ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        float _17794 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_18019.x, int((_247.OutputHeight - 1u) - uint(_18019.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_18076.x ? _18075.x : vec3(0.0, 1.0, 0.0).x, _18076.y ? _18075.y : vec3(0.0, 1.0, 0.0).y, _18076.z ? _18075.z : vec3(0.0, 1.0, 0.0).z), _4816), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_18167.x, int((_247.OutputHeight - 1u) - uint(_18167.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _247.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_17919, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _17798 = max(-_22686.y, 0.20000000298023223876953125);
                        float _22156;
                        if ((_247.CausticParams.x > 0.0) && (_17794 > 0.0))
                        {
                            vec2 _17841 = _17744.xz - ((_22686.xz / vec2(_17798)) * _17748);
                            vec2 _18222 = _17841 * (6.283185482025146484375 / max(_247.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _18245 = _18222 + (vec2(sin((_18222.y * 0.730000019073486328125) + ((_247.ShadeParams.z * 0.89999997615814208984375) * _247.CausticParams.z)), sin((_18222.x * 0.61000001430511474609375) - ((_247.ShadeParams.z * 1.10000002384185791015625) * _247.CausticParams.z))) * 0.89999997615814208984375);
                            float _22155;
                            do
                            {
                                if ((_247.GridParams.z < 2u) || (_247.GridParams.w < 2u))
                                {
                                    _22155 = 1.0;
                                    break;
                                }
                                vec2 _18355 = clamp(((_17841 - _247.PoolMin.xz) / max(_247.PoolMax.xz - _247.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_247.GridParams.z - 1u), float(_247.GridParams.w - 1u)), vec2(0.0), vec2(float(_247.GridParams.z - 1u), float(_247.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _18357 = uvec2(_18355);
                                vec2 _18361 = _18355 - vec2(_18357);
                                uint _18368 = _18357.x + (_18357.y * _247.GridParams.z);
                                _22155 = mix(mix(CausticField_1._data[_18368], CausticField_1._data[_18368 + 1u], _18361.x), mix(CausticField_1._data[_18368 + _247.GridParams.z], CausticField_1._data[(_18368 + _247.GridParams.z) + 1u], _18361.x), _18361.y);
                                break;
                            } while(false);
                            _22156 = min(max(1.0 + ((_247.CausticParams.x * (exp((-_17748) / max(_247.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _17748))) * ((pow(clamp((1.0 - abs(sin(_18245.x + ((_247.ShadeParams.z * 0.60000002384185791015625) * _247.CausticParams.z)) * sin((_18245.y * 1.12999999523162841796875) - ((_247.ShadeParams.z * 0.4000000059604644775390625) * _247.CausticParams.z)))) * (1.0 - abs(sin(((_18245.x + _18245.y) * 0.709999978542327880859375) - ((_247.ShadeParams.z * 0.5) * _247.CausticParams.z)) * sin(((_18245.x - _18245.y) * 0.829999983310699462890625) + ((_247.ShadeParams.z * 0.699999988079071044921875) * _247.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_22155, 0.0500000007450580596923828125), _247.LightParams.y), 2.5);
                        }
                        else
                        {
                            _22156 = 1.0;
                        }
                        _22157 = _17919 * ((mix(vec3(1.0), exp(((-_247.FluidTint.xyz) * _17748) * 1.2000000476837158203125), vec3(_247.LightParams.x)) * (1.0 - _17794)) + ((mix(vec3(1.0), exp(((-_247.FluidTint.xyz) * _17748) / vec3(_17798)), vec3(_247.LightParams.x)) * _17794) * _22156));
                        break;
                    } while(false);
                    vec3 _14394 = exp((-_247.FluidTint.xyz) * max(_21975, 0.0500000007450580596923828125));
                    float _18447 = _247.ScatterParams.z * _247.ScatterParams.z;
                    _22187 = mix(_21775, mix((_22157 * _14394) + ((_247.FluidColor.xyz * ((_5784 * _247.ScatterParams.y) + (_247.SunColor.xyz * (((_247.ScatterParams.x * 0.25) * ((1.0 - _18447) / pow(max((1.0 + _18447) - ((2.0 * _247.ScatterParams.z) * dot(_22686, -_14268)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_4816.y, 0.0, 1.0))))) * (vec3(1.0) - _14394)), mix(_21866, _21860, vec3(_21806)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_14342, -_14268), 0.0, 1.0), max(_247.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _5202), 0.0, 1.0)));
                }
                else
                {
                    _22187 = _21775;
                }
                _22186 = _22187;
            }
            else
            {
                _22186 = _21775;
            }
            _22185 = _22186;
        }
        else
        {
            _22185 = _21775;
        }
        float _5344 = clamp((clamp((float(_11378) * 0.0009765625) / _5054, 0.0, 1.0) * _247.ScatterParams.w) + (((_4791 * 0.000244140625) * _247.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _5356 = exp(max(_247.FluidTint.xyz, vec3(0.0)) * (-_5256)) * (1.0 - (0.85000002384185791015625 * _5344));
        vec3 _18476 = ((_247.SunColor.xyz * ((clamp(_4816.y, 0.0, 1.0) * _21561) * 0.3183098733425140380859375)) + _5784) * 0.85000002384185791015625;
        vec3 _18482 = mix(vec3(dot(_18476, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _18476, vec3(0.5));
        float _18530 = _247.ScatterParams.z * _247.ScatterParams.z;
        vec3 _5393 = ((_22185 * _5356) + ((vec3(1.0) - _5356) * mix(_247.FluidColor.xyz * ((_5784 * _247.ScatterParams.y) + (_247.SunColor.xyz * ((((_247.ScatterParams.x * 0.25) * ((1.0 - _18530) / pow(max((1.0 + _18530) - ((2.0 * _247.ScatterParams.z) * dot(_22686, -_11507)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_4816.y, 0.0, 1.0)) * _21561))), _18482 * 0.800000011920928955078125, vec3(_5344)))) + (_18482 * (((float(_4796) * 2.2888532839715480804443359375e-05) + (_4791 * 3.6621095205191522836685180664062e-05)) * clamp(_247.FoamParams.z, 0.0, 1.0)));
        if (_247.DebugView == 3u)
        {
            float _5400 = clamp(_5256 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_4752), vec4(_5400, _5400, _5400, 1.0));
            break;
        }
        if (_247.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_4752), vec4(_21495.www, 1.0));
            break;
        }
        if (_247.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_4752), vec4(_5393, 1.0));
            break;
        }
        if (_247.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_4752), vec4(clamp((_21522 * 0.0625) + (_4785 * 0.000244140625), 0.0, 1.0), _5344, clamp(_5073 + (_4779 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_247.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_4752), vec4(_21776, 1.0));
            break;
        }
        float _5472 = 0.014999999664723873138427734375 + (2.0 * sqrt(_5149));
        vec3 _5475 = reflect(_4971, _21563);
        vec3 _18564 = ((_247.CameraRight.xyz * _5475.x) + (_247.CameraUp.xyz * _5475.y)) + (_247.CameraForward.xyz * _5475.z);
        float _22299;
        vec3 _22379;
        do
        {
            if (_247.ReflectionDistance <= 0.0)
            {
                _22379 = vec3(0.0);
                _22299 = 0.0;
                break;
            }
            float _18613 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _5475.z);
            if (_18613 <= 0.0)
            {
                _22379 = vec3(0.0);
                _22299 = 0.0;
                break;
            }
            float _18810 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _18622 = max(0.0040000001899898052215576171875 * _21494, 0.00999999977648258209228515625);
            float _18624 = 0.5 * _18622;
            float _18633;
            float _18635;
            bool _22284;
            bool _22289;
            float _22294;
            float _22300;
            vec3 _22380;
            int _22275 = 0;
            float _22276 = _18624;
            float _22277 = _18622;
            float _22295 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_22275 < 80)
                {
                    _18633 = _22276 + _22277;
                    _18635 = _22277 * 1.07000005245208740234375;
                    if (_18633 > _247.ReflectionDistance)
                    {
                        _22380 = vec3(0.0);
                        _22300 = 0.0;
                        _22294 = _22295;
                        _22289 = false;
                        _22284 = true;
                        break;
                    }
                    vec3 _18646 = _8796 + (_5475 * _18633);
                    if (_18646.z <= 0.0500000007450580596923828125)
                    {
                        _22380 = vec3(0.0);
                        _22300 = 0.0;
                        _22294 = _22295;
                        _22289 = false;
                        _22284 = true;
                        break;
                    }
                    float _18850 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _18826 = ((_18646.x * _18850) / _18646.z) + (0.5 * float(_247.OutputWidth));
                    float _18839 = (((-_18646.y) * _18850) / _18646.z) + (0.5 * float(_247.OutputHeight));
                    if (!((((_18826 >= 0.0) && (_18839 >= 0.0)) && (_18826 < float(_247.OutputWidth))) && (_18839 < float(_247.OutputHeight))))
                    {
                        _22380 = vec3(0.0);
                        _22300 = _21646;
                        _22294 = _22295;
                        _22289 = true;
                        _22284 = false;
                        break;
                    }
                    ivec2 _18934 = clamp(ivec2(vec2(_18826, _18839)), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                    vec4 _18886 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18934.x, int((_247.OutputHeight - 1u) - uint(_18934.y)), 0).xy, 0);
                    float _18887 = _18886.x;
                    float _18904 = ((_18887 > 0.0) && (abs(_18887 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_18887 - _247.ProjA)) : 1000000000.0;
                    if (((_18904 < 100000000.0) && (_18646.z > _18904)) && ((_18646.z - _18904) < max(_22277 * 2.1400001049041748046875, 0.25)))
                    {
                        float _22279;
                        _22279 = _18633;
                        float _22715;
                        float _22716;
                        int _22278 = 0;
                        float _22282 = _22276;
                        SPIRV_CROSS_UNROLL
                        for (; _22278 < 6; _22282 = _22715, _22279 = _22716, _22278++)
                        {
                            float _18690 = 0.5 * (_22282 + _22279);
                            vec3 _18695 = _8796 + (_5475 * _18690);
                            float _19004 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _19065 = clamp(ivec2(vec2(((_18695.x * _19004) / _18695.z) + (0.5 * float(_247.OutputWidth)), (((-_18695.y) * _19004) / _18695.z) + (0.5 * float(_247.OutputHeight)))), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                            vec4 _19017 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19065.x, int((_247.OutputHeight - 1u) - uint(_19065.y)), 0).xy, 0);
                            float _19018 = _19017.x;
                            bool _18702 = _18695.z > (((_19018 > 0.0) && (abs(_19018 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_19018 - _247.ProjA)) : 1000000000.0);
                            _22715 = _18702 ? _22282 : _18690;
                            _22716 = _18702 ? _18690 : _22279;
                        }
                        vec3 _18716 = _8796 + (_5475 * _22279);
                        float _19135 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _19125 = vec2(((_18716.x * _19135) / _18716.z) + (0.5 * float(_247.OutputWidth)), (((-_18716.y) * _19135) / _18716.z) + (0.5 * float(_247.OutputHeight)));
                        ivec2 _19193 = clamp(ivec2(_19125), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
                        vec3 _19153 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_19193.x, int((_247.OutputHeight - 1u) - uint(_19193.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _19155 = length(_19153);
                        vec3 _19161 = _19153 / vec3(_19155);
                        bvec3 _19162 = bvec3(_19155 > 0.001000000047497451305389404296875);
                        vec3 _19163 = vec3(_19162.x ? _19161.x : vec3(0.0, 1.0, 0.0).x, _19162.y ? _19161.y : vec3(0.0, 1.0, 0.0).y, _19162.z ? _19161.z : vec3(0.0, 1.0, 0.0).z);
                        if (dot(vec3(dot(_19163, _247.CameraRight.xyz), dot(_19163, _247.CameraUp.xyz), dot(_19163, _247.CameraForward.xyz)), _5475) > 0.100000001490116119384765625)
                        {
                            int _18743 = _22275 + 1;
                            _22277 = _18635;
                            _22276 = _18633;
                            _22275 = _18743;
                            _22295 = _18633;
                            continue;
                        }
                        float _19257 = clamp(((_5472 * _22279) * _18810) / max(_18716.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _19259 = 0.5 * _19257;
                        vec2 _19344 = clamp(_19125 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19370 = clamp((_19125 + vec2(_19259)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19396 = clamp((_19125 + vec2(_19257 * (-0.5), _19259)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19422 = clamp((_19125 + vec2(_19259, _19257 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19448 = clamp((_19125 + vec2(_19257 * (-0.5), _19257 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19474 = clamp((_19125 + vec2(_19257, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19500 = clamp((_19125 + vec2(-_19257, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19526 = clamp((_19125 + vec2(0.0, _19257)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19552 = clamp((_19125 + vec2(0.0, -_19257)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _19329 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19344.x, 1.0 - _19344.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19370.x, 1.0 - _19370.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19396.x, 1.0 - _19396.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19422.x, 1.0 - _19422.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19448.x, 1.0 - _19448.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19474.x, 1.0 - _19474.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19500.x, 1.0 - _19500.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19526.x, 1.0 - _19526.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19552.x, 1.0 - _19552.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _22380 = _19329;
                        _22300 = _18613;
                        _22294 = _18633;
                        _22289 = false;
                        _22284 = true;
                        break;
                    }
                    int _18743 = _22275 + 1;
                    _22277 = _18635;
                    _22276 = _18633;
                    _22275 = _18743;
                    _22295 = _18633;
                    continue;
                }
                else
                {
                    _22380 = vec3(0.0);
                    _22300 = _21646;
                    _22294 = _22295;
                    _22289 = false;
                    _22284 = false;
                    break;
                }
            }
            if (_22284)
            {
                _22379 = _22380;
                _22299 = _22300;
                break;
            }
            if ((!_22289) || (_22294 < 0.0))
            {
                _22379 = _22380;
                _22299 = 0.0;
                break;
            }
            vec3 _18758 = _8796 + (_5475 * _22294);
            float _19603 = (0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _19593 = vec2(((_18758.x * _19603) / _18758.z) + (0.5 * float(_247.OutputWidth)), (((-_18758.y) * _19603) / _18758.z) + (0.5 * float(_247.OutputHeight)));
            ivec2 _19664 = clamp(ivec2(_19593), ivec2(0), ivec2(int(_247.OutputWidth) - 1, int(_247.OutputHeight) - 1));
            vec4 _19616 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19664.x, int((_247.OutputHeight - 1u) - uint(_19664.y)), 0).xy, 0);
            float _19617 = _19616.x;
            float _19634 = ((_19617 > 0.0) && (abs(_19617 - _247.ProjA) > 9.999999717180685365747194737196e-10)) ? (_247.ProjB / (_19617 - _247.ProjA)) : 1000000000.0;
            float _19710 = clamp(((_5472 * _22294) * _18810) / max(_18758.z, 0.0500000007450580596923828125), 0.0, 12.0);
            float _19712 = 0.5 * _19710;
            vec2 _19797 = clamp(_19593 / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19823 = clamp((_19593 + vec2(_19712)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19849 = clamp((_19593 + vec2(_19710 * (-0.5), _19712)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19875 = clamp((_19593 + vec2(_19712, _19710 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19901 = clamp((_19593 + vec2(_19710 * (-0.5), _19710 * (-0.5))) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19927 = clamp((_19593 + vec2(_19710, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19953 = clamp((_19593 + vec2(-_19710, 0.0)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _19979 = clamp((_19593 + vec2(0.0, _19710)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _20005 = clamp((_19593 + vec2(0.0, -_19710)) / vec2(float(_247.OutputWidth), float(_247.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _19782 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19797.x, 1.0 - _19797.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19823.x, 1.0 - _19823.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19849.x, 1.0 - _19849.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19875.x, 1.0 - _19875.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19901.x, 1.0 - _19901.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19927.x, 1.0 - _19927.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19953.x, 1.0 - _19953.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19979.x, 1.0 - _19979.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_20005.x, 1.0 - _20005.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_19634 > 100000000.0)
            {
                _22379 = _19782;
                _22299 = _18613;
                break;
            }
            float _18783 = _19634 - _18758.z;
            if (_18783 <= 0.0)
            {
                _22379 = _19782;
                _22299 = _18613;
                break;
            }
            _22379 = _19782;
            _22299 = _18613 * exp((_18783 / max(_5475.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        vec3 _22385;
        do
        {
            if (_247.EnvParams.x <= 0.0)
            {
                float _20059 = _18564.y;
                _22385 = mix(_247.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_247.SkyHorizon.xyz, _247.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _20059))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _20059)));
                break;
            }
            _22385 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_18564.x, _18564.y, -_18564.z), clamp(clamp(log2(max(_5472 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _247.EnvParams.x;
            break;
        } while(false);
        vec3 _5495 = mix(_22385, _22379, vec3(_22299));
        if (_247.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_4752), vec4(_5495, 1.0));
            break;
        }
        float _5523 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_21524 + _21563), _4973), 0.0, 1.0), max(_247.FresnelPower, 1.0));
        float _5524 = 0.0199999995529651641845703125 + _5523;
        vec3 _5534 = normalize(_247.SunDirection.xyz);
        float _5551 = max((2.0 / ((2.0 / (max(_247.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _5149))) - 2.0, 4.0);
        vec3 _5620 = (mix(_5393, _5495, vec3(clamp(_5524, 0.0, 1.0))) + (((((_247.SunColor.xyz * ((pow(clamp(dot(_21563, normalize(_5534 - _4971)), 0.0, 1.0), _5551) * (_5551 + 8.0)) * 0.0397887341678142547607421875)) * _247.Specular) * _5524) * _21561) * clamp(dot(_21563, _5534) * 4.0, 0.0, 1.0))) + ((_247.SunColor.xyz * _247.FluidColor.xyz) * (((((_247.LightParams.w * 0.60000002384185791015625) * (((1.0 - _5202) * exp(_5197 * (-1.5))) * (1.0 - exp(_5197 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_4816, _11507)), 9.9999997473787516355514526367188e-05), 1.5))) * _21561) * (0.980000019073486328125 - _5523)));
        vec3 _5638 = mix(_5620, (_5620 * 1.25) + (_5784 * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _5192)) * _5202) * _247.LightParams.z, 0.0, 1.0)));
        float _5647 = clamp(1.0 - exp(-((_21522 * 0.07500000298023223876953125) + (_4785 * 0.0003662109375))), 0.0, 1.0);
        vec3 _22461;
        if ((_5647 > 0.0199999995529651641845703125) && (_247.FoamParams.x > 0.0))
        {
            vec2 _20120 = _5079.xz / vec2(max(_247.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _20155 = floor(_20120);
            vec2 _20158 = _20120 - _20155;
            float _22454;
            float _22455;
            _22455 = 8.0;
            _22454 = 8.0;
            float _22470;
            float _22471;
            SPIRV_CROSS_UNROLL
            for (int _22453 = -1; _22453 <= 1; _22455 = _22470, _22454 = _22471, _22453++)
            {
                _22471 = _22454;
                _22470 = _22455;
                float _22718;
                float _22664;
                SPIRV_CROSS_UNROLL
                for (int _22468 = -1; _22468 <= 1; _22471 = _22664, _22470 = _22718, _22468++)
                {
                    vec2 _20173 = vec2(float(_22468), float(_22453));
                    ivec2 _20177 = ivec2(_20155 + _20173);
                    uint _20249 = ((((uint(_20177.x + 4096) * 1973u) + (uint(_20177.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _20257 = ((_20249 >> ((_20249 >> 28u) + 4u)) ^ _20249) * 277803737u;
                    uint _20261 = (_20257 >> 22u) ^ _20257;
                    float _20215 = length((_20173 + (vec2(0.5) + (sin(vec2(_247.ShadeParams.z * 0.5) + ((vec2(float(_20261 & 65535u), float(_20261 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _20158);
                    bool _20218 = _20215 < _22470;
                    if (_20218)
                    {
                        _22664 = _22470;
                    }
                    else
                    {
                        _22664 = (_20215 < _22471) ? _20215 : _22471;
                    }
                    _22718 = _20218 ? _20215 : _22470;
                }
            }
            vec2 _20127 = (_20120 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _20278 = floor(_20127);
            vec2 _20281 = _20127 - _20278;
            float _22457;
            float _22458;
            _22458 = 8.0;
            _22457 = 8.0;
            float _22464;
            float _22465;
            SPIRV_CROSS_UNROLL
            for (int _22456 = -1; _22456 <= 1; _22458 = _22464, _22457 = _22465, _22456++)
            {
                _22465 = _22457;
                _22464 = _22458;
                float _22720;
                float _22658;
                SPIRV_CROSS_UNROLL
                for (int _22462 = -1; _22462 <= 1; _22465 = _22658, _22464 = _22720, _22462++)
                {
                    vec2 _20296 = vec2(float(_22462), float(_22456));
                    ivec2 _20300 = ivec2(_20278 + _20296);
                    uint _20372 = ((((uint(_20300.x + 4096) * 1973u) + (uint(_20300.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _20380 = ((_20372 >> ((_20372 >> 28u) + 4u)) ^ _20372) * 277803737u;
                    uint _20384 = (_20380 >> 22u) ^ _20380;
                    float _20338 = length((_20296 + (vec2(0.5) + (sin(vec2(_247.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_20384 & 65535u), float(_20384 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _20281);
                    bool _20341 = _20338 < _22464;
                    if (_20341)
                    {
                        _22658 = _22464;
                    }
                    else
                    {
                        _22658 = (_20338 < _22465) ? _20338 : _22465;
                    }
                    _22720 = _20341 ? _20338 : _22464;
                }
            }
            float _20138 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _22454 - _22455), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _22457 - _22458))), clamp((((_247.FoamParams.y * ((0.5 * float(_247.OutputHeight)) * max(_247.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_21494, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _22461 = mix(_5638, _18482 * mix(0.85000002384185791015625, 1.0, _20138), vec3(clamp(_5647 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_20138 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_247.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _22461 = _5638;
        }
        float _5693 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _5073) * clamp(_247.Foam * 1.60000002384185791015625, 0.0, 1.0);
        imageStore(OutputTexture, ivec2(_4752), vec4(mix(mix(_22461, _18482, vec3(_5693 * 0.89999997615814208984375)), _18482, vec3(_4839)), max(clamp(_21495.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _5693)), _4839)));
        break;
    } while(false);
}

