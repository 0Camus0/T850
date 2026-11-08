#version 430
// GENERATED from CS_ParticleShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=ce47627d183f87b8f0d838f994b2ad2f1fbfa292f250f73f3251cb0e928bc288; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform ParticleShadeConstants
{
    uint OutputWidth;
    uint OutputHeight;
    uint Mode;
    uint DebugView;
    float ProjScaleY;
    float Opacity;
    float DepthBias;
    float Pad0;
    float ThicknessScale;
    float Refraction;
    float Specular;
    float FresnelPower;
    float NormalScale;
    float Foam;
    float TimeSeconds;
    float NormalStep;
    vec4 FluidTint;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 ViewProjection[4];
    vec4 CameraRight;
    vec4 CameraUp;
    vec4 CameraForward;
    vec4 ReflectionColor;
} _30;

layout(binding = 4, rgba16f) uniform writeonly image2D OutputTexture;
layout(binding = 1) uniform sampler2D SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSceneColorLinearSampler;

void main()
{
    do
    {
        if (_30.Mode == 0u)
        {
            break;
        }
        if ((gl_GlobalInvocationID.x >= _30.OutputWidth) || (gl_GlobalInvocationID.y >= _30.OutputHeight))
        {
            break;
        }
        uvec2 _792 = uvec2(gl_GlobalInvocationID.x, (_30.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _1257 = clamp(ivec2(gl_GlobalInvocationID.xy), ivec2(0), ivec2(int(_30.OutputWidth) - 1, int(_30.OutputHeight) - 1));
        vec4 _1283 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_1257.x, int((_30.OutputHeight - 1u) - uint(_1257.y)), 0).xy, 0);
        if ((_1283.x <= 0.0) || (_1283.w <= 0.001000000047497451305389404296875))
        {
            break;
        }
        float _807 = _1283.x;
        float _814 = max(_1283.y, 0.0) * max(_30.ThicknessScale, 0.0);
        vec2 _832 = (vec2(float(gl_GlobalInvocationID.x), float(gl_GlobalInvocationID.y)) + vec2(0.5)) / vec2(float(_30.OutputWidth), float(_30.OutputHeight));
        ivec2 _835 = ivec2(gl_GlobalInvocationID.xy);
        int _840 = clamp(int(_30.NormalStep + 0.5), 1, 8);
        float _843 = 1.0 / float(_840);
        ivec2 _1297 = clamp(_835 + ivec2(-_840, 0), ivec2(0), ivec2(int(_30.OutputWidth) - 1, int(_30.OutputHeight) - 1));
        vec4 _1323 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_1297.x, int((_30.OutputHeight - 1u) - uint(_1297.y)), 0).xy, 0);
        ivec2 _1337 = clamp(_835 + ivec2(_840, 0), ivec2(0), ivec2(int(_30.OutputWidth) - 1, int(_30.OutputHeight) - 1));
        vec4 _1363 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_1337.x, int((_30.OutputHeight - 1u) - uint(_1337.y)), 0).xy, 0);
        ivec2 _1377 = clamp(_835 + ivec2(0, -_840), ivec2(0), ivec2(int(_30.OutputWidth) - 1, int(_30.OutputHeight) - 1));
        vec4 _1403 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_1377.x, int((_30.OutputHeight - 1u) - uint(_1377.y)), 0).xy, 0);
        ivec2 _1417 = clamp(_835 + ivec2(0, _840), ivec2(0), ivec2(int(_30.OutputWidth) - 1, int(_30.OutputHeight) - 1));
        vec4 _1443 = texelFetch(SPIRV_Cross_CombinedSurfaceDepthSPIRV_Cross_DummySampler, ivec3(_1417.x, int((_30.OutputHeight - 1u) - uint(_1417.y)), 0).xy, 0);
        float _873 = (_1323.x > 0.0) ? (_807 - _1323.x) : 1000000000.0;
        float _881 = (_1363.x > 0.0) ? (_1363.x - _807) : 1000000000.0;
        float _889 = (_1403.x > 0.0) ? (_807 - _1403.x) : 1000000000.0;
        float _897 = (_1443.x > 0.0) ? (_1443.x - _807) : 1000000000.0;
        float _927 = (0.5 * float(_30.OutputHeight)) * max(_30.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec3 _948 = normalize(vec3((-_927) * ((((abs(_881) <= abs(_873)) ? _881 : (-_873)) * _843) * _30.NormalScale), (-_927) * ((((abs(_897) <= abs(_889)) ? _897 : (-_889)) * _843) * _30.NormalScale), -_807));
        vec3 _983 = normalize(vec3(((((_832.x * 2.0) - 1.0) * (float(_30.OutputWidth) / max(float(_30.OutputHeight), 1.0))) * _807) / max(_30.ProjScaleY, 9.9999997473787516355514526367188e-05), ((1.0 - (_832.y * 2.0)) * _807) / max(_30.ProjScaleY, 9.9999997473787516355514526367188e-05), _807));
        vec3 _985 = -_983;
        vec3 _1505;
        if (dot(_948, _985) < 0.0)
        {
            _1505 = -_948;
        }
        else
        {
            _1505 = _948;
        }
        if (_30.DebugView == 1u)
        {
            imageStore(OutputTexture, ivec2(_792), vec4((_1505 * 0.5) + vec3(0.5), 1.0));
            break;
        }
        if (_30.DebugView == 2u)
        {
            float _1016 = clamp(_807 * 0.083333335816860198974609375, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_792), vec4(_1016, _1016, _1016, 1.0));
            break;
        }
        if (_30.DebugView == 3u)
        {
            float _1031 = clamp(_814, 0.0, 1.0);
            imageStore(OutputTexture, ivec2(_792), vec4(_1031, _1031, _1031, 1.0));
            break;
        }
        if (_30.DebugView == 4u)
        {
            imageStore(OutputTexture, ivec2(_792), vec4(_1283.www, 1.0));
            break;
        }
        vec2 _1071 = clamp(_832 + ((_1505.xy * _30.Refraction) * (0.25 + clamp(_814 * 0.800000011920928955078125, 0.0, 1.0))), vec2(0.0), vec2(1.0));
        vec3 _1090 = exp(max(_30.FluidTint.xyz, vec3(0.0)) * (-_814));
        vec2 _1110 = clamp(_832 - ((_1505.xy * _30.Refraction) * 1.5), vec2(0.0), vec2(1.0));
        vec3 _1218 = mix((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_1071.x, 1.0 - _1071.y), 0.0).xyz * _1090) + ((vec3(1.0) - _1090) * vec3(0.100000001490116119384765625, 0.300000011920928955078125, 0.4199999868869781494140625)), mix(textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_1110.x, 1.0 - _1110.y), 0.0).xyz, _30.ReflectionColor.xyz, vec3(clamp((reflect(normalize(((_30.CameraRight.xyz * _983.x) + (_30.CameraUp.xyz * _983.y)) + (_30.CameraForward.xyz * _983.z)), normalize(((_30.CameraRight.xyz * _1505.x) + (_30.CameraUp.xyz * _1505.y)) + (_30.CameraForward.xyz * _1505.z))).y * 0.75) + 0.25, 0.0, 1.0))), vec3(clamp(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_1505, _985), 0.0, 1.0), max(_30.FresnelPower, 1.0))), 0.0, 1.0))) + (_30.SunColor.xyz * (pow(clamp(dot(_1505, normalize(normalize(_30.SunDirection.xyz) - _983)), 0.0, 1.0), 96.0) * _30.Specular));
        imageStore(OutputTexture, ivec2(_792), vec4(_1218 + (((vec3(1.0) - _1090) * _30.Foam) * 0.25), clamp(_1283.w * _30.Opacity, 0.0, 1.0)));
        break;
    } while(false);
}

