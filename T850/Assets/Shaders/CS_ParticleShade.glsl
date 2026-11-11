#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=80ba6bb7ad0149d897d7a74726823b3b1f7925e5ebea3c8beb4f520e95392581; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1658[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _19765;
vec2 _19874;
float _20755;

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
} _242;

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
        if (_242.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _242.OutputWidth) || (gl_GlobalInvocationID.y >= _242.OutputHeight))
        {
            break;
        }
        uvec2 _4592 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _4592.x;
        FluidSize.y = _4592.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _4619 = uvec2(gl_GlobalInvocationID.x, (_242.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _4622 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _4627 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _4641 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _242.OutputWidth)) * 4u;
        uint _4645 = WhitewaterBits_1._data[_4641];
        float _4646 = float(_4645);
        uint _4651 = WhitewaterBits_1._data[_4641 + 1u];
        float _4652 = float(_4651);
        uint _4657 = WhitewaterBits_1._data[_4641 + 2u];
        float _4658 = float(_4657);
        uint _4663 = WhitewaterBits_1._data[_4641 + 3u];
        WhitewaterBits_1._data[_4641] = 0u;
        WhitewaterBits_1._data[_4641 + 1u] = 0u;
        WhitewaterBits_1._data[_4641 + 2u] = 0u;
        WhitewaterBits_1._data[_4641 + 3u] = 0u;
        vec3 _4681 = normalize(_242.SunDirection.xyz);
        vec3 _4683 = normalize(((_242.CameraRight.xyz * _4681.x) + (_242.CameraUp.xyz * _4681.y)) + (_242.CameraForward.xyz * _4681.z));
        vec3 _4686 = refract(-_4683, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _20720 = bvec3((dot(_4686, _4686) < 9.9999999747524270787835121154785e-07) || (_4683.y <= 0.0));
        vec3 _20721 = vec3(_20720.x ? vec3(0.0, -1.0, 0.0).x : _4686.x, _20720.y ? vec3(0.0, -1.0, 0.0).y : _4686.y, _20720.z ? vec3(0.0, -1.0, 0.0).z : _4686.z);
        float _5626 = max(EnvMaxMip - 1.0, 0.0);
        vec3 _19602;
        do
        {
            if (_242.EnvParams.x <= 0.0)
            {
                _19602 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                break;
            }
            _19602 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_5626, 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
            break;
        } while(false);
        vec3 _19603;
        do
        {
            if (_242.EnvParams.x <= 0.0)
            {
                _19603 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _19603 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_5626, 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
            break;
        } while(false);
        vec3 _19605;
        do
        {
            if (_242.EnvParams.x <= 0.0)
            {
                _19605 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _19605 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_5626, 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
            break;
        } while(false);
        vec3 _19607;
        do
        {
            if (_242.EnvParams.x <= 0.0)
            {
                _19607 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _19607 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_5626, 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
            break;
        } while(false);
        vec3 _19609;
        do
        {
            if (_242.EnvParams.x <= 0.0)
            {
                _19609 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                break;
            }
            _19609 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_5626, 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
            break;
        } while(false);
        vec3 _5648 = (((((_19602 * 2.0) + _19603) + _19605) + _19607) + _19609) * vec3(0.16666667163372039794921875);
        float _4706 = clamp(1.0 - exp(_4646 * (-0.0006103515625)), 0.0, 1.0) * clamp(_242.FoamParams.w, 0.0, 1.0);
        float _19613;
        vec4 _19614;
        do
        {
            vec2 _6001 = ((_4627 * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight))) - vec2(0.5);
            ivec2 _6004 = ivec2(floor(_6001));
            vec2 _6008 = _6001 - vec2(_6004);
            ivec2 _6183 = clamp(_6004, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6219 = clamp(_6004 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6255 = clamp(_6004 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _6291 = clamp(_6004 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6044 = vec4((1.0 - _6008.x) * (1.0 - _6008.y), _6008.x * (1.0 - _6008.y), (1.0 - _6008.x) * _6008.y, _6008.x * _6008.y);
            vec4 _5971 = _6044;
            vec4 _6049 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6183.x, int((FluidSize.y - 1u) - uint(_6183.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6219.x, int((FluidSize.y - 1u) - uint(_6219.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6255.x, int((FluidSize.y - 1u) - uint(_6255.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_6291.x, int((FluidSize.y - 1u) - uint(_6291.y)), 0).xy, 0).x);
            vec4 _5972 = _6049;
            float _20616;
            _20616 = 1000000000.0;
            float _20719;
            SPIRV_CROSS_UNROLL
            for (int _19611 = 0; _19611 < 4; _20616 = _20719, _19611++)
            {
                if (_5972[_19611] > 0.0)
                {
                    _20719 = min(_20616, _5972[_19611]);
                }
                else
                {
                    _20719 = _20616;
                }
            }
            vec4 _5975 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _19612 = 0; _19612 < 4; )
            {
                _5975[_19612] = ((_5972[_19612] > 0.0) && (_5972[_19612] < (_20616 + max(0.25, 0.02999999932944774627685546875 * _20616)))) ? _5971[_19612] : 0.0;
                _19612++;
                continue;
            }
            float _6109 = ((_5975.x + _5975.y) + _5975.z) + _5975.w;
            if (_6109 <= 9.9999997473787516355514526367188e-05)
            {
                _19614 = vec4(0.0);
                _19613 = 0.0;
                break;
            }
            ivec2 _6327 = clamp(_6004, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6353 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6327.x, int((FluidSize.y - 1u) - uint(_6327.y)), 0).xy, 0);
            ivec2 _6362 = clamp(_6004 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6388 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6362.x, int((FluidSize.y - 1u) - uint(_6362.y)), 0).xy, 0);
            ivec2 _6397 = clamp(_6004 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6423 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6397.x, int((FluidSize.y - 1u) - uint(_6397.y)), 0).xy, 0);
            ivec2 _6432 = clamp(_6004 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _6458 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_6432.x, int((FluidSize.y - 1u) - uint(_6432.y)), 0).xy, 0);
            _19614 = vec4((((((_6353.xyz * _5975.x) + (_6388.xyz * _5975.y)) + (_6423.xyz * _5975.z)) + (_6458.xyz * _5975.w)) / vec3(_6109)).x, _20755, _20755, dot(_6044, vec4(_6353.w, _6388.w, _6423.w, _6458.w)));
            _19613 = dot(_5975, _6049) / _6109;
            break;
        } while(false);
        ivec2 _6520 = clamp(_4622, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
        vec4 _6471 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_6520.x, int((_242.OutputHeight - 1u) - uint(_6520.y)), 0).xy, 0);
        float _6472 = _6471.x;
        float _6489 = ((_6472 > 0.0) && (abs(_6472 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_6472 - _242.ProjA)) : 1000000000.0;
        if (!(((_19613 > 0.0) && (_19614.w > 0.001000000047497451305389404296875)) && (_19613 < (_6489 - 0.00200000009499490261077880859375))))
        {
            vec2 _6565 = clamp(_4627 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _6576 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_6565.x, 1.0 - _6565.y), 0.0).xyz;
            float _20608;
            vec3 _20612;
            if ((_6489 < 100000000.0) && (_242.WetParams.w > 0.0))
            {
                float _6624 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _4741 = vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w) + (((_242.CameraRight.xyz * (((_4627.x - (0.5 * float(_242.OutputWidth))) * _6489) / _6624)) + (_242.CameraUp.xyz * (((-(_4627.y - (0.5 * float(_242.OutputHeight)))) * _6489) / _6624))) + (_242.CameraForward.xyz * _6489));
                ivec2 _6704 = clamp(_4622, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                vec3 _6664 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_6704.x, int((_242.OutputHeight - 1u) - uint(_6704.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _6666 = length(_6664);
                vec3 _6672 = _6664 / vec3(_6666);
                bvec3 _6673 = bvec3(_6666 > 0.001000000047497451305389404296875);
                vec3 _6674 = vec3(_6673.x ? _6672.x : vec3(0.0, 1.0, 0.0).x, _6673.y ? _6672.y : vec3(0.0, 1.0, 0.0).y, _6673.z ? _6672.z : vec3(0.0, 1.0, 0.0).z);
                float _20609;
                vec3 _20613;
                if ((_6674.y > 0.60000002384185791015625) && (_4741.y > (_242.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _20605;
                    do
                    {
                        if (((_242.GridParams.x < 2u) || (_242.GridParams.y < 2u)) || (_242.WetParams.z <= 0.0))
                        {
                            _20605 = 0.0;
                            break;
                        }
                        vec2 _6773 = ((_4741.xz - _242.WetParams.xy) / vec2(_242.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_6773, vec2(0.0))) || (_6773.x >= float(_242.GridParams.x - 1u))) || (_6773.y >= float(_242.GridParams.y - 1u)))
                        {
                            _20605 = 0.0;
                            break;
                        }
                        uvec2 _6794 = uvec2(_6773);
                        vec2 _6798 = _6773 - vec2(_6794);
                        uint _6805 = _6794.x + (_6794.y * _242.GridParams.x);
                        _20605 = mix(mix(float(Wetness_1._data[_6805]), float(Wetness_1._data[_6805 + 1u]), _6798.x), mix(float(Wetness_1._data[_6805 + _242.GridParams.x]), float(Wetness_1._data[(_6805 + _242.GridParams.x) + 1u]), _6798.x), _6798.y) * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _4761 = clamp(_20605 * _242.WetParams.w, 0.0, 1.0);
                    vec3 _20614;
                    if (_4761 > 0.001000000047497451305389404296875)
                    {
                        vec3 _4768 = normalize(_4741 - vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w));
                        vec3 _6873 = reflect(_4768, _6674);
                        vec3 _20607;
                        do
                        {
                            if (_242.EnvParams.x <= 0.0)
                            {
                                float _6923 = _6873.y;
                                _20607 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _6923))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _6923)));
                                break;
                            }
                            _20607 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_6873.x, _6873.y, -_6873.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
                            break;
                        } while(false);
                        _20614 = (_6576 * mix(1.0, 0.550000011920928955078125, _4761)) + (_20607 * (((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_6674, -_4768), 0.0, 1.0), 5.0))) * _4761) * 0.800000011920928955078125));
                    }
                    else
                    {
                        _20614 = _6576;
                    }
                    _20613 = _20614;
                    _20609 = _4761;
                }
                else
                {
                    _20613 = _6576;
                    _20609 = 0.0;
                }
                _20612 = _20613;
                _20608 = _20609;
            }
            else
            {
                _20612 = _6576;
                _20608 = 0.0;
            }
            if (_242.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_4619), vec4(_20608, _20608, _20608, 1.0));
                break;
            }
            if ((_20608 <= 0.001000000047497451305389404296875) && (_4706 <= 0.001000000047497451305389404296875))
            {
                if (_242.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_4619), vec4(0.0));
                }
                break;
            }
            vec3 _6959 = ((_242.SunColor.xyz * (clamp(_4683.y, 0.0, 1.0) * 0.3183098733425140380859375)) + _5648) * 0.85000002384185791015625;
            vec3 _6965 = mix(vec3(dot(_6959, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _6959, vec3(0.5));
            if (_20608 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_4619), vec4(mix(_20612, _6965, vec3(_4706)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_4619), vec4(_6965, _4706));
            }
            break;
        }
        float _7008 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _6984 = ((_4627.x - (0.5 * float(_242.OutputWidth))) * _19613) / _7008;
        float _6996 = ((-(_4627.y - (0.5 * float(_242.OutputHeight)))) * _19613) / _7008;
        vec3 _6998 = vec3(_6984, _6996, _19613);
        vec3 _4835 = normalize(_6998);
        vec3 _4837 = -_4835;
        int _4853 = clamp(int((_242.NormalStep * max(float(_242.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _4859 = (2.0 * max(_242.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_242.NormalStep, 1.0);
        ivec2 _4861 = ivec2(_4853, 0);
        vec3 _19621;
        do
        {
            float _19617;
            do
            {
                vec2 _7166 = (((vec2(clamp(_4622 + _4861, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight))) - vec2(0.5);
                ivec2 _7169 = ivec2(floor(_7166));
                vec2 _7173 = _7166 - vec2(_7169);
                ivec2 _7348 = clamp(_7169, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _7384 = clamp(_7169 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _7420 = clamp(_7169 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _7456 = clamp(_7169 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _7136 = vec4((1.0 - _7173.x) * (1.0 - _7173.y), _7173.x * (1.0 - _7173.y), (1.0 - _7173.x) * _7173.y, _7173.x * _7173.y);
                vec4 _7214 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7348.x, int((FluidSize.y - 1u) - uint(_7348.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7384.x, int((FluidSize.y - 1u) - uint(_7384.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7420.x, int((FluidSize.y - 1u) - uint(_7420.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7456.x, int((FluidSize.y - 1u) - uint(_7456.y)), 0).xy, 0).x);
                vec4 _7137 = _7214;
                float _20603;
                _20603 = 1000000000.0;
                float _20718;
                SPIRV_CROSS_UNROLL
                for (int _19615 = 0; _19615 < 4; _20603 = _20718, _19615++)
                {
                    if (_7137[_19615] > 0.0)
                    {
                        _20718 = min(_20603, _7137[_19615]);
                    }
                    else
                    {
                        _20718 = _20603;
                    }
                }
                vec4 _7140 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _19616 = 0; _19616 < 4; )
                {
                    _7140[_19616] = ((_7137[_19616] > 0.0) && (_7137[_19616] < (_20603 + max(0.25, 0.02999999932944774627685546875 * _20603)))) ? _7136[_19616] : 0.0;
                    _19616++;
                    continue;
                }
                float _7274 = ((_7140.x + _7140.y) + _7140.z) + _7140.w;
                if (_7274 <= 9.9999997473787516355514526367188e-05)
                {
                    _19617 = 0.0;
                    break;
                }
                _19617 = dot(_7140, _7214) / _7274;
                break;
            } while(false);
            float _19620;
            do
            {
                vec2 _7696 = (((vec2(clamp(_4622 - _4861, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight))) - vec2(0.5);
                ivec2 _7699 = ivec2(floor(_7696));
                vec2 _7703 = _7696 - vec2(_7699);
                ivec2 _7878 = clamp(_7699, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _7914 = clamp(_7699 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _7950 = clamp(_7699 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _7986 = clamp(_7699 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _7666 = vec4((1.0 - _7703.x) * (1.0 - _7703.y), _7703.x * (1.0 - _7703.y), (1.0 - _7703.x) * _7703.y, _7703.x * _7703.y);
                vec4 _7744 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7878.x, int((FluidSize.y - 1u) - uint(_7878.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7914.x, int((FluidSize.y - 1u) - uint(_7914.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7950.x, int((FluidSize.y - 1u) - uint(_7950.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7986.x, int((FluidSize.y - 1u) - uint(_7986.y)), 0).xy, 0).x);
                vec4 _7667 = _7744;
                float _20600;
                _20600 = 1000000000.0;
                float _20717;
                SPIRV_CROSS_UNROLL
                for (int _19618 = 0; _19618 < 4; _20600 = _20717, _19618++)
                {
                    if (_7667[_19618] > 0.0)
                    {
                        _20717 = min(_20600, _7667[_19618]);
                    }
                    else
                    {
                        _20717 = _20600;
                    }
                }
                vec4 _7670 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _19619 = 0; _19619 < 4; )
                {
                    _7670[_19619] = ((_7667[_19619] > 0.0) && (_7667[_19619] < (_20600 + max(0.25, 0.02999999932944774627685546875 * _20600)))) ? _7666[_19619] : 0.0;
                    _19619++;
                    continue;
                }
                float _7804 = ((_7670.x + _7670.y) + _7670.z) + _7670.w;
                if (_7804 <= 9.9999997473787516355514526367188e-05)
                {
                    _19620 = 0.0;
                    break;
                }
                _19620 = dot(_7670, _7744) / _7804;
                break;
            } while(false);
            bool _7042 = (_19617 > 0.0) && (abs(_19617 - _19613) < _4859);
            bool _7051 = (_19620 > 0.0) && (abs(_19613 - _19620) < _4859);
            vec2 _7058 = vec2(_4622 + _4861) + vec2(0.5);
            float _8192 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _7062 = vec3(((_7058.x - (0.5 * float(_242.OutputWidth))) * _19617) / _8192, ((-(_7058.y - (0.5 * float(_242.OutputHeight)))) * _19617) / _8192, _19617) - _6998;
            bvec3 _7063 = bvec3(_7042);
            vec3 _7064 = vec3(_7063.x ? _7062.x : vec3(0.0).x, _7063.y ? _7062.y : vec3(0.0).y, _7063.z ? _7062.z : vec3(0.0).z);
            vec2 _7072 = vec2(_4622 - _4861) + vec2(0.5);
            float _8231 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _7075 = _6998 - vec3(((_7072.x - (0.5 * float(_242.OutputWidth))) * _19620) / _8231, ((-(_7072.y - (0.5 * float(_242.OutputHeight)))) * _19620) / _8231, _19620);
            bvec3 _7076 = bvec3(_7051);
            vec3 _7077 = vec3(_7076.x ? _7075.x : vec3(0.0).x, _7076.y ? _7075.y : vec3(0.0).y, _7076.z ? _7075.z : vec3(0.0).z);
            if (_7042 && _7051)
            {
                _19621 = (_7064 + _7077) * 0.5;
                break;
            }
            bvec3 _7090 = bvec3(_7042);
            _19621 = vec3(_7090.x ? _7064.x : _7077.x, _7090.y ? _7064.y : _7077.y, _7090.z ? _7064.z : _7077.z);
            break;
        } while(false);
        ivec2 _4868 = ivec2(0, _4853);
        vec3 _19628;
        do
        {
            float _19624;
            do
            {
                vec2 _8389 = (((vec2(clamp(_4622 + _4868, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight))) - vec2(0.5);
                ivec2 _8392 = ivec2(floor(_8389));
                vec2 _8396 = _8389 - vec2(_8392);
                ivec2 _8571 = clamp(_8392, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _8607 = clamp(_8392 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _8643 = clamp(_8392 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _8679 = clamp(_8392 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _8359 = vec4((1.0 - _8396.x) * (1.0 - _8396.y), _8396.x * (1.0 - _8396.y), (1.0 - _8396.x) * _8396.y, _8396.x * _8396.y);
                vec4 _8437 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_8571.x, int((FluidSize.y - 1u) - uint(_8571.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_8607.x, int((FluidSize.y - 1u) - uint(_8607.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_8643.x, int((FluidSize.y - 1u) - uint(_8643.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_8679.x, int((FluidSize.y - 1u) - uint(_8679.y)), 0).xy, 0).x);
                vec4 _8360 = _8437;
                float _20597;
                _20597 = 1000000000.0;
                float _20716;
                SPIRV_CROSS_UNROLL
                for (int _19622 = 0; _19622 < 4; _20597 = _20716, _19622++)
                {
                    if (_8360[_19622] > 0.0)
                    {
                        _20716 = min(_20597, _8360[_19622]);
                    }
                    else
                    {
                        _20716 = _20597;
                    }
                }
                vec4 _8363 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _19623 = 0; _19623 < 4; )
                {
                    _8363[_19623] = ((_8360[_19623] > 0.0) && (_8360[_19623] < (_20597 + max(0.25, 0.02999999932944774627685546875 * _20597)))) ? _8359[_19623] : 0.0;
                    _19623++;
                    continue;
                }
                float _8497 = ((_8363.x + _8363.y) + _8363.z) + _8363.w;
                if (_8497 <= 9.9999997473787516355514526367188e-05)
                {
                    _19624 = 0.0;
                    break;
                }
                _19624 = dot(_8363, _8437) / _8497;
                break;
            } while(false);
            float _19627;
            do
            {
                vec2 _8919 = (((vec2(clamp(_4622 - _4868, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight))) - vec2(0.5);
                ivec2 _8922 = ivec2(floor(_8919));
                vec2 _8926 = _8919 - vec2(_8922);
                ivec2 _9101 = clamp(_8922, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9137 = clamp(_8922 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9173 = clamp(_8922 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _9209 = clamp(_8922 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _8889 = vec4((1.0 - _8926.x) * (1.0 - _8926.y), _8926.x * (1.0 - _8926.y), (1.0 - _8926.x) * _8926.y, _8926.x * _8926.y);
                vec4 _8967 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9101.x, int((FluidSize.y - 1u) - uint(_9101.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9137.x, int((FluidSize.y - 1u) - uint(_9137.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9173.x, int((FluidSize.y - 1u) - uint(_9173.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_9209.x, int((FluidSize.y - 1u) - uint(_9209.y)), 0).xy, 0).x);
                vec4 _8890 = _8967;
                float _20594;
                _20594 = 1000000000.0;
                float _20715;
                SPIRV_CROSS_UNROLL
                for (int _19625 = 0; _19625 < 4; _20594 = _20715, _19625++)
                {
                    if (_8890[_19625] > 0.0)
                    {
                        _20715 = min(_20594, _8890[_19625]);
                    }
                    else
                    {
                        _20715 = _20594;
                    }
                }
                vec4 _8893 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _19626 = 0; _19626 < 4; )
                {
                    _8893[_19626] = ((_8890[_19626] > 0.0) && (_8890[_19626] < (_20594 + max(0.25, 0.02999999932944774627685546875 * _20594)))) ? _8889[_19626] : 0.0;
                    _19626++;
                    continue;
                }
                float _9027 = ((_8893.x + _8893.y) + _8893.z) + _8893.w;
                if (_9027 <= 9.9999997473787516355514526367188e-05)
                {
                    _19627 = 0.0;
                    break;
                }
                _19627 = dot(_8893, _8967) / _9027;
                break;
            } while(false);
            bool _8265 = (_19624 > 0.0) && (abs(_19624 - _19613) < _4859);
            bool _8274 = (_19627 > 0.0) && (abs(_19613 - _19627) < _4859);
            vec2 _8281 = vec2(_4622 + _4868) + vec2(0.5);
            float _9415 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _8285 = vec3(((_8281.x - (0.5 * float(_242.OutputWidth))) * _19624) / _9415, ((-(_8281.y - (0.5 * float(_242.OutputHeight)))) * _19624) / _9415, _19624) - _6998;
            bvec3 _8286 = bvec3(_8265);
            vec3 _8287 = vec3(_8286.x ? _8285.x : vec3(0.0).x, _8286.y ? _8285.y : vec3(0.0).y, _8286.z ? _8285.z : vec3(0.0).z);
            vec2 _8295 = vec2(_4622 - _4868) + vec2(0.5);
            float _9454 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _8298 = _6998 - vec3(((_8295.x - (0.5 * float(_242.OutputWidth))) * _19627) / _9454, ((-(_8295.y - (0.5 * float(_242.OutputHeight)))) * _19627) / _9454, _19627);
            bvec3 _8299 = bvec3(_8274);
            vec3 _8300 = vec3(_8299.x ? _8298.x : vec3(0.0).x, _8299.y ? _8298.y : vec3(0.0).y, _8299.z ? _8298.z : vec3(0.0).z);
            if (_8265 && _8274)
            {
                _19628 = (_8287 + _8300) * 0.5;
                break;
            }
            bvec3 _8313 = bvec3(_8265);
            _19628 = vec3(_8313.x ? _8287.x : _8300.x, _8313.y ? _8287.y : _8300.y, _8313.z ? _8287.z : _8300.z);
            break;
        } while(false);
        vec2 _4879 = _19621.xy * max(_242.NormalScale, 0.0);
        vec3 _19049 = _19621;
        _19049.x = _4879.x;
        _19049.y = _4879.y;
        vec2 _4889 = _19628.xy * max(_242.NormalScale, 0.0);
        vec3 _19053 = _19628;
        _19053.x = _4889.x;
        _19053.y = _4889.y;
        bvec3 _20722 = bvec3(length(_19049) < 9.9999999747524270787835121154785e-07);
        bvec3 _20724 = bvec3(length(_19053) < 9.9999999747524270787835121154785e-07);
        vec3 _4907 = normalize(cross(vec3(_20722.x ? vec3(1.0, 0.0, 0.0).x : _19049.x, _20722.y ? vec3(1.0, 0.0, 0.0).y : _19049.y, _20722.z ? vec3(1.0, 0.0, 0.0).z : _19049.z), vec3(_20724.x ? vec3(0.0, -1.0, 0.0).x : _19053.x, _20724.y ? vec3(0.0, -1.0, 0.0).y : _19053.y, _20724.z ? vec3(0.0, -1.0, 0.0).z : _19053.z)));
        vec3 _19643;
        if (dot(_4907, _4837) < 0.0)
        {
            _19643 = -_4907;
        }
        else
        {
            _19643 = _4907;
        }
        float _4918 = max(_19614.x, 9.9999997473787516355514526367188e-05);
        ivec2 _9549 = clamp(ivec2(floor((_4627 * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        ivec2 _9559 = clamp(_9549, ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _9570 = (uint(_9559.x) + (uint(_9559.y) * FluidSize.x)) * 4u;
        uint _9580 = AuxBits_1._data[_9570 + 1u];
        uint _9592 = AuxBits_1._data[_9570 + 3u];
        float _19641;
        _19641 = float(AuxBits_1._data[_9570]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _19640 = 0; _19640 < 4; )
        {
            ivec2 _9484 = ivec2((_19640 * 2) - 1, 0);
            ivec2 _9489 = ivec2(0, ((_19640 - 2) * 2) - 1);
            bvec2 _9490 = bvec2(_19640 < 2);
            ivec2 _9605 = clamp(_9549 + ivec2(_9490.x ? _9484.x : _9489.x, _9490.y ? _9484.y : _9489.y), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _9651 = clamp(_9549 + ivec2(((_19640 & 1) != int(0u)) ? 1 : (-1), ((_19640 & 2) != int(0u)) ? 1 : (-1)), ivec2(0), ivec2(FluidSize) - ivec2(1));
            _19641 += ((float(AuxBits_1._data[(uint(_9605.x) + (uint(_9605.y) * FluidSize.x)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_9651.x) + (uint(_9651.y) * FluidSize.x)) * 4u]) * 1.525902189314365386962890625e-05));
            _19640++;
            continue;
        }
        float _4937 = clamp((float(_9592) * 0.0009765625) / _4918, 0.0, 1.0);
        vec3 _9709 = ((_242.CameraRight.xyz * _4835.x) + (_242.CameraUp.xyz * _4835.y)) + (_242.CameraForward.xyz * _4835.z);
        vec3 _4943 = vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w) + (((_242.CameraRight.xyz * _6984) + (_242.CameraUp.xyz * _6996)) + (_242.CameraForward.xyz * _19613));
        vec3 _9762 = ((_242.CameraRight.xyz * _19643.x) + (_242.CameraUp.xyz * _19643.y)) + (_242.CameraForward.xyz * _19643.z);
        float _4954 = (_19613 / ((0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_19643, _4837)), 0.07999999821186065673828125);
        float _4978 = (_242.RippleParams.x * mix(_242.RippleFlow.y, 1.0 + _242.RippleFlow.x, clamp((float(AuxBits_1._data[_9570 + 2u]) * 0.0009765625) / _4918, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _9762.y) * clamp(1.0 - (length(_6998) / max(_242.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _19648;
        vec3 _19682;
        if (_4978 > 0.0)
        {
            vec2 _4984 = _4943.xz;
            float _9792 = max(_242.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _19645;
            float _19646;
            _19646 = 0.0;
            _19645 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _19644 = 0; _19644 < 6; )
            {
                float _9804 = _9792 * _1658[_19644];
                float _9809 = _4978 * pow(0.7799999713897705078125, float(_19644));
                float _9816 = clamp((_9804 / max(_4954 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _9892 = (((uint(_19644) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _9900 = ((_9892 >> ((_9892 >> 28u) + 4u)) ^ _9892) * 277803737u;
                float _9823 = 0.60000002384185791015625 + (float(((_9900 >> 22u) ^ _9900) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _9828 = vec2(cos(_9823), sin(_9823));
                uint _9919 = (((uint(_19644) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _9927 = ((_9919 >> ((_9919 >> 28u) + 4u)) ^ _9919) * 277803737u;
                _19646 += (((0.5 * _9809) * _9809) * (1.0 - (_9816 * _9816)));
                _19645 += (_9828 * ((_9809 * _9816) * cos(((dot(_9828, _4984) * (6.283185482025146484375 / _9804)) + ((sqrt(61.63805389404296875 / _9804) * _242.RippleParams.z) * _242.ShadeParams.z)) + (float(((_9927 >> 22u) ^ _9927) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _19644++;
                continue;
            }
            vec3 _4999 = normalize(_9762 + vec3(-_19645.x, 0.0, -_19645.y));
            vec3 _5001 = normalize(vec3(dot(_4999, _242.CameraRight.xyz), dot(_4999, _242.CameraUp.xyz), dot(_4999, _242.CameraForward.xyz)));
            bvec3 _20726 = bvec3(dot(_5001, _4837) < 0.0199999995529651641845703125);
            _19682 = vec3(_20726.x ? _19643.x : _5001.x, _20726.y ? _19643.y : _5001.y, _20726.z ? _19643.z : _5001.z);
            _19648 = _19646;
        }
        else
        {
            _19682 = _19643;
            _19648 = 0.0;
        }
        float _5013 = _19648 * _242.RippleFlow.z;
        if (_242.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_4619), vec4((_19682 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_242.DebugView == 2u)
        {
            float _5036 = clamp(_19613 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_4619), vec4(_5036, _5036, _5036, 1.0));
            break;
        }
        float _5056 = (_6489 < 100000000.0) ? (max(_6489 - _19613, 0.0) / max(_4835.z, 0.0500000007450580596923828125)) : 10000.0;
        float _5061 = _4918 * max(_242.ThicknessScale, 0.0);
        float _5066 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _5061 / max(_5056, 0.001000000047497451305389404296875));
        float _19680;
        do
        {
            vec3 _9983 = (_4943 + (_20721 * (max(_4943.y - _242.PoolMin.y, 0.0500000007450580596923828125) / max(-_20721.y, 0.20000000298023223876953125)))) - vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w);
            float _10049 = dot(_9983, _242.CameraForward.xyz);
            if (_10049 <= 0.100000001490116119384765625)
            {
                _19680 = 1.0;
                break;
            }
            float _10090 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _10066 = ((dot(_9983, _242.CameraRight.xyz) * _10090) / _10049) + (0.5 * float(_242.OutputWidth));
            float _10079 = (((-dot(_9983, _242.CameraUp.xyz)) * _10090) / _10049) + (0.5 * float(_242.OutputHeight));
            vec2 _10080 = vec2(_10066, _10079);
            if (!((((_10066 >= 0.0) && (_10079 >= 0.0)) && (_10066 < float(_242.OutputWidth))) && (_10079 < float(_242.OutputHeight))))
            {
                _19680 = 1.0;
                break;
            }
            ivec2 _10174 = clamp(ivec2(_10080), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            vec4 _10126 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_10174.x, int((_242.OutputHeight - 1u) - uint(_10174.y)), 0).xy, 0);
            float _10127 = _10126.x;
            if (abs((((_10127 > 0.0) && (abs(_10127 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_10127 - _242.ProjA)) : 1000000000.0) - _10049) > ((0.1500000059604644775390625 * _10049) + 0.100000001490116119384765625))
            {
                _19680 = 1.0;
                break;
            }
            ivec2 _10234 = clamp(ivec2(_10080), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            _19680 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_10234.x, int((_242.OutputHeight - 1u) - uint(_10234.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _5072 = refract(_4835, _19682, 0.75187969207763671875);
        bvec3 _20730 = bvec3(dot(_5072, _5072) < 9.9999999747524270787835121154785e-07);
        vec3 _5091 = normalize(mix(_4835, vec3(_20730.x ? _4835.x : _5072.x, _20730.y ? _4835.y : _5072.y, _20730.z ? _4835.z : _5072.z), vec3(clamp(_242.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _5066))));
        float _5099 = clamp((_5056 * 1.2000000476837158203125) + 0.20000000298023223876953125, 0.300000011920928955078125, max(_242.MaxTravel, 0.300000011920928955078125) * 2.0);
        float _19732;
        float _19769;
        float _19805;
        vec2 _19841;
        do
        {
            ivec2 _10510 = clamp(ivec2(_4627), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            vec4 _10462 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_10510.x, int((_242.OutputHeight - 1u) - uint(_10510.y)), 0).xy, 0);
            float _10463 = _10462.x;
            vec2 _19696;
            float _19701;
            _19701 = ((_10463 > 0.0) && (abs(_10463 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_10463 - _242.ProjA)) : 1000000000.0;
            _19696 = _4627;
            float _10323;
            float _20734;
            vec2 _20736;
            bool _19692;
            float _19712;
            float _19733;
            float _19770;
            float _19806;
            vec2 _19842;
            int _19685 = 1;
            float _19687 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_19685 <= 16)
                {
                    float _10317 = float(_19685) * 0.0625;
                    _10323 = (_5099 * _10317) * sqrt(_10317);
                    vec3 _10328 = _6998 + (_5091 * _10323);
                    if (_10328.z <= 0.0500000007450580596923828125)
                    {
                        _19842 = _19874;
                        _19806 = _19765;
                        _19770 = _19765;
                        _19733 = _19765;
                        _19712 = 0.0;
                        _19692 = false;
                        break;
                    }
                    float _10580 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _10556 = ((_10328.x * _10580) / _10328.z) + (0.5 * float(_242.OutputWidth));
                    float _10569 = (((-_10328.y) * _10580) / _10328.z) + (0.5 * float(_242.OutputHeight));
                    vec2 _10570 = vec2(_10556, _10569);
                    if (!((((_10556 >= 0.0) && (_10569 >= 0.0)) && (_10556 < float(_242.OutputWidth))) && (_10569 < float(_242.OutputHeight))))
                    {
                        _19842 = _19874;
                        _19806 = _19765;
                        _19770 = _19765;
                        _19733 = _19765;
                        _19712 = 0.0;
                        _19692 = false;
                        break;
                    }
                    ivec2 _10664 = clamp(ivec2(_10570), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                    vec4 _10616 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_10664.x, int((_242.OutputHeight - 1u) - uint(_10664.y)), 0).xy, 0);
                    float _10617 = _10616.x;
                    float _10634 = ((_10617 > 0.0) && (abs(_10617 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_10617 - _242.ProjA)) : 1000000000.0;
                    if (_10328.z >= _10634)
                    {
                        ivec2 _10350 = ivec2(_10570);
                        ivec2 _10745 = clamp(ivec2(floor(((vec2(_10350) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _10754 = clamp(_10745, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _10780 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10754.x, int((FluidSize.y - 1u) - uint(_10754.y)), 0).xy, 0);
                        float _10781 = _10780.x;
                        ivec2 _10790 = clamp(_10745, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _10877 = clamp(_10350, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        vec4 _10829 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_10877.x, int((_242.OutputHeight - 1u) - uint(_10877.y)), 0).xy, 0);
                        float _10830 = _10829.x;
                        if (!(((_10781 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_10790.x, int((FluidSize.y - 1u) - uint(_10790.y)), 0).xy, 0).w > 0.5)) && ((((_10830 > 0.0) && (abs(_10830 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_10830 - _242.ProjA)) : 1000000000.0) >= (_10781 + 0.0500000007450580596923828125))))
                        {
                            _19842 = _19874;
                            _19806 = _19765;
                            _19770 = _19765;
                            _19733 = _19765;
                            _19712 = 0.0;
                            _19692 = false;
                            break;
                        }
                        float _19689;
                        _19689 = _10323;
                        float _20732;
                        float _20733;
                        int _19688 = 0;
                        float _19690 = _19687;
                        SPIRV_CROSS_UNROLL
                        for (; _19688 < 5; _19690 = _20732, _19689 = _20733, _19688++)
                        {
                            float _10365 = 0.5 * (_19690 + _19689);
                            vec3 _10370 = _6998 + (_5091 * _10365);
                            float _10947 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _11008 = clamp(ivec2(vec2(((_10370.x * _10947) / _10370.z) + (0.5 * float(_242.OutputWidth)), (((-_10370.y) * _10947) / _10370.z) + (0.5 * float(_242.OutputHeight)))), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                            vec4 _10960 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_11008.x, int((_242.OutputHeight - 1u) - uint(_11008.y)), 0).xy, 0);
                            float _10961 = _10960.x;
                            bool _10377 = _10370.z >= (((_10961 > 0.0) && (abs(_10961 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_10961 - _242.ProjA)) : 1000000000.0);
                            _20732 = _10377 ? _19690 : _10365;
                            _20733 = _10377 ? _10365 : _19689;
                        }
                        vec3 _10391 = _6998 + (_5091 * _19689);
                        float _11078 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _10402 = clamp(vec2(((_10391.x * _11078) / _10391.z) + (0.5 * float(_242.OutputWidth)), (((-_10391.y) * _11078) / _10391.z) + (0.5 * float(_242.OutputHeight))), vec2(0.5), vec2(float(_242.OutputWidth), float(_242.OutputHeight)) - vec2(0.5));
                        ivec2 _11139 = clamp(ivec2(_10402), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        vec4 _11091 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_11139.x, int((_242.OutputHeight - 1u) - uint(_11139.y)), 0).xy, 0);
                        float _11092 = _11091.x;
                        _19842 = _10402;
                        _19806 = ((_11092 > 0.0) && (abs(_11092 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_11092 - _242.ProjA)) : 1000000000.0;
                        _19770 = _19689;
                        _19733 = 1.0;
                        _19712 = 1.0;
                        _19692 = true;
                        break;
                    }
                    ivec2 _10417 = ivec2(_10570 - vec2(0.5));
                    ivec2 _11220 = clamp(ivec2(floor(((vec2(_10417) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _11229 = clamp(_11220, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _11255 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11229.x, int((FluidSize.y - 1u) - uint(_11229.y)), 0).xy, 0);
                    float _11256 = _11255.x;
                    ivec2 _11265 = clamp(_11220, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _11352 = clamp(_10417, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                    vec4 _11304 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_11352.x, int((_242.OutputHeight - 1u) - uint(_11352.y)), 0).xy, 0);
                    float _11305 = _11304.x;
                    ivec2 _10422 = ivec2(_10570 + vec2(0.5));
                    ivec2 _11433 = clamp(ivec2(floor(((vec2(_10422) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _11442 = clamp(_11433, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _11468 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11442.x, int((FluidSize.y - 1u) - uint(_11442.y)), 0).xy, 0);
                    float _11469 = _11468.x;
                    ivec2 _11478 = clamp(_11433, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _11565 = clamp(_10422, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                    vec4 _11517 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_11565.x, int((_242.OutputHeight - 1u) - uint(_11565.y)), 0).xy, 0);
                    float _11518 = _11517.x;
                    bool _10424 = (((_11256 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_11265.x, int((FluidSize.y - 1u) - uint(_11265.y)), 0).xy, 0).w > 0.5)) && ((((_11305 > 0.0) && (abs(_11305 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_11305 - _242.ProjA)) : 1000000000.0) >= (_11256 + 0.0500000007450580596923828125))) && (((_11469 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_11478.x, int((FluidSize.y - 1u) - uint(_11478.y)), 0).xy, 0).w > 0.5)) && ((((_11518 > 0.0) && (abs(_11518 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_11518 - _242.ProjA)) : 1000000000.0) >= (_11469 + 0.0500000007450580596923828125)));
                    _20734 = _10424 ? _10634 : _19701;
                    bvec2 _20735 = bvec2(_10424);
                    _20736 = vec2(_20735.x ? _10570.x : _19696.x, _20735.y ? _10570.y : _19696.y);
                    _19685++;
                    _19701 = _20734;
                    _19696 = _20736;
                    _19687 = _10323;
                    continue;
                }
                else
                {
                    _19842 = _19874;
                    _19806 = _19765;
                    _19770 = _19765;
                    _19733 = _19765;
                    _19712 = 0.0;
                    _19692 = false;
                    break;
                }
            }
            if (_19692)
            {
                _19841 = _19842;
                _19805 = _19806;
                _19769 = _19770;
                _19732 = _19733;
                break;
            }
            vec3 _11617 = ((_242.CameraRight.xyz * _5091.x) + (_242.CameraUp.xyz * _5091.y)) + (_242.CameraForward.xyz * _5091.z);
            float _19710;
            do
            {
                if ((((_4943.x < _242.PoolMin.x) || (_4943.x > _242.PoolMax.x)) || (_4943.z < _242.PoolMin.z)) || (_4943.z > _242.PoolMax.z))
                {
                    _19710 = 1000.0;
                    break;
                }
                float _19705;
                if (_11617.x > 9.9999997473787516355514526367188e-05)
                {
                    _19705 = min(1000.0, (_242.PoolMax.x - _4943.x) / _11617.x);
                }
                else
                {
                    float _19706;
                    if (_11617.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _19706 = min(1000.0, (_242.PoolMin.x - _4943.x) / _11617.x);
                    }
                    else
                    {
                        _19706 = 1000.0;
                    }
                    _19705 = _19706;
                }
                float _19707;
                if (_11617.z > 9.9999997473787516355514526367188e-05)
                {
                    _19707 = min(_19705, (_242.PoolMax.z - _4943.z) / _11617.z);
                }
                else
                {
                    float _19708;
                    if (_11617.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _19708 = min(_19705, (_242.PoolMin.z - _4943.z) / _11617.z);
                    }
                    else
                    {
                        _19708 = _19705;
                    }
                    _19707 = _19708;
                }
                float _19709;
                if (_11617.y < (-9.9999997473787516355514526367188e-05))
                {
                    _19709 = min(_19707, (_242.PoolMin.y - _4943.y) / _11617.y);
                }
                else
                {
                    _19709 = _19707;
                }
                _19710 = max(_19709, 0.0);
                break;
            } while(false);
            _19841 = _19696;
            _19805 = _19701;
            _19769 = min(_19710, _5099);
            _19732 = _19712;
            break;
        } while(false);
        vec3 _19894;
        vec3 _19895;
        do
        {
            vec2 _11953 = clamp(_19841 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _11964 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_11953.x, 1.0 - _11953.y), 0.0).xyz;
            if ((_19732 < 0.5) || (_19805 > 100000000.0))
            {
                _19895 = vec3(0.0, 1.0, 0.5);
                _19894 = _11964;
                break;
            }
            ivec2 _11781 = ivec2(_19841);
            float _12012 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _11789 = vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w) + (((_242.CameraRight.xyz * (((_19841.x - (0.5 * float(_242.OutputWidth))) * _19805) / _12012)) + (_242.CameraUp.xyz * (((-(_19841.y - (0.5 * float(_242.OutputHeight)))) * _19805) / _12012))) + (_242.CameraForward.xyz * _19805));
            float _11793 = _4943.y - _11789.y;
            if (_11793 <= 0.00999999977648258209228515625)
            {
                _19895 = vec3(0.0, 1.0, 0.5);
                _19894 = _11964;
                break;
            }
            ivec2 _12064 = clamp(_11781, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            ivec2 _12152 = clamp(_11781, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            vec3 _12112 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_12152.x, int((_242.OutputHeight - 1u) - uint(_12152.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _12114 = length(_12112);
            vec3 _12120 = _12112 / vec3(_12114);
            bvec3 _12121 = bvec3(_12114 > 0.001000000047497451305389404296875);
            ivec2 _12212 = clamp(_11781, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            float _11839 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_12064.x, int((_242.OutputHeight - 1u) - uint(_12064.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_12121.x ? _12120.x : vec3(0.0, 1.0, 0.0).x, _12121.y ? _12120.y : vec3(0.0, 1.0, 0.0).y, _12121.z ? _12120.z : vec3(0.0, 1.0, 0.0).z), _4683), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_12212.x, int((_242.OutputHeight - 1u) - uint(_12212.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _242.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_11964, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _11843 = max(-_20721.y, 0.20000000298023223876953125);
            vec3 _11857 = mix(vec3(1.0), exp(((-_242.FluidTint.xyz) * _11793) / vec3(_11843)), vec3(_242.LightParams.x));
            float _19893;
            if ((_242.CausticParams.x > 0.0) && (_11839 > 0.0))
            {
                vec2 _11886 = _11789.xz - ((_20721.xz / vec2(_11843)) * _11793);
                vec2 _12267 = _11886 * (6.283185482025146484375 / max(_242.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _12290 = _12267 + (vec2(sin((_12267.y * 0.730000019073486328125) + ((_242.ShadeParams.z * 0.89999997615814208984375) * _242.CausticParams.z)), sin((_12267.x * 0.61000001430511474609375) - ((_242.ShadeParams.z * 1.10000002384185791015625) * _242.CausticParams.z))) * 0.89999997615814208984375);
                float _19892;
                do
                {
                    if ((_242.GridParams.z < 2u) || (_242.GridParams.w < 2u))
                    {
                        _19892 = 1.0;
                        break;
                    }
                    vec2 _12400 = clamp(((_11886 - _242.PoolMin.xz) / max(_242.PoolMax.xz - _242.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_242.GridParams.z - 1u), float(_242.GridParams.w - 1u)), vec2(0.0), vec2(float(_242.GridParams.z - 1u), float(_242.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _12402 = uvec2(_12400);
                    vec2 _12406 = _12400 - vec2(_12402);
                    uint _12413 = _12402.x + (_12402.y * _242.GridParams.z);
                    _19892 = mix(mix(CausticField_1._data[_12413], CausticField_1._data[_12413 + 1u], _12406.x), mix(CausticField_1._data[_12413 + _242.GridParams.z], CausticField_1._data[(_12413 + _242.GridParams.z) + 1u], _12406.x), _12406.y);
                    break;
                } while(false);
                _19893 = min(max(1.0 + ((_242.CausticParams.x * (exp((-_11793) / max(_242.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _11793))) * ((pow(clamp((1.0 - abs(sin(_12290.x + ((_242.ShadeParams.z * 0.60000002384185791015625) * _242.CausticParams.z)) * sin((_12290.y * 1.12999999523162841796875) - ((_242.ShadeParams.z * 0.4000000059604644775390625) * _242.CausticParams.z)))) * (1.0 - abs(sin(((_12290.x + _12290.y) * 0.709999978542327880859375) - ((_242.ShadeParams.z * 0.5) * _242.CausticParams.z)) * sin(((_12290.x - _12290.y) * 0.829999983310699462890625) + ((_242.ShadeParams.z * 0.699999988079071044921875) * _242.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_19892, 0.0500000007450580596923828125), _242.LightParams.y), 2.5);
            }
            else
            {
                _19893 = 1.0;
            }
            _19895 = vec3(_11839, dot(_11857, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _19893);
            _19894 = _11964 * ((mix(vec3(1.0), exp(((-_242.FluidTint.xyz) * _11793) * 1.2000000476837158203125), vec3(_242.LightParams.x)) * (1.0 - _11839)) + ((_11857 * _11839) * _19893));
            break;
        } while(false);
        float _5120 = mix(_5061, max(_19769, _5061), _5066);
        vec3 _20304;
        if (_5066 < 0.5)
        {
            vec3 _12470 = ((_242.CameraRight.xyz * _5091.x) + (_242.CameraUp.xyz * _5091.y)) + (_242.CameraForward.xyz * _5091.z);
            vec3 _20305;
            if (_12470.y < (-0.0199999995529651641845703125))
            {
                float _5137 = (_242.PoolMin.w - _4943.y) / _12470.y;
                vec3 _5142 = _4943 + (_12470 * _5137);
                vec3 _20306;
                if (((_5137 > _5061) && ((((_5142.x > _242.PoolMin.x) && (_5142.x < _242.PoolMax.x)) && (_5142.z > _242.PoolMin.z)) && (_5142.z < _242.PoolMax.z))) && ((_19732 < 0.5) || (_5137 < _19769)))
                {
                    float _12526 = length(_5142 - vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w)) / ((0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _12531 = _242.RippleParams.x * _242.RippleFlow.y;
                    vec2 _12533 = _5142.xz;
                    float _12665 = max(_242.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _19900;
                    _19900 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _19899 = 0; _19899 < 6; )
                    {
                        float _12677 = _12665 * _1658[_19899];
                        uint _12765 = (((uint(_19899) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _12773 = ((_12765 >> ((_12765 >> 28u) + 4u)) ^ _12765) * 277803737u;
                        float _12696 = 0.60000002384185791015625 + (float(((_12773 >> 22u) ^ _12773) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _12701 = vec2(cos(_12696), sin(_12696));
                        uint _12792 = (((uint(_19899) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _12800 = ((_12792 >> ((_12792 >> 28u) + 4u)) ^ _12792) * 277803737u;
                        _19900 += (_12701 * (((_12531 * pow(0.7799999713897705078125, float(_19899))) * clamp((_12677 / max(_12526 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_12701, _12533) * (6.283185482025146484375 / _12677)) + ((sqrt(61.63805389404296875 / _12677) * _242.RippleParams.z) * _242.ShadeParams.z)) + (float(((_12800 >> 22u) ^ _12800) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _19899++;
                        continue;
                    }
                    vec3 _12544 = normalize(vec3(-_19900.x, 1.0, -_19900.y));
                    vec3 _12547 = _5142 - vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w);
                    float _12820 = dot(_12547, _242.CameraRight.xyz);
                    float _12825 = dot(_12547, _242.CameraUp.xyz);
                    float _12830 = dot(_12547, _242.CameraForward.xyz);
                    vec3 _12831 = vec3(_12820, _12825, _12830);
                    vec3 _12551 = reflect(_12470, _12544);
                    float _12848 = dot(_12551, _242.CameraForward.xyz);
                    vec3 _12849 = vec3(dot(_12551, _242.CameraRight.xyz), dot(_12551, _242.CameraUp.xyz), _12848);
                    float _19925;
                    vec3 _19979;
                    do
                    {
                        if (_242.ReflectionDistance <= 0.0)
                        {
                            _19979 = vec3(0.0);
                            _19925 = 0.0;
                            break;
                        }
                        float _12898 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _12848);
                        if (_12898 <= 0.0)
                        {
                            _19979 = vec3(0.0);
                            _19925 = 0.0;
                            break;
                        }
                        float _13095 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _12907 = max(0.0040000001899898052215576171875 * _12830, 0.00999999977648258209228515625);
                        float _12909 = 0.5 * _12907;
                        float _12918;
                        float _12920;
                        bool _19910;
                        bool _19915;
                        float _19920;
                        float _19926;
                        vec3 _19980;
                        int _19901 = 0;
                        float _19902 = _12909;
                        float _19903 = _12907;
                        float _19921 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_19901 < 80)
                            {
                                _12918 = _19902 + _19903;
                                _12920 = _19903 * 1.07000005245208740234375;
                                if (_12918 > _242.ReflectionDistance)
                                {
                                    _19980 = vec3(0.0);
                                    _19926 = 0.0;
                                    _19920 = _19921;
                                    _19915 = false;
                                    _19910 = true;
                                    break;
                                }
                                vec3 _12931 = _12831 + (_12849 * _12918);
                                if (_12931.z <= 0.0500000007450580596923828125)
                                {
                                    _19980 = vec3(0.0);
                                    _19926 = 0.0;
                                    _19920 = _19921;
                                    _19915 = false;
                                    _19910 = true;
                                    break;
                                }
                                float _13135 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _13111 = ((_12931.x * _13135) / _12931.z) + (0.5 * float(_242.OutputWidth));
                                float _13124 = (((-_12931.y) * _13135) / _12931.z) + (0.5 * float(_242.OutputHeight));
                                if (!((((_13111 >= 0.0) && (_13124 >= 0.0)) && (_13111 < float(_242.OutputWidth))) && (_13124 < float(_242.OutputHeight))))
                                {
                                    _19980 = vec3(0.0);
                                    _19926 = _19765;
                                    _19920 = _19921;
                                    _19915 = true;
                                    _19910 = false;
                                    break;
                                }
                                ivec2 _13219 = clamp(ivec2(vec2(_13111, _13124)), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                vec4 _13171 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13219.x, int((_242.OutputHeight - 1u) - uint(_13219.y)), 0).xy, 0);
                                float _13172 = _13171.x;
                                float _13189 = ((_13172 > 0.0) && (abs(_13172 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_13172 - _242.ProjA)) : 1000000000.0;
                                if (((_13189 < 100000000.0) && (_12931.z > _13189)) && ((_12931.z - _13189) < max(_19903 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _19905;
                                    _19905 = _12918;
                                    float _20737;
                                    float _20738;
                                    int _19904 = 0;
                                    float _19908 = _19902;
                                    SPIRV_CROSS_UNROLL
                                    for (; _19904 < 6; _19908 = _20737, _19905 = _20738, _19904++)
                                    {
                                        float _12975 = 0.5 * (_19908 + _19905);
                                        vec3 _12980 = _12831 + (_12849 * _12975);
                                        float _13289 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _13350 = clamp(ivec2(vec2(((_12980.x * _13289) / _12980.z) + (0.5 * float(_242.OutputWidth)), (((-_12980.y) * _13289) / _12980.z) + (0.5 * float(_242.OutputHeight)))), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                        vec4 _13302 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13350.x, int((_242.OutputHeight - 1u) - uint(_13350.y)), 0).xy, 0);
                                        float _13303 = _13302.x;
                                        bool _12987 = _12980.z > (((_13303 > 0.0) && (abs(_13303 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_13303 - _242.ProjA)) : 1000000000.0);
                                        _20737 = _12987 ? _19908 : _12975;
                                        _20738 = _12987 ? _12975 : _19905;
                                    }
                                    vec3 _13001 = _12831 + (_12849 * _19905);
                                    float _13420 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _13410 = vec2(((_13001.x * _13420) / _13001.z) + (0.5 * float(_242.OutputWidth)), (((-_13001.y) * _13420) / _13001.z) + (0.5 * float(_242.OutputHeight)));
                                    ivec2 _13478 = clamp(ivec2(_13410), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                    vec3 _13438 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_13478.x, int((_242.OutputHeight - 1u) - uint(_13478.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _13440 = length(_13438);
                                    vec3 _13446 = _13438 / vec3(_13440);
                                    bvec3 _13447 = bvec3(_13440 > 0.001000000047497451305389404296875);
                                    vec3 _13448 = vec3(_13447.x ? _13446.x : vec3(0.0, 1.0, 0.0).x, _13447.y ? _13446.y : vec3(0.0, 1.0, 0.0).y, _13447.z ? _13446.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_13448, _242.CameraRight.xyz), dot(_13448, _242.CameraUp.xyz), dot(_13448, _242.CameraForward.xyz)), _12849) > 0.100000001490116119384765625)
                                    {
                                        int _13028 = _19901 + 1;
                                        _19903 = _12920;
                                        _19902 = _12918;
                                        _19901 = _13028;
                                        _19921 = _12918;
                                        continue;
                                    }
                                    float _13542 = clamp(((0.0199999995529651641845703125 * _19905) * _13095) / max(_13001.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _13544 = 0.5 * _13542;
                                    vec2 _13629 = clamp(_13410 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13655 = clamp((_13410 + vec2(_13544)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13681 = clamp((_13410 + vec2(_13542 * (-0.5), _13544)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13707 = clamp((_13410 + vec2(_13544, _13542 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13733 = clamp((_13410 + vec2(_13542 * (-0.5), _13542 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13759 = clamp((_13410 + vec2(_13542, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13785 = clamp((_13410 + vec2(-_13542, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13811 = clamp((_13410 + vec2(0.0, _13542)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _13837 = clamp((_13410 + vec2(0.0, -_13542)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _13614 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13629.x, 1.0 - _13629.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13655.x, 1.0 - _13655.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13681.x, 1.0 - _13681.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13707.x, 1.0 - _13707.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13733.x, 1.0 - _13733.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13759.x, 1.0 - _13759.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13785.x, 1.0 - _13785.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13811.x, 1.0 - _13811.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_13837.x, 1.0 - _13837.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _19980 = _13614;
                                    _19926 = _12898;
                                    _19920 = _12918;
                                    _19915 = false;
                                    _19910 = true;
                                    break;
                                }
                                int _13028 = _19901 + 1;
                                _19903 = _12920;
                                _19902 = _12918;
                                _19901 = _13028;
                                _19921 = _12918;
                                continue;
                            }
                            else
                            {
                                _19980 = vec3(0.0);
                                _19926 = _19765;
                                _19920 = _19921;
                                _19915 = false;
                                _19910 = false;
                                break;
                            }
                        }
                        if (_19910)
                        {
                            _19979 = _19980;
                            _19925 = _19926;
                            break;
                        }
                        if ((!_19915) || (_19920 < 0.0))
                        {
                            _19979 = _19980;
                            _19925 = 0.0;
                            break;
                        }
                        vec3 _13043 = _12831 + (_12849 * _19920);
                        float _13888 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _13878 = vec2(((_13043.x * _13888) / _13043.z) + (0.5 * float(_242.OutputWidth)), (((-_13043.y) * _13888) / _13043.z) + (0.5 * float(_242.OutputHeight)));
                        ivec2 _13949 = clamp(ivec2(_13878), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        vec4 _13901 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13949.x, int((_242.OutputHeight - 1u) - uint(_13949.y)), 0).xy, 0);
                        float _13902 = _13901.x;
                        float _13919 = ((_13902 > 0.0) && (abs(_13902 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_13902 - _242.ProjA)) : 1000000000.0;
                        float _13995 = clamp(((0.0199999995529651641845703125 * _19920) * _13095) / max(_13043.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _13997 = 0.5 * _13995;
                        vec2 _14082 = clamp(_13878 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14108 = clamp((_13878 + vec2(_13997)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14134 = clamp((_13878 + vec2(_13995 * (-0.5), _13997)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14160 = clamp((_13878 + vec2(_13997, _13995 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14186 = clamp((_13878 + vec2(_13995 * (-0.5), _13995 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14212 = clamp((_13878 + vec2(_13995, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14238 = clamp((_13878 + vec2(-_13995, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14264 = clamp((_13878 + vec2(0.0, _13995)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _14290 = clamp((_13878 + vec2(0.0, -_13995)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _14067 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14082.x, 1.0 - _14082.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14108.x, 1.0 - _14108.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14134.x, 1.0 - _14134.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14160.x, 1.0 - _14160.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14186.x, 1.0 - _14186.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14212.x, 1.0 - _14212.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14238.x, 1.0 - _14238.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14264.x, 1.0 - _14264.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_14290.x, 1.0 - _14290.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_13919 > 100000000.0)
                        {
                            _19979 = _14067;
                            _19925 = _12898;
                            break;
                        }
                        float _13068 = _13919 - _13043.z;
                        if (_13068 <= 0.0)
                        {
                            _19979 = _14067;
                            _19925 = _12898;
                            break;
                        }
                        _19979 = _14067;
                        _19925 = _12898 * exp((_13068 / max(_12848, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _19985;
                    do
                    {
                        if (_242.EnvParams.x <= 0.0)
                        {
                            float _14344 = _12551.y;
                            _19985 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _14344))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _14344)));
                            break;
                        }
                        _19985 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_12551.x, _12551.y, -_12551.z), 0.0).xyz * _242.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _12565 = refract(_12470, _12544, 0.75187969207763671875);
                    bvec3 _20739 = bvec3(dot(_12565, _12565) < 9.9999999747524270787835121154785e-07);
                    vec3 _20740 = vec3(_20739.x ? _12470.x : _12565.x, _20739.y ? _12470.y : _12565.y, _20739.z ? _12470.z : _12565.z);
                    float _14370 = dot(_20740, _242.CameraRight.xyz);
                    float _14375 = dot(_20740, _242.CameraUp.xyz);
                    float _14380 = dot(_20740, _242.CameraForward.xyz);
                    vec3 _14381 = vec3(_14370, _14375, _14380);
                    float _14421 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _14411 = vec2(((_12820 * _14421) / _12830) + (0.5 * float(_242.OutputWidth)), (((-_12825) * _14421) / _12830) + (0.5 * float(_242.OutputHeight)));
                    float _20034;
                    float _20094;
                    float _20154;
                    vec2 _20214;
                    do
                    {
                        ivec2 _14667 = clamp(ivec2(_14411), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        vec4 _14619 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14667.x, int((_242.OutputHeight - 1u) - uint(_14667.y)), 0).xy, 0);
                        float _14620 = _14619.x;
                        vec2 _19998;
                        float _20003;
                        _20003 = ((_14620 > 0.0) && (abs(_14620 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_14620 - _242.ProjA)) : 1000000000.0;
                        _19998 = _14411;
                        float _14480;
                        float _20743;
                        vec2 _20745;
                        bool _19994;
                        float _20014;
                        float _20035;
                        float _20095;
                        float _20155;
                        vec2 _20215;
                        int _19987 = 1;
                        float _19989 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_19987 <= 16)
                            {
                                float _14473 = float(_19987);
                                _14480 = (_14473 * 0.25) * sqrt(_14473 * 0.0625);
                                vec3 _14485 = _12831 + (_14381 * _14480);
                                if (_14485.z <= 0.0500000007450580596923828125)
                                {
                                    _20215 = _19874;
                                    _20155 = _19765;
                                    _20095 = _19765;
                                    _20035 = _19765;
                                    _20014 = 0.0;
                                    _19994 = false;
                                    break;
                                }
                                float _14737 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _14713 = ((_14485.x * _14737) / _14485.z) + (0.5 * float(_242.OutputWidth));
                                float _14726 = (((-_14485.y) * _14737) / _14485.z) + (0.5 * float(_242.OutputHeight));
                                vec2 _14727 = vec2(_14713, _14726);
                                if (!((((_14713 >= 0.0) && (_14726 >= 0.0)) && (_14713 < float(_242.OutputWidth))) && (_14726 < float(_242.OutputHeight))))
                                {
                                    _20215 = _19874;
                                    _20155 = _19765;
                                    _20095 = _19765;
                                    _20035 = _19765;
                                    _20014 = 0.0;
                                    _19994 = false;
                                    break;
                                }
                                ivec2 _14821 = clamp(ivec2(_14727), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                vec4 _14773 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14821.x, int((_242.OutputHeight - 1u) - uint(_14821.y)), 0).xy, 0);
                                float _14774 = _14773.x;
                                float _14791 = ((_14774 > 0.0) && (abs(_14774 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_14774 - _242.ProjA)) : 1000000000.0;
                                if (_14485.z >= _14791)
                                {
                                    ivec2 _14507 = ivec2(_14727);
                                    ivec2 _14902 = clamp(ivec2(floor(((vec2(_14507) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _14911 = clamp(_14902, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _14937 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14911.x, int((FluidSize.y - 1u) - uint(_14911.y)), 0).xy, 0);
                                    float _14938 = _14937.x;
                                    ivec2 _14947 = clamp(_14902, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _15034 = clamp(_14507, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                    vec4 _14986 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15034.x, int((_242.OutputHeight - 1u) - uint(_15034.y)), 0).xy, 0);
                                    float _14987 = _14986.x;
                                    if (!(((_14938 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14947.x, int((FluidSize.y - 1u) - uint(_14947.y)), 0).xy, 0).w > 0.5)) && ((((_14987 > 0.0) && (abs(_14987 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_14987 - _242.ProjA)) : 1000000000.0) >= (_14938 + 0.0500000007450580596923828125))))
                                    {
                                        _20215 = _19874;
                                        _20155 = _19765;
                                        _20095 = _19765;
                                        _20035 = _19765;
                                        _20014 = 0.0;
                                        _19994 = false;
                                        break;
                                    }
                                    float _19991;
                                    _19991 = _14480;
                                    float _20741;
                                    float _20742;
                                    int _19990 = 0;
                                    float _19992 = _19989;
                                    SPIRV_CROSS_UNROLL
                                    for (; _19990 < 5; _19992 = _20741, _19991 = _20742, _19990++)
                                    {
                                        float _14522 = 0.5 * (_19992 + _19991);
                                        vec3 _14527 = _12831 + (_14381 * _14522);
                                        float _15104 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _15165 = clamp(ivec2(vec2(((_14527.x * _15104) / _14527.z) + (0.5 * float(_242.OutputWidth)), (((-_14527.y) * _15104) / _14527.z) + (0.5 * float(_242.OutputHeight)))), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                        vec4 _15117 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15165.x, int((_242.OutputHeight - 1u) - uint(_15165.y)), 0).xy, 0);
                                        float _15118 = _15117.x;
                                        bool _14534 = _14527.z >= (((_15118 > 0.0) && (abs(_15118 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_15118 - _242.ProjA)) : 1000000000.0);
                                        _20741 = _14534 ? _19992 : _14522;
                                        _20742 = _14534 ? _14522 : _19991;
                                    }
                                    vec3 _14548 = _12831 + (_14381 * _19991);
                                    float _15235 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _14559 = clamp(vec2(((_14548.x * _15235) / _14548.z) + (0.5 * float(_242.OutputWidth)), (((-_14548.y) * _15235) / _14548.z) + (0.5 * float(_242.OutputHeight))), vec2(0.5), vec2(float(_242.OutputWidth), float(_242.OutputHeight)) - vec2(0.5));
                                    ivec2 _15296 = clamp(ivec2(_14559), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                    vec4 _15248 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15296.x, int((_242.OutputHeight - 1u) - uint(_15296.y)), 0).xy, 0);
                                    float _15249 = _15248.x;
                                    _20215 = _14559;
                                    _20155 = ((_15249 > 0.0) && (abs(_15249 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_15249 - _242.ProjA)) : 1000000000.0;
                                    _20095 = _19991;
                                    _20035 = 1.0;
                                    _20014 = 1.0;
                                    _19994 = true;
                                    break;
                                }
                                ivec2 _14574 = ivec2(_14727 - vec2(0.5));
                                ivec2 _15377 = clamp(ivec2(floor(((vec2(_14574) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _15386 = clamp(_15377, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _15412 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15386.x, int((FluidSize.y - 1u) - uint(_15386.y)), 0).xy, 0);
                                float _15413 = _15412.x;
                                ivec2 _15422 = clamp(_15377, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _15509 = clamp(_14574, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                vec4 _15461 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15509.x, int((_242.OutputHeight - 1u) - uint(_15509.y)), 0).xy, 0);
                                float _15462 = _15461.x;
                                ivec2 _14579 = ivec2(_14727 + vec2(0.5));
                                ivec2 _15590 = clamp(ivec2(floor(((vec2(_14579) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _15599 = clamp(_15590, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _15625 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15599.x, int((FluidSize.y - 1u) - uint(_15599.y)), 0).xy, 0);
                                float _15626 = _15625.x;
                                ivec2 _15635 = clamp(_15590, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _15722 = clamp(_14579, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                                vec4 _15674 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15722.x, int((_242.OutputHeight - 1u) - uint(_15722.y)), 0).xy, 0);
                                float _15675 = _15674.x;
                                bool _14581 = (((_15413 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15422.x, int((FluidSize.y - 1u) - uint(_15422.y)), 0).xy, 0).w > 0.5)) && ((((_15462 > 0.0) && (abs(_15462 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_15462 - _242.ProjA)) : 1000000000.0) >= (_15413 + 0.0500000007450580596923828125))) && (((_15626 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15635.x, int((FluidSize.y - 1u) - uint(_15635.y)), 0).xy, 0).w > 0.5)) && ((((_15675 > 0.0) && (abs(_15675 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_15675 - _242.ProjA)) : 1000000000.0) >= (_15626 + 0.0500000007450580596923828125)));
                                _20743 = _14581 ? _14791 : _20003;
                                bvec2 _20744 = bvec2(_14581);
                                _20745 = vec2(_20744.x ? _14727.x : _19998.x, _20744.y ? _14727.y : _19998.y);
                                _19987++;
                                _20003 = _20743;
                                _19998 = _20745;
                                _19989 = _14480;
                                continue;
                            }
                            else
                            {
                                _20215 = _19874;
                                _20155 = _19765;
                                _20095 = _19765;
                                _20035 = _19765;
                                _20014 = 0.0;
                                _19994 = false;
                                break;
                            }
                        }
                        if (_19994)
                        {
                            _20214 = _20215;
                            _20154 = _20155;
                            _20094 = _20095;
                            _20034 = _20035;
                            break;
                        }
                        vec3 _15774 = ((_242.CameraRight.xyz * _14370) + (_242.CameraUp.xyz * _14375)) + (_242.CameraForward.xyz * _14380);
                        float _20012;
                        do
                        {
                            if ((((_5142.x < _242.PoolMin.x) || (_5142.x > _242.PoolMax.x)) || (_5142.z < _242.PoolMin.z)) || (_5142.z > _242.PoolMax.z))
                            {
                                _20012 = 1000.0;
                                break;
                            }
                            float _20007;
                            if (_15774.x > 9.9999997473787516355514526367188e-05)
                            {
                                _20007 = min(1000.0, (_242.PoolMax.x - _5142.x) / _15774.x);
                            }
                            else
                            {
                                float _20008;
                                if (_15774.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _20008 = min(1000.0, (_242.PoolMin.x - _5142.x) / _15774.x);
                                }
                                else
                                {
                                    _20008 = 1000.0;
                                }
                                _20007 = _20008;
                            }
                            float _20009;
                            if (_15774.z > 9.9999997473787516355514526367188e-05)
                            {
                                _20009 = min(_20007, (_242.PoolMax.z - _5142.z) / _15774.z);
                            }
                            else
                            {
                                float _20010;
                                if (_15774.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _20010 = min(_20007, (_242.PoolMin.z - _5142.z) / _15774.z);
                                }
                                else
                                {
                                    _20010 = _20007;
                                }
                                _20009 = _20010;
                            }
                            float _20011;
                            if (_15774.y < (-9.9999997473787516355514526367188e-05))
                            {
                                _20011 = min(_20009, (_242.PoolMin.y - _5142.y) / _15774.y);
                            }
                            else
                            {
                                _20011 = _20009;
                            }
                            _20012 = max(_20011, 0.0);
                            break;
                        } while(false);
                        _20214 = _19998;
                        _20154 = _20003;
                        _20094 = min(_20012, 4.0);
                        _20034 = _20014;
                        break;
                    } while(false);
                    vec3 _20276;
                    do
                    {
                        vec2 _16110 = clamp(_20214 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _16121 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_16110.x, 1.0 - _16110.y), 0.0).xyz;
                        if ((_20034 < 0.5) || (_20154 > 100000000.0))
                        {
                            _20276 = _16121;
                            break;
                        }
                        ivec2 _15938 = ivec2(_20214);
                        float _16169 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec3 _15946 = vec3(_242.CameraRight.w, _242.CameraUp.w, _242.CameraForward.w) + (((_242.CameraRight.xyz * (((_20214.x - (0.5 * float(_242.OutputWidth))) * _20154) / _16169)) + (_242.CameraUp.xyz * (((-(_20214.y - (0.5 * float(_242.OutputHeight)))) * _20154) / _16169))) + (_242.CameraForward.xyz * _20154));
                        float _15950 = _5142.y - _15946.y;
                        if (_15950 <= 0.00999999977648258209228515625)
                        {
                            _20276 = _16121;
                            break;
                        }
                        ivec2 _16221 = clamp(_15938, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        ivec2 _16309 = clamp(_15938, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        vec3 _16269 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_16309.x, int((_242.OutputHeight - 1u) - uint(_16309.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _16271 = length(_16269);
                        vec3 _16277 = _16269 / vec3(_16271);
                        bvec3 _16278 = bvec3(_16271 > 0.001000000047497451305389404296875);
                        ivec2 _16369 = clamp(_15938, ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        float _15996 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_16221.x, int((_242.OutputHeight - 1u) - uint(_16221.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_16278.x ? _16277.x : vec3(0.0, 1.0, 0.0).x, _16278.y ? _16277.y : vec3(0.0, 1.0, 0.0).y, _16278.z ? _16277.z : vec3(0.0, 1.0, 0.0).z), _4683), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_16369.x, int((_242.OutputHeight - 1u) - uint(_16369.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _242.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_16121, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _16000 = max(-_20721.y, 0.20000000298023223876953125);
                        float _20275;
                        if ((_242.CausticParams.x > 0.0) && (_15996 > 0.0))
                        {
                            vec2 _16043 = _15946.xz - ((_20721.xz / vec2(_16000)) * _15950);
                            vec2 _16424 = _16043 * (6.283185482025146484375 / max(_242.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _16447 = _16424 + (vec2(sin((_16424.y * 0.730000019073486328125) + ((_242.ShadeParams.z * 0.89999997615814208984375) * _242.CausticParams.z)), sin((_16424.x * 0.61000001430511474609375) - ((_242.ShadeParams.z * 1.10000002384185791015625) * _242.CausticParams.z))) * 0.89999997615814208984375);
                            float _20274;
                            do
                            {
                                if ((_242.GridParams.z < 2u) || (_242.GridParams.w < 2u))
                                {
                                    _20274 = 1.0;
                                    break;
                                }
                                vec2 _16557 = clamp(((_16043 - _242.PoolMin.xz) / max(_242.PoolMax.xz - _242.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_242.GridParams.z - 1u), float(_242.GridParams.w - 1u)), vec2(0.0), vec2(float(_242.GridParams.z - 1u), float(_242.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _16559 = uvec2(_16557);
                                vec2 _16563 = _16557 - vec2(_16559);
                                uint _16570 = _16559.x + (_16559.y * _242.GridParams.z);
                                _20274 = mix(mix(CausticField_1._data[_16570], CausticField_1._data[_16570 + 1u], _16563.x), mix(CausticField_1._data[_16570 + _242.GridParams.z], CausticField_1._data[(_16570 + _242.GridParams.z) + 1u], _16563.x), _16563.y);
                                break;
                            } while(false);
                            _20275 = min(max(1.0 + ((_242.CausticParams.x * (exp((-_15950) / max(_242.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _15950))) * ((pow(clamp((1.0 - abs(sin(_16447.x + ((_242.ShadeParams.z * 0.60000002384185791015625) * _242.CausticParams.z)) * sin((_16447.y * 1.12999999523162841796875) - ((_242.ShadeParams.z * 0.4000000059604644775390625) * _242.CausticParams.z)))) * (1.0 - abs(sin(((_16447.x + _16447.y) * 0.709999978542327880859375) - ((_242.ShadeParams.z * 0.5) * _242.CausticParams.z)) * sin(((_16447.x - _16447.y) * 0.829999983310699462890625) + ((_242.ShadeParams.z * 0.699999988079071044921875) * _242.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_20274, 0.0500000007450580596923828125), _242.LightParams.y), 2.5);
                        }
                        else
                        {
                            _20275 = 1.0;
                        }
                        _20276 = _16121 * ((mix(vec3(1.0), exp(((-_242.FluidTint.xyz) * _15950) * 1.2000000476837158203125), vec3(_242.LightParams.x)) * (1.0 - _15996)) + ((mix(vec3(1.0), exp(((-_242.FluidTint.xyz) * _15950) / vec3(_16000)), vec3(_242.LightParams.x)) * _15996) * _20275));
                        break;
                    } while(false);
                    vec3 _12596 = exp((-_242.FluidTint.xyz) * max(_20094, 0.0500000007450580596923828125));
                    float _16649 = _242.ScatterParams.z * _242.ScatterParams.z;
                    _20306 = mix(_19894, mix((_20276 * _12596) + ((_242.FluidColor.xyz * ((_5648 * _242.ScatterParams.y) + (_242.SunColor.xyz * (((_242.ScatterParams.x * 0.25) * ((1.0 - _16649) / pow(max((1.0 + _16649) - ((2.0 * _242.ScatterParams.z) * dot(_20721, -_12470)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_4683.y, 0.0, 1.0))))) * (vec3(1.0) - _12596)), mix(_19985, _19979, vec3(_19925)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_12544, -_12470), 0.0, 1.0), max(_242.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _5066), 0.0, 1.0)));
                }
                else
                {
                    _20306 = _19894;
                }
                _20305 = _20306;
            }
            else
            {
                _20305 = _19894;
            }
            _20304 = _20305;
        }
        else
        {
            _20304 = _19894;
        }
        float _5208 = clamp((clamp((float(_9580) * 0.0009765625) / _4918, 0.0, 1.0) * _242.ScatterParams.w) + (((_4658 * 0.000244140625) * _242.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _5220 = exp(max(_242.FluidTint.xyz, vec3(0.0)) * (-_5120)) * (1.0 - (0.85000002384185791015625 * _5208));
        vec3 _16678 = ((_242.SunColor.xyz * ((clamp(_4683.y, 0.0, 1.0) * _19680) * 0.3183098733425140380859375)) + _5648) * 0.85000002384185791015625;
        vec3 _16684 = mix(vec3(dot(_16678, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _16678, vec3(0.5));
        float _16732 = _242.ScatterParams.z * _242.ScatterParams.z;
        vec3 _5257 = ((_20304 * _5220) + ((vec3(1.0) - _5220) * mix(_242.FluidColor.xyz * ((_5648 * _242.ScatterParams.y) + (_242.SunColor.xyz * ((((_242.ScatterParams.x * 0.25) * ((1.0 - _16732) / pow(max((1.0 + _16732) - ((2.0 * _242.ScatterParams.z) * dot(_20721, -_9709)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_4683.y, 0.0, 1.0)) * _19680))), _16684 * 0.800000011920928955078125, vec3(_5208)))) + (_16684 * (((float(_4663) * 2.2888532839715480804443359375e-05) + (_4658 * 3.6621095205191522836685180664062e-05)) * clamp(_242.FoamParams.z, 0.0, 1.0)));
        if (_242.DebugView == 3u)
        {
            float _5264 = clamp(_5120 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_4619), vec4(_5264, _5264, _5264, 1.0));
            break;
        }
        if (_242.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_4619), vec4(_19614.www, 1.0));
            break;
        }
        if (_242.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_4619), vec4(_5257, 1.0));
            break;
        }
        if (_242.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_4619), vec4(clamp((_19641 * 0.0625) + (_4652 * 0.000244140625), 0.0, 1.0), _5208, clamp(_4937 + (_4646 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_242.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_4619), vec4(_19895, 1.0));
            break;
        }
        float _5336 = 0.014999999664723873138427734375 + (2.0 * sqrt(_5013));
        vec3 _5339 = reflect(_4835, _19682);
        vec3 _16766 = ((_242.CameraRight.xyz * _5339.x) + (_242.CameraUp.xyz * _5339.y)) + (_242.CameraForward.xyz * _5339.z);
        float _20418;
        vec3 _20498;
        do
        {
            if (_242.ReflectionDistance <= 0.0)
            {
                _20498 = vec3(0.0);
                _20418 = 0.0;
                break;
            }
            float _16815 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _5339.z);
            if (_16815 <= 0.0)
            {
                _20498 = vec3(0.0);
                _20418 = 0.0;
                break;
            }
            float _17012 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _16824 = max(0.0040000001899898052215576171875 * _19613, 0.00999999977648258209228515625);
            float _16826 = 0.5 * _16824;
            float _16835;
            float _16837;
            bool _20403;
            bool _20408;
            float _20413;
            float _20419;
            vec3 _20499;
            int _20394 = 0;
            float _20395 = _16826;
            float _20396 = _16824;
            float _20414 = -1.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_20394 < 80)
                {
                    _16835 = _20395 + _20396;
                    _16837 = _20396 * 1.07000005245208740234375;
                    if (_16835 > _242.ReflectionDistance)
                    {
                        _20499 = vec3(0.0);
                        _20419 = 0.0;
                        _20413 = _20414;
                        _20408 = false;
                        _20403 = true;
                        break;
                    }
                    vec3 _16848 = _6998 + (_5339 * _16835);
                    if (_16848.z <= 0.0500000007450580596923828125)
                    {
                        _20499 = vec3(0.0);
                        _20419 = 0.0;
                        _20413 = _20414;
                        _20408 = false;
                        _20403 = true;
                        break;
                    }
                    float _17052 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _17028 = ((_16848.x * _17052) / _16848.z) + (0.5 * float(_242.OutputWidth));
                    float _17041 = (((-_16848.y) * _17052) / _16848.z) + (0.5 * float(_242.OutputHeight));
                    if (!((((_17028 >= 0.0) && (_17041 >= 0.0)) && (_17028 < float(_242.OutputWidth))) && (_17041 < float(_242.OutputHeight))))
                    {
                        _20499 = vec3(0.0);
                        _20419 = _19765;
                        _20413 = _20414;
                        _20408 = true;
                        _20403 = false;
                        break;
                    }
                    ivec2 _17136 = clamp(ivec2(vec2(_17028, _17041)), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                    vec4 _17088 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17136.x, int((_242.OutputHeight - 1u) - uint(_17136.y)), 0).xy, 0);
                    float _17089 = _17088.x;
                    float _17106 = ((_17089 > 0.0) && (abs(_17089 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_17089 - _242.ProjA)) : 1000000000.0;
                    if (((_17106 < 100000000.0) && (_16848.z > _17106)) && ((_16848.z - _17106) < max(_20396 * 2.1400001049041748046875, 0.25)))
                    {
                        float _20398;
                        _20398 = _16835;
                        float _20748;
                        float _20749;
                        int _20397 = 0;
                        float _20401 = _20395;
                        SPIRV_CROSS_UNROLL
                        for (; _20397 < 6; _20401 = _20748, _20398 = _20749, _20397++)
                        {
                            float _16892 = 0.5 * (_20401 + _20398);
                            vec3 _16897 = _6998 + (_5339 * _16892);
                            float _17206 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _17267 = clamp(ivec2(vec2(((_16897.x * _17206) / _16897.z) + (0.5 * float(_242.OutputWidth)), (((-_16897.y) * _17206) / _16897.z) + (0.5 * float(_242.OutputHeight)))), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                            vec4 _17219 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17267.x, int((_242.OutputHeight - 1u) - uint(_17267.y)), 0).xy, 0);
                            float _17220 = _17219.x;
                            bool _16904 = _16897.z > (((_17220 > 0.0) && (abs(_17220 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_17220 - _242.ProjA)) : 1000000000.0);
                            _20748 = _16904 ? _20401 : _16892;
                            _20749 = _16904 ? _16892 : _20398;
                        }
                        vec3 _16918 = _6998 + (_5339 * _20398);
                        float _17337 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _17327 = vec2(((_16918.x * _17337) / _16918.z) + (0.5 * float(_242.OutputWidth)), (((-_16918.y) * _17337) / _16918.z) + (0.5 * float(_242.OutputHeight)));
                        ivec2 _17395 = clamp(ivec2(_17327), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
                        vec3 _17355 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_17395.x, int((_242.OutputHeight - 1u) - uint(_17395.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _17357 = length(_17355);
                        vec3 _17363 = _17355 / vec3(_17357);
                        bvec3 _17364 = bvec3(_17357 > 0.001000000047497451305389404296875);
                        vec3 _17365 = vec3(_17364.x ? _17363.x : vec3(0.0, 1.0, 0.0).x, _17364.y ? _17363.y : vec3(0.0, 1.0, 0.0).y, _17364.z ? _17363.z : vec3(0.0, 1.0, 0.0).z);
                        if (dot(vec3(dot(_17365, _242.CameraRight.xyz), dot(_17365, _242.CameraUp.xyz), dot(_17365, _242.CameraForward.xyz)), _5339) > 0.100000001490116119384765625)
                        {
                            int _16945 = _20394 + 1;
                            _20396 = _16837;
                            _20395 = _16835;
                            _20394 = _16945;
                            _20414 = _16835;
                            continue;
                        }
                        float _17459 = clamp(((_5336 * _20398) * _17012) / max(_16918.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _17461 = 0.5 * _17459;
                        vec2 _17546 = clamp(_17327 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17572 = clamp((_17327 + vec2(_17461)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17598 = clamp((_17327 + vec2(_17459 * (-0.5), _17461)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17624 = clamp((_17327 + vec2(_17461, _17459 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17650 = clamp((_17327 + vec2(_17459 * (-0.5), _17459 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17676 = clamp((_17327 + vec2(_17459, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17702 = clamp((_17327 + vec2(-_17459, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17728 = clamp((_17327 + vec2(0.0, _17459)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _17754 = clamp((_17327 + vec2(0.0, -_17459)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _17531 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17546.x, 1.0 - _17546.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17572.x, 1.0 - _17572.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17598.x, 1.0 - _17598.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17624.x, 1.0 - _17624.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17650.x, 1.0 - _17650.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17676.x, 1.0 - _17676.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17702.x, 1.0 - _17702.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17728.x, 1.0 - _17728.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17754.x, 1.0 - _17754.y), 0.0).xyz) * 0.100000001490116119384765625;
                        _20499 = _17531;
                        _20419 = _16815;
                        _20413 = _16835;
                        _20408 = false;
                        _20403 = true;
                        break;
                    }
                    int _16945 = _20394 + 1;
                    _20396 = _16837;
                    _20395 = _16835;
                    _20394 = _16945;
                    _20414 = _16835;
                    continue;
                }
                else
                {
                    _20499 = vec3(0.0);
                    _20419 = _19765;
                    _20413 = _20414;
                    _20408 = false;
                    _20403 = false;
                    break;
                }
            }
            if (_20403)
            {
                _20498 = _20499;
                _20418 = _20419;
                break;
            }
            if ((!_20408) || (_20413 < 0.0))
            {
                _20498 = _20499;
                _20418 = 0.0;
                break;
            }
            vec3 _16960 = _6998 + (_5339 * _20413);
            float _17805 = (0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec2 _17795 = vec2(((_16960.x * _17805) / _16960.z) + (0.5 * float(_242.OutputWidth)), (((-_16960.y) * _17805) / _16960.z) + (0.5 * float(_242.OutputHeight)));
            ivec2 _17866 = clamp(ivec2(_17795), ivec2(0), ivec2(int(_242.OutputWidth) - 1, int(_242.OutputHeight) - 1));
            vec4 _17818 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17866.x, int((_242.OutputHeight - 1u) - uint(_17866.y)), 0).xy, 0);
            float _17819 = _17818.x;
            float _17836 = ((_17819 > 0.0) && (abs(_17819 - _242.ProjA) > 9.999999717180685365747194737196e-10)) ? (_242.ProjB / (_17819 - _242.ProjA)) : 1000000000.0;
            float _17912 = clamp(((_5336 * _20413) * _17012) / max(_16960.z, 0.0500000007450580596923828125), 0.0, 12.0);
            float _17914 = 0.5 * _17912;
            vec2 _17999 = clamp(_17795 / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18025 = clamp((_17795 + vec2(_17914)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18051 = clamp((_17795 + vec2(_17912 * (-0.5), _17914)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18077 = clamp((_17795 + vec2(_17914, _17912 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18103 = clamp((_17795 + vec2(_17912 * (-0.5), _17912 * (-0.5))) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18129 = clamp((_17795 + vec2(_17912, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18155 = clamp((_17795 + vec2(-_17912, 0.0)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18181 = clamp((_17795 + vec2(0.0, _17912)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec2 _18207 = clamp((_17795 + vec2(0.0, -_17912)) / vec2(float(_242.OutputWidth), float(_242.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _17984 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17999.x, 1.0 - _17999.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18025.x, 1.0 - _18025.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18051.x, 1.0 - _18051.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18077.x, 1.0 - _18077.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18103.x, 1.0 - _18103.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18129.x, 1.0 - _18129.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18155.x, 1.0 - _18155.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18181.x, 1.0 - _18181.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_18207.x, 1.0 - _18207.y), 0.0).xyz) * 0.100000001490116119384765625;
            if (_17836 > 100000000.0)
            {
                _20498 = _17984;
                _20418 = _16815;
                break;
            }
            float _16985 = _17836 - _16960.z;
            if (_16985 <= 0.0)
            {
                _20498 = _17984;
                _20418 = _16815;
                break;
            }
            _20498 = _17984;
            _20418 = _16815 * exp((_16985 / max(_5339.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
            break;
        } while(false);
        vec3 _20504;
        do
        {
            if (_242.EnvParams.x <= 0.0)
            {
                float _18261 = _16766.y;
                _20504 = mix(_242.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_242.SkyHorizon.xyz, _242.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _18261))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _18261)));
                break;
            }
            _20504 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_16766.x, _16766.y, -_16766.z), clamp(clamp(log2(max(_5336 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _242.EnvParams.x;
            break;
        } while(false);
        vec3 _5359 = mix(_20504, _20498, vec3(_20418));
        if (_242.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_4619), vec4(_5359, 1.0));
            break;
        }
        float _5387 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_19643 + _19682), _4837), 0.0, 1.0), max(_242.FresnelPower, 1.0));
        float _5388 = 0.0199999995529651641845703125 + _5387;
        vec3 _5398 = normalize(_242.SunDirection.xyz);
        float _5415 = max((2.0 / ((2.0 / (max(_242.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _5013))) - 2.0, 4.0);
        vec3 _5484 = (mix(_5257, _5359, vec3(clamp(_5388, 0.0, 1.0))) + (((((_242.SunColor.xyz * ((pow(clamp(dot(_19682, normalize(_5398 - _4835)), 0.0, 1.0), _5415) * (_5415 + 8.0)) * 0.0397887341678142547607421875)) * _242.Specular) * _5388) * _19680) * clamp(dot(_19682, _5398) * 4.0, 0.0, 1.0))) + ((_242.SunColor.xyz * _242.FluidColor.xyz) * (((((_242.LightParams.w * 0.60000002384185791015625) * (((1.0 - _5066) * exp(_5061 * (-1.5))) * (1.0 - exp(_5061 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_4683, _9709)), 9.9999997473787516355514526367188e-05), 1.5))) * _19680) * (0.980000019073486328125 - _5387)));
        vec3 _5502 = mix(_5484, (_5484 * 1.25) + (_5648 * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _5056)) * _5066) * _242.LightParams.z, 0.0, 1.0)));
        float _5511 = clamp(1.0 - exp(-((_19641 * 0.07500000298023223876953125) + (_4652 * 0.0003662109375))), 0.0, 1.0);
        vec3 _20580;
        if ((_5511 > 0.0199999995529651641845703125) && (_242.FoamParams.x > 0.0))
        {
            vec2 _18322 = _4943.xz / vec2(max(_242.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _18357 = floor(_18322);
            vec2 _18360 = _18322 - _18357;
            float _20573;
            float _20574;
            _20574 = 8.0;
            _20573 = 8.0;
            float _20589;
            float _20590;
            SPIRV_CROSS_UNROLL
            for (int _20572 = -1; _20572 <= 1; _20574 = _20589, _20573 = _20590, _20572++)
            {
                _20590 = _20573;
                _20589 = _20574;
                float _20751;
                float _20713;
                SPIRV_CROSS_UNROLL
                for (int _20587 = -1; _20587 <= 1; _20590 = _20713, _20589 = _20751, _20587++)
                {
                    vec2 _18375 = vec2(float(_20587), float(_20572));
                    ivec2 _18379 = ivec2(_18357 + _18375);
                    uint _18451 = ((((uint(_18379.x + 4096) * 1973u) + (uint(_18379.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _18459 = ((_18451 >> ((_18451 >> 28u) + 4u)) ^ _18451) * 277803737u;
                    uint _18463 = (_18459 >> 22u) ^ _18459;
                    float _18417 = length((_18375 + (vec2(0.5) + (sin(vec2(_242.ShadeParams.z * 0.5) + ((vec2(float(_18463 & 65535u), float(_18463 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _18360);
                    bool _18420 = _18417 < _20589;
                    if (_18420)
                    {
                        _20713 = _20589;
                    }
                    else
                    {
                        _20713 = (_18417 < _20590) ? _18417 : _20590;
                    }
                    _20751 = _18420 ? _18417 : _20589;
                }
            }
            vec2 _18329 = (_18322 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _18480 = floor(_18329);
            vec2 _18483 = _18329 - _18480;
            float _20576;
            float _20577;
            _20577 = 8.0;
            _20576 = 8.0;
            float _20583;
            float _20584;
            SPIRV_CROSS_UNROLL
            for (int _20575 = -1; _20575 <= 1; _20577 = _20583, _20576 = _20584, _20575++)
            {
                _20584 = _20576;
                _20583 = _20577;
                float _20753;
                float _20707;
                SPIRV_CROSS_UNROLL
                for (int _20581 = -1; _20581 <= 1; _20584 = _20707, _20583 = _20753, _20581++)
                {
                    vec2 _18498 = vec2(float(_20581), float(_20575));
                    ivec2 _18502 = ivec2(_18480 + _18498);
                    uint _18574 = ((((uint(_18502.x + 4096) * 1973u) + (uint(_18502.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _18582 = ((_18574 >> ((_18574 >> 28u) + 4u)) ^ _18574) * 277803737u;
                    uint _18586 = (_18582 >> 22u) ^ _18582;
                    float _18540 = length((_18498 + (vec2(0.5) + (sin(vec2(_242.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_18586 & 65535u), float(_18586 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _18483);
                    bool _18543 = _18540 < _20583;
                    if (_18543)
                    {
                        _20707 = _20583;
                    }
                    else
                    {
                        _20707 = (_18540 < _20584) ? _18540 : _20584;
                    }
                    _20753 = _18543 ? _18540 : _20583;
                }
            }
            float _18340 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _20573 - _20574), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _20576 - _20577))), clamp((((_242.FoamParams.y * ((0.5 * float(_242.OutputHeight)) * max(_242.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_19613, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _20580 = mix(_5502, _16684 * mix(0.85000002384185791015625, 1.0, _18340), vec3(clamp(_5511 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_18340 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_242.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _20580 = _5502;
        }
        float _5557 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _4937) * clamp(_242.Foam * 1.60000002384185791015625, 0.0, 1.0);
        imageStore(OutputTexture, ivec2(_4619), vec4(mix(mix(_20580, _16684, vec3(_5557 * 0.89999997615814208984375)), _16684, vec3(_4706)), max(clamp(_19614.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _5557)), _4706)));
        break;
    } while(false);
}

