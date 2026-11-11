#version 430
// GENERATED from CS_FluidSphereShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=ce342e3bd82d037adc82441c4faa2ad5396d650a9cb9fef7aec4f294d1906573; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, std140) uniform ParticleSphereConstants
{
    uint OutputWidth;
    uint OutputHeight;
    uint ParticleCount;
    uint Style;
    float Radius;
    float Opacity;
    float ProjScaleY;
    float Ambient;
    float ProjA;
    float ProjB;
    float Refraction;
    float Specular;
    float FresnelPower;
    float ThicknessScale;
    float InvPackingFactor;
    float Pad0;
    vec4 SunDirection;
    vec4 SunColor;
    vec4 SkyColor;
    vec4 WorldUp;
    vec4 FluidTint;
    vec4 FluidColor;
    vec4 Color0;
    vec4 Color1;
    vec4 Color2;
    vec4 Color3;
} _110;

layout(binding = 4, std430) readonly buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

layout(binding = 2, std430) readonly buffer Keys
{
    uint _data[];
} Keys_1;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 3, rgba16f) uniform writeonly image2D OutputTexture;
layout(binding = 7) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;
layout(binding = 5) uniform sampler2D SPIRV_Cross_CombinedSceneColorLinearSampler;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _110.OutputWidth) || (gl_GlobalInvocationID.y >= _110.OutputHeight))
        {
            break;
        }
        uvec2 _827 = uvec2(gl_GlobalInvocationID.x, (_110.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        ivec2 _830 = ivec2(gl_GlobalInvocationID.xy);
        ivec2 _1061 = clamp(_830, ivec2(0), ivec2(int(_110.OutputWidth) - 1, int(_110.OutputHeight) - 1));
        vec4 _1087 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_1061.x, int((_110.OutputHeight - 1u) - uint(_1061.y)), 0).xy, 0);
        float _1088 = _1087.x;
        float _1561;
        do
        {
            if ((((_830.x < 0) || (_830.y < 0)) || (_830.x >= int(_110.OutputWidth))) || (_830.y >= int(_110.OutputHeight)))
            {
                _1561 = 0.0;
                break;
            }
            _1561 = float(ThicknessBits_1._data[uint(_830.x) + (uint(_830.y) * _110.OutputWidth)]) * 0.0009765625;
            break;
        } while(false);
        vec2 _1164 = clamp((vec2(_830) + vec2(0.5)) / vec2(float(_110.OutputWidth), float(_110.OutputHeight)), vec2(0.0), vec2(1.0));
        vec3 _1175 = textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_1164.x, 1.0 - _1164.y), 0.0).xyz;
        float _1046 = (float((((_1088 > 0.0) && (abs(_1088 - _110.ProjA) > 9.999999717180685365747194737196e-10)) ? (_110.ProjB / (_1088 - _110.ProjA)) : 1000000000.0) > 1000000015047466219876688855040.0) + float(_1561 > 1000000015047466219876688855040.0)) + float(dot(_1175, _1175) > 1000000015047466219876688855040.0);
        uint _839 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _110.OutputWidth);
        uint _841 = Keys_1._data[_839];
        uint _843 = _841 & 131071u;
        if ((_841 == 4294967295u) || (_843 >= _110.ParticleCount))
        {
            imageStore(OutputTexture, ivec2(_827), vec4(0.0, 0.0, 0.0, _1046));
            break;
        }
        vec4 _860 = Projected_1._data[_843];
        vec2 _872 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        float _875 = min(_860.z, 64.0);
        vec2 _885 = (_872 - (_860.xy * vec2(float(_110.OutputWidth), float(_110.OutputHeight)))) / vec2(max(_875, 0.001000000047497451305389404296875));
        float _888 = dot(_885, _885);
        if ((_860.z <= 0.0) || (_888 > 1.0))
        {
            imageStore(OutputTexture, ivec2(_827), vec4(0.0, 0.0, 0.0, _1046));
            break;
        }
        if (_110.Style == 1u)
        {
            float _913 = max(1.0 - (2.0 / max(_875, 2.0)), 0.0);
            float _916 = 1.0 / max(_875, 1.0);
            uint _927 = _843 * 2654435761u;
            uint _1181 = (_927 << 13u) ^ _927;
            float _1204 = clamp(float(((_1181 * (((_1181 * _1181) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1208 = min(uint(floor(_1204)), 2u);
            bvec3 _1226 = bvec3(_1208 == 1u);
            vec3 _1227 = vec3(_1226.x ? _110.Color1.xyz.x : _110.Color2.xyz.x, _1226.y ? _110.Color1.xyz.y : _110.Color2.xyz.y, _1226.z ? _110.Color1.xyz.z : _110.Color2.xyz.z);
            bvec3 _1228 = bvec3(_1208 == 0u);
            bvec3 _1243 = bvec3(_1208 == 1u);
            vec3 _1244 = vec3(_1243.x ? _110.Color2.xyz.x : _110.Color3.xyz.x, _1243.y ? _110.Color2.xyz.y : _110.Color3.xyz.y, _1243.z ? _110.Color2.xyz.z : _110.Color3.xyz.z);
            bvec3 _1245 = bvec3(_1208 == 0u);
            imageStore(OutputTexture, ivec2(_827), vec4(mix(vec3(_1228.x ? _110.Color0.xyz.x : _1227.x, _1228.y ? _110.Color0.xyz.y : _1227.y, _1228.z ? _110.Color0.xyz.z : _1227.z), vec3(_1245.x ? _110.Color1.xyz.x : _1244.x, _1245.y ? _110.Color1.xyz.y : _1244.y, _1245.z ? _110.Color1.xyz.z : _1244.z), vec3(_1204 - float(_1208))), (clamp(_110.Opacity, 0.0, 1.0) * smoothstep(_913 - _916, _913 + _916, sqrt(_888))) + _1046));
            break;
        }
        if (_110.Style == 2u)
        {
            uint _952 = _843 * 2654435761u;
            uint _1257 = (_952 << 13u) ^ _952;
            float _1280 = clamp(float(((_1257 * (((_1257 * _1257) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1284 = min(uint(floor(_1280)), 2u);
            bvec3 _1302 = bvec3(_1284 == 1u);
            vec3 _1303 = vec3(_1302.x ? _110.Color1.xyz.x : _110.Color2.xyz.x, _1302.y ? _110.Color1.xyz.y : _110.Color2.xyz.y, _1302.z ? _110.Color1.xyz.z : _110.Color2.xyz.z);
            bvec3 _1304 = bvec3(_1284 == 0u);
            bvec3 _1319 = bvec3(_1284 == 1u);
            vec3 _1320 = vec3(_1319.x ? _110.Color2.xyz.x : _110.Color3.xyz.x, _1319.y ? _110.Color2.xyz.y : _110.Color3.xyz.y, _1319.z ? _110.Color2.xyz.z : _110.Color3.xyz.z);
            bvec3 _1321 = bvec3(_1284 == 0u);
            imageStore(OutputTexture, ivec2(_827), vec4(mix(vec3(_1304.x ? _110.Color0.xyz.x : _1303.x, _1304.y ? _110.Color0.xyz.y : _1303.y, _1304.z ? _110.Color0.xyz.z : _1303.z), vec3(_1321.x ? _110.Color1.xyz.x : _1320.x, _1321.y ? _110.Color1.xyz.y : _1320.y, _1321.z ? _110.Color1.xyz.z : _1320.z), vec3(_1280 - float(_1284))), clamp(_110.Opacity, 0.0, 1.0) + _1046));
            break;
        }
        if (_110.Style == 3u)
        {
            vec3 _982 = normalize(vec3(_885.x, -_885.y, -sqrt(1.0 - _888)));
            float _1366 = (0.5 * float(_110.OutputHeight)) * max(_110.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _985 = normalize(vec3((_872.x - (0.5 * float(_110.OutputWidth))) / _1366, (-(_872.y - (0.5 * float(_110.OutputHeight)))) / _1366, 1.0));
            uint _1381 = _843 * 2654435761u;
            uint _1460 = (_1381 << 13u) ^ _1381;
            float _1483 = clamp(float(((_1460 * (((_1460 * _1460) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1487 = min(uint(floor(_1483)), 2u);
            bvec3 _1505 = bvec3(_1487 == 1u);
            vec3 _1506 = vec3(_1505.x ? _110.Color1.xyz.x : _110.Color2.xyz.x, _1505.y ? _110.Color1.xyz.y : _110.Color2.xyz.y, _1505.z ? _110.Color1.xyz.z : _110.Color2.xyz.z);
            bvec3 _1507 = bvec3(_1487 == 0u);
            bvec3 _1522 = bvec3(_1487 == 1u);
            vec3 _1523 = vec3(_1522.x ? _110.Color2.xyz.x : _110.Color3.xyz.x, _1522.y ? _110.Color2.xyz.y : _110.Color3.xyz.y, _1522.z ? _110.Color2.xyz.z : _110.Color3.xyz.z);
            bvec3 _1524 = bvec3(_1487 == 0u);
            vec3 _1388 = normalize(_110.SunDirection.xyz);
            vec3 _1446 = ((mix(vec3(_1507.x ? _110.Color0.xyz.x : _1506.x, _1507.y ? _110.Color0.xyz.y : _1506.y, _1507.z ? _110.Color0.xyz.z : _1506.z), vec3(_1524.x ? _110.Color1.xyz.x : _1523.x, _1524.y ? _110.Color1.xyz.y : _1523.y, _1524.z ? _110.Color1.xyz.z : _1523.z), vec3(_1483 - float(_1487))) * ((_110.SunColor.xyz * clamp(dot(_982, _1388), 0.0, 1.0)) + (_110.SkyColor.xyz * (_110.Ambient * mix(0.3499999940395355224609375, 1.0, (dot(_982, _110.WorldUp.xyz) * 0.5) + 0.5))))) + (_110.SunColor.xyz * (pow(clamp(dot(_982, normalize(_1388 - _985)), 0.0, 1.0), 48.0) * 0.4000000059604644775390625))) + (_110.SkyColor.xyz * (pow(1.0 - clamp(dot(_982, -_985), 0.0, 1.0), 4.0) * 0.300000011920928955078125));
            imageStore(OutputTexture, ivec2(_827), vec4(_1446, clamp(_110.Opacity, 0.0, 1.0) + _1046));
            break;
        }
        imageStore(OutputTexture, ivec2(_827), vec4(0.0, 0.0, 0.0, _1046));
        break;
    } while(false);
}

