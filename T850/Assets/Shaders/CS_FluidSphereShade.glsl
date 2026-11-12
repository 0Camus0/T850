#version 430
// GENERATED from CS_FluidSphereShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=c6131c34bec49e2af6eb6afebe4a49a37020e0ee51efc5281b483a7c63819b99; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _93;

layout(binding = 2, std430) readonly buffer Keys
{
    uint _data[];
} Keys_1;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 3, rgba16f) uniform writeonly image2D OutputTexture;

void main()
{
    do
    {
        if ((gl_GlobalInvocationID.x >= _93.OutputWidth) || (gl_GlobalInvocationID.y >= _93.OutputHeight))
        {
            break;
        }
        uvec2 _598 = uvec2(gl_GlobalInvocationID.x, (_93.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        uint _606 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _93.OutputWidth);
        uint _608 = Keys_1._data[_606];
        uint _610 = _608 & 131071u;
        if ((_608 == 4294967295u) || (_610 >= _93.ParticleCount))
        {
            imageStore(OutputTexture, ivec2(_598), vec4(0.0));
            break;
        }
        vec4 _625 = Projected_1._data[_610];
        vec2 _637 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        float _640 = min(_625.z, 64.0);
        vec2 _650 = (_637 - (_625.xy * vec2(float(_93.OutputWidth), float(_93.OutputHeight)))) / vec2(max(_640, 0.001000000047497451305389404296875));
        float _653 = dot(_650, _650);
        if ((_625.z <= 0.0) || (_653 > 1.0))
        {
            imageStore(OutputTexture, ivec2(_598), vec4(0.0));
            break;
        }
        if (_93.Style == 1u)
        {
            float _676 = max(1.0 - (2.0 / max(_640, 2.0)), 0.0);
            float _679 = 1.0 / max(_640, 1.0);
            uint _690 = _610 * 2654435761u;
            uint _763 = (_690 << 13u) ^ _690;
            float _786 = clamp(float(((_763 * (((_763 * _763) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _790 = min(uint(floor(_786)), 2u);
            bvec3 _808 = bvec3(_790 == 1u);
            vec3 _809 = vec3(_808.x ? _93.Color1.xyz.x : _93.Color2.xyz.x, _808.y ? _93.Color1.xyz.y : _93.Color2.xyz.y, _808.z ? _93.Color1.xyz.z : _93.Color2.xyz.z);
            bvec3 _810 = bvec3(_790 == 0u);
            bvec3 _825 = bvec3(_790 == 1u);
            vec3 _826 = vec3(_825.x ? _93.Color2.xyz.x : _93.Color3.xyz.x, _825.y ? _93.Color2.xyz.y : _93.Color3.xyz.y, _825.z ? _93.Color2.xyz.z : _93.Color3.xyz.z);
            bvec3 _827 = bvec3(_790 == 0u);
            imageStore(OutputTexture, ivec2(_598), vec4(mix(vec3(_810.x ? _93.Color0.xyz.x : _809.x, _810.y ? _93.Color0.xyz.y : _809.y, _810.z ? _93.Color0.xyz.z : _809.z), vec3(_827.x ? _93.Color1.xyz.x : _826.x, _827.y ? _93.Color1.xyz.y : _826.y, _827.z ? _93.Color1.xyz.z : _826.z), vec3(_786 - float(_790))), clamp(_93.Opacity, 0.0, 1.0) * smoothstep(_676 - _679, _676 + _679, sqrt(_653))));
            break;
        }
        if (_93.Style == 2u)
        {
            uint _713 = _610 * 2654435761u;
            uint _839 = (_713 << 13u) ^ _713;
            float _862 = clamp(float(((_839 * (((_839 * _839) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _866 = min(uint(floor(_862)), 2u);
            bvec3 _884 = bvec3(_866 == 1u);
            vec3 _885 = vec3(_884.x ? _93.Color1.xyz.x : _93.Color2.xyz.x, _884.y ? _93.Color1.xyz.y : _93.Color2.xyz.y, _884.z ? _93.Color1.xyz.z : _93.Color2.xyz.z);
            bvec3 _886 = bvec3(_866 == 0u);
            bvec3 _901 = bvec3(_866 == 1u);
            vec3 _902 = vec3(_901.x ? _93.Color2.xyz.x : _93.Color3.xyz.x, _901.y ? _93.Color2.xyz.y : _93.Color3.xyz.y, _901.z ? _93.Color2.xyz.z : _93.Color3.xyz.z);
            bvec3 _903 = bvec3(_866 == 0u);
            imageStore(OutputTexture, ivec2(_598), vec4(mix(vec3(_886.x ? _93.Color0.xyz.x : _885.x, _886.y ? _93.Color0.xyz.y : _885.y, _886.z ? _93.Color0.xyz.z : _885.z), vec3(_903.x ? _93.Color1.xyz.x : _902.x, _903.y ? _93.Color1.xyz.y : _902.y, _903.z ? _93.Color1.xyz.z : _902.z), vec3(_862 - float(_866))), clamp(_93.Opacity, 0.0, 1.0)));
            break;
        }
        if (_93.Style == 3u)
        {
            vec3 _741 = normalize(vec3(_650.x, -_650.y, -sqrt(1.0 - _653)));
            float _948 = (0.5 * float(_93.OutputHeight)) * max(_93.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _744 = normalize(vec3((_637.x - (0.5 * float(_93.OutputWidth))) / _948, (-(_637.y - (0.5 * float(_93.OutputHeight)))) / _948, 1.0));
            uint _963 = _610 * 2654435761u;
            uint _1042 = (_963 << 13u) ^ _963;
            float _1065 = clamp(float(((_1042 * (((_1042 * _1042) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1069 = min(uint(floor(_1065)), 2u);
            bvec3 _1087 = bvec3(_1069 == 1u);
            vec3 _1088 = vec3(_1087.x ? _93.Color1.xyz.x : _93.Color2.xyz.x, _1087.y ? _93.Color1.xyz.y : _93.Color2.xyz.y, _1087.z ? _93.Color1.xyz.z : _93.Color2.xyz.z);
            bvec3 _1089 = bvec3(_1069 == 0u);
            bvec3 _1104 = bvec3(_1069 == 1u);
            vec3 _1105 = vec3(_1104.x ? _93.Color2.xyz.x : _93.Color3.xyz.x, _1104.y ? _93.Color2.xyz.y : _93.Color3.xyz.y, _1104.z ? _93.Color2.xyz.z : _93.Color3.xyz.z);
            bvec3 _1106 = bvec3(_1069 == 0u);
            vec3 _970 = normalize(_93.SunDirection.xyz);
            vec3 _1028 = ((mix(vec3(_1089.x ? _93.Color0.xyz.x : _1088.x, _1089.y ? _93.Color0.xyz.y : _1088.y, _1089.z ? _93.Color0.xyz.z : _1088.z), vec3(_1106.x ? _93.Color1.xyz.x : _1105.x, _1106.y ? _93.Color1.xyz.y : _1105.y, _1106.z ? _93.Color1.xyz.z : _1105.z), vec3(_1065 - float(_1069))) * ((_93.SunColor.xyz * clamp(dot(_741, _970), 0.0, 1.0)) + (_93.SkyColor.xyz * (_93.Ambient * mix(0.3499999940395355224609375, 1.0, (dot(_741, _93.WorldUp.xyz) * 0.5) + 0.5))))) + (_93.SunColor.xyz * (pow(clamp(dot(_741, normalize(_970 - _744)), 0.0, 1.0), 48.0) * 0.4000000059604644775390625))) + (_93.SkyColor.xyz * (pow(1.0 - clamp(dot(_741, -_744), 0.0, 1.0), 4.0) * 0.300000011920928955078125));
            imageStore(OutputTexture, ivec2(_598), vec4(_1028, clamp(_93.Opacity, 0.0, 1.0)));
            break;
        }
        imageStore(OutputTexture, ivec2(_598), vec4(0.0));
        break;
    } while(false);
}

