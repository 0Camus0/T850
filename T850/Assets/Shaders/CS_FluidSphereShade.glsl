#version 430
// GENERATED from CS_FluidSphereShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=6b54cdc0645646316695477097dceddee027ccb87c8d42dc5f6814de9f4d68bd; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        uvec2 _606 = uvec2(gl_GlobalInvocationID.x, (_93.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        uint _614 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _93.OutputWidth);
        uint _616 = Keys_1._data[_614];
        uint _618 = _616 & 131071u;
        if ((_616 == 4294967295u) || (_618 >= _93.ParticleCount))
        {
            imageStore(OutputTexture, ivec2(_606), vec4(0.0));
            break;
        }
        vec4 _633 = Projected_1._data[_618];
        vec2 _645 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        float _648 = min(_633.z, 64.0);
        vec2 _658 = (_645 - (_633.xy * vec2(float(_93.OutputWidth), float(_93.OutputHeight)))) / vec2(max(_648, 0.001000000047497451305389404296875));
        float _661 = dot(_658, _658);
        if ((_633.z <= 0.0) || (_661 > 1.0))
        {
            imageStore(OutputTexture, ivec2(_606), vec4(0.0));
            break;
        }
        if (_93.Style == 1u)
        {
            float _684 = max(1.0 - (2.0 / max(_648, 2.0)), 0.0);
            float _687 = 1.0 / max(_648, 1.0);
            uint _698 = _618 * 2654435761u;
            uint _771 = (_698 << 13u) ^ _698;
            float _794 = clamp(float(((_771 * (((_771 * _771) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _798 = min(uint(floor(_794)), 2u);
            bvec3 _816 = bvec3(_798 == 1u);
            vec3 _817 = vec3(_816.x ? _93.Color1.xyz.x : _93.Color2.xyz.x, _816.y ? _93.Color1.xyz.y : _93.Color2.xyz.y, _816.z ? _93.Color1.xyz.z : _93.Color2.xyz.z);
            bvec3 _818 = bvec3(_798 == 0u);
            bvec3 _833 = bvec3(_798 == 1u);
            vec3 _834 = vec3(_833.x ? _93.Color2.xyz.x : _93.Color3.xyz.x, _833.y ? _93.Color2.xyz.y : _93.Color3.xyz.y, _833.z ? _93.Color2.xyz.z : _93.Color3.xyz.z);
            bvec3 _835 = bvec3(_798 == 0u);
            imageStore(OutputTexture, ivec2(_606), vec4(mix(vec3(_818.x ? _93.Color0.xyz.x : _817.x, _818.y ? _93.Color0.xyz.y : _817.y, _818.z ? _93.Color0.xyz.z : _817.z), vec3(_835.x ? _93.Color1.xyz.x : _834.x, _835.y ? _93.Color1.xyz.y : _834.y, _835.z ? _93.Color1.xyz.z : _834.z), vec3(_794 - float(_798))), clamp(_93.Opacity, 0.0, 1.0) * smoothstep(_684 - _687, _684 + _687, sqrt(_661))));
            break;
        }
        if (_93.Style == 2u)
        {
            uint _721 = _618 * 2654435761u;
            uint _847 = (_721 << 13u) ^ _721;
            float _870 = clamp(float(((_847 * (((_847 * _847) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _874 = min(uint(floor(_870)), 2u);
            bvec3 _892 = bvec3(_874 == 1u);
            vec3 _893 = vec3(_892.x ? _93.Color1.xyz.x : _93.Color2.xyz.x, _892.y ? _93.Color1.xyz.y : _93.Color2.xyz.y, _892.z ? _93.Color1.xyz.z : _93.Color2.xyz.z);
            bvec3 _894 = bvec3(_874 == 0u);
            bvec3 _909 = bvec3(_874 == 1u);
            vec3 _910 = vec3(_909.x ? _93.Color2.xyz.x : _93.Color3.xyz.x, _909.y ? _93.Color2.xyz.y : _93.Color3.xyz.y, _909.z ? _93.Color2.xyz.z : _93.Color3.xyz.z);
            bvec3 _911 = bvec3(_874 == 0u);
            imageStore(OutputTexture, ivec2(_606), vec4(mix(vec3(_894.x ? _93.Color0.xyz.x : _893.x, _894.y ? _93.Color0.xyz.y : _893.y, _894.z ? _93.Color0.xyz.z : _893.z), vec3(_911.x ? _93.Color1.xyz.x : _910.x, _911.y ? _93.Color1.xyz.y : _910.y, _911.z ? _93.Color1.xyz.z : _910.z), vec3(_870 - float(_874))), clamp(_93.Opacity, 0.0, 1.0)));
            break;
        }
        if (_93.Style == 3u)
        {
            vec3 _749 = normalize(vec3(_658.x, -_658.y, -sqrt(1.0 - _661)));
            float _956 = (0.5 * float(_93.OutputHeight)) * max(_93.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _752 = normalize(vec3((_645.x - (0.5 * float(_93.OutputWidth))) / _956, (-(_645.y - (0.5 * float(_93.OutputHeight)))) / _956, 1.0));
            uint _971 = _618 * 2654435761u;
            uint _1050 = (_971 << 13u) ^ _971;
            float _1073 = clamp(float(((_1050 * (((_1050 * _1050) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1077 = min(uint(floor(_1073)), 2u);
            bvec3 _1095 = bvec3(_1077 == 1u);
            vec3 _1096 = vec3(_1095.x ? _93.Color1.xyz.x : _93.Color2.xyz.x, _1095.y ? _93.Color1.xyz.y : _93.Color2.xyz.y, _1095.z ? _93.Color1.xyz.z : _93.Color2.xyz.z);
            bvec3 _1097 = bvec3(_1077 == 0u);
            bvec3 _1112 = bvec3(_1077 == 1u);
            vec3 _1113 = vec3(_1112.x ? _93.Color2.xyz.x : _93.Color3.xyz.x, _1112.y ? _93.Color2.xyz.y : _93.Color3.xyz.y, _1112.z ? _93.Color2.xyz.z : _93.Color3.xyz.z);
            bvec3 _1114 = bvec3(_1077 == 0u);
            vec3 _978 = normalize(_93.SunDirection.xyz);
            vec3 _1036 = ((mix(vec3(_1097.x ? _93.Color0.xyz.x : _1096.x, _1097.y ? _93.Color0.xyz.y : _1096.y, _1097.z ? _93.Color0.xyz.z : _1096.z), vec3(_1114.x ? _93.Color1.xyz.x : _1113.x, _1114.y ? _93.Color1.xyz.y : _1113.y, _1114.z ? _93.Color1.xyz.z : _1113.z), vec3(_1073 - float(_1077))) * ((_93.SunColor.xyz * clamp(dot(_749, _978), 0.0, 1.0)) + (_93.SkyColor.xyz * (_93.Ambient * mix(0.3499999940395355224609375, 1.0, (dot(_749, _93.WorldUp.xyz) * 0.5) + 0.5))))) + (_93.SunColor.xyz * (pow(clamp(dot(_749, normalize(_978 - _752)), 0.0, 1.0), 48.0) * 0.4000000059604644775390625))) + (_93.SkyColor.xyz * (pow(1.0 - clamp(dot(_749, -_752), 0.0, 1.0), 4.0) * 0.300000011920928955078125));
            imageStore(OutputTexture, ivec2(_606), vec4(_1036, clamp(_93.Opacity, 0.0, 1.0)));
            break;
        }
        imageStore(OutputTexture, ivec2(_606), vec4(0.0));
        break;
    } while(false);
}

