#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=bab6bec8fe37458fbc9dbcb75b8e54ec597c2f09129528ac206557966b6ccb94; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _1935[6] = float[](1.0, 0.612999975681304931640625, 0.3770000040531158447265625, 0.2389999926090240478515625, 0.14900000393390655517578125, 0.093000002205371856689453125);
float _27234;
bool _27453;
uint _27566;
vec3 _27745;
float _27802;
vec2 _27971;
float _30073;

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
} _289;

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
layout(binding = 7) uniform samplerCube SPIRV_Cross_CombinedEnvironmentMapLinearSampler;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler;
layout(binding = 7) uniform samplerCube SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler;
layout(binding = 6) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;
layout(binding = 3) uniform sampler2D SPIRV_Cross_CombinedSceneColorLinearSampler;
layout(binding = 9) uniform sampler2D SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler;
layout(binding = 15) uniform sampler2D SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler;
layout(binding = 10) uniform sampler2D SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler;
layout(binding = 8) uniform sampler2D SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler;

uvec2 FluidSize;
float EnvMaxMip;
shared vec3 g_skyIrradiance;

void main()
{
    do
    {
        bool _5630 = (gl_GlobalInvocationID.x < _289.OutputWidth) && (gl_GlobalInvocationID.y < _289.OutputHeight);
        if (((_289.Mode != 0u) && _5630) && (((gl_GlobalInvocationID.x & 7u) == 0u) ? ((gl_GlobalInvocationID.y & 7u) == 0u) : (((gl_GlobalInvocationID.x + 1u) & 7u) == 0u)))
        {
            float _6738 = max(EnvMaxMip - 1.0, 0.0);
            vec3 _27235;
            do
            {
                if (_289.EnvParams.x <= 0.0)
                {
                    _27235 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 1.0))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 1.0)));
                    break;
                }
                _27235 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 1.0, -0.0), clamp(_6738, 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
                break;
            } while(false);
            vec3 _27236;
            do
            {
                if (_289.EnvParams.x <= 0.0)
                {
                    _27236 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                    break;
                }
                _27236 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6738, 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
                break;
            } while(false);
            vec3 _27238;
            do
            {
                if (_289.EnvParams.x <= 0.0)
                {
                    _27238 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                    break;
                }
                _27238 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(0.707106769084930419921875, 0.707106769084930419921875, -0.0), clamp(_6738, 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
                break;
            } while(false);
            vec3 _27240;
            do
            {
                if (_289.EnvParams.x <= 0.0)
                {
                    _27240 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                    break;
                }
                _27240 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, -0.707106769084930419921875), clamp(_6738, 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
                break;
            } while(false);
            vec3 _27242;
            do
            {
                if (_289.EnvParams.x <= 0.0)
                {
                    _27242 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, 0.707106769084930419921875))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, 0.707106769084930419921875)));
                    break;
                }
                _27242 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-0.0, 0.707106769084930419921875, 0.707106769084930419921875), clamp(_6738, 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
                break;
            } while(false);
            g_skyIrradiance = (((((_27235 * 2.0) + _27236) + _27238) + _27240) + _27242) * vec3(0.16666667163372039794921875);
        }
        barrier();
        if (!_5630)
        {
            break;
        }
        if (_289.Mode == 0u)
        {
            break;
        }
        uvec2 _5663 = uvec2(textureSize(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, 0));
        FluidSize.x = _5663.x;
        FluidSize.y = _5663.y;
        EnvMaxMip = float(max(uint(textureQueryLevels(SPIRV_Cross_CombinedEnvironmentMapSPIRV_Cross_DummySampler)), 1u) - 1u);
        uvec2 _5690 = uvec2(gl_GlobalInvocationID.x, (_289.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _5693 = ivec2(gl_GlobalInvocationID.xy);
        vec2 _5698 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        uint _5712 = (gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _289.OutputWidth)) * 4u;
        uint _5716 = WhitewaterBits_1._data[_5712];
        float _5717 = float(_5716);
        uint _5722 = WhitewaterBits_1._data[_5712 + 1u];
        float _5723 = float(_5722);
        uint _5728 = WhitewaterBits_1._data[_5712 + 2u];
        float _5729 = float(_5728);
        uint _5734 = WhitewaterBits_1._data[_5712 + 3u];
        WhitewaterBits_1._data[_5712] = 0u;
        WhitewaterBits_1._data[_5712 + 1u] = 0u;
        WhitewaterBits_1._data[_5712 + 2u] = 0u;
        WhitewaterBits_1._data[_5712 + 3u] = 0u;
        vec3 _5752 = normalize(_289.SunDirection.xyz);
        vec3 _5754 = normalize(((_289.CameraRight.xyz * _5752.x) + (_289.CameraUp.xyz * _5752.y)) + (_289.CameraForward.xyz * _5752.z));
        vec3 _5757 = refract(-_5754, vec3(0.0, 1.0, 0.0), 0.75187969207763671875);
        bvec3 _30011 = bvec3((dot(_5757, _5757) < 9.9999999747524270787835121154785e-07) || (_5754.y <= 0.0));
        vec3 _30012 = vec3(_30011.x ? vec3(0.0, -1.0, 0.0).x : _5757.x, _30011.y ? vec3(0.0, -1.0, 0.0).y : _5757.y, _30011.z ? vec3(0.0, -1.0, 0.0).z : _5757.z);
        float _5776 = clamp(1.0 - exp(_5717 * (-0.0006103515625)), 0.0, 1.0) * clamp(_289.FoamParams.w, 0.0, 1.0);
        float _27246;
        vec4 _27247;
        do
        {
            vec2 _7135 = ((_5698 * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight))) - vec2(0.5);
            ivec2 _7138 = ivec2(floor(_7135));
            vec2 _7142 = _7135 - vec2(_7138);
            ivec2 _7317 = clamp(_7138, ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7353 = clamp(_7138 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7389 = clamp(_7138 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            ivec2 _7425 = clamp(_7138 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7178 = vec4((1.0 - _7142.x) * (1.0 - _7142.y), _7142.x * (1.0 - _7142.y), (1.0 - _7142.x) * _7142.y, _7142.x * _7142.y);
            vec4 _7105 = _7178;
            vec4 _7183 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7317.x, int((FluidSize.y - 1u) - uint(_7317.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7353.x, int((FluidSize.y - 1u) - uint(_7353.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7389.x, int((FluidSize.y - 1u) - uint(_7389.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_7425.x, int((FluidSize.y - 1u) - uint(_7425.y)), 0).xy, 0).x);
            vec4 _7106 = _7183;
            float _29740;
            _29740 = 1000000000.0;
            float _30010;
            SPIRV_CROSS_UNROLL
            for (int _27244 = 0; _27244 < 4; _29740 = _30010, _27244++)
            {
                if (_7106[_27244] > 0.0)
                {
                    _30010 = min(_29740, _7106[_27244]);
                }
                else
                {
                    _30010 = _29740;
                }
            }
            vec4 _7109 = vec4(0.0);
            SPIRV_CROSS_UNROLL
            for (int _27245 = 0; _27245 < 4; )
            {
                _7109[_27245] = ((_7106[_27245] > 0.0) && (_7106[_27245] < (_29740 + max(0.25, 0.02999999932944774627685546875 * _29740)))) ? _7105[_27245] : 0.0;
                _27245++;
                continue;
            }
            float _7243 = ((_7109.x + _7109.y) + _7109.z) + _7109.w;
            if (_7243 <= 9.9999997473787516355514526367188e-05)
            {
                _27247 = vec4(0.0);
                _27246 = 0.0;
                break;
            }
            ivec2 _7461 = clamp(_7138, ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7487 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7461.x, int((FluidSize.y - 1u) - uint(_7461.y)), 0).xy, 0);
            ivec2 _7496 = clamp(_7138 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7522 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7496.x, int((FluidSize.y - 1u) - uint(_7496.y)), 0).xy, 0);
            ivec2 _7531 = clamp(_7138 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7557 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7531.x, int((FluidSize.y - 1u) - uint(_7531.y)), 0).xy, 0);
            ivec2 _7566 = clamp(_7138 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
            vec4 _7592 = texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_7566.x, int((FluidSize.y - 1u) - uint(_7566.y)), 0).xy, 0);
            _27247 = vec4((((((_7487.xyz * _7109.x) + (_7522.xyz * _7109.y)) + (_7557.xyz * _7109.z)) + (_7592.xyz * _7109.w)) / vec3(_7243)).x, _30073, _30073, dot(_7178, vec4(_7487.w, _7522.w, _7557.w, _7592.w)));
            _27246 = dot(_7109, _7183) / _7243;
            break;
        } while(false);
        ivec2 _7654 = clamp(_5693, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
        vec4 _7605 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_7654.x, int((_289.OutputHeight - 1u) - uint(_7654.y)), 0).xy, 0);
        float _7606 = _7605.x;
        float _7623 = ((_7606 > 0.0) && (abs(_7606 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_7606 - _289.ProjA)) : 1000000000.0;
        if (!(((_27246 > 0.0) && (_27247.w > 0.001000000047497451305389404296875)) && (_27246 < (_7623 - 0.00200000009499490261077880859375))))
        {
            vec2 _7699 = clamp(_5698 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _7710 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_7699.x, 1.0 - _7699.y), 0.0).xyz;
            float _29725;
            vec3 _29736;
            if ((_7623 < 100000000.0) && (_289.WetParams.w > 0.0))
            {
                float _7758 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec3 _5811 = vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w) + (((_289.CameraRight.xyz * (((_5698.x - (0.5 * float(_289.OutputWidth))) * _7623) / _7758)) + (_289.CameraUp.xyz * (((-(_5698.y - (0.5 * float(_289.OutputHeight)))) * _7623) / _7758))) + (_289.CameraForward.xyz * _7623));
                ivec2 _7838 = clamp(_5693, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                vec3 _7798 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_7838.x, int((_289.OutputHeight - 1u) - uint(_7838.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                float _7800 = length(_7798);
                vec3 _7806 = _7798 / vec3(_7800);
                bvec3 _7807 = bvec3(_7800 > 0.001000000047497451305389404296875);
                float _29726;
                vec3 _29737;
                if ((vec3(_7807.x ? _7806.x : vec3(0.0, 1.0, 0.0).x, _7807.y ? _7806.y : vec3(0.0, 1.0, 0.0).y, _7807.z ? _7806.z : vec3(0.0, 1.0, 0.0).z).y > 0.60000002384185791015625) && (_5811.y > (_289.PoolMin.y + 0.20000000298023223876953125)))
                {
                    float _29666;
                    do
                    {
                        int _7891 = int(_289.GridParams.x);
                        int _7894 = int(_289.GridParams.y);
                        if (((_7891 < 4) || (_7894 < 4)) || (_289.WetParams.z <= 0.0))
                        {
                            _29666 = 0.0;
                            break;
                        }
                        vec2 _7916 = ((_5811.xz - _289.WetParams.xy) / vec2(_289.WetParams.z)) - vec2(0.5);
                        if ((any(lessThan(_7916, vec2(-1.0))) || (_7916.x > float(_7891))) || (_7916.y > float(_7894)))
                        {
                            _29666 = 0.0;
                            break;
                        }
                        ivec2 _7936 = ivec2(floor(_7916));
                        vec2 _7940 = _7916 - vec2(_7936);
                        float _7942 = _7940.x;
                        float _8023 = _7942 * _7942;
                        float _8026 = _8023 * _7942;
                        vec4 _7876 = vec4(((1.0 - (3.0 * _7942)) + (3.0 * _8023)) - _8026, (4.0 - (6.0 * _8023)) + (3.0 * _8026), ((1.0 + (3.0 * _7942)) + (3.0 * _8023)) - (3.0 * _8026), _8026) * 0.16666667163372039794921875;
                        float _7945 = _7940.y;
                        float _8059 = _7945 * _7945;
                        float _8062 = _8059 * _7945;
                        vec4 _7878 = vec4(((1.0 - (3.0 * _7945)) + (3.0 * _8059)) - _8062, (4.0 - (6.0 * _8059)) + (3.0 * _8062), ((1.0 + (3.0 * _7945)) + (3.0 * _8059)) - (3.0 * _8062), _8062) * 0.16666667163372039794921875;
                        float _29659;
                        _29659 = 0.0;
                        float _29996;
                        SPIRV_CROSS_UNROLL
                        for (int _29658 = 0; _29658 < 4; _29659 = _29996, _29658++)
                        {
                            int _7956 = (_7936.y - 1) + _29658;
                            if ((_7956 < 0) || (_7956 >= _7894))
                            {
                                _29996 = _29659;
                                continue;
                            }
                            float _29662;
                            _29662 = 0.0;
                            float _29998;
                            SPIRV_CROSS_UNROLL
                            for (int _29660 = 0; _29660 < 4; _29662 = _29998, _29660++)
                            {
                                int _7974 = (_7936.x - 1) + _29660;
                                if ((_7974 >= 0) && (_7974 < _7891))
                                {
                                    _29998 = _29662 + (_7876[_29660] * float(Wetness_1._data[uint(_7974 + (_7956 * _7891))]));
                                }
                                else
                                {
                                    _29998 = _29662;
                                }
                            }
                            _29996 = _29659 + (_7878[_29658] * _29662);
                        }
                        _29666 = _29659 * 1.525902189314365386962890625e-05;
                        break;
                    } while(false);
                    float _5833 = clamp(((_29666 * _289.WetParams.w) - 0.00999999977648258209228515625) * 1.01010096073150634765625, 0.0, 1.0);
                    vec3 _29738;
                    if (_5833 > 0.001000000047497451305389404296875)
                    {
                        vec3 _5840 = normalize(_5811 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w));
                        vec3 _29724;
                        do
                        {
                            float _8125 = smoothstep(0.449999988079071044921875, 1.0, _5833);
                            vec3 _8129 = _7710 * mix(1.0, 0.5, clamp(_5833 * 2.0, 0.0, 1.0));
                            if (_8125 <= 0.0)
                            {
                                _29724 = _8129;
                                break;
                            }
                            vec3 _8147 = reflect(_5840, vec3(0.0, 1.0, 0.0));
                            float _8206 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec3 _8196 = vec3(((_5698.x - (0.5 * float(_289.OutputWidth))) * _7623) / _8206, ((-(_5698.y - (0.5 * float(_289.OutputHeight)))) * _7623) / _8206, _7623);
                            float _8223 = dot(_8147, _289.CameraForward.xyz);
                            vec3 _8224 = vec3(dot(_8147, _289.CameraRight.xyz), dot(_8147, _289.CameraUp.xyz), _8223);
                            float _29696;
                            vec3 _29717;
                            do
                            {
                                if (_289.ReflectionDistance <= 0.0)
                                {
                                    _29717 = vec3(0.0);
                                    _29696 = 0.0;
                                    break;
                                }
                                float _8273 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _8223);
                                if (_8273 <= 0.0)
                                {
                                    _29717 = vec3(0.0);
                                    _29696 = 0.0;
                                    break;
                                }
                                float _8470 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _8282 = max(0.0040000001899898052215576171875 * _7623, 0.00999999977648258209228515625);
                                float _8284 = 0.5 * _8282;
                                float _8293;
                                float _8295;
                                bool _29681;
                                bool _29686;
                                float _29691;
                                float _29697;
                                vec3 _29718;
                                int _29672 = 0;
                                float _29673 = _8284;
                                float _29674 = _8282;
                                float _29692 = -1.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_29672 < 80)
                                    {
                                        _8293 = _29673 + _29674;
                                        _8295 = _29674 * 1.07000005245208740234375;
                                        if (_8293 > _289.ReflectionDistance)
                                        {
                                            _29718 = vec3(0.0);
                                            _29697 = 0.0;
                                            _29691 = _29692;
                                            _29686 = false;
                                            _29681 = true;
                                            break;
                                        }
                                        vec3 _8306 = _8196 + (_8224 * _8293);
                                        if (_8306.z <= 0.0500000007450580596923828125)
                                        {
                                            _29718 = vec3(0.0);
                                            _29697 = 0.0;
                                            _29691 = _29692;
                                            _29686 = false;
                                            _29681 = true;
                                            break;
                                        }
                                        float _8510 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        float _8486 = ((_8306.x * _8510) / _8306.z) + (0.5 * float(_289.OutputWidth));
                                        float _8499 = (((-_8306.y) * _8510) / _8306.z) + (0.5 * float(_289.OutputHeight));
                                        if (!((((_8486 >= 0.0) && (_8499 >= 0.0)) && (_8486 < float(_289.OutputWidth))) && (_8499 < float(_289.OutputHeight))))
                                        {
                                            _29718 = vec3(0.0);
                                            _29697 = _27802;
                                            _29691 = _29692;
                                            _29686 = true;
                                            _29681 = false;
                                            break;
                                        }
                                        ivec2 _8594 = clamp(ivec2(vec2(_8486, _8499)), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                        vec4 _8546 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8594.x, int((_289.OutputHeight - 1u) - uint(_8594.y)), 0).xy, 0);
                                        float _8547 = _8546.x;
                                        float _8564 = ((_8547 > 0.0) && (abs(_8547 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_8547 - _289.ProjA)) : 1000000000.0;
                                        if (((_8564 < 100000000.0) && (_8306.z > _8564)) && ((_8306.z - _8564) < max(_29674 * 2.1400001049041748046875, 0.25)))
                                        {
                                            float _29676;
                                            _29676 = _8293;
                                            float _30013;
                                            float _30014;
                                            int _29675 = 0;
                                            float _29679 = _29673;
                                            SPIRV_CROSS_UNROLL
                                            for (; _29675 < 6; _29679 = _30013, _29676 = _30014, _29675++)
                                            {
                                                float _8350 = 0.5 * (_29679 + _29676);
                                                vec3 _8355 = _8196 + (_8224 * _8350);
                                                float _8664 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                ivec2 _8725 = clamp(ivec2(vec2(((_8355.x * _8664) / _8355.z) + (0.5 * float(_289.OutputWidth)), (((-_8355.y) * _8664) / _8355.z) + (0.5 * float(_289.OutputHeight)))), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                                vec4 _8677 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_8725.x, int((_289.OutputHeight - 1u) - uint(_8725.y)), 0).xy, 0);
                                                float _8678 = _8677.x;
                                                bool _8362 = _8355.z > (((_8678 > 0.0) && (abs(_8678 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_8678 - _289.ProjA)) : 1000000000.0);
                                                _30013 = _8362 ? _29679 : _8350;
                                                _30014 = _8362 ? _8350 : _29676;
                                            }
                                            vec3 _8376 = _8196 + (_8224 * _29676);
                                            float _8795 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            vec2 _8785 = vec2(((_8376.x * _8795) / _8376.z) + (0.5 * float(_289.OutputWidth)), (((-_8376.y) * _8795) / _8376.z) + (0.5 * float(_289.OutputHeight)));
                                            ivec2 _8853 = clamp(ivec2(_8785), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                            vec3 _8813 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_8853.x, int((_289.OutputHeight - 1u) - uint(_8853.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                            float _8815 = length(_8813);
                                            vec3 _8821 = _8813 / vec3(_8815);
                                            bvec3 _8822 = bvec3(_8815 > 0.001000000047497451305389404296875);
                                            vec3 _8823 = vec3(_8822.x ? _8821.x : vec3(0.0, 1.0, 0.0).x, _8822.y ? _8821.y : vec3(0.0, 1.0, 0.0).y, _8822.z ? _8821.z : vec3(0.0, 1.0, 0.0).z);
                                            if (dot(vec3(dot(_8823, _289.CameraRight.xyz), dot(_8823, _289.CameraUp.xyz), dot(_8823, _289.CameraForward.xyz)), _8224) > 0.100000001490116119384765625)
                                            {
                                                int _8403 = _29672 + 1;
                                                _29674 = _8295;
                                                _29673 = _8293;
                                                _29672 = _8403;
                                                _29692 = _8293;
                                                continue;
                                            }
                                            float _8917 = clamp(((0.039999999105930328369140625 * _29676) * _8470) / max(_8376.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                            float _8919 = 0.5 * _8917;
                                            vec2 _9004 = clamp(_8785 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9030 = clamp((_8785 + vec2(_8919)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9056 = clamp((_8785 + vec2(_8917 * (-0.5), _8919)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9082 = clamp((_8785 + vec2(_8919, _8917 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9108 = clamp((_8785 + vec2(_8917 * (-0.5), _8917 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9134 = clamp((_8785 + vec2(_8917, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9160 = clamp((_8785 + vec2(-_8917, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9186 = clamp((_8785 + vec2(0.0, _8917)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec2 _9212 = clamp((_8785 + vec2(0.0, -_8917)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                            vec3 _8989 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9004.x, 1.0 - _9004.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9030.x, 1.0 - _9030.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9056.x, 1.0 - _9056.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9082.x, 1.0 - _9082.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9108.x, 1.0 - _9108.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9134.x, 1.0 - _9134.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9160.x, 1.0 - _9160.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9186.x, 1.0 - _9186.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9212.x, 1.0 - _9212.y), 0.0).xyz) * 0.100000001490116119384765625;
                                            _29718 = _8989;
                                            _29697 = _8273;
                                            _29691 = _8293;
                                            _29686 = false;
                                            _29681 = true;
                                            break;
                                        }
                                        int _8403 = _29672 + 1;
                                        _29674 = _8295;
                                        _29673 = _8293;
                                        _29672 = _8403;
                                        _29692 = _8293;
                                        continue;
                                    }
                                    else
                                    {
                                        _29718 = vec3(0.0);
                                        _29697 = _27802;
                                        _29691 = _29692;
                                        _29686 = false;
                                        _29681 = false;
                                        break;
                                    }
                                }
                                if (_29681)
                                {
                                    _29717 = _29718;
                                    _29696 = _29697;
                                    break;
                                }
                                if ((!_29686) || (_29691 < 0.0))
                                {
                                    _29717 = _29718;
                                    _29696 = 0.0;
                                    break;
                                }
                                vec3 _8418 = _8196 + (_8224 * _29691);
                                float _9263 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec2 _9253 = vec2(((_8418.x * _9263) / _8418.z) + (0.5 * float(_289.OutputWidth)), (((-_8418.y) * _9263) / _8418.z) + (0.5 * float(_289.OutputHeight)));
                                ivec2 _9324 = clamp(ivec2(_9253), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                vec4 _9276 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_9324.x, int((_289.OutputHeight - 1u) - uint(_9324.y)), 0).xy, 0);
                                float _9277 = _9276.x;
                                float _9294 = ((_9277 > 0.0) && (abs(_9277 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_9277 - _289.ProjA)) : 1000000000.0;
                                float _9370 = clamp(((0.039999999105930328369140625 * _29691) * _8470) / max(_8418.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                float _9372 = 0.5 * _9370;
                                vec2 _9457 = clamp(_9253 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9483 = clamp((_9253 + vec2(_9372)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9509 = clamp((_9253 + vec2(_9370 * (-0.5), _9372)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9535 = clamp((_9253 + vec2(_9372, _9370 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9561 = clamp((_9253 + vec2(_9370 * (-0.5), _9370 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9587 = clamp((_9253 + vec2(_9370, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9613 = clamp((_9253 + vec2(-_9370, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9639 = clamp((_9253 + vec2(0.0, _9370)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec2 _9665 = clamp((_9253 + vec2(0.0, -_9370)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                vec3 _9442 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9457.x, 1.0 - _9457.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9483.x, 1.0 - _9483.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9509.x, 1.0 - _9509.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9535.x, 1.0 - _9535.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9561.x, 1.0 - _9561.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9587.x, 1.0 - _9587.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9613.x, 1.0 - _9613.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9639.x, 1.0 - _9639.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_9665.x, 1.0 - _9665.y), 0.0).xyz) * 0.100000001490116119384765625;
                                if (_9294 > 100000000.0)
                                {
                                    _29717 = _9442;
                                    _29696 = _8273;
                                    break;
                                }
                                float _8443 = _9294 - _8418.z;
                                if (_8443 <= 0.0)
                                {
                                    _29717 = _9442;
                                    _29696 = _8273;
                                    break;
                                }
                                _29717 = _9442;
                                _29696 = _8273 * exp((_8443 / max(_8223, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                                break;
                            } while(false);
                            vec3 _29723;
                            do
                            {
                                if (_289.EnvParams.x <= 0.0)
                                {
                                    float _9719 = _8147.y;
                                    _29723 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _9719))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _9719)));
                                    break;
                                }
                                _29723 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_8147.x, _8147.y, -_8147.z), clamp(1.0, 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
                                break;
                            } while(false);
                            _29724 = mix(_8129, mix(_29723, _29717, vec3(_29696)), vec3((0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp((-_5840).y, 0.0, 1.0), 5.0))) * _8125));
                            break;
                        } while(false);
                        _29738 = _29724;
                    }
                    else
                    {
                        _29738 = _7710;
                    }
                    _29737 = _29738;
                    _29726 = _5833;
                }
                else
                {
                    _29737 = _7710;
                    _29726 = 0.0;
                }
                _29736 = _29737;
                _29725 = _29726;
            }
            else
            {
                _29736 = _7710;
                _29725 = 0.0;
            }
            if (_289.DebugView == 9u)
            {
                imageStore(OutputTexture, ivec2(_5690), vec4(_29725, _29725, _29725, 1.0));
                break;
            }
            if ((_29725 <= 0.001000000047497451305389404296875) && (_5776 <= 0.001000000047497451305389404296875))
            {
                if (_289.ShadeParams.x > 0.5)
                {
                    imageStore(OutputTexture, ivec2(_5690), vec4(0.0));
                }
                break;
            }
            vec3 _9755 = ((_289.SunColor.xyz * (clamp(_5754.y, 0.0, 1.0) * 0.3183098733425140380859375)) + g_skyIrradiance) * 0.85000002384185791015625;
            vec3 _9761 = mix(vec3(dot(_9755, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _9755, vec3(0.5));
            if (_29725 > 0.001000000047497451305389404296875)
            {
                imageStore(OutputTexture, ivec2(_5690), vec4(mix(_29736, _9761, vec3(_5776)), 1.0));
            }
            else
            {
                imageStore(OutputTexture, ivec2(_5690), vec4(_9761, _5776));
            }
            break;
        }
        float _9804 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _9780 = ((_5698.x - (0.5 * float(_289.OutputWidth))) * _27246) / _9804;
        float _9792 = ((-(_5698.y - (0.5 * float(_289.OutputHeight)))) * _27246) / _9804;
        vec3 _9794 = vec3(_9780, _9792, _27246);
        vec3 _5908 = normalize(_9794);
        vec3 _5910 = -_5908;
        int _5926 = clamp(int((_289.NormalStep * max(float(_289.OutputHeight) / float(max(FluidSize.y, 1u)), 1.0)) + 0.5), 1, 16);
        float _5932 = (2.0 * max(_289.ShadeParams.y, 0.001000000047497451305389404296875)) * max(_289.NormalStep, 1.0);
        ivec2 _5934 = ivec2(_5926, 0);
        vec3 _27254;
        do
        {
            float _27250;
            do
            {
                vec2 _9962 = (((vec2(clamp(_5693 + _5934, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight))) - vec2(0.5);
                ivec2 _9965 = ivec2(floor(_9962));
                vec2 _9969 = _9962 - vec2(_9965);
                ivec2 _10144 = clamp(_9965, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10180 = clamp(_9965 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10216 = clamp(_9965 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10252 = clamp(_9965 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _9932 = vec4((1.0 - _9969.x) * (1.0 - _9969.y), _9969.x * (1.0 - _9969.y), (1.0 - _9969.x) * _9969.y, _9969.x * _9969.y);
                vec4 _10010 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10144.x, int((FluidSize.y - 1u) - uint(_10144.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10180.x, int((FluidSize.y - 1u) - uint(_10180.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10216.x, int((FluidSize.y - 1u) - uint(_10216.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10252.x, int((FluidSize.y - 1u) - uint(_10252.y)), 0).xy, 0).x);
                vec4 _9933 = _10010;
                float _29656;
                _29656 = 1000000000.0;
                float _29995;
                SPIRV_CROSS_UNROLL
                for (int _27248 = 0; _27248 < 4; _29656 = _29995, _27248++)
                {
                    if (_9933[_27248] > 0.0)
                    {
                        _29995 = min(_29656, _9933[_27248]);
                    }
                    else
                    {
                        _29995 = _29656;
                    }
                }
                vec4 _9936 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27249 = 0; _27249 < 4; )
                {
                    _9936[_27249] = ((_9933[_27249] > 0.0) && (_9933[_27249] < (_29656 + max(0.25, 0.02999999932944774627685546875 * _29656)))) ? _9932[_27249] : 0.0;
                    _27249++;
                    continue;
                }
                float _10070 = ((_9936.x + _9936.y) + _9936.z) + _9936.w;
                if (_10070 <= 9.9999997473787516355514526367188e-05)
                {
                    _27250 = 0.0;
                    break;
                }
                _27250 = dot(_9936, _10010) / _10070;
                break;
            } while(false);
            float _27253;
            do
            {
                vec2 _10492 = (((vec2(clamp(_5693 - _5934, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight))) - vec2(0.5);
                ivec2 _10495 = ivec2(floor(_10492));
                vec2 _10499 = _10492 - vec2(_10495);
                ivec2 _10674 = clamp(_10495, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10710 = clamp(_10495 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10746 = clamp(_10495 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _10782 = clamp(_10495 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _10462 = vec4((1.0 - _10499.x) * (1.0 - _10499.y), _10499.x * (1.0 - _10499.y), (1.0 - _10499.x) * _10499.y, _10499.x * _10499.y);
                vec4 _10540 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10674.x, int((FluidSize.y - 1u) - uint(_10674.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10710.x, int((FluidSize.y - 1u) - uint(_10710.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10746.x, int((FluidSize.y - 1u) - uint(_10746.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_10782.x, int((FluidSize.y - 1u) - uint(_10782.y)), 0).xy, 0).x);
                vec4 _10463 = _10540;
                float _29653;
                _29653 = 1000000000.0;
                float _29994;
                SPIRV_CROSS_UNROLL
                for (int _27251 = 0; _27251 < 4; _29653 = _29994, _27251++)
                {
                    if (_10463[_27251] > 0.0)
                    {
                        _29994 = min(_29653, _10463[_27251]);
                    }
                    else
                    {
                        _29994 = _29653;
                    }
                }
                vec4 _10466 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27252 = 0; _27252 < 4; )
                {
                    _10466[_27252] = ((_10463[_27252] > 0.0) && (_10463[_27252] < (_29653 + max(0.25, 0.02999999932944774627685546875 * _29653)))) ? _10462[_27252] : 0.0;
                    _27252++;
                    continue;
                }
                float _10600 = ((_10466.x + _10466.y) + _10466.z) + _10466.w;
                if (_10600 <= 9.9999997473787516355514526367188e-05)
                {
                    _27253 = 0.0;
                    break;
                }
                _27253 = dot(_10466, _10540) / _10600;
                break;
            } while(false);
            bool _9838 = (_27250 > 0.0) && (abs(_27250 - _27246) < _5932);
            bool _9847 = (_27253 > 0.0) && (abs(_27246 - _27253) < _5932);
            vec2 _9854 = vec2(_5693 + _5934) + vec2(0.5);
            float _10988 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9858 = vec3(((_9854.x - (0.5 * float(_289.OutputWidth))) * _27250) / _10988, ((-(_9854.y - (0.5 * float(_289.OutputHeight)))) * _27250) / _10988, _27250) - _9794;
            bvec3 _9859 = bvec3(_9838);
            vec3 _9860 = vec3(_9859.x ? _9858.x : vec3(0.0).x, _9859.y ? _9858.y : vec3(0.0).y, _9859.z ? _9858.z : vec3(0.0).z);
            vec2 _9868 = vec2(_5693 - _5934) + vec2(0.5);
            float _11027 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _9871 = _9794 - vec3(((_9868.x - (0.5 * float(_289.OutputWidth))) * _27253) / _11027, ((-(_9868.y - (0.5 * float(_289.OutputHeight)))) * _27253) / _11027, _27253);
            bvec3 _9872 = bvec3(_9847);
            vec3 _9873 = vec3(_9872.x ? _9871.x : vec3(0.0).x, _9872.y ? _9871.y : vec3(0.0).y, _9872.z ? _9871.z : vec3(0.0).z);
            if (_9838 && _9847)
            {
                _27254 = (_9860 + _9873) * 0.5;
                break;
            }
            bvec3 _9886 = bvec3(_9838);
            _27254 = vec3(_9886.x ? _9860.x : _9873.x, _9886.y ? _9860.y : _9873.y, _9886.z ? _9860.z : _9873.z);
            break;
        } while(false);
        ivec2 _5941 = ivec2(0, _5926);
        vec3 _27261;
        do
        {
            float _27257;
            do
            {
                vec2 _11185 = (((vec2(clamp(_5693 + _5941, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight))) - vec2(0.5);
                ivec2 _11188 = ivec2(floor(_11185));
                vec2 _11192 = _11185 - vec2(_11188);
                ivec2 _11367 = clamp(_11188, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11403 = clamp(_11188 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11439 = clamp(_11188 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11475 = clamp(_11188 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11155 = vec4((1.0 - _11192.x) * (1.0 - _11192.y), _11192.x * (1.0 - _11192.y), (1.0 - _11192.x) * _11192.y, _11192.x * _11192.y);
                vec4 _11233 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11367.x, int((FluidSize.y - 1u) - uint(_11367.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11403.x, int((FluidSize.y - 1u) - uint(_11403.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11439.x, int((FluidSize.y - 1u) - uint(_11439.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11475.x, int((FluidSize.y - 1u) - uint(_11475.y)), 0).xy, 0).x);
                vec4 _11156 = _11233;
                float _29650;
                _29650 = 1000000000.0;
                float _29993;
                SPIRV_CROSS_UNROLL
                for (int _27255 = 0; _27255 < 4; _29650 = _29993, _27255++)
                {
                    if (_11156[_27255] > 0.0)
                    {
                        _29993 = min(_29650, _11156[_27255]);
                    }
                    else
                    {
                        _29993 = _29650;
                    }
                }
                vec4 _11159 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27256 = 0; _27256 < 4; )
                {
                    _11159[_27256] = ((_11156[_27256] > 0.0) && (_11156[_27256] < (_29650 + max(0.25, 0.02999999932944774627685546875 * _29650)))) ? _11155[_27256] : 0.0;
                    _27256++;
                    continue;
                }
                float _11293 = ((_11159.x + _11159.y) + _11159.z) + _11159.w;
                if (_11293 <= 9.9999997473787516355514526367188e-05)
                {
                    _27257 = 0.0;
                    break;
                }
                _27257 = dot(_11159, _11233) / _11293;
                break;
            } while(false);
            float _27260;
            do
            {
                vec2 _11715 = (((vec2(clamp(_5693 - _5941, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1))) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight))) - vec2(0.5);
                ivec2 _11718 = ivec2(floor(_11715));
                vec2 _11722 = _11715 - vec2(_11718);
                ivec2 _11897 = clamp(_11718, ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11933 = clamp(_11718 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _11969 = clamp(_11718 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                ivec2 _12005 = clamp(_11718 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                vec4 _11685 = vec4((1.0 - _11722.x) * (1.0 - _11722.y), _11722.x * (1.0 - _11722.y), (1.0 - _11722.x) * _11722.y, _11722.x * _11722.y);
                vec4 _11763 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11897.x, int((FluidSize.y - 1u) - uint(_11897.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11933.x, int((FluidSize.y - 1u) - uint(_11933.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_11969.x, int((FluidSize.y - 1u) - uint(_11969.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12005.x, int((FluidSize.y - 1u) - uint(_12005.y)), 0).xy, 0).x);
                vec4 _11686 = _11763;
                float _29647;
                _29647 = 1000000000.0;
                float _29992;
                SPIRV_CROSS_UNROLL
                for (int _27258 = 0; _27258 < 4; _29647 = _29992, _27258++)
                {
                    if (_11686[_27258] > 0.0)
                    {
                        _29992 = min(_29647, _11686[_27258]);
                    }
                    else
                    {
                        _29992 = _29647;
                    }
                }
                vec4 _11689 = vec4(0.0);
                SPIRV_CROSS_UNROLL
                for (int _27259 = 0; _27259 < 4; )
                {
                    _11689[_27259] = ((_11686[_27259] > 0.0) && (_11686[_27259] < (_29647 + max(0.25, 0.02999999932944774627685546875 * _29647)))) ? _11685[_27259] : 0.0;
                    _27259++;
                    continue;
                }
                float _11823 = ((_11689.x + _11689.y) + _11689.z) + _11689.w;
                if (_11823 <= 9.9999997473787516355514526367188e-05)
                {
                    _27260 = 0.0;
                    break;
                }
                _27260 = dot(_11689, _11763) / _11823;
                break;
            } while(false);
            bool _11061 = (_27257 > 0.0) && (abs(_27257 - _27246) < _5932);
            bool _11070 = (_27260 > 0.0) && (abs(_27246 - _27260) < _5932);
            vec2 _11077 = vec2(_5693 + _5941) + vec2(0.5);
            float _12211 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _11081 = vec3(((_11077.x - (0.5 * float(_289.OutputWidth))) * _27257) / _12211, ((-(_11077.y - (0.5 * float(_289.OutputHeight)))) * _27257) / _12211, _27257) - _9794;
            bvec3 _11082 = bvec3(_11061);
            vec3 _11083 = vec3(_11082.x ? _11081.x : vec3(0.0).x, _11082.y ? _11081.y : vec3(0.0).y, _11082.z ? _11081.z : vec3(0.0).z);
            vec2 _11091 = vec2(_5693 - _5941) + vec2(0.5);
            float _12250 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _11094 = _9794 - vec3(((_11091.x - (0.5 * float(_289.OutputWidth))) * _27260) / _12250, ((-(_11091.y - (0.5 * float(_289.OutputHeight)))) * _27260) / _12250, _27260);
            bvec3 _11095 = bvec3(_11070);
            vec3 _11096 = vec3(_11095.x ? _11094.x : vec3(0.0).x, _11095.y ? _11094.y : vec3(0.0).y, _11095.z ? _11094.z : vec3(0.0).z);
            if (_11061 && _11070)
            {
                _27261 = (_11083 + _11096) * 0.5;
                break;
            }
            bvec3 _11109 = bvec3(_11061);
            _27261 = vec3(_11109.x ? _11083.x : _11096.x, _11109.y ? _11083.y : _11096.y, _11109.z ? _11083.z : _11096.z);
            break;
        } while(false);
        vec2 _5952 = _27254.xy * max(_289.NormalScale, 0.0);
        vec3 _26361 = _27254;
        _26361.x = _5952.x;
        _26361.y = _5952.y;
        vec2 _5962 = _27261.xy * max(_289.NormalScale, 0.0);
        vec3 _26365 = _27261;
        _26365.x = _5962.x;
        _26365.y = _5962.y;
        bvec3 _30015 = bvec3(length(_26361) < 9.9999999747524270787835121154785e-07);
        bvec3 _30017 = bvec3(length(_26365) < 9.9999999747524270787835121154785e-07);
        vec3 _5980 = normalize(cross(vec3(_30015.x ? vec3(1.0, 0.0, 0.0).x : _26361.x, _30015.y ? vec3(1.0, 0.0, 0.0).y : _26361.y, _30015.z ? vec3(1.0, 0.0, 0.0).z : _26361.z), vec3(_30017.x ? vec3(0.0, -1.0, 0.0).x : _26365.x, _30017.y ? vec3(0.0, -1.0, 0.0).y : _26365.y, _30017.z ? vec3(0.0, -1.0, 0.0).z : _26365.z)));
        vec3 _27290;
        if (dot(_5980, _5910) < 0.0)
        {
            _27290 = -_5980;
        }
        else
        {
            _27290 = _5980;
        }
        vec3 _27298;
        if (_289.Mode == 2u)
        {
            vec4 _27284;
            do
            {
                float _27275;
                do
                {
                    vec2 _12448 = ((_5698 * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight))) - vec2(0.5);
                    ivec2 _12451 = ivec2(floor(_12448));
                    vec2 _12455 = _12448 - vec2(_12451);
                    ivec2 _12630 = clamp(_12451, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12666 = clamp(_12451 + ivec2(1, 0), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12702 = clamp(_12451 + ivec2(0, 1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _12738 = clamp(_12451 + ivec2(1), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12418 = vec4((1.0 - _12455.x) * (1.0 - _12455.y), _12455.x * (1.0 - _12455.y), (1.0 - _12455.x) * _12455.y, _12455.x * _12455.y);
                    vec4 _12496 = vec4(texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12630.x, int((FluidSize.y - 1u) - uint(_12630.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12666.x, int((FluidSize.y - 1u) - uint(_12666.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12702.x, int((FluidSize.y - 1u) - uint(_12702.y)), 0).xy, 0).x, texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12738.x, int((FluidSize.y - 1u) - uint(_12738.y)), 0).xy, 0).x);
                    vec4 _12419 = _12496;
                    float _29644;
                    _29644 = 1000000000.0;
                    float _29991;
                    SPIRV_CROSS_UNROLL
                    for (int _27273 = 0; _27273 < 4; _29644 = _29991, _27273++)
                    {
                        if (_12419[_27273] > 0.0)
                        {
                            _29991 = min(_29644, _12419[_27273]);
                        }
                        else
                        {
                            _29991 = _29644;
                        }
                    }
                    vec4 _12422 = vec4(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _27274 = 0; _27274 < 4; )
                    {
                        _12422[_27274] = ((_12419[_27274] > 0.0) && (_12419[_27274] < (_29644 + max(0.25, 0.02999999932944774627685546875 * _29644)))) ? _12418[_27274] : 0.0;
                        _27274++;
                        continue;
                    }
                    float _12556 = ((_12422.x + _12422.y) + _12422.z) + _12422.w;
                    if (_12556 <= 9.9999997473787516355514526367188e-05)
                    {
                        _27275 = 0.0;
                        break;
                    }
                    _27275 = dot(_12422, _12496) / _12556;
                    break;
                } while(false);
                if (_27275 <= 0.0)
                {
                    _27284 = vec4(0.0);
                    break;
                }
                float _12295 = clamp(((max(_289.ShadeParams.y, 0.001000000047497451305389404296875) * ((0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05))) / _27275) * _289.EnvParams.y, 1.0, 48.0);
                vec2 _12305 = vec2(FluidSize) / vec2(float(_289.OutputWidth), float(_289.OutputHeight));
                float _12308 = max(0.25, 0.02999999932944774627685546875 * _27275);
                float _27277;
                vec4 _27278;
                _27278 = vec4(0.0);
                _27277 = 0.0;
                float _29744;
                vec4 _29745;
                SPIRV_CROSS_UNROLL
                for (int _27276 = 0; _27276 < 13; _27278 = _29745, _27277 = _29744, _27276++)
                {
                    vec2 _27279;
                    if (_27276 > 0)
                    {
                        float _12332 = (float((_27276 <= 6) ? _27276 : (_27276 - 6)) * 1.0471975803375244140625) + ((_27276 <= 6) ? 0.0 : 0.52359998226165771484375);
                        _27279 = vec2(cos(_12332), sin(_12332)) * (((_27276 <= 6) ? 0.5 : 1.0) * _12295);
                    }
                    else
                    {
                        _27279 = vec2(0.0);
                    }
                    ivec2 _12349 = ivec2(floor((_5698 + _27279) * _12305));
                    ivec2 _12924 = clamp(_12349, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12950 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_12924.x, int((FluidSize.y - 1u) - uint(_12924.y)), 0).xy, 0);
                    float _12951 = _12950.x;
                    if ((_12951 <= 0.0) || (abs(_12951 - _27275) > _12308))
                    {
                        _29745 = _27278;
                        _29744 = _27277;
                        continue;
                    }
                    ivec2 _12960 = clamp(_12349, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _12986 = texelFetch(SPIRV_Cross_CombinedFluidNormalSPIRV_Cross_DummySampler, ivec3(_12960.x, int((FluidSize.y - 1u) - uint(_12960.y)), 0).xy, 0);
                    float _12378 = exp((-2.0) * (dot(_27279, _27279) / max(_12295 * _12295, 9.9999997473787516355514526367188e-05))) * _12986.w;
                    _29745 = _27278 + (_12986 * _12378);
                    _29744 = _27277 + _12378;
                }
                if (_27277 <= 9.9999997473787516355514526367188e-05)
                {
                    _27284 = vec4(0.0);
                    break;
                }
                vec4 _12398 = _27278 / vec4(_27277);
                _12398.w = min(_12398.w, 1.0);
                _27284 = _12398;
                break;
            } while(false);
            float _5997 = length(_27284.xyz);
            vec3 _27299;
            if (_5997 > 0.001000000047497451305389404296875)
            {
                vec3 _6005 = _27284.xyz / vec3(_5997);
                vec3 _27294;
                if (dot(_6005, _5910) < 0.0)
                {
                    _27294 = -_6005;
                }
                else
                {
                    _27294 = _6005;
                }
                _27299 = normalize(mix(_27290, _27294, vec3(clamp(_27284.w * 4.0, 0.0, 1.0))));
            }
            else
            {
                _27299 = _27290;
            }
            _27298 = _27299;
        }
        else
        {
            _27298 = _27290;
        }
        float _6027 = max(_27247.x, 9.9999997473787516355514526367188e-05);
        ivec2 _13081 = clamp(ivec2(floor((_5698 * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
        uint _13091 = min(_289.ThicknessShift, 1u);
        uint _13099 = ((FluidSize.x + (1u << _13091)) - 1u) >> _13091;
        ivec2 _13119 = clamp(_13081 >> ivec2(uvec2(_13091)), ivec2(0), ivec2(int(_13099), int(((FluidSize.y + (1u << _13091)) - 1u) >> _13091)) - ivec2(1));
        uint _13129 = (uint(_13119.x) + (uint(_13119.y) * _13099)) * 4u;
        uint _13139 = AuxBits_1._data[_13129 + 1u];
        uint _13151 = AuxBits_1._data[_13129 + 3u];
        float _27296;
        _27296 = float(AuxBits_1._data[_13129]) * 6.1036087572574615478515625e-05;
        SPIRV_CROSS_UNROLL
        for (int _27295 = 0; _27295 < 4; )
        {
            ivec2 _13016 = ivec2((_27295 * 2) - 1, 0);
            ivec2 _13021 = ivec2(0, ((_27295 - 2) * 2) - 1);
            bvec2 _13022 = bvec2(_27295 < 2);
            uint _13164 = min(_289.ThicknessShift, 1u);
            uint _13172 = ((FluidSize.x + (1u << _13164)) - 1u) >> _13164;
            ivec2 _13192 = clamp((_13081 + ivec2(_13022.x ? _13016.x : _13021.x, _13022.y ? _13016.y : _13021.y)) >> ivec2(uvec2(_13164)), ivec2(0), ivec2(int(_13172), int(((FluidSize.y + (1u << _13164)) - 1u) >> _13164)) - ivec2(1));
            uint _13237 = min(_289.ThicknessShift, 1u);
            uint _13245 = ((FluidSize.x + (1u << _13237)) - 1u) >> _13237;
            ivec2 _13265 = clamp((_13081 + ivec2(((_27295 & 1) != int(0u)) ? 1 : (-1), ((_27295 & 2) != int(0u)) ? 1 : (-1))) >> ivec2(uvec2(_13237)), ivec2(0), ivec2(int(_13245), int(((FluidSize.y + (1u << _13237)) - 1u) >> _13237)) - ivec2(1));
            _27296 += ((float(AuxBits_1._data[(uint(_13192.x) + (uint(_13192.y) * _13172)) * 4u]) * 3.05180437862873077392578125e-05) + (float(AuxBits_1._data[(uint(_13265.x) + (uint(_13265.y) * _13245)) * 4u]) * 1.525902189314365386962890625e-05));
            _27295++;
            continue;
        }
        float _6046 = clamp((float(_13151) * 0.0009765625) / _6027, 0.0, 1.0);
        vec3 _13322 = ((_289.CameraRight.xyz * _5908.x) + (_289.CameraUp.xyz * _5908.y)) + (_289.CameraForward.xyz * _5908.z);
        vec3 _6052 = vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w) + (((_289.CameraRight.xyz * _9780) + (_289.CameraUp.xyz * _9792)) + (_289.CameraForward.xyz * _27246));
        vec3 _13375 = ((_289.CameraRight.xyz * _27298.x) + (_289.CameraUp.xyz * _27298.y)) + (_289.CameraForward.xyz * _27298.z);
        float _6063 = (_27246 / ((0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(abs(dot(_27298, _5910)), 0.07999999821186065673828125);
        float _6087 = (_289.RippleParams.x * mix(_289.RippleFlow.y, 1.0 + _289.RippleFlow.x, clamp((float(AuxBits_1._data[_13129 + 2u]) * 0.0009765625) / _6027, 0.0, 1.0))) * (smoothstep(0.75, 0.949999988079071044921875, _13375.y) * clamp(1.0 - (length(_9794) / max(_289.RippleParams.w, 0.00999999977648258209228515625)), 0.0, 1.0));
        float _27304;
        vec3 _27343;
        if (_6087 > 0.0)
        {
            vec2 _6093 = _6052.xz;
            float _13405 = max(_289.RippleParams.y, 0.00999999977648258209228515625);
            vec2 _27301;
            float _27302;
            _27302 = 0.0;
            _27301 = vec2(0.0);
            SPIRV_CROSS_UNROLL
            for (int _27300 = 0; _27300 < 6; )
            {
                float _13417 = _13405 * _1935[_27300];
                float _13422 = _6087 * pow(0.7799999713897705078125, float(_27300));
                float _13429 = clamp((_13417 / max(_6063 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0);
                uint _13505 = (((uint(_27300) * 7919u) + 17u) * 747796405u) + 2891336453u;
                uint _13513 = ((_13505 >> ((_13505 >> 28u) + 4u)) ^ _13505) * 277803737u;
                float _13436 = 0.60000002384185791015625 + (float(((_13513 >> 22u) ^ _13513) >> 8u) * 3.7450703871400037314742803573608e-07);
                vec2 _13441 = vec2(cos(_13436), sin(_13436));
                uint _13532 = (((uint(_27300) * 104729u) + 3u) * 747796405u) + 2891336453u;
                uint _13540 = ((_13532 >> ((_13532 >> 28u) + 4u)) ^ _13532) * 277803737u;
                _27302 += (((0.5 * _13422) * _13422) * (1.0 - (_13429 * _13429)));
                _27301 += (_13441 * ((_13422 * _13429) * cos(((dot(_13441, _6093) * (6.283185482025146484375 / _13417)) + ((sqrt(61.63805389404296875 / _13417) * _289.RippleParams.z) * _289.ShadeParams.z)) + (float(((_13540 >> 22u) ^ _13540) >> 8u) * 3.7450703871400037314742803573608e-07))));
                _27300++;
                continue;
            }
            vec3 _6108 = normalize(_13375 + vec3(-_27301.x, 0.0, -_27301.y));
            vec3 _6110 = normalize(vec3(dot(_6108, _289.CameraRight.xyz), dot(_6108, _289.CameraUp.xyz), dot(_6108, _289.CameraForward.xyz)));
            bvec3 _30021 = bvec3(dot(_6110, _5910) < 0.0199999995529651641845703125);
            _27343 = vec3(_30021.x ? _27298.x : _6110.x, _30021.y ? _27298.y : _6110.y, _30021.z ? _27298.z : _6110.z);
            _27304 = _27302;
        }
        else
        {
            _27343 = _27298;
            _27304 = 0.0;
        }
        float _6122 = _27304 * _289.RippleFlow.z;
        if (_289.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_5690), vec4((_27343 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_289.DebugView == 2u)
        {
            float _6145 = clamp(_27246 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5690), vec4(_6145, _6145, _6145, 1.0));
            break;
        }
        float _6165 = (_7623 < 100000000.0) ? (max(_7623 - _27246, 0.0) / max(_5908.z, 0.0500000007450580596923828125)) : 10000.0;
        float _6170 = _6027 * max(_289.ThicknessScale, 0.0);
        float _6175 = smoothstep(0.449999988079071044921875, 0.800000011920928955078125, _6170 / max(_6165, 0.001000000047497451305389404296875));
        float _27341;
        do
        {
            vec3 _13596 = (_6052 + (_30012 * (max(_6052.y - _289.PoolMin.y, 0.0500000007450580596923828125) / max(-_30012.y, 0.20000000298023223876953125)))) - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
            float _13662 = dot(_13596, _289.CameraForward.xyz);
            if (_13662 <= 0.100000001490116119384765625)
            {
                _27341 = 1.0;
                break;
            }
            float _13703 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
            float _13679 = ((dot(_13596, _289.CameraRight.xyz) * _13703) / _13662) + (0.5 * float(_289.OutputWidth));
            float _13692 = (((-dot(_13596, _289.CameraUp.xyz)) * _13703) / _13662) + (0.5 * float(_289.OutputHeight));
            vec2 _13693 = vec2(_13679, _13692);
            if (!((((_13679 >= 0.0) && (_13692 >= 0.0)) && (_13679 < float(_289.OutputWidth))) && (_13692 < float(_289.OutputHeight))))
            {
                _27341 = 1.0;
                break;
            }
            ivec2 _13787 = clamp(ivec2(_13693), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
            vec4 _13739 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_13787.x, int((_289.OutputHeight - 1u) - uint(_13787.y)), 0).xy, 0);
            float _13740 = _13739.x;
            if (abs((((_13740 > 0.0) && (abs(_13740 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_13740 - _289.ProjA)) : 1000000000.0) - _13662) > ((0.1500000059604644775390625 * _13662) + 0.100000001490116119384765625))
            {
                _27341 = 1.0;
                break;
            }
            ivec2 _13847 = clamp(ivec2(_13693), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
            _27341 = texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_13847.x, int((_289.OutputHeight - 1u) - uint(_13847.y)), 0).xy, 0).x;
            break;
        } while(false);
        vec3 _6181 = refract(_5908, _27343, 0.75187969207763671875);
        bvec3 _30025 = bvec3(dot(_6181, _6181) < 9.9999999747524270787835121154785e-07);
        vec3 _6200 = normalize(mix(_5908, vec3(_30025.x ? _5908.x : _6181.x, _30025.y ? _5908.y : _6181.y, _30025.z ? _5908.z : _6181.z), vec3(clamp(_289.Refraction, 0.0, 1.0) * mix(0.300000011920928955078125, 1.0, _6175))));
        vec3 _13899 = ((_289.CameraRight.xyz * _6200.x) + (_289.CameraUp.xyz * _6200.y)) + (_289.CameraForward.xyz * _6200.z);
        float _27358;
        do
        {
            if ((((_6052.x < _289.PoolMin.x) || (_6052.x > _289.PoolMax.x)) || (_6052.z < _289.PoolMin.z)) || (_6052.z > _289.PoolMax.z))
            {
                _27358 = 1000.0;
                break;
            }
            float _27346;
            if (_13899.x > 9.9999997473787516355514526367188e-05)
            {
                _27346 = (_289.PoolMax.x - _6052.x) / _13899.x;
            }
            else
            {
                float _27347;
                if (_13899.x < (-9.9999997473787516355514526367188e-05))
                {
                    _27347 = (_289.PoolMin.x - _6052.x) / _13899.x;
                }
                else
                {
                    _27347 = 1000.0;
                }
                _27346 = _27347;
            }
            float _30027 = (_27346 < 1000.0) ? _27346 : 1000.0;
            float _27350;
            if (_13899.z > 9.9999997473787516355514526367188e-05)
            {
                _27350 = (_289.PoolMax.z - _6052.z) / _13899.z;
            }
            else
            {
                float _27351;
                if (_13899.z < (-9.9999997473787516355514526367188e-05))
                {
                    _27351 = (_289.PoolMin.z - _6052.z) / _13899.z;
                }
                else
                {
                    _27351 = 1000.0;
                }
                _27350 = _27351;
            }
            float _30028 = (_27350 < _30027) ? _27350 : _30027;
            float _27356;
            if (_13899.y < (-9.9999997473787516355514526367188e-05))
            {
                float _14029 = (_289.PoolMin.y - _6052.y) / _13899.y;
                _27356 = (_14029 < _30028) ? _14029 : _30028;
            }
            else
            {
                _27356 = _30028;
            }
            _27358 = max(_27356, 0.0);
            break;
        } while(false);
        float _6219 = clamp(max((_6165 * 1.2000000476837158203125) + 0.20000000298023223876953125, (_27358 < 100.0) ? ((_27358 * 1.10000002384185791015625) + 0.100000001490116119384765625) : 0.0), 0.300000011920928955078125, max(_289.MaxTravel, 0.300000011920928955078125) * 2.0);
        vec3 _27714;
        float _27771;
        float _27828;
        float _27884;
        vec2 _27940;
        do
        {
            ivec2 _14371 = clamp(ivec2(_5698), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
            vec4 _14323 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14371.x, int((_289.OutputHeight - 1u) - uint(_14371.y)), 0).xy, 0);
            float _14324 = _14323.x;
            vec2 _27629;
            float _27659;
            _27659 = ((_14324 > 0.0) && (abs(_14324 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_14324 - _289.ProjA)) : 1000000000.0;
            _27629 = _5698;
            float _14119;
            float _30032;
            vec2 _30034;
            bool _27366;
            float _27688;
            vec3 _27715;
            float _27772;
            float _27829;
            float _27885;
            vec2 _27941;
            int _27359 = 1;
            float _27361 = 0.0;
            SPIRV_CROSS_LOOP
            for (;;)
            {
                if (_27359 <= 16)
                {
                    float _14113 = float(_27359) * 0.0625;
                    _14119 = (_6219 * _14113) * sqrt(_14113);
                    vec3 _14124 = _9794 + (_6200 * _14119);
                    if (_14124.z <= 0.0500000007450580596923828125)
                    {
                        _27941 = _27971;
                        _27885 = _27802;
                        _27829 = _27802;
                        _27772 = _27802;
                        _27715 = _27745;
                        _27688 = 0.0;
                        _27366 = false;
                        break;
                    }
                    float _14441 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    float _14417 = ((_14124.x * _14441) / _14124.z) + (0.5 * float(_289.OutputWidth));
                    float _14430 = (((-_14124.y) * _14441) / _14124.z) + (0.5 * float(_289.OutputHeight));
                    vec2 _14431 = vec2(_14417, _14430);
                    if (!((((_14417 >= 0.0) && (_14430 >= 0.0)) && (_14417 < float(_289.OutputWidth))) && (_14430 < float(_289.OutputHeight))))
                    {
                        _27941 = _27971;
                        _27885 = _27802;
                        _27829 = _27802;
                        _27772 = _27802;
                        _27715 = _27745;
                        _27688 = 0.0;
                        _27366 = false;
                        break;
                    }
                    ivec2 _14525 = clamp(ivec2(_14431), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                    vec4 _14477 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14525.x, int((_289.OutputHeight - 1u) - uint(_14525.y)), 0).xy, 0);
                    float _14478 = _14477.x;
                    float _14495 = ((_14478 > 0.0) && (abs(_14478 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_14478 - _289.ProjA)) : 1000000000.0;
                    if (_14124.z >= _14495)
                    {
                        ivec2 _14146 = ivec2(_14431);
                        ivec2 _14606 = clamp(ivec2(floor(((vec2(_14146) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14615 = clamp(_14606, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        vec4 _14641 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_14615.x, int((FluidSize.y - 1u) - uint(_14615.y)), 0).xy, 0);
                        float _14642 = _14641.x;
                        ivec2 _14651 = clamp(_14606, ivec2(0), ivec2(FluidSize) - ivec2(1));
                        ivec2 _14738 = clamp(_14146, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec4 _14690 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14738.x, int((_289.OutputHeight - 1u) - uint(_14738.y)), 0).xy, 0);
                        float _14691 = _14690.x;
                        if (!(((_14642 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_14651.x, int((FluidSize.y - 1u) - uint(_14651.y)), 0).xy, 0).w > 0.5)) && ((((_14691 > 0.0) && (abs(_14691 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_14691 - _289.ProjA)) : 1000000000.0) >= (_14642 + 0.0500000007450580596923828125))))
                        {
                            _27941 = _27971;
                            _27885 = _27802;
                            _27829 = _27802;
                            _27772 = _27802;
                            _27715 = _27745;
                            _27688 = 0.0;
                            _27366 = false;
                            break;
                        }
                        float _27363;
                        _27363 = _14119;
                        float _30030;
                        float _30031;
                        int _27362 = 0;
                        float _27364 = _27361;
                        SPIRV_CROSS_UNROLL
                        for (; _27362 < 5; _27364 = _30030, _27363 = _30031, _27362++)
                        {
                            float _14161 = 0.5 * (_27364 + _27363);
                            vec3 _14166 = _9794 + (_6200 * _14161);
                            float _14808 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            ivec2 _14869 = clamp(ivec2(vec2(((_14166.x * _14808) / _14166.z) + (0.5 * float(_289.OutputWidth)), (((-_14166.y) * _14808) / _14166.z) + (0.5 * float(_289.OutputHeight)))), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                            vec4 _14821 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_14869.x, int((_289.OutputHeight - 1u) - uint(_14869.y)), 0).xy, 0);
                            float _14822 = _14821.x;
                            bool _14173 = _14166.z >= (((_14822 > 0.0) && (abs(_14822 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_14822 - _289.ProjA)) : 1000000000.0);
                            _30030 = _14173 ? _27364 : _14161;
                            _30031 = _14173 ? _14161 : _27363;
                        }
                        vec3 _14187 = _9794 + (_6200 * _27363);
                        float _14939 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _14198 = clamp(vec2(((_14187.x * _14939) / _14187.z) + (0.5 * float(_289.OutputWidth)), (((-_14187.y) * _14939) / _14187.z) + (0.5 * float(_289.OutputHeight))), vec2(0.5), vec2(float(_289.OutputWidth), float(_289.OutputHeight)) - vec2(0.5));
                        ivec2 _15000 = clamp(ivec2(_14198), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec4 _14952 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15000.x, int((_289.OutputHeight - 1u) - uint(_15000.y)), 0).xy, 0);
                        float _14953 = _14952.x;
                        float _14970 = ((_14953 > 0.0) && (abs(_14953 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_14953 - _289.ProjA)) : 1000000000.0;
                        float _15078 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        _27941 = _14198;
                        _27885 = _14970;
                        _27829 = _27363;
                        _27772 = 1.0;
                        _27715 = vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w) + (((_289.CameraRight.xyz * (((_14198.x - (0.5 * float(_289.OutputWidth))) * _14970) / _15078)) + (_289.CameraUp.xyz * (((-(_14198.y - (0.5 * float(_289.OutputHeight)))) * _14970) / _15078))) + (_289.CameraForward.xyz * _14970));
                        _27688 = 1.0;
                        _27366 = true;
                        break;
                    }
                    ivec2 _14222 = ivec2(_14431 - vec2(0.5));
                    ivec2 _15151 = clamp(ivec2(floor(((vec2(_14222) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15160 = clamp(_15151, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _15186 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15160.x, int((FluidSize.y - 1u) - uint(_15160.y)), 0).xy, 0);
                    float _15187 = _15186.x;
                    ivec2 _15196 = clamp(_15151, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15283 = clamp(_14222, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                    vec4 _15235 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15283.x, int((_289.OutputHeight - 1u) - uint(_15283.y)), 0).xy, 0);
                    float _15236 = _15235.x;
                    ivec2 _14227 = ivec2(_14431 + vec2(0.5));
                    ivec2 _15364 = clamp(ivec2(floor(((vec2(_14227) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15373 = clamp(_15364, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    vec4 _15399 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_15373.x, int((FluidSize.y - 1u) - uint(_15373.y)), 0).xy, 0);
                    float _15400 = _15399.x;
                    ivec2 _15409 = clamp(_15364, ivec2(0), ivec2(FluidSize) - ivec2(1));
                    ivec2 _15496 = clamp(_14227, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                    vec4 _15448 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_15496.x, int((_289.OutputHeight - 1u) - uint(_15496.y)), 0).xy, 0);
                    float _15449 = _15448.x;
                    bool _14229 = (((_15187 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15196.x, int((FluidSize.y - 1u) - uint(_15196.y)), 0).xy, 0).w > 0.5)) && ((((_15236 > 0.0) && (abs(_15236 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_15236 - _289.ProjA)) : 1000000000.0) >= (_15187 + 0.0500000007450580596923828125))) && (((_15400 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_15409.x, int((FluidSize.y - 1u) - uint(_15409.y)), 0).xy, 0).w > 0.5)) && ((((_15449 > 0.0) && (abs(_15449 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_15449 - _289.ProjA)) : 1000000000.0) >= (_15400 + 0.0500000007450580596923828125)));
                    _30032 = _14229 ? _14495 : _27659;
                    bvec2 _30033 = bvec2(_14229);
                    _30034 = vec2(_30033.x ? _14431.x : _27629.x, _30033.y ? _14431.y : _27629.y);
                    _27359++;
                    _27659 = _30032;
                    _27629 = _30034;
                    _27361 = _14119;
                    continue;
                }
                else
                {
                    _27941 = _27971;
                    _27885 = _27802;
                    _27829 = _27802;
                    _27772 = _27802;
                    _27715 = _27745;
                    _27688 = 0.0;
                    _27366 = false;
                    break;
                }
            }
            if (_27366)
            {
                _27940 = _27941;
                _27884 = _27885;
                _27828 = _27829;
                _27771 = _27772;
                _27714 = _27715;
                break;
            }
            vec3 _15548 = ((_289.CameraRight.xyz * _6200.x) + (_289.CameraUp.xyz * _6200.y)) + (_289.CameraForward.xyz * _6200.z);
            float _27381;
            vec3 _27382;
            do
            {
                if ((((_6052.x < _289.PoolMin.x) || (_6052.x > _289.PoolMax.x)) || (_6052.z < _289.PoolMin.z)) || (_6052.z > _289.PoolMax.z))
                {
                    _27382 = vec3(0.0, 1.0, 0.0);
                    _27381 = 1000.0;
                    break;
                }
                float _27369;
                if (_15548.x > 9.9999997473787516355514526367188e-05)
                {
                    _27369 = (_289.PoolMax.x - _6052.x) / _15548.x;
                }
                else
                {
                    float _27370;
                    if (_15548.x < (-9.9999997473787516355514526367188e-05))
                    {
                        _27370 = (_289.PoolMin.x - _6052.x) / _15548.x;
                    }
                    else
                    {
                        _27370 = 1000.0;
                    }
                    _27369 = _27370;
                }
                bool _15610 = _27369 < 1000.0;
                vec3 _27386;
                if (_15610)
                {
                    _27386 = vec3((_15548.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                }
                else
                {
                    _27386 = vec3(0.0, 1.0, 0.0);
                }
                float _30035 = _15610 ? _27369 : 1000.0;
                float _27373;
                if (_15548.z > 9.9999997473787516355514526367188e-05)
                {
                    _27373 = (_289.PoolMax.z - _6052.z) / _15548.z;
                }
                else
                {
                    float _27374;
                    if (_15548.z < (-9.9999997473787516355514526367188e-05))
                    {
                        _27374 = (_289.PoolMin.z - _6052.z) / _15548.z;
                    }
                    else
                    {
                        _27374 = 1000.0;
                    }
                    _27373 = _27374;
                }
                bool _15648 = _27373 < _30035;
                vec3 _27384;
                if (_15648)
                {
                    _27384 = vec3(0.0, 0.0, _30073);
                }
                else
                {
                    _27384 = _27386;
                }
                float _30036 = _15648 ? _27373 : _30035;
                float _27379;
                vec3 _27383;
                if (_15548.y < (-9.9999997473787516355514526367188e-05))
                {
                    float _15668 = (_289.PoolMin.y - _6052.y) / _15548.y;
                    bool _15671 = _15668 < _30036;
                    bvec3 _30037 = bvec3(_15671);
                    _27383 = vec3(_30037.x ? vec3(0.0, 1.0, 0.0).x : _27384.x, _30037.y ? vec3(0.0, 1.0, 0.0).y : _27384.y, _30037.z ? vec3(0.0, 1.0, 0.0).z : _27384.z);
                    _27379 = _15671 ? _15668 : _30036;
                }
                else
                {
                    _27383 = _27384;
                    _27379 = _30036;
                }
                _27382 = _27383;
                _27381 = max(_27379, 0.0);
                break;
            } while(false);
            if (_27381 < 100.0)
            {
                vec3 _14254 = _6052 + (_15548 * _27381);
                uint _27530;
                vec2 _27612;
                float _27619;
                do
                {
                    bool _27391;
                    vec2 _27392;
                    float _27393;
                    do
                    {
                        vec3 _15922 = _14254 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                        float _15980 = dot(_15922, _289.CameraForward.xyz);
                        if (_15980 <= 0.0500000007450580596923828125)
                        {
                            _27393 = 1000000000.0;
                            _27392 = vec2(0.5);
                            _27391 = false;
                            break;
                        }
                        float _16021 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _15997 = ((dot(_15922, _289.CameraRight.xyz) * _16021) / _15980) + (0.5 * float(_289.OutputWidth));
                        float _16010 = (((-dot(_15922, _289.CameraUp.xyz)) * _16021) / _15980) + (0.5 * float(_289.OutputHeight));
                        if (!((((_15997 >= 0.0) && (_16010 >= 0.0)) && (_15997 < float(_289.OutputWidth))) && (_16010 < float(_289.OutputHeight))))
                        {
                            _27393 = 1000000000.0;
                            _27392 = vec2(0.5);
                            _27391 = false;
                            break;
                        }
                        vec2 _15939 = floor(vec2(_15997, _16010)) + vec2(0.5);
                        ivec2 _16105 = clamp(ivec2(_15939), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec4 _16057 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16105.x, int((_289.OutputHeight - 1u) - uint(_16105.y)), 0).xy, 0);
                        float _16058 = _16057.x;
                        float _16075 = ((_16058 > 0.0) && (abs(_16058 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_16058 - _289.ProjA)) : 1000000000.0;
                        _27393 = _16075;
                        _27392 = _15939;
                        _27391 = abs(_16075 - _15980) < ((0.0199999995529651641845703125 * _15980) + 0.02999999932944774627685546875);
                        break;
                    } while(false);
                    if (_27391)
                    {
                        _27619 = _27393;
                        _27612 = _27392;
                        _27530 = 1u;
                        break;
                    }
                    vec3 _15728 = _14254 - vec3(_289.CameraRight.w, _27234, _289.CameraForward.w);
                    vec3 _15736 = (_289.PoolMin.xyz + _289.PoolMax.xyz) * 0.5;
                    vec3 _15741 = vec3((_15728.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15746 = vec3(0.0, 0.0, (_15728.z >= 0.0) ? 1.0 : (-1.0));
                    vec3 _15753 = vec3((_15736.x >= _14254.x) ? 1.0 : (-1.0), 0.0, 0.0);
                    vec3 _15760 = vec3(0.0, 0.0, (_15736.z >= _14254.z) ? 1.0 : (-1.0));
                    vec3 _27394;
                    vec3 _27395;
                    vec3 _27400;
                    vec3 _27402;
                    if (abs(_27382.y) > 0.5)
                    {
                        bool _15772 = abs(_15728.z) >= abs(_15728.x);
                        bvec3 _15776 = bvec3(_15772);
                        bvec3 _15781 = bvec3(_15772);
                        bvec3 _15786 = bvec3(_15772);
                        bvec3 _15791 = bvec3(_15772);
                        _27402 = vec3(_15776.x ? _15746.x : _15741.x, _15776.y ? _15746.y : _15741.y, _15776.z ? _15746.z : _15741.z);
                        _27400 = vec3(_15786.x ? _15741.x : _15746.x, _15786.y ? _15741.y : _15746.y, _15786.z ? _15741.z : _15746.z);
                        _27395 = vec3(_15791.x ? _15760.x : _15753.x, _15791.y ? _15760.y : _15753.y, _15791.z ? _15760.z : _15753.z);
                        _27394 = vec3(_15781.x ? _15753.x : _15760.x, _15781.y ? _15753.y : _15760.y, _15781.z ? _15753.z : _15760.z);
                    }
                    else
                    {
                        bool _15797 = abs(_27382.x) > 0.5;
                        bvec3 _15801 = bvec3(_15797);
                        bvec3 _15806 = bvec3(_15797);
                        _27402 = vec3(_15801.x ? _15746.x : _15741.x, _15801.y ? _15746.y : _15741.y, _15801.z ? _15746.z : _15741.z);
                        _27400 = vec3(0.0, 1.0, 0.0);
                        _27395 = vec3(_15806.x ? _15760.x : _15753.x, _15806.y ? _15760.y : _15753.y, _15806.z ? _15760.z : _15753.z);
                        _27394 = vec3(0.0, 1.0, 0.0);
                    }
                    float _16188 = ((_27394.y > 0.5) ? (_289.PoolMin.w - _14254.y) : ((_27394.x > 0.5) ? (_289.PoolMax.x - _14254.x) : ((_27394.x < (-0.5)) ? (_14254.x - _289.PoolMin.x) : ((_27394.z > 0.5) ? (_289.PoolMax.z - _14254.z) : (_14254.z - _289.PoolMin.z))))) - 0.02999999932944774627685546875;
                    float _16232 = ((_27395.y > 0.5) ? (_289.PoolMin.w - _14254.y) : ((_27395.x > 0.5) ? (_289.PoolMax.x - _14254.x) : ((_27395.x < (-0.5)) ? (_14254.x - _289.PoolMin.x) : ((_27395.z > 0.5) ? (_289.PoolMax.z - _14254.z) : (_14254.z - _289.PoolMin.z))))) - 0.02999999932944774627685546875;
                    vec2 _27614;
                    _27614 = _27392;
                    bool _27516;
                    uint _27531;
                    vec2 _27613;
                    vec2 _27616;
                    float _27620;
                    float _27623;
                    bool _29801;
                    int _27396 = 0;
                    bool _27419;
                    float _27621 = _27393;
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_27396 < 11)
                        {
                            if (_27396 == 1)
                            {
                                vec3 _15825 = _14254 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                float _16258 = dot(_15825, _289.CameraForward.xyz);
                                vec2 _27617;
                                float _27624;
                                if (_16258 > 0.0500000007450580596923828125)
                                {
                                    float _16299 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _16275 = ((dot(_15825, _289.CameraRight.xyz) * _16299) / _16258) + (0.5 * float(_289.OutputWidth));
                                    float _16288 = (((-dot(_15825, _289.CameraUp.xyz)) * _16299) / _16258) + (0.5 * float(_289.OutputHeight));
                                    vec2 _16289 = vec2(_16275, _16288);
                                    ivec2 _15836 = ivec2(_16289);
                                    ivec2 _16373 = clamp(ivec2(floor(((vec2(_15836) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16382 = clamp(_16373, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _16408 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_16382.x, int((FluidSize.y - 1u) - uint(_16382.y)), 0).xy, 0);
                                    float _16409 = _16408.x;
                                    ivec2 _16418 = clamp(_16373, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _16505 = clamp(_15836, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                    vec4 _16457 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16505.x, int((_289.OutputHeight - 1u) - uint(_16505.y)), 0).xy, 0);
                                    float _16458 = _16457.x;
                                    vec2 _27618;
                                    float _27625;
                                    if (((((_16275 >= 0.0) && (_16288 >= 0.0)) && (_16275 < float(_289.OutputWidth))) && (_16288 < float(_289.OutputHeight))) && (((_16409 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_16418.x, int((FluidSize.y - 1u) - uint(_16418.y)), 0).xy, 0).w > 0.5)) && ((((_16458 > 0.0) && (abs(_16458 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_16458 - _289.ProjA)) : 1000000000.0) >= (_16409 + 0.0500000007450580596923828125))))
                                    {
                                        vec2 _15843 = floor(_16289) + vec2(0.5);
                                        ivec2 _16596 = clamp(ivec2(_15843), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                        vec4 _16548 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16596.x, int((_289.OutputHeight - 1u) - uint(_16596.y)), 0).xy, 0);
                                        float _16549 = _16548.x;
                                        float _16566 = ((_16549 > 0.0) && (abs(_16549 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_16549 - _289.ProjA)) : 1000000000.0;
                                        if (_16566 < _16258)
                                        {
                                            _27620 = _16566;
                                            _27613 = _15843;
                                            _27531 = 2u;
                                            _27516 = true;
                                            break;
                                        }
                                        _27625 = _16566;
                                        _27618 = _15843;
                                    }
                                    else
                                    {
                                        _27625 = _27621;
                                        _27618 = _27614;
                                    }
                                    _27624 = _27625;
                                    _27617 = _27618;
                                }
                                else
                                {
                                    _27624 = _27621;
                                    _27617 = _27614;
                                }
                                _29801 = _27419;
                                _27623 = _27624;
                                _27616 = _27617;
                                int _15903 = _27396 + 1;
                                _27419 = _29801;
                                _27396 = _15903;
                                _27621 = _27623;
                                _27614 = _27616;
                                continue;
                            }
                            bool _15857 = _27396 == 2;
                            bool _15859 = _27396 >= 3;
                            int _15861 = _27396 & 1;
                            bool _15864 = _15857 || (_15859 && (_15861 == 0));
                            bool _15866 = _27396 < 3;
                            int _15868 = _27396 - 3;
                            float _15873 = _15866 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_15868 / 2)));
                            if (_15873 >= (_15864 ? _16232 : _16188))
                            {
                                _29801 = _27419;
                                _27623 = _27621;
                                _27616 = _27614;
                                int _15903 = _27396 + 1;
                                _27419 = _29801;
                                _27396 = _15903;
                                _27621 = _27623;
                                _27614 = _27616;
                                continue;
                            }
                            bvec3 _15886 = bvec3(_15864);
                            vec3 _15890 = _14254 + (vec3(_15886.x ? _27395.x : _27394.x, _15886.y ? _27395.y : _27394.y, _15886.z ? _27395.z : _27394.z) * _15873);
                            bvec3 _15894 = bvec3(_15864);
                            vec3 _15895 = vec3(_15894.x ? _27400.x : _27402.x, _15894.y ? _27400.y : _27402.y, _15894.z ? _27400.z : _27402.z);
                            bool _27416;
                            vec2 _27491;
                            float _27495;
                            do
                            {
                                float _16759 = ((_15895.y > 0.5) ? (_289.PoolMin.w - _15890.y) : ((_15895.x > 0.5) ? (_289.PoolMax.x - _15890.x) : ((_15895.x < (-0.5)) ? (_15890.x - _289.PoolMin.x) : ((_15895.z > 0.5) ? (_289.PoolMax.z - _15890.z) : (_15890.z - _289.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _27492;
                                _27492 = vec2(0.5);
                                float _16665;
                                float _16707;
                                vec2 _27407;
                                float _27408;
                                bool _27413;
                                bool _27417;
                                int _27403 = 0;
                                float _27404 = 0.0;
                                float _27405 = 0.0599999986588954925537109375;
                                float _27496 = 1000000000.0;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if ((_27403 < 40) && (_27404 < _16759))
                                    {
                                        _16665 = min(_27404 + _27405, _16759);
                                        bool _27406;
                                        do
                                        {
                                            vec3 _16773 = (_15890 + (_15895 * _16665)) - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                            float _16831 = dot(_16773, _289.CameraForward.xyz);
                                            if (_16831 <= 0.0500000007450580596923828125)
                                            {
                                                _27408 = 1000000000.0;
                                                _27407 = vec2(0.5);
                                                _27406 = false;
                                                break;
                                            }
                                            float _16872 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                            float _16848 = ((dot(_16773, _289.CameraRight.xyz) * _16872) / _16831) + (0.5 * float(_289.OutputWidth));
                                            float _16861 = (((-dot(_16773, _289.CameraUp.xyz)) * _16872) / _16831) + (0.5 * float(_289.OutputHeight));
                                            if (!((((_16848 >= 0.0) && (_16861 >= 0.0)) && (_16848 < float(_289.OutputWidth))) && (_16861 < float(_289.OutputHeight))))
                                            {
                                                _27408 = 1000000000.0;
                                                _27407 = vec2(0.5);
                                                _27406 = false;
                                                break;
                                            }
                                            vec2 _16790 = floor(vec2(_16848, _16861)) + vec2(0.5);
                                            ivec2 _16956 = clamp(ivec2(_16790), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                            vec4 _16908 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_16956.x, int((_289.OutputHeight - 1u) - uint(_16956.y)), 0).xy, 0);
                                            float _16909 = _16908.x;
                                            float _16926 = ((_16909 > 0.0) && (abs(_16909 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_16909 - _289.ProjA)) : 1000000000.0;
                                            _27408 = _16926;
                                            _27407 = _16790;
                                            _27406 = abs(_16926 - _16831) < ((0.0199999995529651641845703125 * _16831) + 0.02999999932944774627685546875);
                                            break;
                                        } while(false);
                                        if (_27406)
                                        {
                                            vec2 _27493;
                                            float _27497;
                                            _27497 = _27408;
                                            _27493 = _27407;
                                            float _30040;
                                            float _30041;
                                            float _30042;
                                            vec2 _30044;
                                            int _27412 = 0;
                                            float _27498 = _27404;
                                            float _27500 = _16665;
                                            SPIRV_CROSS_UNROLL
                                            for (; _27412 < 4; _27500 = _30040, _27498 = _30041, _27497 = _30042, _27493 = _30044, _27412++)
                                            {
                                                float _16683 = 0.5 * (_27498 + _27500);
                                                bool _27501;
                                                vec2 _27502;
                                                float _27503;
                                                do
                                                {
                                                    vec3 _17000 = (_15890 + (_15895 * _16683)) - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                                    float _17058 = dot(_17000, _289.CameraForward.xyz);
                                                    if (_17058 <= 0.0500000007450580596923828125)
                                                    {
                                                        _27503 = 1000000000.0;
                                                        _27502 = vec2(0.5);
                                                        _27501 = false;
                                                        break;
                                                    }
                                                    float _17099 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                    float _17075 = ((dot(_17000, _289.CameraRight.xyz) * _17099) / _17058) + (0.5 * float(_289.OutputWidth));
                                                    float _17088 = (((-dot(_17000, _289.CameraUp.xyz)) * _17099) / _17058) + (0.5 * float(_289.OutputHeight));
                                                    if (!((((_17075 >= 0.0) && (_17088 >= 0.0)) && (_17075 < float(_289.OutputWidth))) && (_17088 < float(_289.OutputHeight))))
                                                    {
                                                        _27503 = 1000000000.0;
                                                        _27502 = vec2(0.5);
                                                        _27501 = false;
                                                        break;
                                                    }
                                                    vec2 _17017 = floor(vec2(_17075, _17088)) + vec2(0.5);
                                                    ivec2 _17183 = clamp(ivec2(_17017), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                                    vec4 _17135 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_17183.x, int((_289.OutputHeight - 1u) - uint(_17183.y)), 0).xy, 0);
                                                    float _17136 = _17135.x;
                                                    float _17153 = ((_17136 > 0.0) && (abs(_17136 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_17136 - _289.ProjA)) : 1000000000.0;
                                                    _27503 = _17153;
                                                    _27502 = _17017;
                                                    _27501 = abs(_17153 - _17058) < ((0.0199999995529651641845703125 * _17058) + 0.02999999932944774627685546875);
                                                    break;
                                                } while(false);
                                                _30040 = _27501 ? _16683 : _27500;
                                                _30041 = _27501 ? _27498 : _16683;
                                                _30042 = _27501 ? _27503 : _27497;
                                                bvec2 _30043 = bvec2(_27501);
                                                _30044 = vec2(_30043.x ? _27502.x : _27493.x, _30043.y ? _27502.y : _27493.y);
                                            }
                                            _27495 = _27497;
                                            _27491 = _27493;
                                            _27417 = true;
                                            _27413 = true;
                                            break;
                                        }
                                        _16707 = min(_27405 * 1.2000000476837158203125, 0.300000011920928955078125);
                                        _27405 = _16707;
                                        _27404 = _16665;
                                        _27403++;
                                        _27496 = _27408;
                                        _27492 = _27407;
                                        continue;
                                    }
                                    else
                                    {
                                        _27495 = _27496;
                                        _27491 = _27492;
                                        _27417 = _27419;
                                        _27413 = false;
                                        break;
                                    }
                                }
                                if (_27413)
                                {
                                    _27416 = _27417;
                                    break;
                                }
                                _27416 = false;
                                break;
                            } while(false);
                            if (_27416)
                            {
                                _27620 = _27495;
                                _27613 = _27491;
                                _27531 = 1u;
                                _27516 = true;
                                break;
                            }
                            _29801 = _27416;
                            _27623 = _27495;
                            _27616 = _27491;
                            int _15903 = _27396 + 1;
                            _27419 = _29801;
                            _27396 = _15903;
                            _27621 = _27623;
                            _27614 = _27616;
                            continue;
                        }
                        else
                        {
                            _27620 = _27621;
                            _27613 = _27614;
                            _27531 = _27566;
                            _27516 = false;
                            break;
                        }
                    }
                    if (_27516)
                    {
                        _27619 = _27620;
                        _27612 = _27613;
                        _27530 = _27531;
                        break;
                    }
                    _27619 = _27620;
                    _27612 = _27613;
                    _27530 = 0u;
                    break;
                } while(false);
                if (_27530 != 0u)
                {
                    float _17261 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec3 _14275 = vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w) + (((_289.CameraRight.xyz * (((_27612.x - (0.5 * float(_289.OutputWidth))) * _27619) / _17261)) + (_289.CameraUp.xyz * (((-(_27612.y - (0.5 * float(_289.OutputHeight)))) * _27619) / _17261))) + (_289.CameraForward.xyz * _27619));
                    bvec3 _14276 = bvec3(_27530 == 1u);
                    vec3 _14277 = vec3(_14276.x ? _14254.x : _14275.x, _14276.y ? _14254.y : _14275.y, _14276.z ? _14254.z : _14275.z);
                    _27940 = _27612;
                    _27884 = _27619;
                    _27828 = (_27530 == 1u) ? _27381 : length(_14277 - _6052);
                    _27771 = 1.0;
                    _27714 = _14277;
                    break;
                }
            }
            float _14299 = min(_27381, _6219);
            _27940 = _27629;
            _27884 = _27659;
            _27828 = _14299;
            _27771 = _27688;
            _27714 = _6052 + (_15548 * _14299);
            break;
        } while(false);
        vec3 _28041;
        vec3 _28042;
        do
        {
            vec2 _17498 = clamp(_27940 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
            vec3 _17509 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_17498.x, 1.0 - _17498.y), 0.0).xyz;
            if ((_27771 < 0.5) || (_27884 > 100000000.0))
            {
                _28042 = vec3(0.0, 1.0, 0.5);
                _28041 = _17509;
                break;
            }
            ivec2 _17332 = ivec2(_27940);
            float _17338 = _6052.y - _27714.y;
            if (_17338 <= 0.00999999977648258209228515625)
            {
                _28042 = vec3(0.0, 1.0, 0.5);
                _28041 = _17509;
                break;
            }
            ivec2 _17539 = clamp(_17332, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
            ivec2 _17627 = clamp(_17332, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
            vec3 _17587 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_17627.x, int((_289.OutputHeight - 1u) - uint(_17627.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
            float _17589 = length(_17587);
            vec3 _17595 = _17587 / vec3(_17589);
            bvec3 _17596 = bvec3(_17589 > 0.001000000047497451305389404296875);
            ivec2 _17687 = clamp(_17332, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
            float _17384 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_17539.x, int((_289.OutputHeight - 1u) - uint(_17539.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_17596.x ? _17595.x : vec3(0.0, 1.0, 0.0).x, _17596.y ? _17595.y : vec3(0.0, 1.0, 0.0).y, _17596.z ? _17595.z : vec3(0.0, 1.0, 0.0).z), _5754), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_17687.x, int((_289.OutputHeight - 1u) - uint(_17687.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _289.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_17509, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
            float _17388 = max(-_30012.y, 0.20000000298023223876953125);
            vec3 _17402 = mix(vec3(1.0), exp(((-_289.FluidTint.xyz) * _17338) / vec3(_17388)), vec3(_289.LightParams.x));
            float _28040;
            if ((_289.CausticParams.x > 0.0) && (_17384 > 0.0))
            {
                vec2 _17431 = _27714.xz - ((_30012.xz / vec2(_17388)) * _17338);
                vec2 _17742 = _17431 * (6.283185482025146484375 / max(_289.CausticParams.y, 0.0500000007450580596923828125));
                vec2 _17765 = _17742 + (vec2(sin((_17742.y * 0.730000019073486328125) + ((_289.ShadeParams.z * 0.89999997615814208984375) * _289.CausticParams.z)), sin((_17742.x * 0.61000001430511474609375) - ((_289.ShadeParams.z * 1.10000002384185791015625) * _289.CausticParams.z))) * 0.89999997615814208984375);
                float _28039;
                do
                {
                    if ((_289.GridParams.z < 2u) || (_289.GridParams.w < 2u))
                    {
                        _28039 = 1.0;
                        break;
                    }
                    vec2 _17875 = clamp(((_17431 - _289.PoolMin.xz) / max(_289.PoolMax.xz - _289.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_289.GridParams.z - 1u), float(_289.GridParams.w - 1u)), vec2(0.0), vec2(float(_289.GridParams.z - 1u), float(_289.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                    uvec2 _17877 = uvec2(_17875);
                    vec2 _17881 = _17875 - vec2(_17877);
                    uint _17888 = _17877.x + (_17877.y * _289.GridParams.z);
                    _28039 = mix(mix(CausticField_1._data[_17888], CausticField_1._data[_17888 + 1u], _17881.x), mix(CausticField_1._data[_17888 + _289.GridParams.z], CausticField_1._data[(_17888 + _289.GridParams.z) + 1u], _17881.x), _17881.y);
                    break;
                } while(false);
                _28040 = min(max(1.0 + ((_289.CausticParams.x * (exp((-_17338) / max(_289.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _17338))) * ((pow(clamp((1.0 - abs(sin(_17765.x + ((_289.ShadeParams.z * 0.60000002384185791015625) * _289.CausticParams.z)) * sin((_17765.y * 1.12999999523162841796875) - ((_289.ShadeParams.z * 0.4000000059604644775390625) * _289.CausticParams.z)))) * (1.0 - abs(sin(((_17765.x + _17765.y) * 0.709999978542327880859375) - ((_289.ShadeParams.z * 0.5) * _289.CausticParams.z)) * sin(((_17765.x - _17765.y) * 0.829999983310699462890625) + ((_289.ShadeParams.z * 0.699999988079071044921875) * _289.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_28039, 0.0500000007450580596923828125), _289.LightParams.y), 2.5);
            }
            else
            {
                _28040 = 1.0;
            }
            _28042 = vec3(_17384, dot(_17402, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 0.5 * _28040);
            _28041 = _17509 * ((mix(vec3(1.0), exp(((-_289.FluidTint.xyz) * _17338) * 1.2000000476837158203125), vec3(_289.LightParams.x)) * (1.0 - _17384)) + ((_17402 * _17384) * _28040));
            break;
        } while(false);
        float _6240 = mix(_6170, max(_27828, _6170), _6175);
        vec3 _29156;
        if (_6175 < 0.5)
        {
            vec3 _17945 = ((_289.CameraRight.xyz * _6200.x) + (_289.CameraUp.xyz * _6200.y)) + (_289.CameraForward.xyz * _6200.z);
            vec3 _29157;
            if (_17945.y < (-0.0199999995529651641845703125))
            {
                float _6257 = (_289.PoolMin.w - _6052.y) / _17945.y;
                vec3 _6262 = _6052 + (_17945 * _6257);
                vec3 _29158;
                if (((_6257 > _6170) && ((((_6262.x > _289.PoolMin.x) && (_6262.x < _289.PoolMax.x)) && (_6262.z > _289.PoolMin.z)) && (_6262.z < _289.PoolMax.z))) && ((_27771 < 0.5) || (_6257 < _27828)))
                {
                    float _18001 = length(_6262 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w)) / ((0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05));
                    float _18006 = _289.RippleParams.x * _289.RippleFlow.y;
                    vec2 _18008 = _6262.xz;
                    float _18140 = max(_289.RippleParams.y, 0.00999999977648258209228515625);
                    vec2 _28047;
                    _28047 = vec2(0.0);
                    SPIRV_CROSS_UNROLL
                    for (int _28046 = 0; _28046 < 6; )
                    {
                        float _18152 = _18140 * _1935[_28046];
                        uint _18240 = (((uint(_28046) * 7919u) + 17u) * 747796405u) + 2891336453u;
                        uint _18248 = ((_18240 >> ((_18240 >> 28u) + 4u)) ^ _18240) * 277803737u;
                        float _18171 = 0.60000002384185791015625 + (float(((_18248 >> 22u) ^ _18248) >> 8u) * 3.7450703871400037314742803573608e-07);
                        vec2 _18176 = vec2(cos(_18171), sin(_18171));
                        uint _18267 = (((uint(_28046) * 104729u) + 3u) * 747796405u) + 2891336453u;
                        uint _18275 = ((_18267 >> ((_18267 >> 28u) + 4u)) ^ _18267) * 277803737u;
                        _28047 += (_18176 * (((_18006 * pow(0.7799999713897705078125, float(_28046))) * clamp((_18152 / max(_18001 * 6.0, 9.9999997473787516355514526367188e-06)) - 1.0, 0.0, 1.0)) * cos(((dot(_18176, _18008) * (6.283185482025146484375 / _18152)) + ((sqrt(61.63805389404296875 / _18152) * _289.RippleParams.z) * _289.ShadeParams.z)) + (float(((_18275 >> 22u) ^ _18275) >> 8u) * 3.7450703871400037314742803573608e-07))));
                        _28046++;
                        continue;
                    }
                    vec3 _18019 = normalize(vec3(-_28047.x, 1.0, -_28047.y));
                    vec3 _18022 = _6262 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                    float _18295 = dot(_18022, _289.CameraRight.xyz);
                    float _18300 = dot(_18022, _289.CameraUp.xyz);
                    float _18305 = dot(_18022, _289.CameraForward.xyz);
                    vec3 _18306 = vec3(_18295, _18300, _18305);
                    vec3 _18026 = reflect(_17945, _18019);
                    float _18323 = dot(_18026, _289.CameraForward.xyz);
                    vec3 _18324 = vec3(dot(_18026, _289.CameraRight.xyz), dot(_18026, _289.CameraUp.xyz), _18323);
                    float _28072;
                    vec3 _28165;
                    do
                    {
                        if (_289.ReflectionDistance <= 0.0)
                        {
                            _28165 = vec3(0.0);
                            _28072 = 0.0;
                            break;
                        }
                        float _18373 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _18323);
                        if (_18373 <= 0.0)
                        {
                            _28165 = vec3(0.0);
                            _28072 = 0.0;
                            break;
                        }
                        float _18570 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _18382 = max(0.0040000001899898052215576171875 * _18305, 0.00999999977648258209228515625);
                        float _18384 = 0.5 * _18382;
                        float _18393;
                        float _18395;
                        bool _28057;
                        bool _28062;
                        float _28067;
                        float _28073;
                        vec3 _28166;
                        int _28048 = 0;
                        float _28049 = _18384;
                        float _28050 = _18382;
                        float _28068 = -1.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_28048 < 80)
                            {
                                _18393 = _28049 + _28050;
                                _18395 = _28050 * 1.07000005245208740234375;
                                if (_18393 > _289.ReflectionDistance)
                                {
                                    _28166 = vec3(0.0);
                                    _28073 = 0.0;
                                    _28067 = _28068;
                                    _28062 = false;
                                    _28057 = true;
                                    break;
                                }
                                vec3 _18406 = _18306 + (_18324 * _18393);
                                if (_18406.z <= 0.0500000007450580596923828125)
                                {
                                    _28166 = vec3(0.0);
                                    _28073 = 0.0;
                                    _28067 = _28068;
                                    _28062 = false;
                                    _28057 = true;
                                    break;
                                }
                                float _18610 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _18586 = ((_18406.x * _18610) / _18406.z) + (0.5 * float(_289.OutputWidth));
                                float _18599 = (((-_18406.y) * _18610) / _18406.z) + (0.5 * float(_289.OutputHeight));
                                if (!((((_18586 >= 0.0) && (_18599 >= 0.0)) && (_18586 < float(_289.OutputWidth))) && (_18599 < float(_289.OutputHeight))))
                                {
                                    _28166 = vec3(0.0);
                                    _28073 = _27802;
                                    _28067 = _28068;
                                    _28062 = true;
                                    _28057 = false;
                                    break;
                                }
                                ivec2 _18694 = clamp(ivec2(vec2(_18586, _18599)), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                vec4 _18646 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18694.x, int((_289.OutputHeight - 1u) - uint(_18694.y)), 0).xy, 0);
                                float _18647 = _18646.x;
                                float _18664 = ((_18647 > 0.0) && (abs(_18647 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_18647 - _289.ProjA)) : 1000000000.0;
                                if (((_18664 < 100000000.0) && (_18406.z > _18664)) && ((_18406.z - _18664) < max(_28050 * 2.1400001049041748046875, 0.25)))
                                {
                                    float _28052;
                                    _28052 = _18393;
                                    float _30045;
                                    float _30046;
                                    int _28051 = 0;
                                    float _28055 = _28049;
                                    SPIRV_CROSS_UNROLL
                                    for (; _28051 < 6; _28055 = _30045, _28052 = _30046, _28051++)
                                    {
                                        float _18450 = 0.5 * (_28055 + _28052);
                                        vec3 _18455 = _18306 + (_18324 * _18450);
                                        float _18764 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _18825 = clamp(ivec2(vec2(((_18455.x * _18764) / _18455.z) + (0.5 * float(_289.OutputWidth)), (((-_18455.y) * _18764) / _18455.z) + (0.5 * float(_289.OutputHeight)))), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                        vec4 _18777 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_18825.x, int((_289.OutputHeight - 1u) - uint(_18825.y)), 0).xy, 0);
                                        float _18778 = _18777.x;
                                        bool _18462 = _18455.z > (((_18778 > 0.0) && (abs(_18778 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_18778 - _289.ProjA)) : 1000000000.0);
                                        _30045 = _18462 ? _28055 : _18450;
                                        _30046 = _18462 ? _18450 : _28052;
                                    }
                                    vec3 _18476 = _18306 + (_18324 * _28052);
                                    float _18895 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _18885 = vec2(((_18476.x * _18895) / _18476.z) + (0.5 * float(_289.OutputWidth)), (((-_18476.y) * _18895) / _18476.z) + (0.5 * float(_289.OutputHeight)));
                                    ivec2 _18953 = clamp(ivec2(_18885), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                    vec3 _18913 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_18953.x, int((_289.OutputHeight - 1u) - uint(_18953.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                                    float _18915 = length(_18913);
                                    vec3 _18921 = _18913 / vec3(_18915);
                                    bvec3 _18922 = bvec3(_18915 > 0.001000000047497451305389404296875);
                                    vec3 _18923 = vec3(_18922.x ? _18921.x : vec3(0.0, 1.0, 0.0).x, _18922.y ? _18921.y : vec3(0.0, 1.0, 0.0).y, _18922.z ? _18921.z : vec3(0.0, 1.0, 0.0).z);
                                    if (dot(vec3(dot(_18923, _289.CameraRight.xyz), dot(_18923, _289.CameraUp.xyz), dot(_18923, _289.CameraForward.xyz)), _18324) > 0.100000001490116119384765625)
                                    {
                                        int _18503 = _28048 + 1;
                                        _28050 = _18395;
                                        _28049 = _18393;
                                        _28048 = _18503;
                                        _28068 = _18393;
                                        continue;
                                    }
                                    float _19017 = clamp(((0.0199999995529651641845703125 * _28052) * _18570) / max(_18476.z, 0.0500000007450580596923828125), 0.0, 12.0);
                                    float _19019 = 0.5 * _19017;
                                    vec2 _19104 = clamp(_18885 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19130 = clamp((_18885 + vec2(_19019)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19156 = clamp((_18885 + vec2(_19017 * (-0.5), _19019)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19182 = clamp((_18885 + vec2(_19019, _19017 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19208 = clamp((_18885 + vec2(_19017 * (-0.5), _19017 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19234 = clamp((_18885 + vec2(_19017, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19260 = clamp((_18885 + vec2(-_19017, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19286 = clamp((_18885 + vec2(0.0, _19017)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec2 _19312 = clamp((_18885 + vec2(0.0, -_19017)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                                    vec3 _19089 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19104.x, 1.0 - _19104.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19130.x, 1.0 - _19130.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19156.x, 1.0 - _19156.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19182.x, 1.0 - _19182.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19208.x, 1.0 - _19208.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19234.x, 1.0 - _19234.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19260.x, 1.0 - _19260.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19286.x, 1.0 - _19286.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19312.x, 1.0 - _19312.y), 0.0).xyz) * 0.100000001490116119384765625;
                                    _28166 = _19089;
                                    _28073 = _18373;
                                    _28067 = _18393;
                                    _28062 = false;
                                    _28057 = true;
                                    break;
                                }
                                int _18503 = _28048 + 1;
                                _28050 = _18395;
                                _28049 = _18393;
                                _28048 = _18503;
                                _28068 = _18393;
                                continue;
                            }
                            else
                            {
                                _28166 = vec3(0.0);
                                _28073 = _27802;
                                _28067 = _28068;
                                _28062 = false;
                                _28057 = false;
                                break;
                            }
                        }
                        if (_28057)
                        {
                            _28165 = _28166;
                            _28072 = _28073;
                            break;
                        }
                        if ((!_28062) || (_28067 < 0.0))
                        {
                            _28165 = _28166;
                            _28072 = 0.0;
                            break;
                        }
                        vec3 _18518 = _18306 + (_18324 * _28067);
                        float _19363 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        vec2 _19353 = vec2(((_18518.x * _19363) / _18518.z) + (0.5 * float(_289.OutputWidth)), (((-_18518.y) * _19363) / _18518.z) + (0.5 * float(_289.OutputHeight)));
                        ivec2 _19424 = clamp(ivec2(_19353), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec4 _19376 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_19424.x, int((_289.OutputHeight - 1u) - uint(_19424.y)), 0).xy, 0);
                        float _19377 = _19376.x;
                        float _19394 = ((_19377 > 0.0) && (abs(_19377 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_19377 - _289.ProjA)) : 1000000000.0;
                        float _19470 = clamp(((0.0199999995529651641845703125 * _28067) * _18570) / max(_18518.z, 0.0500000007450580596923828125), 0.0, 12.0);
                        float _19472 = 0.5 * _19470;
                        vec2 _19557 = clamp(_19353 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19583 = clamp((_19353 + vec2(_19472)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19609 = clamp((_19353 + vec2(_19470 * (-0.5), _19472)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19635 = clamp((_19353 + vec2(_19472, _19470 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19661 = clamp((_19353 + vec2(_19470 * (-0.5), _19470 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19687 = clamp((_19353 + vec2(_19470, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19713 = clamp((_19353 + vec2(-_19470, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19739 = clamp((_19353 + vec2(0.0, _19470)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec2 _19765 = clamp((_19353 + vec2(0.0, -_19470)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _19542 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19557.x, 1.0 - _19557.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19583.x, 1.0 - _19583.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19609.x, 1.0 - _19609.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19635.x, 1.0 - _19635.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19661.x, 1.0 - _19661.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19687.x, 1.0 - _19687.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19713.x, 1.0 - _19713.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19739.x, 1.0 - _19739.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_19765.x, 1.0 - _19765.y), 0.0).xyz) * 0.100000001490116119384765625;
                        if (_19394 > 100000000.0)
                        {
                            _28165 = _19542;
                            _28072 = _18373;
                            break;
                        }
                        float _18543 = _19394 - _18518.z;
                        if (_18543 <= 0.0)
                        {
                            _28165 = _19542;
                            _28072 = _18373;
                            break;
                        }
                        _28165 = _19542;
                        _28072 = _18373 * exp((_18543 / max(_18323, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                        break;
                    } while(false);
                    vec3 _28171;
                    do
                    {
                        if (_289.EnvParams.x <= 0.0)
                        {
                            float _19819 = _18026.y;
                            _28171 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _19819))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _19819)));
                            break;
                        }
                        _28171 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_18026.x, _18026.y, -_18026.z), 0.0).xyz * _289.EnvParams.x;
                        break;
                    } while(false);
                    vec3 _18040 = refract(_17945, _18019, 0.75187969207763671875);
                    bvec3 _30047 = bvec3(dot(_18040, _18040) < 9.9999999747524270787835121154785e-07);
                    vec3 _30048 = vec3(_30047.x ? _17945.x : _18040.x, _30047.y ? _17945.y : _18040.y, _30047.z ? _17945.z : _18040.z);
                    float _19845 = dot(_30048, _289.CameraRight.xyz);
                    float _19850 = dot(_30048, _289.CameraUp.xyz);
                    float _19855 = dot(_30048, _289.CameraForward.xyz);
                    vec3 _19856 = vec3(_19845, _19850, _19855);
                    float _19896 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                    vec2 _19886 = vec2(((_18295 * _19896) / _18305) + (0.5 * float(_289.OutputWidth)), (((-_18300) * _19896) / _18305) + (0.5 * float(_289.OutputHeight)));
                    vec3 _28612;
                    float _28711;
                    float _28810;
                    float _28909;
                    vec2 _29008;
                    do
                    {
                        ivec2 _20227 = clamp(ivec2(_19886), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec4 _20179 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20227.x, int((_289.OutputHeight - 1u) - uint(_20227.y)), 0).xy, 0);
                        float _20180 = _20179.x;
                        vec2 _28527;
                        float _28557;
                        _28557 = ((_20180 > 0.0) && (abs(_20180 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_20180 - _289.ProjA)) : 1000000000.0;
                        _28527 = _19886;
                        float _19975;
                        float _30051;
                        vec2 _30053;
                        bool _28180;
                        float _28586;
                        vec3 _28613;
                        float _28712;
                        float _28811;
                        float _28910;
                        vec2 _29009;
                        int _28173 = 1;
                        float _28175 = 0.0;
                        SPIRV_CROSS_LOOP
                        for (;;)
                        {
                            if (_28173 <= 16)
                            {
                                float _19968 = float(_28173);
                                _19975 = (_19968 * 0.25) * sqrt(_19968 * 0.0625);
                                vec3 _19980 = _18306 + (_19856 * _19975);
                                if (_19980.z <= 0.0500000007450580596923828125)
                                {
                                    _29009 = _27971;
                                    _28910 = _27802;
                                    _28811 = _27802;
                                    _28712 = _27802;
                                    _28613 = _27745;
                                    _28586 = 0.0;
                                    _28180 = false;
                                    break;
                                }
                                float _20297 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                float _20273 = ((_19980.x * _20297) / _19980.z) + (0.5 * float(_289.OutputWidth));
                                float _20286 = (((-_19980.y) * _20297) / _19980.z) + (0.5 * float(_289.OutputHeight));
                                vec2 _20287 = vec2(_20273, _20286);
                                if (!((((_20273 >= 0.0) && (_20286 >= 0.0)) && (_20273 < float(_289.OutputWidth))) && (_20286 < float(_289.OutputHeight))))
                                {
                                    _29009 = _27971;
                                    _28910 = _27802;
                                    _28811 = _27802;
                                    _28712 = _27802;
                                    _28613 = _27745;
                                    _28586 = 0.0;
                                    _28180 = false;
                                    break;
                                }
                                ivec2 _20381 = clamp(ivec2(_20287), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                vec4 _20333 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20381.x, int((_289.OutputHeight - 1u) - uint(_20381.y)), 0).xy, 0);
                                float _20334 = _20333.x;
                                float _20351 = ((_20334 > 0.0) && (abs(_20334 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_20334 - _289.ProjA)) : 1000000000.0;
                                if (_19980.z >= _20351)
                                {
                                    ivec2 _20002 = ivec2(_20287);
                                    ivec2 _20462 = clamp(ivec2(floor(((vec2(_20002) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20471 = clamp(_20462, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    vec4 _20497 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_20471.x, int((FluidSize.y - 1u) - uint(_20471.y)), 0).xy, 0);
                                    float _20498 = _20497.x;
                                    ivec2 _20507 = clamp(_20462, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                    ivec2 _20594 = clamp(_20002, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                    vec4 _20546 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20594.x, int((_289.OutputHeight - 1u) - uint(_20594.y)), 0).xy, 0);
                                    float _20547 = _20546.x;
                                    if (!(((_20498 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_20507.x, int((FluidSize.y - 1u) - uint(_20507.y)), 0).xy, 0).w > 0.5)) && ((((_20547 > 0.0) && (abs(_20547 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_20547 - _289.ProjA)) : 1000000000.0) >= (_20498 + 0.0500000007450580596923828125))))
                                    {
                                        _29009 = _27971;
                                        _28910 = _27802;
                                        _28811 = _27802;
                                        _28712 = _27802;
                                        _28613 = _27745;
                                        _28586 = 0.0;
                                        _28180 = false;
                                        break;
                                    }
                                    float _28177;
                                    _28177 = _19975;
                                    float _30049;
                                    float _30050;
                                    int _28176 = 0;
                                    float _28178 = _28175;
                                    SPIRV_CROSS_UNROLL
                                    for (; _28176 < 5; _28178 = _30049, _28177 = _30050, _28176++)
                                    {
                                        float _20017 = 0.5 * (_28178 + _28177);
                                        vec3 _20022 = _18306 + (_19856 * _20017);
                                        float _20664 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                        ivec2 _20725 = clamp(ivec2(vec2(((_20022.x * _20664) / _20022.z) + (0.5 * float(_289.OutputWidth)), (((-_20022.y) * _20664) / _20022.z) + (0.5 * float(_289.OutputHeight)))), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                        vec4 _20677 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20725.x, int((_289.OutputHeight - 1u) - uint(_20725.y)), 0).xy, 0);
                                        float _20678 = _20677.x;
                                        bool _20029 = _20022.z >= (((_20678 > 0.0) && (abs(_20678 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_20678 - _289.ProjA)) : 1000000000.0);
                                        _30049 = _20029 ? _28178 : _20017;
                                        _30050 = _20029 ? _20017 : _28177;
                                    }
                                    vec3 _20043 = _18306 + (_19856 * _28177);
                                    float _20795 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    vec2 _20054 = clamp(vec2(((_20043.x * _20795) / _20043.z) + (0.5 * float(_289.OutputWidth)), (((-_20043.y) * _20795) / _20043.z) + (0.5 * float(_289.OutputHeight))), vec2(0.5), vec2(float(_289.OutputWidth), float(_289.OutputHeight)) - vec2(0.5));
                                    ivec2 _20856 = clamp(ivec2(_20054), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                    vec4 _20808 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_20856.x, int((_289.OutputHeight - 1u) - uint(_20856.y)), 0).xy, 0);
                                    float _20809 = _20808.x;
                                    float _20826 = ((_20809 > 0.0) && (abs(_20809 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_20809 - _289.ProjA)) : 1000000000.0;
                                    float _20934 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    _29009 = _20054;
                                    _28910 = _20826;
                                    _28811 = _28177;
                                    _28712 = 1.0;
                                    _28613 = vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w) + (((_289.CameraRight.xyz * (((_20054.x - (0.5 * float(_289.OutputWidth))) * _20826) / _20934)) + (_289.CameraUp.xyz * (((-(_20054.y - (0.5 * float(_289.OutputHeight)))) * _20826) / _20934))) + (_289.CameraForward.xyz * _20826));
                                    _28586 = 1.0;
                                    _28180 = true;
                                    break;
                                }
                                ivec2 _20078 = ivec2(_20287 - vec2(0.5));
                                ivec2 _21007 = clamp(ivec2(floor(((vec2(_20078) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21016 = clamp(_21007, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _21042 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_21016.x, int((FluidSize.y - 1u) - uint(_21016.y)), 0).xy, 0);
                                float _21043 = _21042.x;
                                ivec2 _21052 = clamp(_21007, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21139 = clamp(_20078, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                vec4 _21091 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21139.x, int((_289.OutputHeight - 1u) - uint(_21139.y)), 0).xy, 0);
                                float _21092 = _21091.x;
                                ivec2 _20083 = ivec2(_20287 + vec2(0.5));
                                ivec2 _21220 = clamp(ivec2(floor(((vec2(_20083) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21229 = clamp(_21220, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                vec4 _21255 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_21229.x, int((FluidSize.y - 1u) - uint(_21229.y)), 0).xy, 0);
                                float _21256 = _21255.x;
                                ivec2 _21265 = clamp(_21220, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                ivec2 _21352 = clamp(_20083, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                vec4 _21304 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21352.x, int((_289.OutputHeight - 1u) - uint(_21352.y)), 0).xy, 0);
                                float _21305 = _21304.x;
                                bool _20085 = (((_21043 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_21052.x, int((FluidSize.y - 1u) - uint(_21052.y)), 0).xy, 0).w > 0.5)) && ((((_21092 > 0.0) && (abs(_21092 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_21092 - _289.ProjA)) : 1000000000.0) >= (_21043 + 0.0500000007450580596923828125))) && (((_21256 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_21265.x, int((FluidSize.y - 1u) - uint(_21265.y)), 0).xy, 0).w > 0.5)) && ((((_21305 > 0.0) && (abs(_21305 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_21305 - _289.ProjA)) : 1000000000.0) >= (_21256 + 0.0500000007450580596923828125)));
                                _30051 = _20085 ? _20351 : _28557;
                                bvec2 _30052 = bvec2(_20085);
                                _30053 = vec2(_30052.x ? _20287.x : _28527.x, _30052.y ? _20287.y : _28527.y);
                                _28173++;
                                _28557 = _30051;
                                _28527 = _30053;
                                _28175 = _19975;
                                continue;
                            }
                            else
                            {
                                _29009 = _27971;
                                _28910 = _27802;
                                _28811 = _27802;
                                _28712 = _27802;
                                _28613 = _27745;
                                _28586 = 0.0;
                                _28180 = false;
                                break;
                            }
                        }
                        if (_28180)
                        {
                            _29008 = _29009;
                            _28909 = _28910;
                            _28810 = _28811;
                            _28711 = _28712;
                            _28612 = _28613;
                            break;
                        }
                        vec3 _21404 = ((_289.CameraRight.xyz * _19845) + (_289.CameraUp.xyz * _19850)) + (_289.CameraForward.xyz * _19855);
                        float _28195;
                        vec3 _28196;
                        do
                        {
                            if ((((_6262.x < _289.PoolMin.x) || (_6262.x > _289.PoolMax.x)) || (_6262.z < _289.PoolMin.z)) || (_6262.z > _289.PoolMax.z))
                            {
                                _28196 = vec3(0.0, 1.0, 0.0);
                                _28195 = 1000.0;
                                break;
                            }
                            float _28183;
                            if (_21404.x > 9.9999997473787516355514526367188e-05)
                            {
                                _28183 = (_289.PoolMax.x - _6262.x) / _21404.x;
                            }
                            else
                            {
                                float _28184;
                                if (_21404.x < (-9.9999997473787516355514526367188e-05))
                                {
                                    _28184 = (_289.PoolMin.x - _6262.x) / _21404.x;
                                }
                                else
                                {
                                    _28184 = 1000.0;
                                }
                                _28183 = _28184;
                            }
                            bool _21466 = _28183 < 1000.0;
                            vec3 _28200;
                            if (_21466)
                            {
                                _28200 = vec3((_21404.x > 0.0) ? (-1.0) : 1.0, 0.0, 0.0);
                            }
                            else
                            {
                                _28200 = vec3(0.0, 1.0, 0.0);
                            }
                            float _30054 = _21466 ? _28183 : 1000.0;
                            float _28187;
                            if (_21404.z > 9.9999997473787516355514526367188e-05)
                            {
                                _28187 = (_289.PoolMax.z - _6262.z) / _21404.z;
                            }
                            else
                            {
                                float _28188;
                                if (_21404.z < (-9.9999997473787516355514526367188e-05))
                                {
                                    _28188 = (_289.PoolMin.z - _6262.z) / _21404.z;
                                }
                                else
                                {
                                    _28188 = 1000.0;
                                }
                                _28187 = _28188;
                            }
                            bool _21504 = _28187 < _30054;
                            vec3 _28198;
                            if (_21504)
                            {
                                _28198 = vec3(0.0, 0.0, _30073);
                            }
                            else
                            {
                                _28198 = _28200;
                            }
                            float _30055 = _21504 ? _28187 : _30054;
                            float _28193;
                            vec3 _28197;
                            if (_21404.y < (-9.9999997473787516355514526367188e-05))
                            {
                                float _21524 = (_289.PoolMin.y - _6262.y) / _21404.y;
                                bool _21527 = _21524 < _30055;
                                bvec3 _30056 = bvec3(_21527);
                                _28197 = vec3(_30056.x ? vec3(0.0, 1.0, 0.0).x : _28198.x, _30056.y ? vec3(0.0, 1.0, 0.0).y : _28198.y, _30056.z ? vec3(0.0, 1.0, 0.0).z : _28198.z);
                                _28193 = _21527 ? _21524 : _30055;
                            }
                            else
                            {
                                _28197 = _28198;
                                _28193 = _30055;
                            }
                            _28196 = _28197;
                            _28195 = max(_28193, 0.0);
                            break;
                        } while(false);
                        if (_28195 < 100.0)
                        {
                            vec3 _20110 = _6262 + (_21404 * _28195);
                            uint _28386;
                            vec2 _28510;
                            float _28517;
                            do
                            {
                                bool _28205;
                                vec2 _28206;
                                float _28207;
                                do
                                {
                                    vec3 _21778 = _20110 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                    float _21836 = dot(_21778, _289.CameraForward.xyz);
                                    if (_21836 <= 0.0500000007450580596923828125)
                                    {
                                        _28207 = 1000000000.0;
                                        _28206 = vec2(0.5);
                                        _28205 = false;
                                        break;
                                    }
                                    float _21877 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                    float _21853 = ((dot(_21778, _289.CameraRight.xyz) * _21877) / _21836) + (0.5 * float(_289.OutputWidth));
                                    float _21866 = (((-dot(_21778, _289.CameraUp.xyz)) * _21877) / _21836) + (0.5 * float(_289.OutputHeight));
                                    if (!((((_21853 >= 0.0) && (_21866 >= 0.0)) && (_21853 < float(_289.OutputWidth))) && (_21866 < float(_289.OutputHeight))))
                                    {
                                        _28207 = 1000000000.0;
                                        _28206 = vec2(0.5);
                                        _28205 = false;
                                        break;
                                    }
                                    vec2 _21795 = floor(vec2(_21853, _21866)) + vec2(0.5);
                                    ivec2 _21961 = clamp(ivec2(_21795), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                    vec4 _21913 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_21961.x, int((_289.OutputHeight - 1u) - uint(_21961.y)), 0).xy, 0);
                                    float _21914 = _21913.x;
                                    float _21931 = ((_21914 > 0.0) && (abs(_21914 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_21914 - _289.ProjA)) : 1000000000.0;
                                    _28207 = _21931;
                                    _28206 = _21795;
                                    _28205 = abs(_21931 - _21836) < ((0.0199999995529651641845703125 * _21836) + 0.02999999932944774627685546875);
                                    break;
                                } while(false);
                                if (_28205)
                                {
                                    _28517 = _28207;
                                    _28510 = _28206;
                                    _28386 = 1u;
                                    break;
                                }
                                vec3 _21584 = _20110 - vec3(_289.CameraRight.w, _27234, _289.CameraForward.w);
                                vec3 _21592 = (_289.PoolMin.xyz + _289.PoolMax.xyz) * 0.5;
                                vec3 _21597 = vec3((_21584.x >= 0.0) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21602 = vec3(0.0, 0.0, (_21584.z >= 0.0) ? 1.0 : (-1.0));
                                vec3 _21609 = vec3((_21592.x >= _20110.x) ? 1.0 : (-1.0), 0.0, 0.0);
                                vec3 _21616 = vec3(0.0, 0.0, (_21592.z >= _20110.z) ? 1.0 : (-1.0));
                                vec3 _28208;
                                vec3 _28209;
                                vec3 _28214;
                                vec3 _28216;
                                if (abs(_28196.y) > 0.5)
                                {
                                    bool _21628 = abs(_21584.z) >= abs(_21584.x);
                                    bvec3 _21632 = bvec3(_21628);
                                    bvec3 _21637 = bvec3(_21628);
                                    bvec3 _21642 = bvec3(_21628);
                                    bvec3 _21647 = bvec3(_21628);
                                    _28216 = vec3(_21632.x ? _21602.x : _21597.x, _21632.y ? _21602.y : _21597.y, _21632.z ? _21602.z : _21597.z);
                                    _28214 = vec3(_21642.x ? _21597.x : _21602.x, _21642.y ? _21597.y : _21602.y, _21642.z ? _21597.z : _21602.z);
                                    _28209 = vec3(_21647.x ? _21616.x : _21609.x, _21647.y ? _21616.y : _21609.y, _21647.z ? _21616.z : _21609.z);
                                    _28208 = vec3(_21637.x ? _21609.x : _21616.x, _21637.y ? _21609.y : _21616.y, _21637.z ? _21609.z : _21616.z);
                                }
                                else
                                {
                                    bool _21653 = abs(_28196.x) > 0.5;
                                    bvec3 _21657 = bvec3(_21653);
                                    bvec3 _21662 = bvec3(_21653);
                                    _28216 = vec3(_21657.x ? _21602.x : _21597.x, _21657.y ? _21602.y : _21597.y, _21657.z ? _21602.z : _21597.z);
                                    _28214 = vec3(0.0, 1.0, 0.0);
                                    _28209 = vec3(_21662.x ? _21616.x : _21609.x, _21662.y ? _21616.y : _21609.y, _21662.z ? _21616.z : _21609.z);
                                    _28208 = vec3(0.0, 1.0, 0.0);
                                }
                                float _22044 = ((_28208.y > 0.5) ? (_289.PoolMin.w - _20110.y) : ((_28208.x > 0.5) ? (_289.PoolMax.x - _20110.x) : ((_28208.x < (-0.5)) ? (_20110.x - _289.PoolMin.x) : ((_28208.z > 0.5) ? (_289.PoolMax.z - _20110.z) : (_20110.z - _289.PoolMin.z))))) - 0.02999999932944774627685546875;
                                float _22088 = ((_28209.y > 0.5) ? (_289.PoolMin.w - _20110.y) : ((_28209.x > 0.5) ? (_289.PoolMax.x - _20110.x) : ((_28209.x < (-0.5)) ? (_20110.x - _289.PoolMin.x) : ((_28209.z > 0.5) ? (_289.PoolMax.z - _20110.z) : (_20110.z - _289.PoolMin.z))))) - 0.02999999932944774627685546875;
                                vec2 _28512;
                                _28512 = _28206;
                                bool _28372;
                                uint _28387;
                                vec2 _28511;
                                vec2 _28514;
                                float _28518;
                                float _28521;
                                bool _29911;
                                int _28210 = 0;
                                bool _28233;
                                float _28519 = _28207;
                                SPIRV_CROSS_LOOP
                                for (;;)
                                {
                                    if (_28210 < 11)
                                    {
                                        if (_28210 == 1)
                                        {
                                            vec3 _21681 = _20110 - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                            float _22114 = dot(_21681, _289.CameraForward.xyz);
                                            vec2 _28515;
                                            float _28522;
                                            if (_22114 > 0.0500000007450580596923828125)
                                            {
                                                float _22155 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                float _22131 = ((dot(_21681, _289.CameraRight.xyz) * _22155) / _22114) + (0.5 * float(_289.OutputWidth));
                                                float _22144 = (((-dot(_21681, _289.CameraUp.xyz)) * _22155) / _22114) + (0.5 * float(_289.OutputHeight));
                                                vec2 _22145 = vec2(_22131, _22144);
                                                ivec2 _21692 = ivec2(_22145);
                                                ivec2 _22229 = clamp(ivec2(floor(((vec2(_21692) + vec2(0.5)) * vec2(FluidSize)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)))), ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _22238 = clamp(_22229, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                vec4 _22264 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_22238.x, int((FluidSize.y - 1u) - uint(_22238.y)), 0).xy, 0);
                                                float _22265 = _22264.x;
                                                ivec2 _22274 = clamp(_22229, ivec2(0), ivec2(FluidSize) - ivec2(1));
                                                ivec2 _22361 = clamp(_21692, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                                vec4 _22313 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22361.x, int((_289.OutputHeight - 1u) - uint(_22361.y)), 0).xy, 0);
                                                float _22314 = _22313.x;
                                                vec2 _28516;
                                                float _28523;
                                                if (((((_22131 >= 0.0) && (_22144 >= 0.0)) && (_22131 < float(_289.OutputWidth))) && (_22144 < float(_289.OutputHeight))) && (((_22265 > 0.0) && (texelFetch(SPIRV_Cross_CombinedFluidThicknessSPIRV_Cross_DummySampler, ivec3(_22274.x, int((FluidSize.y - 1u) - uint(_22274.y)), 0).xy, 0).w > 0.5)) && ((((_22314 > 0.0) && (abs(_22314 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_22314 - _289.ProjA)) : 1000000000.0) >= (_22265 + 0.0500000007450580596923828125))))
                                                {
                                                    vec2 _21699 = floor(_22145) + vec2(0.5);
                                                    ivec2 _22452 = clamp(ivec2(_21699), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                                    vec4 _22404 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22452.x, int((_289.OutputHeight - 1u) - uint(_22452.y)), 0).xy, 0);
                                                    float _22405 = _22404.x;
                                                    float _22422 = ((_22405 > 0.0) && (abs(_22405 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_22405 - _289.ProjA)) : 1000000000.0;
                                                    if (_22422 < _22114)
                                                    {
                                                        _28518 = _22422;
                                                        _28511 = _21699;
                                                        _28387 = 2u;
                                                        _28372 = true;
                                                        break;
                                                    }
                                                    _28523 = _22422;
                                                    _28516 = _21699;
                                                }
                                                else
                                                {
                                                    _28523 = _28519;
                                                    _28516 = _28512;
                                                }
                                                _28522 = _28523;
                                                _28515 = _28516;
                                            }
                                            else
                                            {
                                                _28522 = _28519;
                                                _28515 = _28512;
                                            }
                                            _29911 = _28233;
                                            _28521 = _28522;
                                            _28514 = _28515;
                                            int _21759 = _28210 + 1;
                                            _28233 = _29911;
                                            _28210 = _21759;
                                            _28519 = _28521;
                                            _28512 = _28514;
                                            continue;
                                        }
                                        bool _21713 = _28210 == 2;
                                        bool _21715 = _28210 >= 3;
                                        int _21717 = _28210 & 1;
                                        bool _21720 = _21713 || (_21715 && (_21717 == 0));
                                        bool _21722 = _28210 < 3;
                                        int _21724 = _28210 - 3;
                                        float _21729 = _21722 ? 0.0 : (0.4000000059604644775390625 * exp2(float(_21724 / 2)));
                                        if (_21729 >= (_21720 ? _22088 : _22044))
                                        {
                                            _29911 = _28233;
                                            _28521 = _28519;
                                            _28514 = _28512;
                                            int _21759 = _28210 + 1;
                                            _28233 = _29911;
                                            _28210 = _21759;
                                            _28519 = _28521;
                                            _28512 = _28514;
                                            continue;
                                        }
                                        bvec3 _21742 = bvec3(_21720);
                                        vec3 _21746 = _20110 + (vec3(_21742.x ? _28209.x : _28208.x, _21742.y ? _28209.y : _28208.y, _21742.z ? _28209.z : _28208.z) * _21729);
                                        bvec3 _21750 = bvec3(_21720);
                                        vec3 _21751 = vec3(_21750.x ? _28214.x : _28216.x, _21750.y ? _28214.y : _28216.y, _21750.z ? _28214.z : _28216.z);
                                        bool _28230;
                                        vec2 _28347;
                                        float _28351;
                                        do
                                        {
                                            float _22615 = ((_21751.y > 0.5) ? (_289.PoolMin.w - _21746.y) : ((_21751.x > 0.5) ? (_289.PoolMax.x - _21746.x) : ((_21751.x < (-0.5)) ? (_21746.x - _289.PoolMin.x) : ((_21751.z > 0.5) ? (_289.PoolMax.z - _21746.z) : (_21746.z - _289.PoolMin.z))))) - 0.02999999932944774627685546875;
                                            vec2 _28348;
                                            _28348 = vec2(0.5);
                                            float _22521;
                                            float _22563;
                                            vec2 _28221;
                                            float _28222;
                                            bool _28227;
                                            bool _28231;
                                            int _28217 = 0;
                                            float _28218 = 0.0;
                                            float _28219 = 0.0599999986588954925537109375;
                                            float _28352 = 1000000000.0;
                                            SPIRV_CROSS_LOOP
                                            for (;;)
                                            {
                                                if ((_28217 < 40) && (_28218 < _22615))
                                                {
                                                    _22521 = min(_28218 + _28219, _22615);
                                                    bool _28220;
                                                    do
                                                    {
                                                        vec3 _22629 = (_21746 + (_21751 * _22521)) - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                                        float _22687 = dot(_22629, _289.CameraForward.xyz);
                                                        if (_22687 <= 0.0500000007450580596923828125)
                                                        {
                                                            _28222 = 1000000000.0;
                                                            _28221 = vec2(0.5);
                                                            _28220 = false;
                                                            break;
                                                        }
                                                        float _22728 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                        float _22704 = ((dot(_22629, _289.CameraRight.xyz) * _22728) / _22687) + (0.5 * float(_289.OutputWidth));
                                                        float _22717 = (((-dot(_22629, _289.CameraUp.xyz)) * _22728) / _22687) + (0.5 * float(_289.OutputHeight));
                                                        if (!((((_22704 >= 0.0) && (_22717 >= 0.0)) && (_22704 < float(_289.OutputWidth))) && (_22717 < float(_289.OutputHeight))))
                                                        {
                                                            _28222 = 1000000000.0;
                                                            _28221 = vec2(0.5);
                                                            _28220 = false;
                                                            break;
                                                        }
                                                        vec2 _22646 = floor(vec2(_22704, _22717)) + vec2(0.5);
                                                        ivec2 _22812 = clamp(ivec2(_22646), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                                        vec4 _22764 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_22812.x, int((_289.OutputHeight - 1u) - uint(_22812.y)), 0).xy, 0);
                                                        float _22765 = _22764.x;
                                                        float _22782 = ((_22765 > 0.0) && (abs(_22765 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_22765 - _289.ProjA)) : 1000000000.0;
                                                        _28222 = _22782;
                                                        _28221 = _22646;
                                                        _28220 = abs(_22782 - _22687) < ((0.0199999995529651641845703125 * _22687) + 0.02999999932944774627685546875);
                                                        break;
                                                    } while(false);
                                                    if (_28220)
                                                    {
                                                        vec2 _28349;
                                                        float _28353;
                                                        _28353 = _28222;
                                                        _28349 = _28221;
                                                        float _30059;
                                                        float _30060;
                                                        float _30061;
                                                        vec2 _30063;
                                                        int _28226 = 0;
                                                        float _28354 = _28218;
                                                        float _28356 = _22521;
                                                        SPIRV_CROSS_UNROLL
                                                        for (; _28226 < 4; _28356 = _30059, _28354 = _30060, _28353 = _30061, _28349 = _30063, _28226++)
                                                        {
                                                            float _22539 = 0.5 * (_28354 + _28356);
                                                            bool _28357;
                                                            vec2 _28358;
                                                            float _28359;
                                                            do
                                                            {
                                                                vec3 _22856 = (_21746 + (_21751 * _22539)) - vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w);
                                                                float _22914 = dot(_22856, _289.CameraForward.xyz);
                                                                if (_22914 <= 0.0500000007450580596923828125)
                                                                {
                                                                    _28359 = 1000000000.0;
                                                                    _28358 = vec2(0.5);
                                                                    _28357 = false;
                                                                    break;
                                                                }
                                                                float _22955 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                                                float _22931 = ((dot(_22856, _289.CameraRight.xyz) * _22955) / _22914) + (0.5 * float(_289.OutputWidth));
                                                                float _22944 = (((-dot(_22856, _289.CameraUp.xyz)) * _22955) / _22914) + (0.5 * float(_289.OutputHeight));
                                                                if (!((((_22931 >= 0.0) && (_22944 >= 0.0)) && (_22931 < float(_289.OutputWidth))) && (_22944 < float(_289.OutputHeight))))
                                                                {
                                                                    _28359 = 1000000000.0;
                                                                    _28358 = vec2(0.5);
                                                                    _28357 = false;
                                                                    break;
                                                                }
                                                                vec2 _22873 = floor(vec2(_22931, _22944)) + vec2(0.5);
                                                                ivec2 _23039 = clamp(ivec2(_22873), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                                                vec4 _22991 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_23039.x, int((_289.OutputHeight - 1u) - uint(_23039.y)), 0).xy, 0);
                                                                float _22992 = _22991.x;
                                                                float _23009 = ((_22992 > 0.0) && (abs(_22992 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_22992 - _289.ProjA)) : 1000000000.0;
                                                                _28359 = _23009;
                                                                _28358 = _22873;
                                                                _28357 = abs(_23009 - _22914) < ((0.0199999995529651641845703125 * _22914) + 0.02999999932944774627685546875);
                                                                break;
                                                            } while(false);
                                                            _30059 = _28357 ? _22539 : _28356;
                                                            _30060 = _28357 ? _28354 : _22539;
                                                            _30061 = _28357 ? _28359 : _28353;
                                                            bvec2 _30062 = bvec2(_28357);
                                                            _30063 = vec2(_30062.x ? _28358.x : _28349.x, _30062.y ? _28358.y : _28349.y);
                                                        }
                                                        _28351 = _28353;
                                                        _28347 = _28349;
                                                        _28231 = true;
                                                        _28227 = true;
                                                        break;
                                                    }
                                                    _22563 = min(_28219 * 1.2000000476837158203125, 0.300000011920928955078125);
                                                    _28219 = _22563;
                                                    _28218 = _22521;
                                                    _28217++;
                                                    _28352 = _28222;
                                                    _28348 = _28221;
                                                    continue;
                                                }
                                                else
                                                {
                                                    _28351 = _28352;
                                                    _28347 = _28348;
                                                    _28231 = _28233;
                                                    _28227 = false;
                                                    break;
                                                }
                                            }
                                            if (_28227)
                                            {
                                                _28230 = _28231;
                                                break;
                                            }
                                            _28230 = false;
                                            break;
                                        } while(false);
                                        if (_28230)
                                        {
                                            _28518 = _28351;
                                            _28511 = _28347;
                                            _28387 = 1u;
                                            _28372 = true;
                                            break;
                                        }
                                        _29911 = _28230;
                                        _28521 = _28351;
                                        _28514 = _28347;
                                        int _21759 = _28210 + 1;
                                        _28233 = _29911;
                                        _28210 = _21759;
                                        _28519 = _28521;
                                        _28512 = _28514;
                                        continue;
                                    }
                                    else
                                    {
                                        _28518 = _28519;
                                        _28511 = _28512;
                                        _28387 = _27566;
                                        _28372 = false;
                                        break;
                                    }
                                }
                                if (_28372)
                                {
                                    _28517 = _28518;
                                    _28510 = _28511;
                                    _28386 = _28387;
                                    break;
                                }
                                _28517 = _28518;
                                _28510 = _28511;
                                _28386 = 0u;
                                break;
                            } while(false);
                            if (_28386 != 0u)
                            {
                                float _23117 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                vec3 _20131 = vec3(_289.CameraRight.w, _289.CameraUp.w, _289.CameraForward.w) + (((_289.CameraRight.xyz * (((_28510.x - (0.5 * float(_289.OutputWidth))) * _28517) / _23117)) + (_289.CameraUp.xyz * (((-(_28510.y - (0.5 * float(_289.OutputHeight)))) * _28517) / _23117))) + (_289.CameraForward.xyz * _28517));
                                bvec3 _20132 = bvec3(_28386 == 1u);
                                vec3 _20133 = vec3(_20132.x ? _20110.x : _20131.x, _20132.y ? _20110.y : _20131.y, _20132.z ? _20110.z : _20131.z);
                                _29008 = _28510;
                                _28909 = _28517;
                                _28810 = (_28386 == 1u) ? _28195 : length(_20133 - _6262);
                                _28711 = 1.0;
                                _28612 = _20133;
                                break;
                            }
                        }
                        float _20155 = min(_28195, 4.0);
                        _29008 = _28527;
                        _28909 = _28557;
                        _28810 = _20155;
                        _28711 = _28586;
                        _28612 = _6262 + (_21404 * _20155);
                        break;
                    } while(false);
                    vec3 _29109;
                    do
                    {
                        vec2 _23354 = clamp(_29008 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                        vec3 _23365 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_23354.x, 1.0 - _23354.y), 0.0).xyz;
                        if ((_28711 < 0.5) || (_28909 > 100000000.0))
                        {
                            _29109 = _23365;
                            break;
                        }
                        ivec2 _23188 = ivec2(_29008);
                        float _23194 = _6262.y - _28612.y;
                        if (_23194 <= 0.00999999977648258209228515625)
                        {
                            _29109 = _23365;
                            break;
                        }
                        ivec2 _23395 = clamp(_23188, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        ivec2 _23483 = clamp(_23188, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec3 _23443 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_23483.x, int((_289.OutputHeight - 1u) - uint(_23483.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                        float _23445 = length(_23443);
                        vec3 _23451 = _23443 / vec3(_23445);
                        bvec3 _23452 = bvec3(_23445 > 0.001000000047497451305389404296875);
                        ivec2 _23543 = clamp(_23188, ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        float _23240 = clamp(dot((texelFetch(SPIRV_Cross_CombinedSceneAlbedoSPIRV_Cross_DummySampler, ivec3(_23395.x, int((_289.OutputHeight - 1u) - uint(_23395.y)), 0).xy, 0).xyz * ((clamp(dot(vec3(_23452.x ? _23451.x : vec3(0.0, 1.0, 0.0).x, _23452.y ? _23451.y : vec3(0.0, 1.0, 0.0).y, _23452.z ? _23451.z : vec3(0.0, 1.0, 0.0).z), _5754), 0.0, 1.0) * texelFetch(SPIRV_Cross_CombinedSceneShadowSPIRV_Cross_DummySampler, ivec3(_23543.x, int((_289.OutputHeight - 1u) - uint(_23543.y)), 0).xy, 0).x) * 0.305577456951141357421875)) * _289.SunColor.xyz, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(_23365, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-05), 0.0, 1.0);
                        float _23244 = max(-_30012.y, 0.20000000298023223876953125);
                        float _29108;
                        if ((_289.CausticParams.x > 0.0) && (_23240 > 0.0))
                        {
                            vec2 _23287 = _28612.xz - ((_30012.xz / vec2(_23244)) * _23194);
                            vec2 _23598 = _23287 * (6.283185482025146484375 / max(_289.CausticParams.y, 0.0500000007450580596923828125));
                            vec2 _23621 = _23598 + (vec2(sin((_23598.y * 0.730000019073486328125) + ((_289.ShadeParams.z * 0.89999997615814208984375) * _289.CausticParams.z)), sin((_23598.x * 0.61000001430511474609375) - ((_289.ShadeParams.z * 1.10000002384185791015625) * _289.CausticParams.z))) * 0.89999997615814208984375);
                            float _29107;
                            do
                            {
                                if ((_289.GridParams.z < 2u) || (_289.GridParams.w < 2u))
                                {
                                    _29107 = 1.0;
                                    break;
                                }
                                vec2 _23731 = clamp(((_23287 - _289.PoolMin.xz) / max(_289.PoolMax.xz - _289.PoolMin.xz, vec2(0.001000000047497451305389404296875))) * vec2(float(_289.GridParams.z - 1u), float(_289.GridParams.w - 1u)), vec2(0.0), vec2(float(_289.GridParams.z - 1u), float(_289.GridParams.w - 1u)) - vec2(0.001000000047497451305389404296875));
                                uvec2 _23733 = uvec2(_23731);
                                vec2 _23737 = _23731 - vec2(_23733);
                                uint _23744 = _23733.x + (_23733.y * _289.GridParams.z);
                                _29107 = mix(mix(CausticField_1._data[_23744], CausticField_1._data[_23744 + 1u], _23737.x), mix(CausticField_1._data[_23744 + _289.GridParams.z], CausticField_1._data[(_23744 + _289.GridParams.z) + 1u], _23737.x), _23737.y);
                                break;
                            } while(false);
                            _29108 = min(max(1.0 + ((_289.CausticParams.x * (exp((-_23194) / max(_289.CausticParams.w, 0.0500000007450580596923828125)) * smoothstep(0.0199999995529651641845703125, 0.1500000059604644775390625, _23194))) * ((pow(clamp((1.0 - abs(sin(_23621.x + ((_289.ShadeParams.z * 0.60000002384185791015625) * _289.CausticParams.z)) * sin((_23621.y * 1.12999999523162841796875) - ((_289.ShadeParams.z * 0.4000000059604644775390625) * _289.CausticParams.z)))) * (1.0 - abs(sin(((_23621.x + _23621.y) * 0.709999978542327880859375) - ((_289.ShadeParams.z * 0.5) * _289.CausticParams.z)) * sin(((_23621.x - _23621.y) * 0.829999983310699462890625) + ((_289.ShadeParams.z * 0.699999988079071044921875) * _289.CausticParams.z)))), 0.0, 1.0), 6.0) * 2.400000095367431640625) - 0.300000011920928955078125)), 0.0) * pow(max(_29107, 0.0500000007450580596923828125), _289.LightParams.y), 2.5);
                        }
                        else
                        {
                            _29108 = 1.0;
                        }
                        _29109 = _23365 * ((mix(vec3(1.0), exp(((-_289.FluidTint.xyz) * _23194) * 1.2000000476837158203125), vec3(_289.LightParams.x)) * (1.0 - _23240)) + ((mix(vec3(1.0), exp(((-_289.FluidTint.xyz) * _23194) / vec3(_23244)), vec3(_289.LightParams.x)) * _23240) * _29108));
                        break;
                    } while(false);
                    vec3 _18071 = exp((-_289.FluidTint.xyz) * max(_28810, 0.0500000007450580596923828125));
                    float _23823 = _289.ScatterParams.z * _289.ScatterParams.z;
                    _29158 = mix(_28041, mix((_29109 * _18071) + ((_289.FluidColor.xyz * ((g_skyIrradiance * _289.ScatterParams.y) + (_289.SunColor.xyz * (((_289.ScatterParams.x * 0.25) * ((1.0 - _23823) / pow(max((1.0 + _23823) - ((2.0 * _289.ScatterParams.z) * dot(_30012, -_17945)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5754.y, 0.0, 1.0))))) * (vec3(1.0) - _18071)), mix(_28171, _28165, vec3(_28072)), vec3(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_18019, -_17945), 0.0, 1.0), max(_289.FresnelPower, 1.0))))), vec3(clamp(1.0 - (2.0 * _6175), 0.0, 1.0)));
                }
                else
                {
                    _29158 = _28041;
                }
                _29157 = _29158;
            }
            else
            {
                _29157 = _28041;
            }
            _29156 = _29157;
        }
        else
        {
            _29156 = _28041;
        }
        float _6328 = clamp((clamp((float(_13139) * 0.0009765625) / _6027, 0.0, 1.0) * _289.ScatterParams.w) + (((_5729 * 0.000244140625) * _289.FoamParams.z) * 0.300000011920928955078125), 0.0, 1.0);
        vec3 _6340 = exp(max(_289.FluidTint.xyz, vec3(0.0)) * (-_6240)) * (1.0 - (0.85000002384185791015625 * _6328));
        vec3 _23852 = ((_289.SunColor.xyz * ((clamp(_5754.y, 0.0, 1.0) * _27341) * 0.3183098733425140380859375)) + g_skyIrradiance) * 0.85000002384185791015625;
        vec3 _23858 = mix(vec3(dot(_23852, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875))), _23852, vec3(0.5));
        float _23906 = _289.ScatterParams.z * _289.ScatterParams.z;
        vec3 _6377 = ((_29156 * _6340) + ((vec3(1.0) - _6340) * mix(_289.FluidColor.xyz * ((g_skyIrradiance * _289.ScatterParams.y) + (_289.SunColor.xyz * ((((_289.ScatterParams.x * 0.25) * ((1.0 - _23906) / pow(max((1.0 + _23906) - ((2.0 * _289.ScatterParams.z) * dot(_30012, -_13322)), 9.9999997473787516355514526367188e-05), 1.5))) * clamp(_5754.y, 0.0, 1.0)) * _27341))), _23858 * 0.800000011920928955078125, vec3(_6328)))) + (_23858 * (((float(_5734) * 2.2888532839715480804443359375e-05) + (_5729 * 3.6621095205191522836685180664062e-05)) * clamp(_289.FoamParams.z, 0.0, 1.0)));
        if (_289.DebugView == 3u)
        {
            float _6384 = clamp(_6240 * 0.25, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_5690), vec4(_6384, _6384, _6384, 1.0));
            break;
        }
        if (_289.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_5690), vec4(_27247.www, 1.0));
            break;
        }
        if (_289.DebugView == 6u)
        {
            imageStore(OutputTexture, ivec2(_5690), vec4(_6377, 1.0));
            break;
        }
        if (_289.DebugView == 7u)
        {
            imageStore(OutputTexture, ivec2(_5690), vec4(clamp((_27296 * 0.0625) + (_5723 * 0.000244140625), 0.0, 1.0), _6328, clamp(_6046 + (_5717 * 0.000244140625), 0.0, 1.0), 1.0));
            break;
        }
        if (_289.DebugView == 8u)
        {
            imageStore(OutputTexture, ivec2(_5690), vec4(_28042, 1.0));
            break;
        }
        float _6456 = 0.014999999664723873138427734375 + (2.0 * sqrt(_6122));
        vec3 _6459 = reflect(_5908, _27343);
        vec3 _23940 = ((_289.CameraRight.xyz * _6459.x) + (_289.CameraUp.xyz * _6459.y)) + (_289.CameraForward.xyz * _6459.z);
        float _6476 = 0.980000019073486328125 * pow(1.0 - clamp(dot(normalize(_27298 + _27343), _5910), 0.0, 1.0), max(_289.FresnelPower, 1.0));
        float _6477 = 0.0199999995529651641845703125 + _6476;
        float _6479 = smoothstep(0.02999999932944774627685546875, 0.0599999986588954925537109375, _6477);
        vec3 _29603;
        float _29605;
        if (_6479 > 0.0)
        {
            float _29457;
            vec3 _29595;
            do
            {
                if (_289.ReflectionDistance <= 0.0)
                {
                    _29595 = vec3(0.0);
                    _29457 = 0.0;
                    break;
                }
                float _23989 = smoothstep(-0.0199999995529651641845703125, 0.119999997317790985107421875, _6459.z);
                if (_23989 <= 0.0)
                {
                    _29595 = vec3(0.0);
                    _29457 = 0.0;
                    break;
                }
                float _24186 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                float _23998 = max(0.0040000001899898052215576171875 * _27246, 0.00999999977648258209228515625);
                float _24000 = 0.5 * _23998;
                float _24009;
                float _24011;
                bool _29442;
                bool _29447;
                float _29452;
                float _29458;
                vec3 _29596;
                int _29433 = 0;
                float _29434 = _24000;
                float _29435 = _23998;
                float _29453 = -1.0;
                SPIRV_CROSS_LOOP
                for (;;)
                {
                    if (_29433 < 80)
                    {
                        _24009 = _29434 + _29435;
                        _24011 = _29435 * 1.07000005245208740234375;
                        if (_24009 > _289.ReflectionDistance)
                        {
                            _29596 = vec3(0.0);
                            _29458 = 0.0;
                            _29452 = _29453;
                            _29447 = false;
                            _29442 = true;
                            break;
                        }
                        vec3 _24022 = _9794 + (_6459 * _24009);
                        if (_24022.z <= 0.0500000007450580596923828125)
                        {
                            _29596 = vec3(0.0);
                            _29458 = 0.0;
                            _29452 = _29453;
                            _29447 = false;
                            _29442 = true;
                            break;
                        }
                        float _24226 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                        float _24202 = ((_24022.x * _24226) / _24022.z) + (0.5 * float(_289.OutputWidth));
                        float _24215 = (((-_24022.y) * _24226) / _24022.z) + (0.5 * float(_289.OutputHeight));
                        if (!((((_24202 >= 0.0) && (_24215 >= 0.0)) && (_24202 < float(_289.OutputWidth))) && (_24215 < float(_289.OutputHeight))))
                        {
                            _29596 = vec3(0.0);
                            _29458 = _27802;
                            _29452 = _29453;
                            _29447 = true;
                            _29442 = false;
                            break;
                        }
                        ivec2 _24310 = clamp(ivec2(vec2(_24202, _24215)), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                        vec4 _24262 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24310.x, int((_289.OutputHeight - 1u) - uint(_24310.y)), 0).xy, 0);
                        float _24263 = _24262.x;
                        float _24280 = ((_24263 > 0.0) && (abs(_24263 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_24263 - _289.ProjA)) : 1000000000.0;
                        if (((_24280 < 100000000.0) && (_24022.z > _24280)) && ((_24022.z - _24280) < max(_29435 * 2.1400001049041748046875, 0.25)))
                        {
                            float _29437;
                            _29437 = _24009;
                            float _30066;
                            float _30067;
                            int _29436 = 0;
                            float _29440 = _29434;
                            SPIRV_CROSS_UNROLL
                            for (; _29436 < 6; _29440 = _30066, _29437 = _30067, _29436++)
                            {
                                float _24066 = 0.5 * (_29440 + _29437);
                                vec3 _24071 = _9794 + (_6459 * _24066);
                                float _24380 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                                ivec2 _24441 = clamp(ivec2(vec2(((_24071.x * _24380) / _24071.z) + (0.5 * float(_289.OutputWidth)), (((-_24071.y) * _24380) / _24071.z) + (0.5 * float(_289.OutputHeight)))), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                                vec4 _24393 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_24441.x, int((_289.OutputHeight - 1u) - uint(_24441.y)), 0).xy, 0);
                                float _24394 = _24393.x;
                                bool _24078 = _24071.z > (((_24394 > 0.0) && (abs(_24394 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_24394 - _289.ProjA)) : 1000000000.0);
                                _30066 = _24078 ? _29440 : _24066;
                                _30067 = _24078 ? _24066 : _29437;
                            }
                            vec3 _24092 = _9794 + (_6459 * _29437);
                            float _24511 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                            vec2 _24501 = vec2(((_24092.x * _24511) / _24092.z) + (0.5 * float(_289.OutputWidth)), (((-_24092.y) * _24511) / _24092.z) + (0.5 * float(_289.OutputHeight)));
                            ivec2 _24569 = clamp(ivec2(_24501), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                            vec3 _24529 = (texelFetch(SPIRV_Cross_CombinedSceneNormalSPIRV_Cross_DummySampler, ivec3(_24569.x, int((_289.OutputHeight - 1u) - uint(_24569.y)), 0).xy, 0).xyz * 2.0) - vec3(1.0);
                            float _24531 = length(_24529);
                            vec3 _24537 = _24529 / vec3(_24531);
                            bvec3 _24538 = bvec3(_24531 > 0.001000000047497451305389404296875);
                            vec3 _24539 = vec3(_24538.x ? _24537.x : vec3(0.0, 1.0, 0.0).x, _24538.y ? _24537.y : vec3(0.0, 1.0, 0.0).y, _24538.z ? _24537.z : vec3(0.0, 1.0, 0.0).z);
                            if (dot(vec3(dot(_24539, _289.CameraRight.xyz), dot(_24539, _289.CameraUp.xyz), dot(_24539, _289.CameraForward.xyz)), _6459) > 0.100000001490116119384765625)
                            {
                                int _24119 = _29433 + 1;
                                _29435 = _24011;
                                _29434 = _24009;
                                _29433 = _24119;
                                _29453 = _24009;
                                continue;
                            }
                            float _24633 = clamp(((_6456 * _29437) * _24186) / max(_24092.z, 0.0500000007450580596923828125), 0.0, 12.0);
                            float _24635 = 0.5 * _24633;
                            vec2 _24720 = clamp(_24501 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24746 = clamp((_24501 + vec2(_24635)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24772 = clamp((_24501 + vec2(_24633 * (-0.5), _24635)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24798 = clamp((_24501 + vec2(_24635, _24633 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24824 = clamp((_24501 + vec2(_24633 * (-0.5), _24633 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24850 = clamp((_24501 + vec2(_24633, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24876 = clamp((_24501 + vec2(-_24633, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24902 = clamp((_24501 + vec2(0.0, _24633)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec2 _24928 = clamp((_24501 + vec2(0.0, -_24633)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                            vec3 _24705 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24720.x, 1.0 - _24720.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24746.x, 1.0 - _24746.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24772.x, 1.0 - _24772.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24798.x, 1.0 - _24798.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24824.x, 1.0 - _24824.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24850.x, 1.0 - _24850.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24876.x, 1.0 - _24876.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24902.x, 1.0 - _24902.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_24928.x, 1.0 - _24928.y), 0.0).xyz) * 0.100000001490116119384765625;
                            _29596 = _24705;
                            _29458 = _23989;
                            _29452 = _24009;
                            _29447 = false;
                            _29442 = true;
                            break;
                        }
                        int _24119 = _29433 + 1;
                        _29435 = _24011;
                        _29434 = _24009;
                        _29433 = _24119;
                        _29453 = _24009;
                        continue;
                    }
                    else
                    {
                        _29596 = vec3(0.0);
                        _29458 = _27802;
                        _29452 = _29453;
                        _29447 = false;
                        _29442 = false;
                        break;
                    }
                }
                if (_29442)
                {
                    _29595 = _29596;
                    _29457 = _29458;
                    break;
                }
                if ((!_29447) || (_29452 < 0.0))
                {
                    _29595 = _29596;
                    _29457 = 0.0;
                    break;
                }
                vec3 _24134 = _9794 + (_6459 * _29452);
                float _24979 = (0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05);
                vec2 _24969 = vec2(((_24134.x * _24979) / _24134.z) + (0.5 * float(_289.OutputWidth)), (((-_24134.y) * _24979) / _24134.z) + (0.5 * float(_289.OutputHeight)));
                ivec2 _25040 = clamp(ivec2(_24969), ivec2(0), ivec2(int(_289.OutputWidth) - 1, int(_289.OutputHeight) - 1));
                vec4 _24992 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_25040.x, int((_289.OutputHeight - 1u) - uint(_25040.y)), 0).xy, 0);
                float _24993 = _24992.x;
                float _25010 = ((_24993 > 0.0) && (abs(_24993 - _289.ProjA) > 9.999999717180685365747194737196e-10)) ? (_289.ProjB / (_24993 - _289.ProjA)) : 1000000000.0;
                float _25086 = clamp(((_6456 * _29452) * _24186) / max(_24134.z, 0.0500000007450580596923828125), 0.0, 12.0);
                float _25088 = 0.5 * _25086;
                vec2 _25173 = clamp(_24969 / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25199 = clamp((_24969 + vec2(_25088)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25225 = clamp((_24969 + vec2(_25086 * (-0.5), _25088)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25251 = clamp((_24969 + vec2(_25088, _25086 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25277 = clamp((_24969 + vec2(_25086 * (-0.5), _25086 * (-0.5))) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25303 = clamp((_24969 + vec2(_25086, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25329 = clamp((_24969 + vec2(-_25086, 0.0)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25355 = clamp((_24969 + vec2(0.0, _25086)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec2 _25381 = clamp((_24969 + vec2(0.0, -_25086)) / vec2(float(_289.OutputWidth), float(_289.OutputHeight)), vec2(0.0), vec2(1.0));
                vec3 _25158 = (((((((((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25173.x, 1.0 - _25173.y), 0.0).xyz * 2.0) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25199.x, 1.0 - _25199.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25225.x, 1.0 - _25225.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25251.x, 1.0 - _25251.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25277.x, 1.0 - _25277.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25303.x, 1.0 - _25303.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25329.x, 1.0 - _25329.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25355.x, 1.0 - _25355.y), 0.0).xyz) + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_25381.x, 1.0 - _25381.y), 0.0).xyz) * 0.100000001490116119384765625;
                if (_25010 > 100000000.0)
                {
                    _29595 = _25158;
                    _29457 = _23989;
                    break;
                }
                float _24159 = _25010 - _24134.z;
                if (_24159 <= 0.0)
                {
                    _29595 = _25158;
                    _29457 = _23989;
                    break;
                }
                _29595 = _25158;
                _29457 = _23989 * exp((_24159 / max(_6459.z, 0.001000000047497451305389404296875)) * (-0.3333333432674407958984375));
                break;
            } while(false);
            _29605 = _29457;
            _29603 = _29595;
        }
        else
        {
            _29605 = 0.0;
            _29603 = vec3(0.0);
        }
        vec3 _29601;
        do
        {
            if (_289.EnvParams.x <= 0.0)
            {
                float _25435 = _23940.y;
                _29601 = mix(_289.SkyHorizon.xyz * 0.3499999940395355224609375, mix(_289.SkyHorizon.xyz, _289.ReflectionColor.xyz, vec3(smoothstep(0.0, 0.64999997615814208984375, _25435))), vec3(smoothstep(-0.119999997317790985107421875, 0.0199999995529651641845703125, _25435)));
                break;
            }
            _29601 = textureLod(SPIRV_Cross_CombinedEnvironmentMapLinearSampler, vec3(-_23940.x, _23940.y, -_23940.z), clamp(clamp(log2(max(_6456 * 256.0, 1.0)), 0.0, EnvMaxMip), 0.0, EnvMaxMip)).xyz * _289.EnvParams.x;
            break;
        } while(false);
        vec3 _6503 = mix(_29601, _29603, vec3(_29605 * _6479));
        if (_289.DebugView == 5u)
        {
            imageStore(OutputTexture, ivec2(_5690), vec4(_6503, 1.0));
            break;
        }
        vec3 _6526 = normalize(_289.SunDirection.xyz);
        float _6543 = max((2.0 / ((2.0 / (max(_289.ShadeParams.w, 8.0) + 2.0)) + (2.0 * _6122))) - 2.0, 4.0);
        vec3 _6612 = (mix(_6377, _6503, vec3(clamp(_6477, 0.0, 1.0))) + (((((_289.SunColor.xyz * ((pow(clamp(dot(_27343, normalize(_6526 - _5908)), 0.0, 1.0), _6543) * (_6543 + 8.0)) * 0.0397887341678142547607421875)) * _289.Specular) * _6477) * _27341) * clamp(dot(_27343, _6526) * 4.0, 0.0, 1.0))) + ((_289.SunColor.xyz * _289.FluidColor.xyz) * (((((_289.LightParams.w * 0.60000002384185791015625) * (((1.0 - _6175) * exp(_6170 * (-1.5))) * (1.0 - exp(_6170 * (-6.0))))) * (0.63999998569488525390625 / pow(max(1.36000001430511474609375 - (1.2000000476837158203125 * dot(_5754, _13322)), 9.9999997473787516355514526367188e-05), 1.5))) * _27341) * (0.980000019073486328125 - _6476)));
        vec3 _6630 = mix(_6612, (_6612 * 1.25) + (g_skyIrradiance * 0.07999999821186065673828125), vec3(clamp(((1.0 - smoothstep(0.0040000001899898052215576171875, 0.0350000001490116119384765625, _6165)) * _6175) * _289.LightParams.z, 0.0, 1.0)));
        float _6639 = clamp(1.0 - exp(-((_27296 * 0.07500000298023223876953125) + (_5723 * 0.0003662109375))), 0.0, 1.0);
        vec3 _29630;
        if ((_6639 > 0.0199999995529651641845703125) && (_289.FoamParams.x > 0.0))
        {
            vec2 _25496 = _6052.xz / vec2(max(_289.FoamParams.y, 0.0199999995529651641845703125));
            vec2 _25531 = floor(_25496);
            vec2 _25534 = _25496 - _25531;
            float _29623;
            float _29624;
            _29624 = 8.0;
            _29623 = 8.0;
            float _29639;
            float _29640;
            SPIRV_CROSS_UNROLL
            for (int _29622 = -1; _29622 <= 1; _29624 = _29639, _29623 = _29640, _29622++)
            {
                _29640 = _29623;
                _29639 = _29624;
                float _30069;
                float _29989;
                SPIRV_CROSS_UNROLL
                for (int _29637 = -1; _29637 <= 1; _29640 = _29989, _29639 = _30069, _29637++)
                {
                    vec2 _25549 = vec2(float(_29637), float(_29622));
                    ivec2 _25553 = ivec2(_25531 + _25549);
                    uint _25625 = ((((uint(_25553.x + 4096) * 1973u) + (uint(_25553.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25633 = ((_25625 >> ((_25625 >> 28u) + 4u)) ^ _25625) * 277803737u;
                    uint _25637 = (_25633 >> 22u) ^ _25633;
                    float _25591 = length((_25549 + (vec2(0.5) + (sin(vec2(_289.ShadeParams.z * 0.5) + ((vec2(float(_25637 & 65535u), float(_25637 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25534);
                    bool _25594 = _25591 < _29639;
                    if (_25594)
                    {
                        _29989 = _29639;
                    }
                    else
                    {
                        _29989 = (_25591 < _29640) ? _25591 : _29640;
                    }
                    _30069 = _25594 ? _25591 : _29639;
                }
            }
            vec2 _25503 = (_25496 * 2.2999999523162841796875) + vec2(7.099999904632568359375);
            vec2 _25654 = floor(_25503);
            vec2 _25657 = _25503 - _25654;
            float _29626;
            float _29627;
            _29627 = 8.0;
            _29626 = 8.0;
            float _29633;
            float _29634;
            SPIRV_CROSS_UNROLL
            for (int _29625 = -1; _29625 <= 1; _29627 = _29633, _29626 = _29634, _29625++)
            {
                _29634 = _29626;
                _29633 = _29627;
                float _30071;
                float _29983;
                SPIRV_CROSS_UNROLL
                for (int _29631 = -1; _29631 <= 1; _29634 = _29983, _29633 = _30071, _29631++)
                {
                    vec2 _25672 = vec2(float(_29631), float(_29625));
                    ivec2 _25676 = ivec2(_25654 + _25672);
                    uint _25748 = ((((uint(_25676.x + 4096) * 1973u) + (uint(_25676.y + 4096) * 9277u)) + 1u) * 747796405u) + 2891336453u;
                    uint _25756 = ((_25748 >> ((_25748 >> 28u) + 4u)) ^ _25748) * 277803737u;
                    uint _25760 = (_25756 >> 22u) ^ _25756;
                    float _25714 = length((_25672 + (vec2(0.5) + (sin(vec2(_289.ShadeParams.z * 0.64999997615814208984375) + ((vec2(float(_25760 & 65535u), float(_25760 >> 16u)) * vec2(1.525902189314365386962890625e-05)) * 6.283185482025146484375)) * 0.4199999868869781494140625))) - _25657);
                    bool _25717 = _25714 < _29633;
                    if (_25717)
                    {
                        _29983 = _29633;
                    }
                    else
                    {
                        _29983 = (_25714 < _29634) ? _25714 : _29634;
                    }
                    _30071 = _25717 ? _25714 : _29633;
                }
            }
            float _25514 = mix(0.4000000059604644775390625, max(1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29623 - _29624), 0.699999988079071044921875 * (1.0 - smoothstep(0.0, 0.20000000298023223876953125, _29626 - _29627))), clamp((((_289.FoamParams.y * ((0.5 * float(_289.OutputHeight)) * max(_289.ProjScaleY, 9.9999997473787516355514526367188e-05))) / max(_27246, 0.100000001490116119384765625)) - 2.0) * 0.25, 0.0, 1.0));
            _29630 = mix(_6630, _23858 * mix(0.85000002384185791015625, 1.0, _25514), vec3(clamp(_6639 * (0.550000011920928955078125 + (0.89999997615814208984375 * (_25514 - 0.4000000059604644775390625))), 0.0, 1.0) * clamp(_289.FoamParams.x, 0.0, 1.0)));
        }
        else
        {
            _29630 = _6630;
        }
        float _6685 = smoothstep(0.1500000059604644775390625, 0.60000002384185791015625, _6046) * clamp(_289.Foam * 1.60000002384185791015625, 0.0, 1.0);
        imageStore(OutputTexture, ivec2(_5690), vec4(mix(mix(_29630, _23858, vec3(_6685 * 0.89999997615814208984375)), _23858, vec3(_5776)), max(clamp(_27247.w, 0.0, 1.0) * (1.0 - (0.3499999940395355224609375 * _6685)), _5776)));
        break;
    } while(false);
}

