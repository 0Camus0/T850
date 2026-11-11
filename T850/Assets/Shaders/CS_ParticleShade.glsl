#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=3568329719862e4b491d1495e20017f1a870ef10db8cc380c665cb90e324c8d9; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1693[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _25935;
bool _26129;
uint _26231;
vec3 _26399;
float _26445;
vec2 _26581;
float _28544;

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
} _277;

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
        if (_277.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _277.OutputWidth) || (gl_GlobalInvocationID.y >= _277.OutputHeight))
        {
            break;
        }
        uvec2 _5300 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _5300.x;
        FluidSize.y = _5300.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _5327 = uvec2(gl_GlobalInvocationID.x, (_277.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _5330 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _5335 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _5349 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _277.OutputWidth)) * 4u;
        uint _5353 = WhitewaterBits_1._data[_5349];
        float _5354 = float(_5353);
        uint _5359 = WhitewaterBits_1._data[_5349 + 1u];
        float _5360 = float(_5359);
        uint _5365 = WhitewaterBits_1._data[_5349 + 2u];
        float _5366 = float(_5365);
        uint _5371 = WhitewaterBits_1._data[_5349 + 3u];
        WhitewaterBits_1._data[_5349] = 0u;
        WhitewaterBits_1._data[_5349 + 1u] = 0u;
        WhitewaterBits_1._data[_5349 + 2u] = 0u;
        WhitewaterBits_1._data[_5349 + 3u] = 0u;
        vec3 _5389 = normalize(_277.SunDirection.xyz);
        vec3 _5391 = normalize(((_277.CameraRight.xyz * _5389.x) + (_277.CameraUp.xyz * _5389.y)) + (_277.CameraForward.xyz * _5389.z));
        vec3 _5394 = refract(-_5391, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _28484 = bvec3((dot(_5394, _5394) < 9.9999999747524270787835121154785e-07) || (_5391.y <= 0.0));
        vec3 _28485 = vec3(_28484.x ? vec3(0.0, -1.0, 0.0).x : _5394.x, _28484.y ? vec3(0.0, -1.0, 0.0).y : _5394.y, _28484.z ? vec3(0.0, -1.0, 0.0).z : _5394.z);
        float _6348 = max(EnvMaxMip - 1.0, 0.0);
        vec3 _25936;
        do
        {
            if (_277.EnvParams.x <= 0.0)
            {
                _25936 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                break;
            }
            _25936 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_6348, 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
            break;
        } while(false);
        vec3 _25937;
        do
        {
            if (_277.EnvParams.x <= 0.0)
            {
                _25937 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _25937 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6348, 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
            break;
        } while(false);
        vec3 _25939;
        do
        {
            if (_277.EnvParams.x <= 0.0)
            {
                _25939 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _25939 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6348, 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
            break;
        } while(false);
        vec3 _25941;
        do
        {
            if (_277.EnvParams.x <= 0.0)
            {
                _25941 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _25941 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_6348, 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
            break;
        } while(false);
        vec3 _25943;
        do
        {
            if (_277.EnvParams.x <= 0.0)
            {
                _25943 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _25943 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_6348, 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
            break;
        } while(false);
        vec3 _6370 = (((((_25936 * 2.0) + _25937) + _25939) + _25941) + _25943) * vec3(0.16666667163372039794921875);
        float _5414 = clamp(1.0 - exp(_5354 * (-0.0006103515625)), 0.0, 1.0) * clamp(_277.FoamParams.w, 0.0, 1.0);
        float _25947;
        vec4 _25948;
        do
        {
            vec2 _6723 = ((_5335 * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight))) - vec2(0.5);
            ivec2 _6726 = ivec2(floor(_6723));
            vec2 _6730 = _6723 - vec2(_6726);
            ivec2 _6905 = clamp(_6726, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6941 = clamp(_6726 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6977 = clamp(_6726 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7013 = clamp(_6726 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6766 = vec4((1.0 - _6730.x) * (1.0 - _6730.y), _6730.x * (1.0 - _6730.y), (1.0 - _6730.x) * _6730.y, _6730.x * _6730.y);
            vec4 _6693 = _6766;
            vec4 _6771 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6905.x, int((FluidSize.y - 1u) - uint(_6905.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6941.x, int((FluidSize.y - 1u) - uint(_6941.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6977.x, int((FluidSize.y - 1u) - uint(_6977.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7013.x, int((FluidSize.y - 1u) - uint(_7013.y)), 0).xy, 0).x);
            vec4 _6694 = _6771;
            float _28234;
            _28234 = 1000000000.0;
            float _28483;
            SPIRV_CROSS_UNROLL
            for (int _25945 = 0; _25945 < 4; _28234 = _28483, _25945++)
            {
                if (_6694[_25945] > 0.0)
                {
                    _28483 = min(_28234, _6694[_25945]);
                }
                else
                {
                    _28483 = _28234;
                }
            }
            vec4 _6697 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _25946 = 0; _25946 < 4; )
            {
                _6697[_25946] = ((_6694[_25946] > 0.0) && (_6694[_25946] < (_28234 + max(0.25, 0.02999999932944774627685546875 * _28234)))) ? _6693[_25946] : 0.0;
                _25946++;
                continue;
            }
            float _6831 = ((_6697.x + _6697.y) + _6697.z) + _6697.w;
            if (_6831 <= 9.9999997473787516355514526367188e-05)
            {
                _25948 = vec4(0.0);
                _25947 = 0.0;
                break;
            }
            ivec2 _7049 = clamp(_6726, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7075 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7049.x, int((FluidSize.y - 1u) - uint(_7049.y)), 0).xy, 0);
            ivec2 _7084 = clamp(_6726 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7110 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7084.x, int((FluidSize.y - 1u) - uint(_7084.y)), 0).xy, 0);
            ivec2 _7119 = clamp(_6726 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7145 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7119.x, int((FluidSize.y - 1u) - uint(_7119.y)), 0).xy, 0);
            ivec2 _7154 = clamp(_6726 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7180 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7154.x, int((FluidSize.y - 1u) - uint(_7154.y)), 0).xy, 0);
            _25948 = vec4((((((_7075.xyz * _6697.x) + (_7110.xyz * _6697.y)) + (_7145.xyz * _6697.z)) + (_7180.xyz * _6697.w)) / vec3(_6831)).x, _28544, _28544, dot(_6766, vec4(_7075.w, _7110.w, _7145.w, _7180.w)));
            _25947 = dot(_6697, _6771) / _6831;
            break;
        } while(false);
        ivec2 _7242 = clamp(_5330, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
        vec4 _7193 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7242.x, int((_277.OutputHeight - 1u) - uint(_7242.y)), 0).xy, 0);
        float _7194 = _7193.x;
        float _7211 = ((_7194 > 0.0) && (abs(_7194 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_7194 - _277.ProjA)) : 1000000000.0;
        if (!(((_25947 > 0.0) && (_25948.w > 0.001000000047497451305389404296875)) && (_25947 < (_7211 - 0.00200000009499490261077880859375))))
        {
            vec2 _7287 = clamp(_5335 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _7298 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_7287.x, 1.0 - _7287.y), 0.0).xyz;
            float _28219;
            vec3 _28230;
            if ((_7211 < 100000000.0) && (_277.WetParams.w > 0.0))
            {
                float _7346 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _5449 = vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w) + (((_277.CameraRight.xyz * (((_5335.x - (0.5 * float(_277.OutputWidth))) * _7211) / _7346)) + (_277.CameraUp.xyz * (((-(_5335.y - (0.5 * float(_277.OutputHeight)))) * _7211) / _7346))) + (_277.CameraForward.xyz * _7211));
                ivec2 _7426 = clamp(_5330, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                vec3 _7386 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_7426.x, int((_277.OutputHeight - 1u) - uint(_7426.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _7388 = length(_7386);
                vec3 _7394 = _7386 / vec3(_7388);
                bvec3 _7395 = bvec3(_7388 > 0.001000000047497451305389404296875);
                float _28220;
                vec3 _28231;
                if ((vec3(_7395.x ? _7394.x : vec3(0.0, 1.0, 0.0).x, _7395.y ? _7394.y : vec3(0.0, 1.0, 0.0).y, _7395.z ? _7394.z : vec3(0.0, 1.0, 0.0).z).y > 0.60000002384185791015625) && (_5449.y > (_277.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _28161;
                    do
                    {
                        int _7479 = int(_277.GridParams.x);
                        int _7482 = int(_277.GridParams.y);
                        if (((_7479 < 4) || (_7482 < 4)) || (_277.WetParams.z <= 0.0))
                        {
                            _28161 = 0.0;
                            break;
                        }
                        vec2 _7504 = ((_5449.xz - _277.WetParams.xy) / vec2(_277.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_7504, vec2(-1.0))) || (_7504.x > float(_7479))) || (_7504.y > float(_7482)))
                        {
                            _28161 = 0.0;
                            break;
                        }
                        ivec2 _7524 = ivec2(floor(_7504));
                        vec2 _7528 = _7504 - vec2(_7524);
                        float _7530 = _7528.x;
                        float _7611 = _7530 * _7530;
                        float _7614 = _7611 * _7530;
                        vec4 _7464 = vec4(((1.0 - (3.0 * _7530)) + (3.0 * _7611)) - _7614, (4.0 - (6.0 * _7611)) + (3.0 * _7614), ((1.0 + (3.0 * _7530)) + (3.0 * _7611)) - (3.0 * _7614), _7614) * 0.16666667163372039794921875;
                        float _7533 = _7528.y;
                        float _7647 = _7533 * _7533;
                        float _7650 = _7647 * _7533;
                        vec4 _7466 = vec4(((1.0 - (3.0 * _7533)) + (3.0 * _7647)) - _7650, (4.0 - (6.0 * _7647)) + (3.0 * _7650), ((1.0 + (3.0 * _7533)) + (3.0 * _7647)) - (3.0 * _7650), _7650) * 0.16666667163372039794921875;
                        float _28154;
                        _28154 = 0.0;
                        float _28469;
                        SPIRV_CROSS_UNROLL
                        for (int _28153 = 0; _28153 < 4; _28154 = _28469, _28153++)
                        {
                            int _7544 = (_7524.y - 1) + _28153;
                            if ((_7544 < 0) || (_7544 >= _7482))
                            {
                                _28469 = _28154;
                                continue;
                            }
                            float _28157;
                            _28157 = 0.0;
                            float _28471;
                            SPIRV_CROSS_UNROLL
                            for (int _28155 = 0; _28155 < 4; _28157 = _28471, _28155++)
                            {
                                int _7562 = (_7524.x - 1) + _28155;
                                if ((_7562 >= 0) && (_7562 < _7479))
                                {
                                    _28471 = _28157 + (_7464[_28155] * float(Wetness_1._data[uint(_7562 + (_7544 * _7479))]));
                                }
                                else
                                {
                                    _28471 = _28157;
                                }
                            }
                            _28469 = _28154 + (_7466[_28153] * _28157);
                        }
                        _28161 = _28154 * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _5471 = clamp(((_28161 * _277.WetParams.w) - 0.00999999977648258209228515625) * 1.01010096073150634765625, 0.0, 1.0);
                    vec3 _28232;
                    if (_5471 > 0.001000000047497451305389404296875)
                    {
                        vec3 _5478 = normalize(_5449 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w));
                        vec3 _28218;
                        do
                        {
                            float _7713 = smoothstep(0.449999988079071044921875, 1.0, _5471);
                            vec3 _7717 = _7298 * mix(1.0, 0.5, clamp(_5471 * 2.0, 0.0, 1.0));
                            if (_7713 <= 0.0)
                            {
                                _28218 = _7717;
                                break;
                            }
                            vec3 _7735 = reflect(_5478, vec3(0.0, 1.0, 0.0));
                            float _7794 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec3 _7784 = vec3(((_5335.x - (0.5 * float(_277.OutputWidth))) * _7211) / _7794, ((-(_5335.y - (0.5 * float(_277.OutputHeight)))) * _7211) / _7794, _7211);
                            float _7811 = dot(_7735, _277.CameraForward.xyz);
                            vec3 _7812 = vec3(dot(_7735, _277.CameraRight.xyz), dot(_7735, _277.CameraUp.xyz), _7811);
                            float _28191;
                            vec3 _28211;
                            do
                            {
                                if (_277.ReflectionDistance <= 0.0)
                                {
                                    _28211 = vec3(0.0);
                                    _28191 = 0.0;
                                    break;
                                }
                                float _7861 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _7811);
                                if (_7861 <= 0.0)
                                {
                                    _28211 = vec3(0.0);
                                    _28191 = 0.0;
                                    break;
                                }
                                float _8058 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _7870 = max(0.0040000001899898052215576171875 * _7211, 0.00999999977648258209228515625);
                                float _7872 = 0.5 * _7870;
                                float _7881;
                                float _7883;
                                bool _28176;
                                bool _28181;
                                float _28186;
                                float _28192;
                                vec3 _28212;
                                int _28167 = 0;
                                float _28168 = _7872;
                                float _28169 = _7870;
                                float _28187 = -1.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_28167 < 80)
                                    {
                                        _7881 = _28168 + _28169;
                                        _7883 = _28169 * 1.07000005245208740234375;
                                        if (_7881 > _277.ReflectionDistance)
                                        {
                                            _28212 = vec3(0.0);
                                            _28192 = 0.0;
                                            _28186 = _28187;
                                            _28181 = false;
                                            _28176 = true;
                                            break;
                                        }
                                        vec3 _7894 = _7784 + (_7812 * _7881);
                                        if (_7894.z <= 0.0500000007450580596923828125)
                                        {
                                            _28212 = vec3(0.0);
                                            _28192 = 0.0;
                                            _28186 = _28187;
                                            _28181 = false;
                                            _28176 = true;
                                            break;
                                        }
                                        float _8098 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        float _8074 = ((_7894.x * _8098) / _7894.z) + (0.5 * float(_277.OutputWidth));
                                        float _8087 = (((-_7894.y) * _8098) / _7894.z) + (0.5 * float(_277.OutputHeight));
                                        if (!((((_8074 >= 0.0) && (_8087 >= 0.0)) && (_8074 < float(_277.OutputWidth))) && (_8087 < float(_277.OutputHeight))))
                                        {
                                            _28212 = vec3(0.0);
                                            _28192 = _26445;
                                            _28186 = _28187;
                                            _28181 = true;
                                            _28176 = false;
                                            break;
                                        }
                                        ivec2 _8182 = clamp(ivec2(vec2(_8074, _8087)), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                        vec4 _8134 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8182.x, int((_277.OutputHeight - 1u) - uint(_8182.y)), 0).xy, 0);
                                        float _8135 = _8134.x;
                                        float _8152 = ((_8135 > 0.0) && (abs(_8135 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_8135 - _277.ProjA)) : 1000000000.0;
                                        if (((_8152 < 100000000.0) && (_7894.z > _8152)) && ((_7894.z - _8152) < max(_28169 * 2.1400001049041748046875, 0.25)))
                                        {
                                            float _28171;
                                            _28171 = _7881;
                                            float _28486;
                                            float _28487;
                                            int _28170 = 0;
                                            float _28174 = _28168;
                                            SPIRV_CROSS_UNROLL
                                            for (; _28170 < 6; _28174 = _28486, _28171 = _28487, _28170++)
                                            {
                                                float _7938 = 0.5 * (_28174 + _28171);
                                                vec3 _7943 = _7784 + (_7812 * _7938);
                                                float _8252 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                ivec2 _8313 = clamp(ivec2(vec2(((_7943.x * _8252) / _7943.z) + (0.5 * float(_277.OutputWidth)), (((-_7943.y) * _8252) / _7943.z) + (0.5 * float(_277.OutputHeight)))), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                                vec4 _8265 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8313.x, int((_277.OutputHeight - 1u) - uint(_8313.y)), 0).xy, 0);
                                                float _8266 = _8265.x;
                                                bool _7950 = _7943.z > (((_8266 > 0.0) && (abs(_8266 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_8266 - _277.ProjA)) : 1000000000.0);
                                                _28486 = _7950 ? _28174 : _7938;
                                                _28487 = _7950 ? _7938 : _28171;
                                            }
                                            vec3 _7964 = _7784 + (_7812 * _28171);
                                            float _8383 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            vec2 _8373 = vec2(((_7964.x * _8383) / _7964.z) + (0.5 * float(_277.OutputWidth)), (((-_7964.y) * _8383) / _7964.z) + (0.5 * float(_277.OutputHeight)));
                                            ivec2 _8441 = clamp(ivec2(_8373), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                            vec3 _8401 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_8441.x, int((_277.OutputHeight - 1u) - uint(_8441.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                            float _8403 = length(_8401);
                                            vec3 _8409 = _8401 / vec3(_8403);
                                            bvec3 _8410 = bvec3(_8403 > 0.001000000047497451305389404296875);
                                            vec3 _8411 = vec3(_8410.x ? _8409.x : vec3(0.0, 1.0, 0.0).x, _8410.y ? _8409.y : vec3(0.0, 1.0, 0.0).y, _8410.z ? _8409.z : vec3(0.0, 1.0, 0.0).z);
                                            if (dot(vec3(dot(_8411, _277.CameraRight.xyz), dot(_8411, _277.CameraUp.xyz), dot(_8411, _277.CameraForward.xyz)), _7812) > 0.100000001490116119384765625)
                                            {
                                                int _7991 = _28167 + 1;
                                                _28169 = _7883;
                                                _28168 = _7881;
                                                _28167 = _7991;
                                                _28187 = _7881;
                                                continue;
                                            }
                                            float _8505 = clamp(((0.039999999105930328369140625 * _28171) * _8058) / max(_7964.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                            float _8507 = 0.5 * _8505;
                                            vec2 _8592 = clamp(_8373 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8618 = clamp((_8373 + vec2(_8507)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8644 = clamp((_8373 + vec2(_8505 * (-0.5), _8507)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8670 = clamp((_8373 + vec2(_8507, _8505 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8696 = clamp((_8373 + vec2(_8505 * (-0.5), _8505 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8722 = clamp((_8373 + vec2(_8505, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8748 = clamp((_8373 + vec2(-_8505, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8774 = clamp((_8373 + vec2(0.0, _8505)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _8800 = clamp((_8373 + vec2(0.0, -_8505)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec3 _8577 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8592.x, 1.0 - _8592.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8618.x, 1.0 - _8618.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8644.x, 1.0 - _8644.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8670.x, 1.0 - _8670.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8696.x, 1.0 - _8696.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8722.x, 1.0 - _8722.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8748.x, 1.0 - _8748.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8774.x, 1.0 - _8774.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_8800.x, 1.0 - _8800.y), 0.0).xyz) * 0.100000001490116119384765625;
                                            _28212 = _8577;
                                            _28192 = _7861;
                                            _28186 = _7881;
                                            _28181 = false;
                                            _28176 = true;
                                            break;
                                        }
                                        int _7991 = _28167 + 1;
                                        _28169 = _7883;
                                        _28168 = _7881;
                                        _28167 = _7991;
                                        _28187 = _7881;
                                        continue;
                                    }
                                    else
                                    {
                                        _28212 = vec3(0.0);
                                        _28192 = _26445;
                                        _28186 = _28187;
                                        _28181 = false;
                                        _28176 = false;
                                        break;
                                    }
                                }
                                if (_28176)
                                {
                                    _28211 = _28212;
                                    _28191 = _28192;
                                    break;
                                }
                                if ((!_28181) || (_28186 < 0.0))
                                {
                                    _28211 = _28212;
                                    _28191 = 0.0;
                                    break;
                                }
                                vec3 _8006 = _7784 + (_7812 * _28186);
                                float _8851 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec2 _8841 = vec2(((_8006.x * _8851) / _8006.z) + (0.5 * float(_277.OutputWidth)), (((-_8006.y) * _8851) / _8006.z) + (0.5 * float(_277.OutputHeight)));
                                ivec2 _8912 = clamp(ivec2(_8841), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                vec4 _8864 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8912.x, int((_277.OutputHeight - 1u) - uint(_8912.y)), 0).xy, 0);
                                float _8865 = _8864.x;
                                float _8882 = ((_8865 > 0.0) && (abs(_8865 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_8865 - _277.ProjA)) : 1000000000.0;
                                float _8958 = clamp(((0.039999999105930328369140625 * _28186) * _8058) / max(_8006.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                float _8960 = 0.5 * _8958;
                                vec2 _9045 = clamp(_8841 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9071 = clamp((_8841 + vec2(_8960)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9097 = clamp((_8841 + vec2(_8958 * (-0.5), _8960)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9123 = clamp((_8841 + vec2(_8960, _8958 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9149 = clamp((_8841 + vec2(_8958 * (-0.5), _8958 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9175 = clamp((_8841 + vec2(_8958, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9201 = clamp((_8841 + vec2(-_8958, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9227 = clamp((_8841 + vec2(0.0, _8958)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9253 = clamp((_8841 + vec2(0.0, -_8958)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec3 _9030 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9045.x, 1.0 - _9045.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9071.x, 1.0 - _9071.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9097.x, 1.0 - _9097.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9123.x, 1.0 - _9123.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9149.x, 1.0 - _9149.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9175.x, 1.0 - _9175.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9201.x, 1.0 - _9201.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9227.x, 1.0 - _9227.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9253.x, 1.0 - _9253.y), 0.0).xyz) * 0.100000001490116119384765625;
                                if (_8882 > 100000000.0)
                                {
                                    _28211 = _9030;
                                    _28191 = _7861;
                                    break;
                                }
                                float _8031 = _8882 - _8006.z;
                                if (_8031 <= 0.0)
                                {
                                    _28211 = _9030;
                                    _28191 = _7861;
                                    break;
                                }
                                _28211 = _9030;
                                _28191 = _7861 * exp((_8031 / max(_7811, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                                break;
                            } while(false);
                            vec3 _28217;
                            do
                            {
                                if (_277.EnvParams.x <= 0.0)
                                {
                                    float _9307 = _7735.y;
                                    _28217 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _9307))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _9307)));
                                    break;
                                }
                                _28217 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_7735.x, _7735.y, -_7735.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
                                break;
                            } while(false);
                            _28218 = mix(_7717, mix(_28217, _28211, vec3(_28191)), vec3((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp((-_5478).y, 0.0, 1.0), 5.0))) * _7713));
                            break;
                        } while(false);
                        _28232 = _28218;
                    }
                    else
                    {
                        _28232 = _7298;
                    }
                    _28231 = _28232;
                    _28220 = _5471;
                }
                else
                {
                    _28231 = _7298;
                    _28220 = 0.0;
                }
                _28230 = _28231;
                _28219 = _28220;
            }
            else
            {
                _28230 = _7298;
                _28219 = 0.0;
            }
            if (_277.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_5327), vec4(_28219, _28219, _28219, 1.0));
                break;
            }
            if ((_28219 <= 0.001000000047497451305389404296875) && (_5414 <= 0.001000000047497451305389404296875))
            {
                if (_277.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_5327), vec4(0.0));
                }
                break;
            }
            vec3 _9343 = ((_277.SunColor.xyz * (clamp(_5391.y, 0.0, 1.0) * 0.3183098733425140380859375)) + _6370) * 0.85000002384185791015625;
            vec3 _9349 = mix(vec3(dot(_9343, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _9343, vec3(0.5));
            if (_28219 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_5327), vec4(mix(_28230, _9349, vec3(_5414)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_5327), vec4(_9349, _5414));
            }
            break;
        }
        float _9392 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _9368 = ((_5335.x - (0.5 * float(_277.OutputWidth))) * _25947) / _9392;
        float _9380 = ((-(_5335.y - (0.5 * float(_277.OutputHeight)))) * _25947) / _9392;
        vec3 _9382 = vec3(_9368, _9380, _25947);
        vec3 _5546 = normalize(_9382);
        vec3 _5548 = -_5546;
        int _5564 = clamp(int((_277.NormalStep * max(float(_277.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _5570 = (2.0 * max(_277.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_277.NormalStep, 1.0);
        ivec2 _5572 = ivec2(_5564, 0);
        vec3 _25955;
        do
        {
            float _25951;
            do
            {
                vec2 _9550 = (((vec2(clamp(_5330 + _5572, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight))) - vec2(0.5);
                ivec2 _9553 = ivec2(floor(_9550));
                vec2 _9557 = _9550 - vec2(_9553);
                ivec2 _9732 = clamp(_9553, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9768 = clamp(_9553 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9804 = clamp(_9553 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9840 = clamp(_9553 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _9520 = vec4((1.0 - _9557.x) * (1.0 - _9557.y), _9557.x * (1.0 - _9557.y), (1.0 - _9557.x) * _9557.y, _9557.x * _9557.y);
                vec4 _9598 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9732.x, int((FluidSize.y - 1u) - uint(_9732.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9768.x, int((FluidSize.y - 1u) - uint(_9768.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9804.x, int((FluidSize.y - 1u) - uint(_9804.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9840.x, int((FluidSize.y - 1u) - uint(_9840.y)), 0).xy, 0).x);
                vec4 _9521 = _9598;
                float _28151;
                _28151 = 1000000000.0;
                float _28468;
                SPIRV_CROSS_UNROLL
                for (int _25949 = 0; _25949 < 4; _28151 = _28468, _25949++)
                {
                    if (_9521[_25949] > 0.0)
                    {
                        _28468 = min(_28151, _9521[_25949]);
                    }
                    else
                    {
                        _28468 = _28151;
                    }
                }
                vec4 _9524 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _25950 = 0; _25950 < 4; )
                {
                    _9524[_25950] = ((_9521[_25950] > 0.0) && (_9521[_25950] < (_28151 + max(0.25, 0.02999999932944774627685546875 * _28151)))) ? _9520[_25950] : 0.0;
                    _25950++;
                    continue;
                }
                float _9658 = ((_9524.x + _9524.y) + _9524.z) + _9524.w;
                if (_9658 <= 9.9999997473787516355514526367188e-05)
                {
                    _25951 = 0.0;
                    break;
                }
                _25951 = dot(_9524, _9598) / _9658;
                break;
            } while(false);
            float _25954;
            do
            {
                vec2 _10080 = (((vec2(clamp(_5330 - _5572, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight))) - vec2(0.5);
                ivec2 _10083 = ivec2(floor(_10080));
                vec2 _10087 = _10080 - vec2(_10083);
                ivec2 _10262 = clamp(_10083, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10298 = clamp(_10083 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10334 = clamp(_10083 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10370 = clamp(_10083 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10050 = vec4((1.0 - _10087.x) * (1.0 - _10087.y), _10087.x * (1.0 - _10087.y), (1.0 - _10087.x) * _10087.y, _10087.x * _10087.y);
                vec4 _10128 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10262.x, int((FluidSize.y - 1u) - uint(_10262.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10298.x, int((FluidSize.y - 1u) - uint(_10298.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10334.x, int((FluidSize.y - 1u) - uint(_10334.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10370.x, int((FluidSize.y - 1u) - uint(_10370.y)), 0).xy, 0).x);
                vec4 _10051 = _10128;
                float _28148;
                _28148 = 1000000000.0;
                float _28467;
                SPIRV_CROSS_UNROLL
                for (int _25952 = 0; _25952 < 4; _28148 = _28467, _25952++)
                {
                    if (_10051[_25952] > 0.0)
                    {
                        _28467 = min(_28148, _10051[_25952]);
                    }
                    else
                    {
                        _28467 = _28148;
                    }
                }
                vec4 _10054 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _25953 = 0; _25953 < 4; )
                {
                    _10054[_25953] = ((_10051[_25953] > 0.0) && (_10051[_25953] < (_28148 + max(0.25, 0.02999999932944774627685546875 * _28148)))) ? _10050[_25953] : 0.0;
                    _25953++;
                    continue;
                }
                float _10188 = ((_10054.x + _10054.y) + _10054.z) + _10054.w;
                if (_10188 <= 9.9999997473787516355514526367188e-05)
                {
                    _25954 = 0.0;
                    break;
                }
                _25954 = dot(_10054, _10128) / _10188;
                break;
            } while(false);
            bool _9426 = (_25951 > 0.0) && (abs(_25951 - _25947) < _5570);
            bool _9435 = (_25954 > 0.0) && (abs(_25947 - _25954) < _5570);
            vec2 _9442 = vec2(_5330 + _5572) + vec2(0.5);
            float _10576 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9446 = vec3(((_9442.x - (0.5 * float(_277.OutputWidth))) * _25951) / _10576, ((-(_9442.y - (0.5 * float(_277.OutputHeight)))) * _25951) / _10576, _25951) - _9382;
            bvec3 _9447 = bvec3(_9426);
            vec3 _9448 = vec3(_9447.x ? _9446.x : vec3(0.0).x, _9447.y ? _9446.y : vec3(0.0).y, _9447.z ? _9446.z : vec3(0.0).z);
            vec2 _9456 = vec2(_5330 - _5572) + vec2(0.5);
            float _10615 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9459 = _9382 - vec3(((_9456.x - (0.5 * float(_277.OutputWidth))) * _25954) / _10615, ((-(_9456.y - (0.5 * float(_277.OutputHeight)))) * _25954) / _10615, _25954);
            bvec3 _9460 = bvec3(_9435);
            vec3 _9461 = vec3(_9460.x ? _9459.x : vec3(0.0).x, _9460.y ? _9459.y : vec3(0.0).y, _9460.z ? _9459.z : vec3(0.0).z);
            if (_9426 && _9435)
            {
                _25955 = (_9448 + _9461) * 0.5;
                break;
            }
            bvec3 _9474 = bvec3(_9426);
            _25955 = vec3(_9474.x ? _9448.x : _9461.x, _9474.y ? _9448.y : _9461.y, _9474.z ? _9448.z : _9461.z);
            break;
        } while(false);
        ivec2 _5579 = ivec2(0, _5564);
        vec3 _25962;
        do
        {
            float _25958;
            do
            {
                vec2 _10773 = (((vec2(clamp(_5330 + _5579, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight))) - vec2(0.5);
                ivec2 _10776 = ivec2(floor(_10773));
                vec2 _10780 = _10773 - vec2(_10776);
                ivec2 _10955 = clamp(_10776, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10991 = clamp(_10776 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11027 = clamp(_10776 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11063 = clamp(_10776 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10743 = vec4((1.0 - _10780.x) * (1.0 - _10780.y), _10780.x * (1.0 - _10780.y), (1.0 - _10780.x) * _10780.y, _10780.x * _10780.y);
                vec4 _10821 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10955.x, int((FluidSize.y - 1u) - uint(_10955.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10991.x, int((FluidSize.y - 1u) - uint(_10991.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11027.x, int((FluidSize.y - 1u) - uint(_11027.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11063.x, int((FluidSize.y - 1u) - uint(_11063.y)), 0).xy, 0).x);
                vec4 _10744 = _10821;
                float _28145;
                _28145 = 1000000000.0;
                float _28466;
                SPIRV_CROSS_UNROLL
                for (int _25956 = 0; _25956 < 4; _28145 = _28466, _25956++)
                {
                    if (_10744[_25956] > 0.0)
                    {
                        _28466 = min(_28145, _10744[_25956]);
                    }
                    else
                    {
                        _28466 = _28145;
                    }
                }
                vec4 _10747 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _25957 = 0; _25957 < 4; )
                {
                    _10747[_25957] = ((_10744[_25957] > 0.0) && (_10744[_25957] < (_28145 + max(0.25, 0.02999999932944774627685546875 * _28145)))) ? _10743[_25957] : 0.0;
                    _25957++;
                    continue;
                }
                float _10881 = ((_10747.x + _10747.y) + _10747.z) + _10747.w;
                if (_10881 <= 9.9999997473787516355514526367188e-05)
                {
                    _25958 = 0.0;
                    break;
                }
                _25958 = dot(_10747, _10821) / _10881;
                break;
            } while(false);
            float _25961;
            do
            {
                vec2 _11303 = (((vec2(clamp(_5330 - _5579, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight))) - vec2(0.5);
                ivec2 _11306 = ivec2(floor(_11303));
                vec2 _11310 = _11303 - vec2(_11306);
                ivec2 _11485 = clamp(_11306, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11521 = clamp(_11306 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11557 = clamp(_11306 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11593 = clamp(_11306 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11273 = vec4((1.0 - _11310.x) * (1.0 - _11310.y), _11310.x * (1.0 - _11310.y), (1.0 - _11310.x) * _11310.y, _11310.x * _11310.y);
                vec4 _11351 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11485.x, int((FluidSize.y - 1u) - uint(_11485.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11521.x, int((FluidSize.y - 1u) - uint(_11521.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11557.x, int((FluidSize.y - 1u) - uint(_11557.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11593.x, int((FluidSize.y - 1u) - uint(_11593.y)), 0).xy, 0).x);
                vec4 _11274 = _11351;
                float _28142;
                _28142 = 1000000000.0;
                float _28465;
                SPIRV_CROSS_UNROLL
                for (int _25959 = 0; _25959 < 4; _28142 = _28465, _25959++)
                {
                    if (_11274[_25959] > 0.0)
                    {
                        _28465 = min(_28142, _11274[_25959]);
                    }
                    else
                    {
                        _28465 = _28142;
                    }
                }
                vec4 _11277 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _25960 = 0; _25960 < 4; )
                {
                    _11277[_25960] = ((_11274[_25960] > 0.0) && (_11274[_25960] < (_28142 + max(0.25, 0.02999999932944774627685546875 * _28142)))) ? _11273[_25960] : 0.0;
                    _25960++;
                    continue;
                }
                float _11411 = ((_11277.x + _11277.y) + _11277.z) + _11277.w;
                if (_11411 <= 9.9999997473787516355514526367188e-05)
                {
                    _25961 = 0.0;
                    break;
                }
                _25961 = dot(_11277, _11351) / _11411;
                break;
            } while(false);
            bool _10649 = (_25958 > 0.0) && (abs(_25958 - _25947) < _5570);
            bool _10658 = (_25961 > 0.0) && (abs(_25947 - _25961) < _5570);
            vec2 _10665 = vec2(_5330 + _5579) + vec2(0.5);
            float _11799 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10669 = vec3(((_10665.x - (0.5 * float(_277.OutputWidth))) * _25958) / _11799, ((-(_10665.y - (0.5 * float(_277.OutputHeight)))) * _25958) / _11799, _25958) - _9382;
            bvec3 _10670 = bvec3(_10649);
            vec3 _10671 = vec3(_10670.x ? _10669.x : vec3(0.0).x, _10670.y ? _10669.y : vec3(0.0).y, _10670.z ? _10669.z : vec3(0.0).z);
            vec2 _10679 = vec2(_5330 - _5579) + vec2(0.5);
            float _11838 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _10682 = _9382 - vec3(((_10679.x - (0.5 * float(_277.OutputWidth))) * _25961) / _11838, ((-(_10679.y - (0.5 * float(_277.OutputHeight)))) * _25961) / _11838, _25961);
            bvec3 _10683 = bvec3(_10658);
            vec3 _10684 = vec3(_10683.x ? _10682.x : vec3(0.0).x, _10683.y ? _10682.y : vec3(0.0).y, _10683.z ? _10682.z : vec3(0.0).z);
            if (_10649 && _10658)
            {
                _25962 = (_10671 + _10684) * 0.5;
                break;
            }
            bvec3 _10697 = bvec3(_10649);
            _25962 = vec3(_10697.x ? _10671.x : _10684.x, _10697.y ? _10671.y : _10684.y, _10697.z ? _10671.z : _10684.z);
            break;
        } while(false);
        vec2 _5590 = _25955.xy * max(_277.NormalScale, 0.0);
        vec3 _25129 = _25955;
        _25129.x = _5590.x;
        _25129.y = _5590.y;
        vec2 _5600 = _25962.xy * max(_277.NormalScale, 0.0);
        vec3 _25133 = _25962;
        _25133.x = _5600.x;
        _25133.y = _5600.y;
        bvec3 _28488 = bvec3(length(_25129) < 9.9999999747524270787835121154785e-07);
        bvec3 _28490 = bvec3(length(_25133) < 9.9999999747524270787835121154785e-07);
        vec3 _5618 = normalize(cross(vec3(_28488.x ? vec3(1.0, 0.0, 0.0).x : _25129.x, _28488.y ? vec3(1.0, 0.0, 0.0).y : _25129.y, _28488.z ? vec3(1.0, 0.0, 0.0).z : _25129.z), vec3(_28490.x ? vec3(0.0, -1.0, 0.0).x : _25133.x, _28490.y ? vec3(0.0, -1.0, 0.0).y : _25133.y, _28490.z ? vec3(0.0, -1.0, 0.0).z : _25133.z)));
        vec3 _25977;
        if (dot(_5618, _5548) < 0.0)
        {
            _25977 = -_5618;
        }
        else
        {
            _25977 = _5618;
        }
        float _5629 = max(_25948.x, 9.9999997473787516355514526367188e-05);
        ivec2 _11933 = clamp(ivec2(floor((_5335 * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        ivec2 _11943 = clamp(_11933, ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _11954 = (uint(_11943.x) + (uint(_11943.y) * FluidSize.x)) * 4u;
        uint _11964 = AuxBits_1._data[_11954 + 1u];
        uint _11976 = AuxBits_1._data[_11954 + 3u];
        float _25975;
        _25975 = float(AuxBits_1._data[_11954]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _25974 = 0; _25974 < 4; )
        {
            ivec2 _11868 = ivec2((_25974 * 2) - 1, 0);
            ivec2 _11873 = ivec2(0, ((_25974 - 2) * 2) - 1);
            bvec2 _11874 = bvec2(_25974 < 2);
            ivec2 _11989 = clamp(_11933 + ivec2(_11874.x ? _11868.x : _11873.x, _11874.y ? _11868.y : _11873.y), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _12035 = clamp(_11933 + ivec2(((_25974 & 1) != int(0u)) ? 1 : (-1), ((_25974 & 2) != int(0u)) ? 1 : (-1)), ivec2(0), ivec2(FluidSize) - ivec2(1));
            _25975 += ((float(AuxBits_1._data[(uint(_11989.x) + (uint(_11989.y) * FluidSize.x)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_12035.x) + (uint(_12035.y) * FluidSize.x)) * 4u]) * 1.525902189314365386962890625e-05));
            _25974++;
            continue;
        }
        float _5648 = clamp((float(_11976) * 0.0009765625) / _5629, 0.0, 1.0);
        vec3 _12093 = ((_277.CameraRight.xyz * _5546.x) + (_277.CameraUp.xyz * _5546.y)) + (_277.CameraForward.xyz * _5546.z);
        vec3 _5654 = vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w) + (((_277.CameraRight.xyz * _9368) + (_277.CameraUp.xyz * _9380)) + (_277.CameraForward.xyz * _25947));
        vec3 _12146 = ((_277.CameraRight.xyz * _25977.x) + (_277.CameraUp.xyz * _25977.y)) + (_277.CameraForward.xyz * _25977.z);
        float _5665 = (_25947 / ((0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_25977, _5548)), 0.07999999821186065673828125);
        float _5689 = (_277.RippleParams.x * mix(_277.RippleFlow.y, 1.0 + _277.RippleFlow.x, clamp((float(AuxBits_1._data[_11954 + 2u]) * 0.0009765625) / _5629, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _12146.y) * clamp(1.0 - (length(_9382) / max(_277.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _25982;
        vec3 _26016;
        if (_5689 > 0.0)
        {
            vec2 _5695 = _5654.xz;
            float _12176 = max(_277.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _25979;
            float _25980;
            _25980 = 0.0;
            _25979 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _25978 = 0; _25978 < 6; )
            {
                float _12188 = _12176 * _1693[_25978];
                float _12193 = _5689 * pow(0.7799999713897705078125, float(_25978));
                float _12200 = clamp((_12188 / max(_5665 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _12276 = (((uint(_25978) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _12284 = ((_12276 >> ((_12276 >> 28u) + 4u)) ^ _12276) * 277803737u;
                float _12207 = 0.60000002384185791015625 + (float(((_12284 >> 22u) ^ _12284) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _12212 = vec2(cos(_12207), sin(_12207));
                uint _12303 = (((uint(_25978) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _12311 = ((_12303 >> ((_12303 >> 28u) + 4u)) ^ _12303) * 277803737u;
                _25980 += (((0.5 * _12193) * _12193) * (1.0 - (_12200 * _12200)));
                _25979 += (_12212 * ((_12193 * _12200) * cos(((dot(_12212, _5695) * (6.283185482025146484375 / _12188)) + ((sqrt(61.63805389404296875 / _12188) * _277.RippleParams.z) * _277.ShadeParams.z)) + (float(((_12311 >> 22u) ^ _12311) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _25978++;
                continue;
            }
            vec3 _5710 = normalize(_12146 + vec3(-_25979.x, 0.0, -_25979.y));
            vec3 _5712 = normalize(vec3(dot(_5710, _277.CameraRight.xyz), dot(_5710, _277.CameraUp.xyz), dot(_5710, _277.CameraForward.xyz)));
            bvec3 _28492 = bvec3(dot(_5712, _5548) < 0.0199999995529651641845703125);
            _26016 = vec3(_28492.x ? _25977.x : _5712.x, _28492.y ? _25977.y : _5712.y, _28492.z ? _25977.z : _5712.z);
            _25982 = _25980;
        }
        else
        {
            _26016 = _25977;
            _25982 = 0.0;
        }
        float _5724 = _25982 * _277.RippleFlow.z;
        if (_277.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_5327), vec4((_26016 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_277.DebugView == 2u)
        {
            float _5747 = clamp(_25947 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5327), vec4(_5747, _5747, _5747, 1.0));
            break;
        }
        float _5767 = (_7211 < 100000000.0) ? (max(_7211 - _25947, 0.0) / max(_5546.z, 0.0500000007450580596923828125)) : 10000.0;
        float _5772 = _5629 * max(_277.ThicknessScale, 0.0);
        float _5777 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _5772 / max(_5767, 0.001000000047497451305389404296875));
        float _26014;
        do
        {
            vec3 _12367 = (_5654 + (_28485 * (max(_5654.y - _277.PoolMin.y, 0.0500000007450580596923828125) / max(-_28485.y, 0.20000000298023223876953125)))) - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
            float _12433 = dot(_12367, _277.CameraForward.xyz);
            if (_12433 <= 0.100000001490116119384765625)
            {
                _26014 = 1.0;
                break;
            }
            float _12474 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _12450 = ((dot(_12367, _277.CameraRight.xyz) * _12474) / _12433) + (0.5 * float(_277.OutputWidth));
            float _12463 = (((-dot(_12367, _277.CameraUp.xyz)) * _12474) / _12433) + (0.5 * float(_277.OutputHeight));
            vec2 _12464 = vec2(_12450, _12463);
            if (!((((_12450 >= 0.0) && (_12463 >= 0.0)) && (_12450 < float(_277.OutputWidth))) && (_12463 < float(_277.OutputHeight))))
            {
                _26014 = 1.0;
                break;
            }
            ivec2 _12558 = clamp(ivec2(_12464), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            vec4 _12510 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_12558.x, int((_277.OutputHeight - 1u) - uint(_12558.y)), 0).xy, 0);
            float _12511 = _12510.x;
            if (abs((((_12511 > 0.0) && (abs(_12511 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_12511 - _277.ProjA)) : 1000000000.0) - _12433) > ((0.1500000059604644775390625 * _12433) + 0.100000001490116119384765625))
            {
                _26014 = 1.0;
                break;
            }
            ivec2 _12618 = clamp(ivec2(_12464), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            _26014 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_12618.x, int((_277.OutputHeight - 1u) - uint(_12618.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _5783 = refract(_5546, _26016, 0.75187969207763671875);
        bvec3 _28496 = bvec3(dot(_5783, _5783) < 9.9999999747524270787835121154785e-07);
        vec3 _5802 = normalize(mix(_5546, vec3(_28496.x ? _5546.x : _5783.x, _28496.y ? _5546.y : _5783.y, _28496.z ? _5546.z : _5783.z), vec3(clamp(_277.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _5777))));
        vec3 _12670 = ((_277.CameraRight.xyz * _5802.x) + (_277.CameraUp.xyz * _5802.y)) + (_277.CameraForward.xyz * _5802.z);
        float _26031;
        do
        {
            if ((((_5654.x < _277.PoolMin.x) || (_5654.x > _277.PoolMax.x)) || (_5654.z < _277.PoolMin.z)) || (_5654.z > _277.PoolMax.z))
            {
                _26031 = 1000.0;
                break;
            }
            float _26019;
            if (_12670.x > 9.9999997473787516355514526367188e-05)
            {
                _26019 = (_277.PoolMax.x - _5654.x) / _12670.x;
            }
            else
            {
                float _26020;
                if (_12670.x < (-9.9999997473787516355514526367188e-05))
                {
                    _26020 = (_277.PoolMin.x - _5654.x) / _12670.x;
                }
                else
                {
                    _26020 = 1000.0;
                }
                _26019 = _26020;
            }
            float _28498 = (_26019 < 1000.0) ? _26019 : 1000.0;
            float _26023;
            if (_12670.z > 9.9999997473787516355514526367188e-05)
            {
                _26023 = (_277.PoolMax.z - _5654.z) / _12670.z;
            }
            else
            {
                float _26024;
                if (_12670.z < (-9.9999997473787516355514526367188e-05))
                {
                    _26024 = (_277.PoolMin.z - _5654.z) / _12670.z;
                }
                else
                {
                    _26024 = 1000.0;
                }
                _26023 = _26024;
            }
            float _28499 = (_26023 < _28498) ? _26023 : _28498;
            float _26029;
            if (_12670.y < (-9.9999997473787516355514526367188e-05))
            {
                float _12800 = (_277.PoolMin.y - _5654.y) / _12670.y;
                _26029 = (_12800 < _28499) ? _12800 : _28499;
            }
            else
            {
                _26029 = _28499;
            }
            _26031 = max(_26029, 0.0);
            break;
        } while(false);
        float _5821 = clamp(max((_5767 * 1.2000000476837158203125) + 0.20000000298023223876953125, (_26031 < 100.0) ? ((_26031 * 1.10000002384185791015625) + 0.100000001490116119384765625) : 0.0), 0.300000011920928955078125, max(_277.MaxTravel, 0.300000011920928955078125) * 2.0);
        vec3 _26365;
        float _26411;
        float _26457;
        float _26502;
        vec2 _26547;
        do
        {
            ivec2 _13142 = clamp(ivec2(_5335), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            vec4 _13094 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13142.x, int((_277.OutputHeight - 1u) - uint(_13142.y)), 0).xy, 0);
            float _13095 = _13094.x;
            vec2 _26280;
            float _26310;
            _26310 = ((_13095 > 0.0) && (abs(_13095 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_13095 - _277.ProjA)) : 1000000000.0;
            _26280 = _5335;
            float _12890;
            float _28503;
            vec2 _28505;
            bool _26039;
            float _26339;
            vec3 _26366;
            float _26412;
            float _26458;
            float _26503;
            vec2 _26548;
            int _26032 = 1;
            float _26034 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_26032 <= 16)
                {
                    float _12884 = float(_26032) * 0.0625;
                    _12890 = (_5821 * _12884) * sqrt(_12884);
                    vec3 _12895 = _9382 + (_5802 * _12890);
                    if (_12895.z <= 0.0500000007450580596923828125)
                    {
                        _26548 = _26581;
                        _26503 = _26445;
                        _26458 = _26445;
                        _26412 = _26445;
                        _26366 = _26399;
                        _26339 = 0.0;
                        _26039 = false;
                        break;
                    }
                    float _13212 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _13188 = ((_12895.x * _13212) / _12895.z) + (0.5 * float(_277.OutputWidth));
                    float _13201 = (((-_12895.y) * _13212) / _12895.z) + (0.5 * float(_277.OutputHeight));
                    vec2 _13202 = vec2(_13188, _13201);
                    if (!((((_13188 >= 0.0) && (_13201 >= 0.0)) && (_13188 < float(_277.OutputWidth))) && (_13201 < float(_277.OutputHeight))))
                    {
                        _26548 = _26581;
                        _26503 = _26445;
                        _26458 = _26445;
                        _26412 = _26445;
                        _26366 = _26399;
                        _26339 = 0.0;
                        _26039 = false;
                        break;
                    }
                    ivec2 _13296 = clamp(ivec2(_13202), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                    vec4 _13248 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13296.x, int((_277.OutputHeight - 1u) - uint(_13296.y)), 0).xy, 0);
                    float _13249 = _13248.x;
                    float _13266 = ((_13249 > 0.0) && (abs(_13249 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_13249 - _277.ProjA)) : 1000000000.0;
                    if (_12895.z >= _13266)
                    {
                        ivec2 _12917 = ivec2(_13202);
                        ivec2 _13377 = clamp(ivec2(floor(((vec2(_12917) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _13386 = clamp(_13377, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _13412 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_13386.x, int((FluidSize.y - 1u) - uint(_13386.y)), 0).xy, 0);
                        float _13413 = _13412.x;
                        ivec2 _13422 = clamp(_13377, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _13509 = clamp(_12917, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec4 _13461 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13509.x, int((_277.OutputHeight - 1u) - uint(_13509.y)), 0).xy, 0);
                        float _13462 = _13461.x;
                        if (!(((_13413 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_13422.x, int((FluidSize.y - 1u) - uint(_13422.y)), 0).xy, 0).w > 0.5)) && ((((_13462 > 0.0) && (abs(_13462 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_13462 - _277.ProjA)) : 1000000000.0) >= (_13413 + 0.0500000007450580596923828125))))
                        {
                            _26548 = _26581;
                            _26503 = _26445;
                            _26458 = _26445;
                            _26412 = _26445;
                            _26366 = _26399;
                            _26339 = 0.0;
                            _26039 = false;
                            break;
                        }
                        float _26036;
                        _26036 = _12890;
                        float _28501;
                        float _28502;
                        int _26035 = 0;
                        float _26037 = _26034;
                        SPIRV_CROSS_UNROLL
                        for (; _26035 < 5; _26037 = _28501, _26036 = _28502, _26035++)
                        {
                            float _12932 = 0.5 * (_26037 + _26036);
                            vec3 _12937 = _9382 + (_5802 * _12932);
                            float _13579 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _13640 = clamp(ivec2(vec2(((_12937.x * _13579) / _12937.z) + (0.5 * float(_277.OutputWidth)), (((-_12937.y) * _13579) / _12937.z) + (0.5 * float(_277.OutputHeight)))), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                            vec4 _13592 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13640.x, int((_277.OutputHeight - 1u) - uint(_13640.y)), 0).xy, 0);
                            float _13593 = _13592.x;
                            bool _12944 = _12937.z >= (((_13593 > 0.0) && (abs(_13593 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_13593 - _277.ProjA)) : 1000000000.0);
                            _28501 = _12944 ? _26037 : _12932;
                            _28502 = _12944 ? _12932 : _26036;
                        }
                        vec3 _12958 = _9382 + (_5802 * _26036);
                        float _13710 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _12969 = clamp(vec2(((_12958.x * _13710) / _12958.z) + (0.5 * float(_277.OutputWidth)), (((-_12958.y) * _13710) / _12958.z) + (0.5 * float(_277.OutputHeight))), vec2(0.5), vec2(float(_277.OutputWidth), float(_277.OutputHeight)) - vec2(0.5));
                        ivec2 _13771 = clamp(ivec2(_12969), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec4 _13723 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13771.x, int((_277.OutputHeight - 1u) - uint(_13771.y)), 0).xy, 0);
                        float _13724 = _13723.x;
                        float _13741 = ((_13724 > 0.0) && (abs(_13724 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_13724 - _277.ProjA)) : 1000000000.0;
                        float _13849 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        _26548 = _12969;
                        _26503 = _13741;
                        _26458 = _26036;
                        _26412 = 1.0;
                        _26366 = vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w) + (((_277.CameraRight.xyz * (((_12969.x - (0.5 * float(_277.OutputWidth))) * _13741) / _13849)) + (_277.CameraUp.xyz * (((-(_12969.y - (0.5 * float(_277.OutputHeight)))) * _13741) / _13849))) + (_277.CameraForward.xyz * _13741));
                        _26339 = 1.0;
                        _26039 = true;
                        break;
                    }
                    ivec2 _12993 = ivec2(_13202 - vec2(0.5));
                    ivec2 _13922 = clamp(ivec2(floor(((vec2(_12993) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _13931 = clamp(_13922, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _13957 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_13931.x, int((FluidSize.y - 1u) - uint(_13931.y)), 0).xy, 0);
                    float _13958 = _13957.x;
                    ivec2 _13967 = clamp(_13922, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14054 = clamp(_12993, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                    vec4 _14006 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14054.x, int((_277.OutputHeight - 1u) - uint(_14054.y)), 0).xy, 0);
                    float _14007 = _14006.x;
                    ivec2 _12998 = ivec2(_13202 + vec2(0.5));
                    ivec2 _14135 = clamp(ivec2(floor(((vec2(_12998) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14144 = clamp(_14135, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _14170 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14144.x, int((FluidSize.y - 1u) - uint(_14144.y)), 0).xy, 0);
                    float _14171 = _14170.x;
                    ivec2 _14180 = clamp(_14135, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _14267 = clamp(_12998, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                    vec4 _14219 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14267.x, int((_277.OutputHeight - 1u) - uint(_14267.y)), 0).xy, 0);
                    float _14220 = _14219.x;
                    bool _13000 = (((_13958 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_13967.x, int((FluidSize.y - 1u) - uint(_13967.y)), 0).xy, 0).w > 0.5)) && ((((_14007 > 0.0) && (abs(_14007 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_14007 - _277.ProjA)) : 1000000000.0) >= (_13958 + 0.0500000007450580596923828125))) && (((_14171 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14180.x, int((FluidSize.y - 1u) - uint(_14180.y)), 0).xy, 0).w > 0.5)) && ((((_14220 > 0.0) && (abs(_14220 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_14220 - _277.ProjA)) : 1000000000.0) >= (_14171 + 0.0500000007450580596923828125)));
                    _28503 = _13000 ? _13266 : _26310;
                    bvec2 _28504 = bvec2(_13000);
                    _28505 = vec2(_28504.x ? _13202.x : _26280.x, _28504.y ? _13202.y : _26280.y);
                    _26032++;
                    _26310 = _28503;
                    _26280 = _28505;
                    _26034 = _12890;
                    continue;
                }
                else
                {
                    _26548 = _26581;
                    _26503 = _26445;
                    _26458 = _26445;
                    _26412 = _26445;
                    _26366 = _26399;
                    _26339 = 0.0;
                    _26039 = false;
                    break;
                }
            }
            if (_26039)
            {
                _26547 = _26548;
                _26502 = _26503;
                _26457 = _26458;
                _26411 = _26412;
                _26365 = _26366;
                break;
            }
            vec3 _14319 = ((_277.CameraRight.xyz * _5802.x) + (_277.CameraUp.xyz * _5802.y)) + (_277.CameraForward.xyz * _5802.z);
            float _26054;
            vec3 _26055;
            do
            {
                if ((((_5654.x < _277.PoolMin.x) || (_5654.x > _277.PoolMax.x)) || (_5654.z < _277.PoolMin.z)) || (_5654.z > _277.PoolMax.z))
                {
                    _26055 = vec3(0.0, 1.0, 0.0);
                    _26054 = 1000.0;
                    break;
                }
                float _26042;
                if (_14319.x > 9.9999997473787516355514526367188e-05)
                {
                    _26042 = (_277.PoolMax.x - _5654.x) / _14319.x;
                }
                else
                {
                    float _26043;
                    if (_14319.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _26043 = (_277.PoolMin.x - _5654.x) / _14319.x;
                    }
                    else
                    {
                        _26043 = 1000.0;
                    }
                    _26042 = _26043;
                }
                bool _14381 = _26042 < 1000.0;
                vec3 _26059;
                if (_14381)
                {
                    _26059 = vec3((_14319.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                }
                else
                {
                    _26059 = vec3(0.0, 1.0, 0.0);
                }
                float _28506 = _14381 ? _26042 : 1000.0;
                float _26046;
                if (_14319.z > 9.9999997473787516355514526367188e-05)
                {
                    _26046 = (_277.PoolMax.z - _5654.z) / _14319.z;
                }
                else
                {
                    float _26047;
                    if (_14319.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _26047 = (_277.PoolMin.z - _5654.z) / _14319.z;
                    }
                    else
                    {
                        _26047 = 1000.0;
                    }
                    _26046 = _26047;
                }
                bool _14419 = _26046 < _28506;
                vec3 _26057;
                if (_14419)
                {
                    _26057 = vec3(0.0, 0.0, _28544);
                }
                else
                {
                    _26057 = _26059;
                }
                float _28507 = _14419 ? _26046 : _28506;
                float _26052;
                vec3 _26056;
                if (_14319.y < (-9.9999997473787516355514526367188e-05))
                {
                    float _14439 = (_277.PoolMin.y - _5654.y) / _14319.y;
                    bool _14442 = _14439 < _28507;
                    bvec3 _28508 = bvec3(_14442);
                    _26056 = vec3(_28508.x ? vec3(0.0, 1.0, 0.0).x : _26057.x, _28508.y ? vec3(0.0, 1.0, 0.0).y : _26057.y, _28508.z ? vec3(0.0, 1.0, 0.0).z : _26057.z);
                    _26052 = _14442 ? _14439 : _28507;
                }
                else
                {
                    _26056 = _26057;
                    _26052 = _28507;
                }
                _26055 = _26056;
                _26054 = max(_26052, 0.0);
                break;
            } while(false);
            if (_26054 < 100.0)
            {
                vec3 _13025 = _5654 + (_14319 * _26054);
                uint _26192;
                vec2 _26263;
                float _26270;
                do
                {
                    bool _26064;
                    vec2 _26065;
                    float _26066;
                    do
                    {
                        vec3 _14693 = _13025 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                        float _14751 = dot(_14693, _277.CameraForward.xyz);
                        if (_14751 <= 0.0500000007450580596923828125)
                        {
                            _26066 = 1000000000.0;
                            _26065 = vec2(0.5);
                            _26064 = false;
                            break;
                        }
                        float _14792 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _14768 = ((dot(_14693, _277.CameraRight.xyz) * _14792) / _14751) + (0.5 * float(_277.OutputWidth));
                        float _14781 = (((-dot(_14693, _277.CameraUp.xyz)) * _14792) / _14751) + (0.5 * float(_277.OutputHeight));
                        if (!((((_14768 >= 0.0) && (_14781 >= 0.0)) && (_14768 < float(_277.OutputWidth))) && (_14781 < float(_277.OutputHeight))))
                        {
                            _26066 = 1000000000.0;
                            _26065 = vec2(0.5);
                            _26064 = false;
                            break;
                        }
                        vec2 _14710 = floor(vec2(_14768, _14781)) + vec2(0.5);
                        ivec2 _14876 = clamp(ivec2(_14710), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec4 _14828 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14876.x, int((_277.OutputHeight - 1u) - uint(_14876.y)), 0).xy, 0);
                        float _14829 = _14828.x;
                        float _14846 = ((_14829 > 0.0) && (abs(_14829 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_14829 - _277.ProjA)) : 1000000000.0;
                        _26066 = _14846;
                        _26065 = _14710;
                        _26064 = abs(_14846 - _14751) < ((0.0199999995529651641845703125 * _14751) + 0.02999999932944774627685546875);
                        break;
                    } while(false);
                    if (_26064)
                    {
                        _26270 = _26066;
                        _26263 = _26065;
                        _26192 = 1u;
                        break;
                    }
                    vec3 _14499 = _13025 - vec3(_277.CameraRight.w, _25935, _277.CameraForward.w);
                    vec3 _14507 = (_277.PoolMin.xyz + _277.PoolMax.xyz) * 0.5;
                    vec3 _14512 = vec3((_14499.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _14517 = vec3(0.0, 0.0, (_14499.z >= 0.0) ? 1.0 : (-1.0));
                    vec3 _14524 = vec3((_14507.x >= _13025.x) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _14531 = vec3(0.0, 0.0, (_14507.z >= _13025.z) ? 1.0 : (-1.0));
                    vec3 _26067;
                    vec3 _26068;
                    vec3 _26073;
                    vec3 _26075;
                    if (abs(_26055.y) > 0.5)
                    {
                        bool _14543 = abs(_14499.z) >= abs(_14499.x);
                        bvec3 _14547 = bvec3(_14543);
                        bvec3 _14552 = bvec3(_14543);
                        bvec3 _14557 = bvec3(_14543);
                        bvec3 _14562 = bvec3(_14543);
                        _26075 = vec3(_14547.x ? _14517.x : _14512.x, _14547.y ? _14517.y : _14512.y, _14547.z ? _14517.z : _14512.z);
                        _26073 = vec3(_14557.x ? _14512.x : _14517.x, _14557.y ? _14512.y : _14517.y, _14557.z ? _14512.z : _14517.z);
                        _26068 = vec3(_14562.x ? _14531.x : _14524.x, _14562.y ? _14531.y : _14524.y, _14562.z ? _14531.z : _14524.z);
                        _26067 = vec3(_14552.x ? _14524.x : _14531.x, _14552.y ? _14524.y : _14531.y, _14552.z ? _14524.z : _14531.z);
                    }
                    else
                    {
                        bool _14568 = abs(_26055.x) > 0.5;
                        bvec3 _14572 = bvec3(_14568);
                        bvec3 _14577 = bvec3(_14568);
                        _26075 = vec3(_14572.x ? _14517.x : _14512.x, _14572.y ? _14517.y : _14512.y, _14572.z ? _14517.z : _14512.z);
                        _26073 = vec3(0.0, 1.0, 0.0);
                        _26068 = vec3(_14577.x ? _14531.x : _14524.x, _14577.y ? _14531.y : _14524.y, _14577.z ? _14531.z : _14524.z);
                        _26067 = vec3(0.0, 1.0, 0.0);
                    }
                    float _14959 = ((_26067.y > 0.5) ? (_277.PoolMin.w - _13025.y) : ((_26067.x > 0.5) ? (_277.PoolMax.x - _13025.x) : ((_26067.x < (-0.5)) ? (_13025.x - _277.PoolMin.x) : ((_26067.z > 0.5) ? (_277.PoolMax.z - _13025.z) : (_13025.z - _277.PoolMin.z))))) - 0.02999999932944774627685546875;
                    float _15003 = ((_26068.y > 0.5) ? (_277.PoolMin.w - _13025.y) : ((_26068.x > 0.5) ? (_277.PoolMax.x - _13025.x) : ((_26068.x < (-0.5)) ? (_13025.x - _277.PoolMin.x) : ((_26068.z > 0.5) ? (_277.PoolMax.z - _13025.z) : (_13025.z - _277.PoolMin.z))))) - 0.02999999932944774627685546875;
                    vec2 _26265;
                    _26265 = _26065;
                    bool _26178;
                    uint _26193;
                    vec2 _26264;
                    vec2 _26267;
                    float _26271;
                    float _26274;
                    bool _28290;
                    int _26069 = 0;
                    bool _26092;
                    float _26272 = _26066;
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_26069 < 11)
                        {
                            if (_26069 == 1)
                            {
                                vec3 _14596 = _13025 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                float _15029 = dot(_14596, _277.CameraForward.xyz);
                                vec2 _26268;
                                float _26275;
                                if (_15029 > 0.0500000007450580596923828125)
                                {
                                    float _15070 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _15046 = ((dot(_14596, _277.CameraRight.xyz) * _15070) / _15029) + (0.5 * float(_277.OutputWidth));
                                    float _15059 = (((-dot(_14596, _277.CameraUp.xyz)) * _15070) / _15029) + (0.5 * float(_277.OutputHeight));
                                    vec2 _15060 = vec2(_15046, _15059);
                                    ivec2 _14607 = ivec2(_15060);
                                    ivec2 _15144 = clamp(ivec2(floor(((vec2(_14607) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _15153 = clamp(_15144, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _15179 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15153.x, int((FluidSize.y - 1u) - uint(_15153.y)), 0).xy, 0);
                                    float _15180 = _15179.x;
                                    ivec2 _15189 = clamp(_15144, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _15276 = clamp(_14607, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                    vec4 _15228 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15276.x, int((_277.OutputHeight - 1u) - uint(_15276.y)), 0).xy, 0);
                                    float _15229 = _15228.x;
                                    vec2 _26269;
                                    float _26276;
                                    if (((((_15046 >= 0.0) && (_15059 >= 0.0)) && (_15046 < float(_277.OutputWidth))) && (_15059 < float(_277.OutputHeight))) && (((_15180 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15189.x, int((FluidSize.y - 1u) - uint(_15189.y)), 0).xy, 0).w > 0.5)) && ((((_15229 > 0.0) && (abs(_15229 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_15229 - _277.ProjA)) : 1000000000.0) >= (_15180 + 0.0500000007450580596923828125))))
                                    {
                                        vec2 _14614 = floor(_15060) + vec2(0.5);
                                        ivec2 _15367 = clamp(ivec2(_14614), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                        vec4 _15319 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15367.x, int((_277.OutputHeight - 1u) - uint(_15367.y)), 0).xy, 0);
                                        float _15320 = _15319.x;
                                        float _15337 = ((_15320 > 0.0) && (abs(_15320 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_15320 - _277.ProjA)) : 1000000000.0;
                                        if (_15337 < _15029)
                                        {
                                            _26271 = _15337;
                                            _26264 = _14614;
                                            _26193 = 2u;
                                            _26178 = true;
                                            break;
                                        }
                                        _26276 = _15337;
                                        _26269 = _14614;
                                    }
                                    else
                                    {
                                        _26276 = _26272;
                                        _26269 = _26265;
                                    }
                                    _26275 = _26276;
                                    _26268 = _26269;
                                }
                                else
                                {
                                    _26275 = _26272;
                                    _26268 = _26265;
                                }
                                _28290 = _26092;
                                _26274 = _26275;
                                _26267 = _26268;
                                int _14674 = _26069 + 1;
                                _26092 = _28290;
                                _26069 = _14674;
                                _26272 = _26274;
                                _26265 = _26267;
                                continue;
                            }
                            bool _14628 = _26069 == 2;
                            bool _14630 = _26069 >= 3;
                            int _14632 = _26069 & 1;
                            bool _14635 = _14628 || (_14630 && (_14632 == 0));
                            bool _14637 = _26069 < 3;
                            int _14639 = _26069 - 3;
                            float _14644 = _14637 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_14639 / 2)));
                            if (_14644 >= (_14635 ? _15003 : _14959))
                            {
                                _28290 = _26092;
                                _26274 = _26272;
                                _26267 = _26265;
                                int _14674 = _26069 + 1;
                                _26092 = _28290;
                                _26069 = _14674;
                                _26272 = _26274;
                                _26265 = _26267;
                                continue;
                            }
                            bvec3 _14657 = bvec3(_14635);
                            vec3 _14661 = _13025 + (vec3(_14657.x ? _26068.x : _26067.x, _14657.y ? _26068.y : _26067.y, _14657.z ? _26068.z : _26067.z) * _14644);
                            bvec3 _14665 = bvec3(_14635);
                            vec3 _14666 = vec3(_14665.x ? _26073.x : _26075.x, _14665.y ? _26073.y : _26075.y, _14665.z ? _26073.z : _26075.z);
                            bool _26089;
                            vec2 _26153;
                            float _26157;
                            do
                            {
                                float _15530 = ((_14666.y > 0.5) ? (_277.PoolMin.w - _14661.y) : ((_14666.x > 0.5) ? (_277.PoolMax.x - _14661.x) : ((_14666.x < (-0.5)) ? (_14661.x - _277.PoolMin.x) : ((_14666.z > 0.5) ? (_277.PoolMax.z - _14661.z) : (_14661.z - _277.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _26154;
                                _26154 = vec2(0.5);
                                float _15436;
                                float _15478;
                                vec2 _26080;
                                float _26081;
                                bool _26086;
                                bool _26090;
                                int _26076 = 0;
                                float _26077 = 0.0;
                                float _26078 = 0.0599999986588954925537109375;
                                float _26158 = 1000000000.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if ((_26076 < 40) && (_26077 < _15530))
                                    {
                                        _15436 = min(_26077 + _26078, _15530);
                                        bool _26079;
                                        do
                                        {
                                            vec3 _15544 = (_14661 + (_14666 * _15436)) - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                            float _15602 = dot(_15544, _277.CameraForward.xyz);
                                            if (_15602 <= 0.0500000007450580596923828125)
                                            {
                                                _26081 = 1000000000.0;
                                                _26080 = vec2(0.5);
                                                _26079 = false;
                                                break;
                                            }
                                            float _15643 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            float _15619 = ((dot(_15544, _277.CameraRight.xyz) * _15643) / _15602) + (0.5 * float(_277.OutputWidth));
                                            float _15632 = (((-dot(_15544, _277.CameraUp.xyz)) * _15643) / _15602) + (0.5 * float(_277.OutputHeight));
                                            if (!((((_15619 >= 0.0) && (_15632 >= 0.0)) && (_15619 < float(_277.OutputWidth))) && (_15632 < float(_277.OutputHeight))))
                                            {
                                                _26081 = 1000000000.0;
                                                _26080 = vec2(0.5);
                                                _26079 = false;
                                                break;
                                            }
                                            vec2 _15561 = floor(vec2(_15619, _15632)) + vec2(0.5);
                                            ivec2 _15727 = clamp(ivec2(_15561), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                            vec4 _15679 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15727.x, int((_277.OutputHeight - 1u) - uint(_15727.y)), 0).xy, 0);
                                            float _15680 = _15679.x;
                                            float _15697 = ((_15680 > 0.0) && (abs(_15680 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_15680 - _277.ProjA)) : 1000000000.0;
                                            _26081 = _15697;
                                            _26080 = _15561;
                                            _26079 = abs(_15697 - _15602) < ((0.0199999995529651641845703125 * _15602) + 0.02999999932944774627685546875);
                                            break;
                                        } while(false);
                                        if (_26079)
                                        {
                                            vec2 _26155;
                                            float _26159;
                                            _26159 = _26081;
                                            _26155 = _26080;
                                            float _28511;
                                            float _28512;
                                            float _28513;
                                            vec2 _28515;
                                            int _26085 = 0;
                                            float _26160 = _26077;
                                            float _26162 = _15436;
                                            SPIRV_CROSS_UNROLL
                                            for (; _26085 < 4; _26162 = _28511, _26160 = _28512, _26159 = _28513, _26155 = _28515, _26085++)
                                            {
                                                float _15454 = 0.5 * (_26160 + _26162);
                                                bool _26163;
                                                vec2 _26164;
                                                float _26165;
                                                do
                                                {
                                                    vec3 _15771 = (_14661 + (_14666 * _15454)) - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                                    float _15829 = dot(_15771, _277.CameraForward.xyz);
                                                    if (_15829 <= 0.0500000007450580596923828125)
                                                    {
                                                        _26165 = 1000000000.0;
                                                        _26164 = vec2(0.5);
                                                        _26163 = false;
                                                        break;
                                                    }
                                                    float _15870 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                    float _15846 = ((dot(_15771, _277.CameraRight.xyz) * _15870) / _15829) + (0.5 * float(_277.OutputWidth));
                                                    float _15859 = (((-dot(_15771, _277.CameraUp.xyz)) * _15870) / _15829) + (0.5 * float(_277.OutputHeight));
                                                    if (!((((_15846 >= 0.0) && (_15859 >= 0.0)) && (_15846 < float(_277.OutputWidth))) && (_15859 < float(_277.OutputHeight))))
                                                    {
                                                        _26165 = 1000000000.0;
                                                        _26164 = vec2(0.5);
                                                        _26163 = false;
                                                        break;
                                                    }
                                                    vec2 _15788 = floor(vec2(_15846, _15859)) + vec2(0.5);
                                                    ivec2 _15954 = clamp(ivec2(_15788), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                                    vec4 _15906 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15954.x, int((_277.OutputHeight - 1u) - uint(_15954.y)), 0).xy, 0);
                                                    float _15907 = _15906.x;
                                                    float _15924 = ((_15907 > 0.0) && (abs(_15907 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_15907 - _277.ProjA)) : 1000000000.0;
                                                    _26165 = _15924;
                                                    _26164 = _15788;
                                                    _26163 = abs(_15924 - _15829) < ((0.0199999995529651641845703125 * _15829) + 0.02999999932944774627685546875);
                                                    break;
                                                } while(false);
                                                _28511 = _26163 ? _15454 : _26162;
                                                _28512 = _26163 ? _26160 : _15454;
                                                _28513 = _26163 ? _26165 : _26159;
                                                bvec2 _28514 = bvec2(_26163);
                                                _28515 = vec2(_28514.x ? _26164.x : _26155.x, _28514.y ? _26164.y : _26155.y);
                                            }
                                            _26157 = _26159;
                                            _26153 = _26155;
                                            _26090 = true;
                                            _26086 = true;
                                            break;
                                        }
                                        _15478 = min(_26078 * 1.2000000476837158203125, 0.300000011920928955078125);
                                        _26078 = _15478;
                                        _26077 = _15436;
                                        _26076++;
                                        _26158 = _26081;
                                        _26154 = _26080;
                                        continue;
                                    }
                                    else
                                    {
                                        _26157 = _26158;
                                        _26153 = _26154;
                                        _26090 = _26092;
                                        _26086 = false;
                                        break;
                                    }
                                }
                                if (_26086)
                                {
                                    _26089 = _26090;
                                    break;
                                }
                                _26089 = false;
                                break;
                            } while(false);
                            if (_26089)
                            {
                                _26271 = _26157;
                                _26264 = _26153;
                                _26193 = 1u;
                                _26178 = true;
                                break;
                            }
                            _28290 = _26089;
                            _26274 = _26157;
                            _26267 = _26153;
                            int _14674 = _26069 + 1;
                            _26092 = _28290;
                            _26069 = _14674;
                            _26272 = _26274;
                            _26265 = _26267;
                            continue;
                        }
                        else
                        {
                            _26271 = _26272;
                            _26264 = _26265;
                            _26193 = _26231;
                            _26178 = false;
                            break;
                        }
                    }
                    if (_26178)
                    {
                        _26270 = _26271;
                        _26263 = _26264;
                        _26192 = _26193;
                        break;
                    }
                    _26270 = _26271;
                    _26263 = _26264;
                    _26192 = 0u;
                    break;
                } while(false);
                if (_26192 != 0u)
                {
                    float _16032 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec3 _13046 = vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w) + (((_277.CameraRight.xyz * (((_26263.x - (0.5 * float(_277.OutputWidth))) * _26270) / _16032)) + (_277.CameraUp.xyz * (((-(_26263.y - (0.5 * float(_277.OutputHeight)))) * _26270) / _16032))) + (_277.CameraForward.xyz * _26270));
                    bvec3 _13047 = bvec3(_26192 == 1u);
                    vec3 _13048 = vec3(_13047.x ? _13025.x : _13046.x, _13047.y ? _13025.y : _13046.y, _13047.z ? _13025.z : _13046.z);
                    _26547 = _26263;
                    _26502 = _26270;
                    _26457 = (_26192 == 1u) ? _26054 : length(_13048 - _5654);
                    _26411 = 1.0;
                    _26365 = _13048;
                    break;
                }
            }
            float _13070 = min(_26054, _5821);
            _26547 = _26280;
            _26502 = _26310;
            _26457 = _13070;
            _26411 = _26339;
            _26365 = _5654 + (_14319 * _13070);
            break;
        } while(false);
        vec3 _26637;
        vec3 _26638;
        do
        {
            vec2 _16269 = clamp(_26547 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _16280 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_16269.x, 1.0 - _16269.y), 0.0).xyz;
            if ((_26411 < 0.5) || (_26502 > 100000000.0))
            {
                _26638 = vec3(0.0, 1.0, 0.5);
                _26637 = _16280;
                break;
            }
            ivec2 _16103 = ivec2(_26547);
            float _16109 = _5654.y - _26365.y;
            if (_16109 <= 0.00999999977648258209228515625)
            {
                _26638 = vec3(0.0, 1.0, 0.5);
                _26637 = _16280;
                break;
            }
            ivec2 _16310 = clamp(_16103, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            ivec2 _16398 = clamp(_16103, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            vec3 _16358 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_16398.x, int((_277.OutputHeight - 1u) - uint(_16398.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _16360 = length(_16358);
            vec3 _16366 = _16358 / vec3(_16360);
            bvec3 _16367 = bvec3(_16360 > 0.001000000047497451305389404296875);
            ivec2 _16458 = clamp(_16103, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            float _16155 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_16310.x, int((_277.OutputHeight - 1u) - uint(_16310.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_16367.x ? _16366.x : vec3(0.0, 1.0, 0.0).x, _16367.y ? _16366.y : vec3(0.0, 1.0, 0.0).y, _16367.z ? _16366.z : vec3(0.0, 1.0, 0.0).z), _5391), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_16458.x, int((_277.OutputHeight - 1u) - uint(_16458.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _277.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_16280, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _16159 = max(-_28485.y, 0.20000000298023223876953125);
            vec3 _16173 = mix(vec3(1.0), exp(((-_277.FluidTint.xyz) * _16109) / vec3(_16159)), vec3(_277.LightParams.x));
            float _26636;
            if ((_277.CausticParams.x > 0.0) && (_16155 > 0.0))
            {
                vec2 _16202 = _26365.xz - ((_28485.xz / vec2(_16159)) * _16109);
                vec2 _16513 = _16202 * (6.283185482025146484375 / max(_277.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _16536 = _16513 + (vec2(sin((_16513.y * 0.730000019073486328125) + ((_277.ShadeParams.z * 0.89999997615814208984375) * _277.CausticParams.z)), sin((_16513.x * 0.61000001430511474609375) - ((_277.ShadeParams.z * 1.10000002384185791015625) * _277.CausticParams.z))) * 0.89999997615814208984375);
                float _26635;
                do
                {
                    if ((_277.GridParams.z < 2u) || (_277.GridParams.w < 2u))
                    {
                        _26635 = 1.0;
                        break;
                    }
                    vec2 _16646 = clamp(((_16202 - _277.PoolMin.xz) / max(_277.PoolMax.xz - _277.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_277.GridParams.z - 1u), float(_277.GridParams.w - 1u)), vec2(0.0), vec2(float(_277.GridParams.z - 1u), float(_277.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _16648 = uvec2(_16646);
                    vec2 _16652 = _16646 - vec2(_16648);
                    uint _16659 = _16648.x + (_16648.y * _277.GridParams.z);
                    _26635 = mix(mix(CausticField_1._data[_16659], CausticField_1._data[_16659 + 1u], _16652.x), mix(CausticField_1._data[_16659 + _277.GridParams.z], CausticField_1._data[(_16659 + _277.GridParams.z) + 1u], _16652.x), _16652.y);
                    break;
                } while(false);
                _26636 = min(max(1.0 + ((_277.CausticParams.x * (exp((-_16109) / max(_277.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _16109))) * ((pow(clamp((1.0 - abs(sin(_16536.x + ((_277.ShadeParams.z * 0.60000002384185791015625) * _277.CausticParams.z)) * sin((_16536.y * 1.12999999523162841796875) - ((_277.ShadeParams.z * 0.4000000059604644775390625) * _277.CausticParams.z)))) * (1.0 - abs(sin(((_16536.x + _16536.y) * 0.709999978542327880859375) - ((_277.ShadeParams.z * 0.5) * _277.CausticParams.z)) * sin(((_16536.x - _16536.y) * 0.829999983310699462890625) + ((_277.ShadeParams.z * 0.699999988079071044921875) * _277.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_26635, 0.0500000007450580596923828125), _277.LightParams.y), 2.5);
            }
            else
            {
                _26636 = 1.0;
            }
            _26638 = vec3(_16155, dot(_16173, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _26636);
            _26637 = _16280 * ((mix(vec3(1.0), exp(((-_277.FluidTint.xyz) * _16109) * 1.2000000476837158203125), vec3(_277.LightParams.x)) * (1.0 - _16155)) + ((_16173 * _16155) * _26636));
            break;
        } while(false);
        float _5842 = mix(_5772, max(_26457, _5772), _5777);
        vec3 _27664;
        if (_5777 < 0.5)
        {
            vec3 _16716 = ((_277.CameraRight.xyz * _5802.x) + (_277.CameraUp.xyz * _5802.y)) + (_277.CameraForward.xyz * _5802.z);
            vec3 _27665;
            if (_16716.y < (-0.0199999995529651641845703125))
            {
                float _5859 = (_277.PoolMin.w - _5654.y) / _16716.y;
                vec3 _5864 = _5654 + (_16716 * _5859);
                vec3 _27666;
                if (((_5859 > _5772) && ((((_5864.x > _277.PoolMin.x) && (_5864.x < _277.PoolMax.x)) && (_5864.z > _277.PoolMin.z)) && (_5864.z < _277.PoolMax.z))) && ((_26411 < 0.5) || (_5859 < _26457)))
                {
                    float _16772 = length(_5864 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w)) / ((0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _16777 = _277.RippleParams.x * _277.RippleFlow.y;
                    vec2 _16779 = _5864.xz;
                    float _16911 = max(_277.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _26643;
                    _26643 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _26642 = 0; _26642 < 6; )
                    {
                        float _16923 = _16911 * _1693[_26642];
                        uint _17011 = (((uint(_26642) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _17019 = ((_17011 >> ((_17011 >> 28u) + 4u)) ^ _17011) * 277803737u;
                        float _16942 = 0.60000002384185791015625 + (float(((_17019 >> 22u) ^ _17019) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _16947 = vec2(cos(_16942), sin(_16942));
                        uint _17038 = (((uint(_26642) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _17046 = ((_17038 >> ((_17038 >> 28u) + 4u)) ^ _17038) * 277803737u;
                        _26643 += (_16947 * (((_16777 * pow(0.7799999713897705078125, float(_26642))) * clamp((_16923 / max(_16772 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_16947, _16779) * (6.283185482025146484375 / _16923)) + ((sqrt(61.63805389404296875 / _16923) * _277.RippleParams.z) * _277.ShadeParams.z)) + (float(((_17046 >> 22u) ^ _17046) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _26642++;
                        continue;
                    }
                    vec3 _16790 = normalize(vec3(-_26643.x, 1.0, -_26643.y));
                    vec3 _16793 = _5864 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                    float _17066 = dot(_16793, _277.CameraRight.xyz);
                    float _17071 = dot(_16793, _277.CameraUp.xyz);
                    float _17076 = dot(_16793, _277.CameraForward.xyz);
                    vec3 _17077 = vec3(_17066, _17071, _17076);
                    vec3 _16797 = reflect(_16716, _16790);
                    float _17094 = dot(_16797, _277.CameraForward.xyz);
                    vec3 _17095 = vec3(dot(_16797, _277.CameraRight.xyz), dot(_16797, _277.CameraUp.xyz), _17094);
                    float _26668;
                    vec3 _26750;
                    do
                    {
                        if (_277.ReflectionDistance <= 0.0)
                        {
                            _26750 = vec3(0.0);
                            _26668 = 0.0;
                            break;
                        }
                        float _17144 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _17094);
                        if (_17144 <= 0.0)
                        {
                            _26750 = vec3(0.0);
                            _26668 = 0.0;
                            break;
                        }
                        float _17341 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _17153 = max(0.0040000001899898052215576171875 * _17076, 0.00999999977648258209228515625);
                        float _17155 = 0.5 * _17153;
                        float _17164;
                        float _17166;
                        bool _26653;
                        bool _26658;
                        float _26663;
                        float _26669;
                        vec3 _26751;
                        int _26644 = 0;
                        float _26645 = _17155;
                        float _26646 = _17153;
                        float _26664 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_26644 < 80)
                            {
                                _17164 = _26645 + _26646;
                                _17166 = _26646 * 1.07000005245208740234375;
                                if (_17164 > _277.ReflectionDistance)
                                {
                                    _26751 = vec3(0.0);
                                    _26669 = 0.0;
                                    _26663 = _26664;
                                    _26658 = false;
                                    _26653 = true;
                                    break;
                                }
                                vec3 _17177 = _17077 + (_17095 * _17164);
                                if (_17177.z <= 0.0500000007450580596923828125)
                                {
                                    _26751 = vec3(0.0);
                                    _26669 = 0.0;
                                    _26663 = _26664;
                                    _26658 = false;
                                    _26653 = true;
                                    break;
                                }
                                float _17381 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _17357 = ((_17177.x * _17381) / _17177.z) + (0.5 * float(_277.OutputWidth));
                                float _17370 = (((-_17177.y) * _17381) / _17177.z) + (0.5 * float(_277.OutputHeight));
                                if (!((((_17357 >= 0.0) && (_17370 >= 0.0)) && (_17357 < float(_277.OutputWidth))) && (_17370 < float(_277.OutputHeight))))
                                {
                                    _26751 = vec3(0.0);
                                    _26669 = _26445;
                                    _26663 = _26664;
                                    _26658 = true;
                                    _26653 = false;
                                    break;
                                }
                                ivec2 _17465 = clamp(ivec2(vec2(_17357, _17370)), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                vec4 _17417 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17465.x, int((_277.OutputHeight - 1u) - uint(_17465.y)), 0).xy, 0);
                                float _17418 = _17417.x;
                                float _17435 = ((_17418 > 0.0) && (abs(_17418 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_17418 - _277.ProjA)) : 1000000000.0;
                                if (((_17435 < 100000000.0) && (_17177.z > _17435)) && ((_17177.z - _17435) < max(_26646 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _26648;
                                    _26648 = _17164;
                                    float _28516;
                                    float _28517;
                                    int _26647 = 0;
                                    float _26651 = _26645;
                                    SPIRV_CROSS_UNROLL
                                    for (; _26647 < 6; _26651 = _28516, _26648 = _28517, _26647++)
                                    {
                                        float _17221 = 0.5 * (_26651 + _26648);
                                        vec3 _17226 = _17077 + (_17095 * _17221);
                                        float _17535 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _17596 = clamp(ivec2(vec2(((_17226.x * _17535) / _17226.z) + (0.5 * float(_277.OutputWidth)), (((-_17226.y) * _17535) / _17226.z) + (0.5 * float(_277.OutputHeight)))), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                        vec4 _17548 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17596.x, int((_277.OutputHeight - 1u) - uint(_17596.y)), 0).xy, 0);
                                        float _17549 = _17548.x;
                                        bool _17233 = _17226.z > (((_17549 > 0.0) && (abs(_17549 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_17549 - _277.ProjA)) : 1000000000.0);
                                        _28516 = _17233 ? _26651 : _17221;
                                        _28517 = _17233 ? _17221 : _26648;
                                    }
                                    vec3 _17247 = _17077 + (_17095 * _26648);
                                    float _17666 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _17656 = vec2(((_17247.x * _17666) / _17247.z) + (0.5 * float(_277.OutputWidth)), (((-_17247.y) * _17666) / _17247.z) + (0.5 * float(_277.OutputHeight)));
                                    ivec2 _17724 = clamp(ivec2(_17656), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                    vec3 _17684 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_17724.x, int((_277.OutputHeight - 1u) - uint(_17724.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _17686 = length(_17684);
                                    vec3 _17692 = _17684 / vec3(_17686);
                                    bvec3 _17693 = bvec3(_17686 > 0.001000000047497451305389404296875);
                                    vec3 _17694 = vec3(_17693.x ? _17692.x : vec3(0.0, 1.0, 0.0).x, _17693.y ? _17692.y : vec3(0.0, 1.0, 0.0).y, _17693.z ? _17692.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_17694, _277.CameraRight.xyz), dot(_17694, _277.CameraUp.xyz), dot(_17694, _277.CameraForward.xyz)), _17095) > 0.100000001490116119384765625)
                                    {
                                        int _17274 = _26644 + 1;
                                        _26646 = _17166;
                                        _26645 = _17164;
                                        _26644 = _17274;
                                        _26664 = _17164;
                                        continue;
                                    }
                                    float _17788 = clamp(((0.0199999995529651641845703125 * _26648) * _17341) / max(_17247.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _17790 = 0.5 * _17788;
                                    vec2 _17875 = clamp(_17656 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _17901 = clamp((_17656 + vec2(_17790)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _17927 = clamp((_17656 + vec2(_17788 * (-0.5), _17790)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _17953 = clamp((_17656 + vec2(_17790, _17788 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _17979 = clamp((_17656 + vec2(_17788 * (-0.5), _17788 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18005 = clamp((_17656 + vec2(_17788, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18031 = clamp((_17656 + vec2(-_17788, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18057 = clamp((_17656 + vec2(0.0, _17788)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _18083 = clamp((_17656 + vec2(0.0, -_17788)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _17860 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17875.x, 1.0 - _17875.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17901.x, 1.0 - _17901.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17927.x, 1.0 - _17927.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17953.x, 1.0 - _17953.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17979.x, 1.0 - _17979.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18005.x, 1.0 - _18005.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18031.x, 1.0 - _18031.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18057.x, 1.0 - _18057.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18083.x, 1.0 - _18083.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _26751 = _17860;
                                    _26669 = _17144;
                                    _26663 = _17164;
                                    _26658 = false;
                                    _26653 = true;
                                    break;
                                }
                                int _17274 = _26644 + 1;
                                _26646 = _17166;
                                _26645 = _17164;
                                _26644 = _17274;
                                _26664 = _17164;
                                continue;
                            }
                            else
                            {
                                _26751 = vec3(0.0);
                                _26669 = _26445;
                                _26663 = _26664;
                                _26658 = false;
                                _26653 = false;
                                break;
                            }
                        }
                        if (_26653)
                        {
                            _26750 = _26751;
                            _26668 = _26669;
                            break;
                        }
                        if ((!_26658) || (_26663 < 0.0))
                        {
                            _26750 = _26751;
                            _26668 = 0.0;
                            break;
                        }
                        vec3 _17289 = _17077 + (_17095 * _26663);
                        float _18134 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _18124 = vec2(((_17289.x * _18134) / _17289.z) + (0.5 * float(_277.OutputWidth)), (((-_17289.y) * _18134) / _17289.z) + (0.5 * float(_277.OutputHeight)));
                        ivec2 _18195 = clamp(ivec2(_18124), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec4 _18147 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18195.x, int((_277.OutputHeight - 1u) - uint(_18195.y)), 0).xy, 0);
                        float _18148 = _18147.x;
                        float _18165 = ((_18148 > 0.0) && (abs(_18148 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_18148 - _277.ProjA)) : 1000000000.0;
                        float _18241 = clamp(((0.0199999995529651641845703125 * _26663) * _17341) / max(_17289.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _18243 = 0.5 * _18241;
                        vec2 _18328 = clamp(_18124 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18354 = clamp((_18124 + vec2(_18243)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18380 = clamp((_18124 + vec2(_18241 * (-0.5), _18243)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18406 = clamp((_18124 + vec2(_18243, _18241 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18432 = clamp((_18124 + vec2(_18241 * (-0.5), _18241 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18458 = clamp((_18124 + vec2(_18241, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18484 = clamp((_18124 + vec2(-_18241, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18510 = clamp((_18124 + vec2(0.0, _18241)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _18536 = clamp((_18124 + vec2(0.0, -_18241)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _18313 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18328.x, 1.0 - _18328.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18354.x, 1.0 - _18354.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18380.x, 1.0 - _18380.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18406.x, 1.0 - _18406.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18432.x, 1.0 - _18432.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18458.x, 1.0 - _18458.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18484.x, 1.0 - _18484.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18510.x, 1.0 - _18510.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18536.x, 1.0 - _18536.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_18165 > 100000000.0)
                        {
                            _26750 = _18313;
                            _26668 = _17144;
                            break;
                        }
                        float _17314 = _18165 - _17289.z;
                        if (_17314 <= 0.0)
                        {
                            _26750 = _18313;
                            _26668 = _17144;
                            break;
                        }
                        _26750 = _18313;
                        _26668 = _17144 * exp((_17314 / max(_17094, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _26756;
                    do
                    {
                        if (_277.EnvParams.x <= 0.0)
                        {
                            float _18590 = _16797.y;
                            _26756 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _18590))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _18590)));
                            break;
                        }
                        _26756 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_16797.x, _16797.y, -_16797.z), 0.0).xyz * _277.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _16811 = refract(_16716, _16790, 0.75187969207763671875);
                    bvec3 _28518 = bvec3(dot(_16811, _16811) < 9.9999999747524270787835121154785e-07);
                    vec3 _28519 = vec3(_28518.x ? _16716.x : _16811.x, _28518.y ? _16716.y : _16811.y, _28518.z ? _16716.z : _16811.z);
                    float _18616 = dot(_28519, _277.CameraRight.xyz);
                    float _18621 = dot(_28519, _277.CameraUp.xyz);
                    float _18626 = dot(_28519, _277.CameraForward.xyz);
                    vec3 _18627 = vec3(_18616, _18621, _18626);
                    float _18667 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _18657 = vec2(((_17066 * _18667) / _17076) + (0.5 * float(_277.OutputWidth)), (((-_17071) * _18667) / _17076) + (0.5 * float(_277.OutputHeight)));
                    vec3 _27175;
                    float _27263;
                    float _27351;
                    float _27439;
                    vec2 _27527;
                    do
                    {
                        ivec2 _18998 = clamp(ivec2(_18657), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec4 _18950 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18998.x, int((_277.OutputHeight - 1u) - uint(_18998.y)), 0).xy, 0);
                        float _18951 = _18950.x;
                        vec2 _27090;
                        float _27120;
                        _27120 = ((_18951 > 0.0) && (abs(_18951 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_18951 - _277.ProjA)) : 1000000000.0;
                        _27090 = _18657;
                        float _18746;
                        float _28522;
                        vec2 _28524;
                        bool _26765;
                        float _27149;
                        vec3 _27176;
                        float _27264;
                        float _27352;
                        float _27440;
                        vec2 _27528;
                        int _26758 = 1;
                        float _26760 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_26758 <= 16)
                            {
                                float _18739 = float(_26758);
                                _18746 = (_18739 * 0.25) * sqrt(_18739 * 0.0625);
                                vec3 _18751 = _17077 + (_18627 * _18746);
                                if (_18751.z <= 0.0500000007450580596923828125)
                                {
                                    _27528 = _26581;
                                    _27440 = _26445;
                                    _27352 = _26445;
                                    _27264 = _26445;
                                    _27176 = _26399;
                                    _27149 = 0.0;
                                    _26765 = false;
                                    break;
                                }
                                float _19068 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _19044 = ((_18751.x * _19068) / _18751.z) + (0.5 * float(_277.OutputWidth));
                                float _19057 = (((-_18751.y) * _19068) / _18751.z) + (0.5 * float(_277.OutputHeight));
                                vec2 _19058 = vec2(_19044, _19057);
                                if (!((((_19044 >= 0.0) && (_19057 >= 0.0)) && (_19044 < float(_277.OutputWidth))) && (_19057 < float(_277.OutputHeight))))
                                {
                                    _27528 = _26581;
                                    _27440 = _26445;
                                    _27352 = _26445;
                                    _27264 = _26445;
                                    _27176 = _26399;
                                    _27149 = 0.0;
                                    _26765 = false;
                                    break;
                                }
                                ivec2 _19152 = clamp(ivec2(_19058), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                vec4 _19104 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19152.x, int((_277.OutputHeight - 1u) - uint(_19152.y)), 0).xy, 0);
                                float _19105 = _19104.x;
                                float _19122 = ((_19105 > 0.0) && (abs(_19105 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_19105 - _277.ProjA)) : 1000000000.0;
                                if (_18751.z >= _19122)
                                {
                                    ivec2 _18773 = ivec2(_19058);
                                    ivec2 _19233 = clamp(ivec2(floor(((vec2(_18773) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _19242 = clamp(_19233, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _19268 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_19242.x, int((FluidSize.y - 1u) - uint(_19242.y)), 0).xy, 0);
                                    float _19269 = _19268.x;
                                    ivec2 _19278 = clamp(_19233, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _19365 = clamp(_18773, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                    vec4 _19317 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19365.x, int((_277.OutputHeight - 1u) - uint(_19365.y)), 0).xy, 0);
                                    float _19318 = _19317.x;
                                    if (!(((_19269 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_19278.x, int((FluidSize.y - 1u) - uint(_19278.y)), 0).xy, 0).w > 0.5)) && ((((_19318 > 0.0) && (abs(_19318 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_19318 - _277.ProjA)) : 1000000000.0) >= (_19269 + 0.0500000007450580596923828125))))
                                    {
                                        _27528 = _26581;
                                        _27440 = _26445;
                                        _27352 = _26445;
                                        _27264 = _26445;
                                        _27176 = _26399;
                                        _27149 = 0.0;
                                        _26765 = false;
                                        break;
                                    }
                                    float _26762;
                                    _26762 = _18746;
                                    float _28520;
                                    float _28521;
                                    int _26761 = 0;
                                    float _26763 = _26760;
                                    SPIRV_CROSS_UNROLL
                                    for (; _26761 < 5; _26763 = _28520, _26762 = _28521, _26761++)
                                    {
                                        float _18788 = 0.5 * (_26763 + _26762);
                                        vec3 _18793 = _17077 + (_18627 * _18788);
                                        float _19435 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _19496 = clamp(ivec2(vec2(((_18793.x * _19435) / _18793.z) + (0.5 * float(_277.OutputWidth)), (((-_18793.y) * _19435) / _18793.z) + (0.5 * float(_277.OutputHeight)))), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                        vec4 _19448 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19496.x, int((_277.OutputHeight - 1u) - uint(_19496.y)), 0).xy, 0);
                                        float _19449 = _19448.x;
                                        bool _18800 = _18793.z >= (((_19449 > 0.0) && (abs(_19449 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_19449 - _277.ProjA)) : 1000000000.0);
                                        _28520 = _18800 ? _26763 : _18788;
                                        _28521 = _18800 ? _18788 : _26762;
                                    }
                                    vec3 _18814 = _17077 + (_18627 * _26762);
                                    float _19566 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _18825 = clamp(vec2(((_18814.x * _19566) / _18814.z) + (0.5 * float(_277.OutputWidth)), (((-_18814.y) * _19566) / _18814.z) + (0.5 * float(_277.OutputHeight))), vec2(0.5), vec2(float(_277.OutputWidth), float(_277.OutputHeight)) - vec2(0.5));
                                    ivec2 _19627 = clamp(ivec2(_18825), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                    vec4 _19579 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19627.x, int((_277.OutputHeight - 1u) - uint(_19627.y)), 0).xy, 0);
                                    float _19580 = _19579.x;
                                    float _19597 = ((_19580 > 0.0) && (abs(_19580 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_19580 - _277.ProjA)) : 1000000000.0;
                                    float _19705 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    _27528 = _18825;
                                    _27440 = _19597;
                                    _27352 = _26762;
                                    _27264 = 1.0;
                                    _27176 = vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w) + (((_277.CameraRight.xyz * (((_18825.x - (0.5 * float(_277.OutputWidth))) * _19597) / _19705)) + (_277.CameraUp.xyz * (((-(_18825.y - (0.5 * float(_277.OutputHeight)))) * _19597) / _19705))) + (_277.CameraForward.xyz * _19597));
                                    _27149 = 1.0;
                                    _26765 = true;
                                    break;
                                }
                                ivec2 _18849 = ivec2(_19058 - vec2(0.5));
                                ivec2 _19778 = clamp(ivec2(floor(((vec2(_18849) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _19787 = clamp(_19778, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _19813 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_19787.x, int((FluidSize.y - 1u) - uint(_19787.y)), 0).xy, 0);
                                float _19814 = _19813.x;
                                ivec2 _19823 = clamp(_19778, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _19910 = clamp(_18849, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                vec4 _19862 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19910.x, int((_277.OutputHeight - 1u) - uint(_19910.y)), 0).xy, 0);
                                float _19863 = _19862.x;
                                ivec2 _18854 = ivec2(_19058 + vec2(0.5));
                                ivec2 _19991 = clamp(ivec2(floor(((vec2(_18854) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20000 = clamp(_19991, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _20026 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20000.x, int((FluidSize.y - 1u) - uint(_20000.y)), 0).xy, 0);
                                float _20027 = _20026.x;
                                ivec2 _20036 = clamp(_19991, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _20123 = clamp(_18854, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                vec4 _20075 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20123.x, int((_277.OutputHeight - 1u) - uint(_20123.y)), 0).xy, 0);
                                float _20076 = _20075.x;
                                bool _18856 = (((_19814 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_19823.x, int((FluidSize.y - 1u) - uint(_19823.y)), 0).xy, 0).w > 0.5)) && ((((_19863 > 0.0) && (abs(_19863 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_19863 - _277.ProjA)) : 1000000000.0) >= (_19814 + 0.0500000007450580596923828125))) && (((_20027 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20036.x, int((FluidSize.y - 1u) - uint(_20036.y)), 0).xy, 0).w > 0.5)) && ((((_20076 > 0.0) && (abs(_20076 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_20076 - _277.ProjA)) : 1000000000.0) >= (_20027 + 0.0500000007450580596923828125)));
                                _28522 = _18856 ? _19122 : _27120;
                                bvec2 _28523 = bvec2(_18856);
                                _28524 = vec2(_28523.x ? _19058.x : _27090.x, _28523.y ? _19058.y : _27090.y);
                                _26758++;
                                _27120 = _28522;
                                _27090 = _28524;
                                _26760 = _18746;
                                continue;
                            }
                            else
                            {
                                _27528 = _26581;
                                _27440 = _26445;
                                _27352 = _26445;
                                _27264 = _26445;
                                _27176 = _26399;
                                _27149 = 0.0;
                                _26765 = false;
                                break;
                            }
                        }
                        if (_26765)
                        {
                            _27527 = _27528;
                            _27439 = _27440;
                            _27351 = _27352;
                            _27263 = _27264;
                            _27175 = _27176;
                            break;
                        }
                        vec3 _20175 = ((_277.CameraRight.xyz * _18616) + (_277.CameraUp.xyz * _18621)) + (_277.CameraForward.xyz * _18626);
                        float _26780;
                        vec3 _26781;
                        do
                        {
                            if ((((_5864.x < _277.PoolMin.x) || (_5864.x > _277.PoolMax.x)) || (_5864.z < _277.PoolMin.z)) || (_5864.z > _277.PoolMax.z))
                            {
                                _26781 = vec3(0.0, 1.0, 0.0);
                                _26780 = 1000.0;
                                break;
                            }
                            float _26768;
                            if (_20175.x > 9.9999997473787516355514526367188e-05)
                            {
                                _26768 = (_277.PoolMax.x - _5864.x) / _20175.x;
                            }
                            else
                            {
                                float _26769;
                                if (_20175.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _26769 = (_277.PoolMin.x - _5864.x) / _20175.x;
                                }
                                else
                                {
                                    _26769 = 1000.0;
                                }
                                _26768 = _26769;
                            }
                            bool _20237 = _26768 < 1000.0;
                            vec3 _26785;
                            if (_20237)
                            {
                                _26785 = vec3((_20175.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                            }
                            else
                            {
                                _26785 = vec3(0.0, 1.0, 0.0);
                            }
                            float _28525 = _20237 ? _26768 : 1000.0;
                            float _26772;
                            if (_20175.z > 9.9999997473787516355514526367188e-05)
                            {
                                _26772 = (_277.PoolMax.z - _5864.z) / _20175.z;
                            }
                            else
                            {
                                float _26773;
                                if (_20175.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _26773 = (_277.PoolMin.z - _5864.z) / _20175.z;
                                }
                                else
                                {
                                    _26773 = 1000.0;
                                }
                                _26772 = _26773;
                            }
                            bool _20275 = _26772 < _28525;
                            vec3 _26783;
                            if (_20275)
                            {
                                _26783 = vec3(0.0, 0.0, _28544);
                            }
                            else
                            {
                                _26783 = _26785;
                            }
                            float _28526 = _20275 ? _26772 : _28525;
                            float _26778;
                            vec3 _26782;
                            if (_20175.y < (-9.9999997473787516355514526367188e-05))
                            {
                                float _20295 = (_277.PoolMin.y - _5864.y) / _20175.y;
                                bool _20298 = _20295 < _28526;
                                bvec3 _28527 = bvec3(_20298);
                                _26782 = vec3(_28527.x ? vec3(0.0, 1.0, 0.0).x : _26783.x, _28527.y ? vec3(0.0, 1.0, 0.0).y : _26783.y, _28527.z ? vec3(0.0, 1.0, 0.0).z : _26783.z);
                                _26778 = _20298 ? _20295 : _28526;
                            }
                            else
                            {
                                _26782 = _26783;
                                _26778 = _28526;
                            }
                            _26781 = _26782;
                            _26780 = max(_26778, 0.0);
                            break;
                        } while(false);
                        if (_26780 < 100.0)
                        {
                            vec3 _18881 = _5864 + (_20175 * _26780);
                            uint _26960;
                            vec2 _27073;
                            float _27080;
                            do
                            {
                                bool _26790;
                                vec2 _26791;
                                float _26792;
                                do
                                {
                                    vec3 _20549 = _18881 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                    float _20607 = dot(_20549, _277.CameraForward.xyz);
                                    if (_20607 <= 0.0500000007450580596923828125)
                                    {
                                        _26792 = 1000000000.0;
                                        _26791 = vec2(0.5);
                                        _26790 = false;
                                        break;
                                    }
                                    float _20648 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _20624 = ((dot(_20549, _277.CameraRight.xyz) * _20648) / _20607) + (0.5 * float(_277.OutputWidth));
                                    float _20637 = (((-dot(_20549, _277.CameraUp.xyz)) * _20648) / _20607) + (0.5 * float(_277.OutputHeight));
                                    if (!((((_20624 >= 0.0) && (_20637 >= 0.0)) && (_20624 < float(_277.OutputWidth))) && (_20637 < float(_277.OutputHeight))))
                                    {
                                        _26792 = 1000000000.0;
                                        _26791 = vec2(0.5);
                                        _26790 = false;
                                        break;
                                    }
                                    vec2 _20566 = floor(vec2(_20624, _20637)) + vec2(0.5);
                                    ivec2 _20732 = clamp(ivec2(_20566), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                    vec4 _20684 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20732.x, int((_277.OutputHeight - 1u) - uint(_20732.y)), 0).xy, 0);
                                    float _20685 = _20684.x;
                                    float _20702 = ((_20685 > 0.0) && (abs(_20685 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_20685 - _277.ProjA)) : 1000000000.0;
                                    _26792 = _20702;
                                    _26791 = _20566;
                                    _26790 = abs(_20702 - _20607) < ((0.0199999995529651641845703125 * _20607) + 0.02999999932944774627685546875);
                                    break;
                                } while(false);
                                if (_26790)
                                {
                                    _27080 = _26792;
                                    _27073 = _26791;
                                    _26960 = 1u;
                                    break;
                                }
                                vec3 _20355 = _18881 - vec3(_277.CameraRight.w, _25935, _277.CameraForward.w);
                                vec3 _20363 = (_277.PoolMin.xyz + _277.PoolMax.xyz) * 0.5;
                                vec3 _20368 = vec3((_20355.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _20373 = vec3(0.0, 0.0, (_20355.z >= 0.0) ? 1.0 : (-1.0));
                                vec3 _20380 = vec3((_20363.x >= _18881.x) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _20387 = vec3(0.0, 0.0, (_20363.z >= _18881.z) ? 1.0 : (-1.0));
                                vec3 _26793;
                                vec3 _26794;
                                vec3 _26799;
                                vec3 _26801;
                                if (abs(_26781.y) > 0.5)
                                {
                                    bool _20399 = abs(_20355.z) >= abs(_20355.x);
                                    bvec3 _20403 = bvec3(_20399);
                                    bvec3 _20408 = bvec3(_20399);
                                    bvec3 _20413 = bvec3(_20399);
                                    bvec3 _20418 = bvec3(_20399);
                                    _26801 = vec3(_20403.x ? _20373.x : _20368.x, _20403.y ? _20373.y : _20368.y, _20403.z ? _20373.z : _20368.z);
                                    _26799 = vec3(_20413.x ? _20368.x : _20373.x, _20413.y ? _20368.y : _20373.y, _20413.z ? _20368.z : _20373.z);
                                    _26794 = vec3(_20418.x ? _20387.x : _20380.x, _20418.y ? _20387.y : _20380.y, _20418.z ? _20387.z : _20380.z);
                                    _26793 = vec3(_20408.x ? _20380.x : _20387.x, _20408.y ? _20380.y : _20387.y, _20408.z ? _20380.z : _20387.z);
                                }
                                else
                                {
                                    bool _20424 = abs(_26781.x) > 0.5;
                                    bvec3 _20428 = bvec3(_20424);
                                    bvec3 _20433 = bvec3(_20424);
                                    _26801 = vec3(_20428.x ? _20373.x : _20368.x, _20428.y ? _20373.y : _20368.y, _20428.z ? _20373.z : _20368.z);
                                    _26799 = vec3(0.0, 1.0, 0.0);
                                    _26794 = vec3(_20433.x ? _20387.x : _20380.x, _20433.y ? _20387.y : _20380.y, _20433.z ? _20387.z : _20380.z);
                                    _26793 = vec3(0.0, 1.0, 0.0);
                                }
                                float _20815 = ((_26793.y > 0.5) ? (_277.PoolMin.w - _18881.y) : ((_26793.x > 0.5) ? (_277.PoolMax.x - _18881.x) : ((_26793.x < (-0.5)) ? (_18881.x - _277.PoolMin.x) : ((_26793.z > 0.5) ? (_277.PoolMax.z - _18881.z) : (_18881.z - _277.PoolMin.z))))) - 0.02999999932944774627685546875;
                                float _20859 = ((_26794.y > 0.5) ? (_277.PoolMin.w - _18881.y) : ((_26794.x > 0.5) ? (_277.PoolMax.x - _18881.x) : ((_26794.x < (-0.5)) ? (_18881.x - _277.PoolMin.x) : ((_26794.z > 0.5) ? (_277.PoolMax.z - _18881.z) : (_18881.z - _277.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _27075;
                                _27075 = _26791;
                                bool _26946;
                                uint _26961;
                                vec2 _27074;
                                vec2 _27077;
                                float _27081;
                                float _27084;
                                bool _28392;
                                int _26795 = 0;
                                bool _26818;
                                float _27082 = _26792;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_26795 < 11)
                                    {
                                        if (_26795 == 1)
                                        {
                                            vec3 _20452 = _18881 - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                            float _20885 = dot(_20452, _277.CameraForward.xyz);
                                            vec2 _27078;
                                            float _27085;
                                            if (_20885 > 0.0500000007450580596923828125)
                                            {
                                                float _20926 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                float _20902 = ((dot(_20452, _277.CameraRight.xyz) * _20926) / _20885) + (0.5 * float(_277.OutputWidth));
                                                float _20915 = (((-dot(_20452, _277.CameraUp.xyz)) * _20926) / _20885) + (0.5 * float(_277.OutputHeight));
                                                vec2 _20916 = vec2(_20902, _20915);
                                                ivec2 _20463 = ivec2(_20916);
                                                ivec2 _21000 = clamp(ivec2(floor(((vec2(_20463) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _21009 = clamp(_21000, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                vec4 _21035 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_21009.x, int((FluidSize.y - 1u) - uint(_21009.y)), 0).xy, 0);
                                                float _21036 = _21035.x;
                                                ivec2 _21045 = clamp(_21000, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _21132 = clamp(_20463, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                                vec4 _21084 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21132.x, int((_277.OutputHeight - 1u) - uint(_21132.y)), 0).xy, 0);
                                                float _21085 = _21084.x;
                                                vec2 _27079;
                                                float _27086;
                                                if (((((_20902 >= 0.0) && (_20915 >= 0.0)) && (_20902 < float(_277.OutputWidth))) && (_20915 < float(_277.OutputHeight))) && (((_21036 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_21045.x, int((FluidSize.y - 1u) - uint(_21045.y)), 0).xy, 0).w > 0.5)) && ((((_21085 > 0.0) && (abs(_21085 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_21085 - _277.ProjA)) : 1000000000.0) >= (_21036 + 0.0500000007450580596923828125))))
                                                {
                                                    vec2 _20470 = floor(_20916) + vec2(0.5);
                                                    ivec2 _21223 = clamp(ivec2(_20470), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                                    vec4 _21175 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21223.x, int((_277.OutputHeight - 1u) - uint(_21223.y)), 0).xy, 0);
                                                    float _21176 = _21175.x;
                                                    float _21193 = ((_21176 > 0.0) && (abs(_21176 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_21176 - _277.ProjA)) : 1000000000.0;
                                                    if (_21193 < _20885)
                                                    {
                                                        _27081 = _21193;
                                                        _27074 = _20470;
                                                        _26961 = 2u;
                                                        _26946 = true;
                                                        break;
                                                    }
                                                    _27086 = _21193;
                                                    _27079 = _20470;
                                                }
                                                else
                                                {
                                                    _27086 = _27082;
                                                    _27079 = _27075;
                                                }
                                                _27085 = _27086;
                                                _27078 = _27079;
                                            }
                                            else
                                            {
                                                _27085 = _27082;
                                                _27078 = _27075;
                                            }
                                            _28392 = _26818;
                                            _27084 = _27085;
                                            _27077 = _27078;
                                            int _20530 = _26795 + 1;
                                            _26818 = _28392;
                                            _26795 = _20530;
                                            _27082 = _27084;
                                            _27075 = _27077;
                                            continue;
                                        }
                                        bool _20484 = _26795 == 2;
                                        bool _20486 = _26795 >= 3;
                                        int _20488 = _26795 & 1;
                                        bool _20491 = _20484 || (_20486 && (_20488 == 0));
                                        bool _20493 = _26795 < 3;
                                        int _20495 = _26795 - 3;
                                        float _20500 = _20493 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_20495 / 2)));
                                        if (_20500 >= (_20491 ? _20859 : _20815))
                                        {
                                            _28392 = _26818;
                                            _27084 = _27082;
                                            _27077 = _27075;
                                            int _20530 = _26795 + 1;
                                            _26818 = _28392;
                                            _26795 = _20530;
                                            _27082 = _27084;
                                            _27075 = _27077;
                                            continue;
                                        }
                                        bvec3 _20513 = bvec3(_20491);
                                        vec3 _20517 = _18881 + (vec3(_20513.x ? _26794.x : _26793.x, _20513.y ? _26794.y : _26793.y, _20513.z ? _26794.z : _26793.z) * _20500);
                                        bvec3 _20521 = bvec3(_20491);
                                        vec3 _20522 = vec3(_20521.x ? _26799.x : _26801.x, _20521.y ? _26799.y : _26801.y, _20521.z ? _26799.z : _26801.z);
                                        bool _26815;
                                        vec2 _26921;
                                        float _26925;
                                        do
                                        {
                                            float _21386 = ((_20522.y > 0.5) ? (_277.PoolMin.w - _20517.y) : ((_20522.x > 0.5) ? (_277.PoolMax.x - _20517.x) : ((_20522.x < (-0.5)) ? (_20517.x - _277.PoolMin.x) : ((_20522.z > 0.5) ? (_277.PoolMax.z - _20517.z) : (_20517.z - _277.PoolMin.z))))) - 0.02999999932944774627685546875;
                                            vec2 _26922;
                                            _26922 = vec2(0.5);
                                            float _21292;
                                            float _21334;
                                            vec2 _26806;
                                            float _26807;
                                            bool _26812;
                                            bool _26816;
                                            int _26802 = 0;
                                            float _26803 = 0.0;
                                            float _26804 = 0.0599999986588954925537109375;
                                            float _26926 = 1000000000.0;
                                            SPIRV_CROSS_LOOP
                                            for (;;)
                                            {
                                                if ((_26802 < 40) && (_26803 < _21386))
                                                {
                                                    _21292 = min(_26803 + _26804, _21386);
                                                    bool _26805;
                                                    do
                                                    {
                                                        vec3 _21400 = (_20517 + (_20522 * _21292)) - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                                        float _21458 = dot(_21400, _277.CameraForward.xyz);
                                                        if (_21458 <= 0.0500000007450580596923828125)
                                                        {
                                                            _26807 = 1000000000.0;
                                                            _26806 = vec2(0.5);
                                                            _26805 = false;
                                                            break;
                                                        }
                                                        float _21499 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                        float _21475 = ((dot(_21400, _277.CameraRight.xyz) * _21499) / _21458) + (0.5 * float(_277.OutputWidth));
                                                        float _21488 = (((-dot(_21400, _277.CameraUp.xyz)) * _21499) / _21458) + (0.5 * float(_277.OutputHeight));
                                                        if (!((((_21475 >= 0.0) && (_21488 >= 0.0)) && (_21475 < float(_277.OutputWidth))) && (_21488 < float(_277.OutputHeight))))
                                                        {
                                                            _26807 = 1000000000.0;
                                                            _26806 = vec2(0.5);
                                                            _26805 = false;
                                                            break;
                                                        }
                                                        vec2 _21417 = floor(vec2(_21475, _21488)) + vec2(0.5);
                                                        ivec2 _21583 = clamp(ivec2(_21417), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                                        vec4 _21535 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21583.x, int((_277.OutputHeight - 1u) - uint(_21583.y)), 0).xy, 0);
                                                        float _21536 = _21535.x;
                                                        float _21553 = ((_21536 > 0.0) && (abs(_21536 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_21536 - _277.ProjA)) : 1000000000.0;
                                                        _26807 = _21553;
                                                        _26806 = _21417;
                                                        _26805 = abs(_21553 - _21458) < ((0.0199999995529651641845703125 * _21458) + 0.02999999932944774627685546875);
                                                        break;
                                                    } while(false);
                                                    if (_26805)
                                                    {
                                                        vec2 _26923;
                                                        float _26927;
                                                        _26927 = _26807;
                                                        _26923 = _26806;
                                                        float _28530;
                                                        float _28531;
                                                        float _28532;
                                                        vec2 _28534;
                                                        int _26811 = 0;
                                                        float _26928 = _26803;
                                                        float _26930 = _21292;
                                                        SPIRV_CROSS_UNROLL
                                                        for (; _26811 < 4; _26930 = _28530, _26928 = _28531, _26927 = _28532, _26923 = _28534, _26811++)
                                                        {
                                                            float _21310 = 0.5 * (_26928 + _26930);
                                                            bool _26931;
                                                            vec2 _26932;
                                                            float _26933;
                                                            do
                                                            {
                                                                vec3 _21627 = (_20517 + (_20522 * _21310)) - vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w);
                                                                float _21685 = dot(_21627, _277.CameraForward.xyz);
                                                                if (_21685 <= 0.0500000007450580596923828125)
                                                                {
                                                                    _26933 = 1000000000.0;
                                                                    _26932 = vec2(0.5);
                                                                    _26931 = false;
                                                                    break;
                                                                }
                                                                float _21726 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                                float _21702 = ((dot(_21627, _277.CameraRight.xyz) * _21726) / _21685) + (0.5 * float(_277.OutputWidth));
                                                                float _21715 = (((-dot(_21627, _277.CameraUp.xyz)) * _21726) / _21685) + (0.5 * float(_277.OutputHeight));
                                                                if (!((((_21702 >= 0.0) && (_21715 >= 0.0)) && (_21702 < float(_277.OutputWidth))) && (_21715 < float(_277.OutputHeight))))
                                                                {
                                                                    _26933 = 1000000000.0;
                                                                    _26932 = vec2(0.5);
                                                                    _26931 = false;
                                                                    break;
                                                                }
                                                                vec2 _21644 = floor(vec2(_21702, _21715)) + vec2(0.5);
                                                                ivec2 _21810 = clamp(ivec2(_21644), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                                                                vec4 _21762 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21810.x, int((_277.OutputHeight - 1u) - uint(_21810.y)), 0).xy, 0);
                                                                float _21763 = _21762.x;
                                                                float _21780 = ((_21763 > 0.0) && (abs(_21763 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_21763 - _277.ProjA)) : 1000000000.0;
                                                                _26933 = _21780;
                                                                _26932 = _21644;
                                                                _26931 = abs(_21780 - _21685) < ((0.0199999995529651641845703125 * _21685) + 0.02999999932944774627685546875);
                                                                break;
                                                            } while(false);
                                                            _28530 = _26931 ? _21310 : _26930;
                                                            _28531 = _26931 ? _26928 : _21310;
                                                            _28532 = _26931 ? _26933 : _26927;
                                                            bvec2 _28533 = bvec2(_26931);
                                                            _28534 = vec2(_28533.x ? _26932.x : _26923.x, _28533.y ? _26932.y : _26923.y);
                                                        }
                                                        _26925 = _26927;
                                                        _26921 = _26923;
                                                        _26816 = true;
                                                        _26812 = true;
                                                        break;
                                                    }
                                                    _21334 = min(_26804 * 1.2000000476837158203125, 0.300000011920928955078125);
                                                    _26804 = _21334;
                                                    _26803 = _21292;
                                                    _26802++;
                                                    _26926 = _26807;
                                                    _26922 = _26806;
                                                    continue;
                                                }
                                                else
                                                {
                                                    _26925 = _26926;
                                                    _26921 = _26922;
                                                    _26816 = _26818;
                                                    _26812 = false;
                                                    break;
                                                }
                                            }
                                            if (_26812)
                                            {
                                                _26815 = _26816;
                                                break;
                                            }
                                            _26815 = false;
                                            break;
                                        } while(false);
                                        if (_26815)
                                        {
                                            _27081 = _26925;
                                            _27074 = _26921;
                                            _26961 = 1u;
                                            _26946 = true;
                                            break;
                                        }
                                        _28392 = _26815;
                                        _27084 = _26925;
                                        _27077 = _26921;
                                        int _20530 = _26795 + 1;
                                        _26818 = _28392;
                                        _26795 = _20530;
                                        _27082 = _27084;
                                        _27075 = _27077;
                                        continue;
                                    }
                                    else
                                    {
                                        _27081 = _27082;
                                        _27074 = _27075;
                                        _26961 = _26231;
                                        _26946 = false;
                                        break;
                                    }
                                }
                                if (_26946)
                                {
                                    _27080 = _27081;
                                    _27073 = _27074;
                                    _26960 = _26961;
                                    break;
                                }
                                _27080 = _27081;
                                _27073 = _27074;
                                _26960 = 0u;
                                break;
                            } while(false);
                            if (_26960 != 0u)
                            {
                                float _21888 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec3 _18902 = vec3(_277.CameraRight.w, _277.CameraUp.w, _277.CameraForward.w) + (((_277.CameraRight.xyz * (((_27073.x - (0.5 * float(_277.OutputWidth))) * _27080) / _21888)) + (_277.CameraUp.xyz * (((-(_27073.y - (0.5 * float(_277.OutputHeight)))) * _27080) / _21888))) + (_277.CameraForward.xyz * _27080));
                                bvec3 _18903 = bvec3(_26960 == 1u);
                                vec3 _18904 = vec3(_18903.x ? _18881.x : _18902.x, _18903.y ? _18881.y : _18902.y, _18903.z ? _18881.z : _18902.z);
                                _27527 = _27073;
                                _27439 = _27080;
                                _27351 = (_26960 == 1u) ? _26780 : length(_18904 - _5864);
                                _27263 = 1.0;
                                _27175 = _18904;
                                break;
                            }
                        }
                        float _18926 = min(_26780, 4.0);
                        _27527 = _27090;
                        _27439 = _27120;
                        _27351 = _18926;
                        _27263 = _27149;
                        _27175 = _5864 + (_20175 * _18926);
                        break;
                    } while(false);
                    vec3 _27617;
                    do
                    {
                        vec2 _22125 = clamp(_27527 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _22136 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_22125.x, 1.0 - _22125.y), 0.0).xyz;
                        if ((_27263 < 0.5) || (_27439 > 100000000.0))
                        {
                            _27617 = _22136;
                            break;
                        }
                        ivec2 _21959 = ivec2(_27527);
                        float _21965 = _5864.y - _27175.y;
                        if (_21965 <= 0.00999999977648258209228515625)
                        {
                            _27617 = _22136;
                            break;
                        }
                        ivec2 _22166 = clamp(_21959, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        ivec2 _22254 = clamp(_21959, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec3 _22214 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_22254.x, int((_277.OutputHeight - 1u) - uint(_22254.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _22216 = length(_22214);
                        vec3 _22222 = _22214 / vec3(_22216);
                        bvec3 _22223 = bvec3(_22216 > 0.001000000047497451305389404296875);
                        ivec2 _22314 = clamp(_21959, ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        float _22011 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_22166.x, int((_277.OutputHeight - 1u) - uint(_22166.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_22223.x ? _22222.x : vec3(0.0, 1.0, 0.0).x, _22223.y ? _22222.y : vec3(0.0, 1.0, 0.0).y, _22223.z ? _22222.z : vec3(0.0, 1.0, 0.0).z), _5391), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_22314.x, int((_277.OutputHeight - 1u) - uint(_22314.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _277.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_22136, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _22015 = max(-_28485.y, 0.20000000298023223876953125);
                        float _27616;
                        if ((_277.CausticParams.x > 0.0) && (_22011 > 0.0))
                        {
                            vec2 _22058 = _27175.xz - ((_28485.xz / vec2(_22015)) * _21965);
                            vec2 _22369 = _22058 * (6.283185482025146484375 / max(_277.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _22392 = _22369 + (vec2(sin((_22369.y * 0.730000019073486328125) + ((_277.ShadeParams.z * 0.89999997615814208984375) * _277.CausticParams.z)), sin((_22369.x * 0.61000001430511474609375) - ((_277.ShadeParams.z * 1.10000002384185791015625) * _277.CausticParams.z))) * 0.89999997615814208984375);
                            float _27615;
                            do
                            {
                                if ((_277.GridParams.z < 2u) || (_277.GridParams.w < 2u))
                                {
                                    _27615 = 1.0;
                                    break;
                                }
                                vec2 _22502 = clamp(((_22058 - _277.PoolMin.xz) / max(_277.PoolMax.xz - _277.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_277.GridParams.z - 1u), float(_277.GridParams.w - 1u)), vec2(0.0), vec2(float(_277.GridParams.z - 1u), float(_277.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _22504 = uvec2(_22502);
                                vec2 _22508 = _22502 - vec2(_22504);
                                uint _22515 = _22504.x + (_22504.y * _277.GridParams.z);
                                _27615 = mix(mix(CausticField_1._data[_22515], CausticField_1._data[_22515 + 1u], _22508.x), mix(CausticField_1._data[_22515 + _277.GridParams.z], CausticField_1._data[(_22515 + _277.GridParams.z) + 1u], _22508.x), _22508.y);
                                break;
                            } while(false);
                            _27616 = min(max(1.0 + ((_277.CausticParams.x * (exp((-_21965) / max(_277.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _21965))) * ((pow(clamp((1.0 - abs(sin(_22392.x + ((_277.ShadeParams.z * 0.60000002384185791015625) * _277.CausticParams.z)) * sin((_22392.y * 1.12999999523162841796875) - ((_277.ShadeParams.z * 0.4000000059604644775390625) * _277.CausticParams.z)))) * (1.0 - abs(sin(((_22392.x + _22392.y) * 0.709999978542327880859375) - ((_277.ShadeParams.z * 0.5) * _277.CausticParams.z)) * sin(((_22392.x - _22392.y) * 0.829999983310699462890625) + ((_277.ShadeParams.z * 0.699999988079071044921875) * _277.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_27615, 0.0500000007450580596923828125), _277.LightParams.y), 2.5);
                        }
                        else
                        {
                            _27616 = 1.0;
                        }
                        _27617 = _22136 * ((mix(vec3(1.0), exp(((-_277.FluidTint.xyz) * _21965) * 1.2000000476837158203125), vec3(_277.LightParams.x)) * (1.0 - _22011)) + ((mix(vec3(1.0), exp(((-_277.FluidTint.xyz) * _21965) / vec3(_22015)), vec3(_277.LightParams.x)) * _22011) * _27616));
                        break;
                    } while(false);
                    vec3 _16842 = exp((-_277.FluidTint.xyz) * max(_27351, 0.0500000007450580596923828125));
                    float _22594 = _277.ScatterParams.z * _277.ScatterParams.z;
                    _27666 = mix(_26637, mix((_27617 * _16842) + ((_277.FluidColor.xyz * ((_6370 * _277.ScatterParams.y) + (_277.SunColor.xyz * (((_277.ScatterParams.x * 0.25) * ((1.0 - _22594) / pow(max((1.0 + _22594) - ((2.0 * _277.ScatterParams.z) * dot(_28485, -_16716)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5391.y, 0.0, 1.0))))) * (vec3(1.0) - _16842)), mix(_26756, _26750, vec3(_26668)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_16790, -_16716), 0.0, 1.0), max(_277.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _5777), 0.0, 1.0)));
                }
                else
                {
                    _27666 = _26637;
                }
                _27665 = _27666;
            }
            else
            {
                _27665 = _26637;
            }
            _27664 = _27665;
        }
        else
        {
            _27664 = _26637;
        }
        float _5930 = clamp((clamp((float(_11964) * 0.0009765625) / _5629, 0.0, 1.0) * _277.ScatterParams.w) + (((_5366 * 0.000244140625) * _277.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _5942 = exp(max(_277.FluidTint.xyz, vec3(0.0)) * (-_5842)) * (1.0 - (0.85000002384185791015625 * _5930));
        vec3 _22623 = ((_277.SunColor.xyz * ((clamp(_5391.y, 0.0, 1.0) * _26014) * 0.3183098733425140380859375)) + _6370) * 0.85000002384185791015625;
        vec3 _22629 = mix(vec3(dot(_22623, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _22623, vec3(0.5));
        float _22677 = _277.ScatterParams.z * _277.ScatterParams.z;
        vec3 _5979 = ((_27664 * _5942) + ((vec3(1.0) - _5942) * mix(_277.FluidColor.xyz * ((_6370 * _277.ScatterParams.y) + (_277.SunColor.xyz * ((((_277.ScatterParams.x * 0.25) * ((1.0 - _22677) / pow(max((1.0 + _22677) - ((2.0 * _277.ScatterParams.z) * dot(_28485, -_12093)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5391.y, 0.0, 1.0)) * _26014))), _22629 * 0.800000011920928955078125, vec3(_5930)))) + (_22629 * (((float(_5371) * 2.2888532839715480804443359375e-05) + (_5366 * 3.6621095205191522836685180664062e-05)) * clamp(_277.FoamParams.z, 0.0, 1.0)));
        if (_277.DebugView == 3u)
        {
            float _5986 = clamp(_5842 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5327), vec4(_5986, _5986, _5986, 1.0));
            break;
        }
        if (_277.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_5327), vec4(_25948.www, 1.0));
            break;
        }
        if (_277.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_5327), vec4(_5979, 1.0));
            break;
        }
        if (_277.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_5327), vec4(clamp((_25975 * 0.0625) + (_5360 * 0.000244140625), 0.0, 1.0), _5930, clamp(_5648 + (_5354 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_277.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_5327), vec4(_26638, 1.0));
            break;
        }
        float _6058 = 0.014999999664723873138427734375 + (2.0 * sqrt(_5724));
        vec3 _6061 = reflect(_5546, _26016);
        vec3 _22711 = ((_277.CameraRight.xyz * _6061.x) + (_277.CameraUp.xyz * _6061.y)) + (_277.CameraForward.xyz * _6061.z);
        float _27872;
        vec3 _27999;
        do
        {
            if (_277.ReflectionDistance <= 0.0)
            {
                _27999 = vec3(0.0);
                _27872 = 0.0;
                break;
            }
            float _22760 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _6061.z);
            if (_22760 <= 0.0)
            {
                _27999 = vec3(0.0);
                _27872 = 0.0;
                break;
            }
            float _22957 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _22769 = max(0.0040000001899898052215576171875 * _25947, 0.00999999977648258209228515625);
            float _22771 = 0.5 * _22769;
            float _22780;
            float _22782;
            bool _27857;
            bool _27862;
            float _27867;
            float _27873;
            vec3 _28000;
            int _27848 = 0;
            float _27849 = _22771;
            float _27850 = _22769;
            float _27868 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_27848 < 80)
                {
                    _22780 = _27849 + _27850;
                    _22782 = _27850 * 1.07000005245208740234375;
                    if (_22780 > _277.ReflectionDistance)
                    {
                        _28000 = vec3(0.0);
                        _27873 = 0.0;
                        _27867 = _27868;
                        _27862 = false;
                        _27857 = true;
                        break;
                    }
                    vec3 _22793 = _9382 + (_6061 * _22780);
                    if (_22793.z <= 0.0500000007450580596923828125)
                    {
                        _28000 = vec3(0.0);
                        _27873 = 0.0;
                        _27867 = _27868;
                        _27862 = false;
                        _27857 = true;
                        break;
                    }
                    float _22997 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _22973 = ((_22793.x * _22997) / _22793.z) + (0.5 * float(_277.OutputWidth));
                    float _22986 = (((-_22793.y) * _22997) / _22793.z) + (0.5 * float(_277.OutputHeight));
                    if (!((((_22973 >= 0.0) && (_22986 >= 0.0)) && (_22973 < float(_277.OutputWidth))) && (_22986 < float(_277.OutputHeight))))
                    {
                        _28000 = vec3(0.0);
                        _27873 = _26445;
                        _27867 = _27868;
                        _27862 = true;
                        _27857 = false;
                        break;
                    }
                    ivec2 _23081 = clamp(ivec2(vec2(_22973, _22986)), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                    vec4 _23033 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_23081.x, int((_277.OutputHeight - 1u) - uint(_23081.y)), 0).xy, 0);
                    float _23034 = _23033.x;
                    float _23051 = ((_23034 > 0.0) && (abs(_23034 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_23034 - _277.ProjA)) : 1000000000.0;
                    if (((_23051 < 100000000.0) && (_22793.z > _23051)) && ((_22793.z - _23051) < max(_27850 * 2.1400001049041748046875, 0.25)))
                    {
                        float _27852;
                        _27852 = _22780;
                        float _28537;
                        float _28538;
                        int _27851 = 0;
                        float _27855 = _27849;
                        SPIRV_CROSS_UNROLL
                        for (; _27851 < 6; _27855 = _28537, _27852 = _28538, _27851++)
                        {
                            float _22837 = 0.5 * (_27855 + _27852);
                            vec3 _22842 = _9382 + (_6061 * _22837);
                            float _23151 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _23212 = clamp(ivec2(vec2(((_22842.x * _23151) / _22842.z) + (0.5 * float(_277.OutputWidth)), (((-_22842.y) * _23151) / _22842.z) + (0.5 * float(_277.OutputHeight)))), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                            vec4 _23164 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_23212.x, int((_277.OutputHeight - 1u) - uint(_23212.y)), 0).xy, 0);
                            float _23165 = _23164.x;
                            bool _22849 = _22842.z > (((_23165 > 0.0) && (abs(_23165 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_23165 - _277.ProjA)) : 1000000000.0);
                            _28537 = _22849 ? _27855 : _22837;
                            _28538 = _22849 ? _22837 : _27852;
                        }
                        vec3 _22863 = _9382 + (_6061 * _27852);
                        float _23282 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _23272 = vec2(((_22863.x * _23282) / _22863.z) + (0.5 * float(_277.OutputWidth)), (((-_22863.y) * _23282) / _22863.z) + (0.5 * float(_277.OutputHeight)));
                        ivec2 _23340 = clamp(ivec2(_23272), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
                        vec3 _23300 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_23340.x, int((_277.OutputHeight - 1u) - uint(_23340.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _23302 = length(_23300);
                        vec3 _23308 = _23300 / vec3(_23302);
                        bvec3 _23309 = bvec3(_23302 > 0.001000000047497451305389404296875);
                        vec3 _23310 = vec3(_23309.x ? _23308.x : vec3(0.0, 1.0, 0.0).x, _23309.y ? _23308.y : vec3(0.0, 1.0, 0.0).y, _23309.z ? _23308.z : vec3(0.0, 1.0, 0.0).z);
                        if (dot(vec3(dot(_23310, _277.CameraRight.xyz), dot(_23310, _277.CameraUp.xyz), dot(_23310, _277.CameraForward.xyz)), _6061) > 0.100000001490116119384765625)
                        {
                            int _22890 = _27848 + 1;
                            _27850 = _22782;
                            _27849 = _22780;
                            _27848 = _22890;
                            _27868 = _22780;
                            continue;
                        }
                        float _23404 = clamp(((_6058 * _27852) * _22957) / max(_22863.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _23406 = 0.5 * _23404;
                        vec2 _23491 = clamp(_23272 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23517 = clamp((_23272 + vec2(_23406)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23543 = clamp((_23272 + vec2(_23404 * (-0.5), _23406)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23569 = clamp((_23272 + vec2(_23406, _23404 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23595 = clamp((_23272 + vec2(_23404 * (-0.5), _23404 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23621 = clamp((_23272 + vec2(_23404, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23647 = clamp((_23272 + vec2(-_23404, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23673 = clamp((_23272 + vec2(0.0, _23404)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _23699 = clamp((_23272 + vec2(0.0, -_23404)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _23476 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23491.x, 1.0 - _23491.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23517.x, 1.0 - _23517.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23543.x, 1.0 - _23543.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23569.x, 1.0 - _23569.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23595.x, 1.0 - _23595.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23621.x, 1.0 - _23621.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23647.x, 1.0 - _23647.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23673.x, 1.0 - _23673.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23699.x, 1.0 - _23699.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _28000 = _23476;
                        _27873 = _22760;
                        _27867 = _22780;
                        _27862 = false;
                        _27857 = true;
                        break;
                    }
                    int _22890 = _27848 + 1;
                    _27850 = _22782;
                    _27849 = _22780;
                    _27848 = _22890;
                    _27868 = _22780;
                    continue;
                }
                else
                {
                    _28000 = vec3(0.0);
                    _27873 = _26445;
                    _27867 = _27868;
                    _27862 = false;
                    _27857 = false;
                    break;
                }
            }
            if (_27857)
            {
                _27999 = _28000;
                _27872 = _27873;
                break;
            }
            if ((!_27862) || (_27867 < 0.0))
            {
                _27999 = _28000;
                _27872 = 0.0;
                break;
            }
            vec3 _22905 = _9382 + (_6061 * _27867);
            float _23750 = (0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _23740 = vec2(((_22905.x * _23750) / _22905.z) + (0.5 * float(_277.OutputWidth)), (((-_22905.y) * _23750) / _22905.z) + (0.5 * float(_277.OutputHeight)));
            ivec2 _23811 = clamp(ivec2(_23740), ivec2(0), ivec2(int(_277.OutputWidth) - 1, int(_277.OutputHeight) - 1));
            vec4 _23763 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_23811.x, int((_277.OutputHeight - 1u) - uint(_23811.y)), 0).xy, 0);
            float _23764 = _23763.x;
            float _23781 = ((_23764 > 0.0) && (abs(_23764 - _277.ProjA) > 9.999999717180685365747194737196e-10)) ? (_277.ProjB / (_23764 - _277.ProjA)) : 1000000000.0;
            float _23857 = clamp(((_6058 * _27867) * _22957) / max(_22905.z, 0.0500000007450580596923828125), 0.0, 12.0);
            float _23859 = 0.5 * _23857;
            vec2 _23944 = clamp(_23740 / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _23970 = clamp((_23740 + vec2(_23859)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _23996 = clamp((_23740 + vec2(_23857 * (-0.5), _23859)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24022 = clamp((_23740 + vec2(_23859, _23857 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24048 = clamp((_23740 + vec2(_23857 * (-0.5), _23857 * (-0.5))) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24074 = clamp((_23740 + vec2(_23857, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24100 = clamp((_23740 + vec2(-_23857, 0.0)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24126 = clamp((_23740 + vec2(0.0, _23857)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _24152 = clamp((_23740 + vec2(0.0, -_23857)) / vec2(float(_277.OutputWidth), float(_277.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _23929 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23944.x, 1.0 - _23944.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23970.x, 1.0 - _23970.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23996.x, 1.0 - _23996.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24022.x, 1.0 - _24022.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24048.x, 1.0 - _24048.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24074.x, 1.0 - _24074.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24100.x, 1.0 - _24100.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24126.x, 1.0 - _24126.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24152.x, 1.0 - _24152.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_23781 > 100000000.0)
            {
                _27999 = _23929;
                _27872 = _22760;
                break;
            }
            float _22930 = _23781 - _22905.z;
            if (_22930 <= 0.0)
            {
                _27999 = _23929;
                _27872 = _22760;
                break;
            }
            _27999 = _23929;
            _27872 = _22760 * exp((_22930 / max(_6061.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        vec3 _28005;
        do
        {
            if (_277.EnvParams.x <= 0.0)
            {
                float _24206 = _22711.y;
                _28005 = mix(_277.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_277.SkyHorizon.xyz, _277.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _24206))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _24206)));
                break;
            }
            _28005 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_22711.x, _22711.y, -_22711.z), clamp(clamp(log2(max(_6058 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _277.EnvParams.x;
            break;
        } while(false);
        vec3 _6081 = mix(_28005, _27999, vec3(_27872));
        if (_277.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_5327), vec4(_6081, 1.0));
            break;
        }
        float _6109 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_25977 + _26016), _5548), 0.0, 1.0), max(_277.FresnelPower, 1.0));
        float _6110 = 0.0199999995529651641845703125 + _6109;
        vec3 _6120 = normalize(_277.SunDirection.xyz);
        float _6137 = max((2.0 / ((2.0 / (max(_277.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _5724))) - 2.0, 4.0);
        vec3 _6206 = (mix(_5979, _6081, vec3(clamp(_6110, 0.0, 1.0))) + (((((_277.SunColor.xyz * ((pow(clamp(dot(_26016, normalize(_6120 - _5546)), 0.0, 1.0), _6137) * (_6137 + 8.0)) * 0.0397887341678142547607421875)) * _277.Specular) * _6110) * _26014) * clamp(dot(_26016, _6120) * 4.0, 0.0, 1.0))) + ((_277.SunColor.xyz * _277.FluidColor.xyz) * (((((_277.LightParams.w * 0.60000002384185791015625) * (((1.0 - _5777) * exp(_5772 * (-1.5))) * (1.0 - exp(_5772 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_5391, _12093)), 9.9999997473787516355514526367188e-05), 1.5))) * _26014) * (0.980000019073486328125 - _6109)));
        vec3 _6224 = mix(_6206, (_6206 * 1.25) + (_6370 * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _5767)) * _5777) * _277.LightParams.z, 0.0, 1.0)));
        float _6233 = clamp(1.0 - exp(-((_25975 * 0.07500000298023223876953125) + (_5360 * 0.0003662109375))), 0.0, 1.0);
        vec3 _28128;
        if ((_6233 > 0.0199999995529651641845703125) && (_277.FoamParams.x > 0.0))
        {
            vec2 _24267 = _5654.xz / vec2(max(_277.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _24302 = floor(_24267);
            vec2 _24305 = _24267 - _24302;
            float _28121;
            float _28122;
            _28122 = 8.0;
            _28121 = 8.0;
            float _28137;
            float _28138;
            SPIRV_CROSS_UNROLL
            for (int _28120 = -1; _28120 <= 1; _28122 = _28137, _28121 = _28138, _28120++)
            {
                _28138 = _28121;
                _28137 = _28122;
                float _28540;
                float _28463;
                SPIRV_CROSS_UNROLL
                for (int _28135 = -1; _28135 <= 1; _28138 = _28463, _28137 = _28540, _28135++)
                {
                    vec2 _24320 = vec2(float(_28135), float(_28120));
                    ivec2 _24324 = ivec2(_24302 + _24320);
                    uint _24396 = ((((uint(_24324.x + 4096) * 1973u) + (uint(_24324.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _24404 = ((_24396 >> ((_24396 >> 28u) + 4u)) ^ _24396) * 277803737u;
                    uint _24408 = (_24404 >> 22u) ^ _24404;
                    float _24362 = length((_24320 + (vec2(0.5) + (sin(vec2(_277.ShadeParams.z * 0.5) + ((vec2(float(_24408 & 65535u), float(_24408 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _24305);
                    bool _24365 = _24362 < _28137;
                    if (_24365)
                    {
                        _28463 = _28137;
                    }
                    else
                    {
                        _28463 = (_24362 < _28138) ? _24362 : _28138;
                    }
                    _28540 = _24365 ? _24362 : _28137;
                }
            }
            vec2 _24274 = (_24267 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _24425 = floor(_24274);
            vec2 _24428 = _24274 - _24425;
            float _28124;
            float _28125;
            _28125 = 8.0;
            _28124 = 8.0;
            float _28131;
            float _28132;
            SPIRV_CROSS_UNROLL
            for (int _28123 = -1; _28123 <= 1; _28125 = _28131, _28124 = _28132, _28123++)
            {
                _28132 = _28124;
                _28131 = _28125;
                float _28542;
                float _28457;
                SPIRV_CROSS_UNROLL
                for (int _28129 = -1; _28129 <= 1; _28132 = _28457, _28131 = _28542, _28129++)
                {
                    vec2 _24443 = vec2(float(_28129), float(_28123));
                    ivec2 _24447 = ivec2(_24425 + _24443);
                    uint _24519 = ((((uint(_24447.x + 4096) * 1973u) + (uint(_24447.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _24527 = ((_24519 >> ((_24519 >> 28u) + 4u)) ^ _24519) * 277803737u;
                    uint _24531 = (_24527 >> 22u) ^ _24527;
                    float _24485 = length((_24443 + (vec2(0.5) + (sin(vec2(_277.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_24531 & 65535u), float(_24531 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _24428);
                    bool _24488 = _24485 < _28131;
                    if (_24488)
                    {
                        _28457 = _28131;
                    }
                    else
                    {
                        _28457 = (_24485 < _28132) ? _24485 : _28132;
                    }
                    _28542 = _24488 ? _24485 : _28131;
                }
            }
            float _24285 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _28121 - _28122), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _28124 - _28125))), clamp((((_277.FoamParams.y * ((0.5 * float(_277.OutputHeight)) * max(_277.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_25947, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _28128 = mix(_6224, _22629 * mix(0.85000002384185791015625, 1.0, _24285), vec3(clamp(_6233 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_24285 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_277.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _28128 = _6224;
        }
        float _6279 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _5648) * clamp(_277.Foam * 1.60000002384185791015625, 0.0, 1.0);
        imageStore(OutputTexture, ivec2(_5327), vec4(mix(mix(_28128, _22629, vec3(_6279 * 0.89999997615814208984375)), _22629, vec3(_5414)), max(clamp(_25948.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _6279)), _5414)));
        break;
    } while(false);
}

