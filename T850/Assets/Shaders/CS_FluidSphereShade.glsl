#version 430
// GENERATED from CS_FluidSphereShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=971e8d2fd93e9a22529f598e2d51a6495a8d6f1f25ee1dde13c6a130eb2a7ef9; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 2, std430) readonly buffer ThicknessBits
{
    uint _data[];
} Keys;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 3, rgba16f) uniform writeonly image2D OutputTexture;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _110.OutputWidth) || (gl_GlobalInvocationID.y >= _110.OutputHeight))
        {
            break;
        }
        uvec2 _816 = uvec2(gl_GlobalInvocationID.x, (_110.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        uint _828 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _110.OutputWidth);
        uint _830 = Keys._data[_828];
        uint _832 = _830 & 131071u;
        if ((_830 == 4294967295u) || (_832 >= _110.ParticleCount))
        {
            imageStore(OutputTexture, ivec2(_816), vec4(0.0));
            break;
        }
        vec4 _849 = Projected_1._data[_832];
        vec2 _861 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        float _864 = min(_849.z, 64.0);
        vec2 _874 = (_861 - (_849.xy * vec2(float(_110.OutputWidth), float(_110.OutputHeight)))) / vec2(max(_864, 0.001000000047497451305389404296875));
        float _877 = dot(_874, _874);
        if ((_849.z <= 0.0) || (_877 > 1.0))
        {
            imageStore(OutputTexture, ivec2(_816), vec4(0.0));
            break;
        }
        if (_110.Style == 1u)
        {
            float _902 = max(1.0 - (2.0 / max(_864, 2.0)), 0.0);
            float _905 = 1.0 / max(_864, 1.0);
            uint _916 = _832 * 2654435761u;
            uint _1159 = (_916 << 13u) ^ _916;
            float _1182 = clamp(float(((_1159 * (((_1159 * _1159) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1186 = min(uint(floor(_1182)), 2u);
            bvec3 _1204 = bvec3(_1186 == 1u);
            vec3 _1205 = vec3(_1204.x ? _110.Color1.xyz.x : _110.Color2.xyz.x, _1204.y ? _110.Color1.xyz.y : _110.Color2.xyz.y, _1204.z ? _110.Color1.xyz.z : _110.Color2.xyz.z);
            bvec3 _1206 = bvec3(_1186 == 0u);
            bvec3 _1221 = bvec3(_1186 == 1u);
            vec3 _1222 = vec3(_1221.x ? _110.Color2.xyz.x : _110.Color3.xyz.x, _1221.y ? _110.Color2.xyz.y : _110.Color3.xyz.y, _1221.z ? _110.Color2.xyz.z : _110.Color3.xyz.z);
            bvec3 _1223 = bvec3(_1186 == 0u);
            imageStore(OutputTexture, ivec2(_816), vec4(mix(vec3(_1206.x ? _110.Color0.xyz.x : _1205.x, _1206.y ? _110.Color0.xyz.y : _1205.y, _1206.z ? _110.Color0.xyz.z : _1205.z), vec3(_1223.x ? _110.Color1.xyz.x : _1222.x, _1223.y ? _110.Color1.xyz.y : _1222.y, _1223.z ? _110.Color1.xyz.z : _1222.z), vec3(_1182 - float(_1186))), clamp(_110.Opacity, 0.0, 1.0) * smoothstep(_902 - _905, _902 + _905, sqrt(_877))));
            break;
        }
        if (_110.Style == 2u)
        {
            uint _941 = _832 * 2654435761u;
            uint _1235 = (_941 << 13u) ^ _941;
            float _1258 = clamp(float(((_1235 * (((_1235 * _1235) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1262 = min(uint(floor(_1258)), 2u);
            bvec3 _1280 = bvec3(_1262 == 1u);
            vec3 _1281 = vec3(_1280.x ? _110.Color1.xyz.x : _110.Color2.xyz.x, _1280.y ? _110.Color1.xyz.y : _110.Color2.xyz.y, _1280.z ? _110.Color1.xyz.z : _110.Color2.xyz.z);
            bvec3 _1282 = bvec3(_1262 == 0u);
            bvec3 _1297 = bvec3(_1262 == 1u);
            vec3 _1298 = vec3(_1297.x ? _110.Color2.xyz.x : _110.Color3.xyz.x, _1297.y ? _110.Color2.xyz.y : _110.Color3.xyz.y, _1297.z ? _110.Color2.xyz.z : _110.Color3.xyz.z);
            bvec3 _1299 = bvec3(_1262 == 0u);
            imageStore(OutputTexture, ivec2(_816), vec4(mix(vec3(_1282.x ? _110.Color0.xyz.x : _1281.x, _1282.y ? _110.Color0.xyz.y : _1281.y, _1282.z ? _110.Color0.xyz.z : _1281.z), vec3(_1299.x ? _110.Color1.xyz.x : _1298.x, _1299.y ? _110.Color1.xyz.y : _1298.y, _1299.z ? _110.Color1.xyz.z : _1298.z), vec3(_1258 - float(_1262))), clamp(_110.Opacity, 0.0, 1.0)));
            break;
        }
        if (_110.Style == 3u)
        {
            vec3 _971 = normalize(vec3(_874.x, -_874.y, -sqrt(1.0 - _877)));
            float _1344 = (0.5 * float(_110.OutputHeight)) * max(_110.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _974 = normalize(vec3((_861.x - (0.5 * float(_110.OutputWidth))) / _1344, (-(_861.y - (0.5 * float(_110.OutputHeight)))) / _1344, 1.0));
            uint _1359 = _832 * 2654435761u;
            uint _1438 = (_1359 << 13u) ^ _1359;
            float _1461 = clamp(float(((_1438 * (((_1438 * _1438) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1465 = min(uint(floor(_1461)), 2u);
            bvec3 _1483 = bvec3(_1465 == 1u);
            vec3 _1484 = vec3(_1483.x ? _110.Color1.xyz.x : _110.Color2.xyz.x, _1483.y ? _110.Color1.xyz.y : _110.Color2.xyz.y, _1483.z ? _110.Color1.xyz.z : _110.Color2.xyz.z);
            bvec3 _1485 = bvec3(_1465 == 0u);
            bvec3 _1500 = bvec3(_1465 == 1u);
            vec3 _1501 = vec3(_1500.x ? _110.Color2.xyz.x : _110.Color3.xyz.x, _1500.y ? _110.Color2.xyz.y : _110.Color3.xyz.y, _1500.z ? _110.Color2.xyz.z : _110.Color3.xyz.z);
            bvec3 _1502 = bvec3(_1465 == 0u);
            vec3 _1366 = normalize(_110.SunDirection.xyz);
            vec3 _1424 = ((mix(vec3(_1485.x ? _110.Color0.xyz.x : _1484.x, _1485.y ? _110.Color0.xyz.y : _1484.y, _1485.z ? _110.Color0.xyz.z : _1484.z), vec3(_1502.x ? _110.Color1.xyz.x : _1501.x, _1502.y ? _110.Color1.xyz.y : _1501.y, _1502.z ? _110.Color1.xyz.z : _1501.z), vec3(_1461 - float(_1465))) * ((_110.SunColor.xyz * clamp(dot(_971, _1366), 0.0, 1.0)) + (_110.SkyColor.xyz * (_110.Ambient * mix(0.3499999940395355224609375, 1.0, (dot(_971, _110.WorldUp.xyz) * 0.5) + 0.5))))) + (_110.SunColor.xyz * (pow(clamp(dot(_971, normalize(_1366 - _974)), 0.0, 1.0), 48.0) * 0.4000000059604644775390625))) + (_110.SkyColor.xyz * (pow(1.0 - clamp(dot(_971, -_974), 0.0, 1.0), 4.0) * 0.300000011920928955078125));
            imageStore(OutputTexture, ivec2(_816), vec4(_1424, clamp(_110.Opacity, 0.0, 1.0)));
            break;
        }
        imageStore(OutputTexture, ivec2(_816), vec4(0.0));
        break;
    } while(false);
}

