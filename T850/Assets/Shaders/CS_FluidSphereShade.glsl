#version 430
// GENERATED from CS_FluidSphereShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=8d7b2b94fc009165810b2431c1d855916ca007df8e213d95b2ae04478d8e22f9; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

const float _609[9] = float[](-1.0, -6.0, -14.0, -14.0, 0.0, 14.0, 14.0, 6.0, 1.0);
const float _619[9] = float[](1.0, 8.0, 28.0, 56.0, 70.0, 56.0, 28.0, 8.0, 1.0);

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
} _132;

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
        if ((gl_GlobalInvocationID.x >= _132.OutputWidth) || (gl_GlobalInvocationID.y >= _132.OutputHeight))
        {
            break;
        }
        uvec2 _1335 = uvec2(gl_GlobalInvocationID.x, (_132.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        uint _1343 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _132.OutputWidth);
        uint _1345 = Keys_1._data[_1343];
        uint _1347 = _1345 & 131071u;
        if ((_1345 == 4294967295u) || (_1347 >= _132.ParticleCount))
        {
            imageStore(OutputTexture, ivec2(_1335), vec4(0.0));
            break;
        }
        vec4 _1362 = Projected_1._data[_1347];
        vec2 _1374 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        float _1377 = min(_1362.z, 64.0);
        vec2 _1387 = (_1374 - (_1362.xy * vec2(float(_132.OutputWidth), float(_132.OutputHeight)))) / vec2(max(_1377, 0.001000000047497451305389404296875));
        float _1390 = dot(_1387, _1387);
        if ((_1362.z <= 0.0) || (_1390 > 1.0))
        {
            imageStore(OutputTexture, ivec2(_1335), vec4(0.0));
            break;
        }
        float _1404 = sqrt(1.0 - _1390);
        if (_132.Style == 1u)
        {
            float _1413 = max(1.0 - (2.0 / max(_1377, 2.0)), 0.0);
            float _1416 = 1.0 / max(_1377, 1.0);
            uint _1427 = _1347 * 2654435761u;
            uint _1514 = (_1427 << 13u) ^ _1427;
            float _1537 = clamp(float(((_1514 * (((_1514 * _1514) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1541 = min(uint(floor(_1537)), 2u);
            bvec3 _1559 = bvec3(_1541 == 1u);
            vec3 _1560 = vec3(_1559.x ? _132.Color1.xyz.x : _132.Color2.xyz.x, _1559.y ? _132.Color1.xyz.y : _132.Color2.xyz.y, _1559.z ? _132.Color1.xyz.z : _132.Color2.xyz.z);
            bvec3 _1561 = bvec3(_1541 == 0u);
            bvec3 _1576 = bvec3(_1541 == 1u);
            vec3 _1577 = vec3(_1576.x ? _132.Color2.xyz.x : _132.Color3.xyz.x, _1576.y ? _132.Color2.xyz.y : _132.Color3.xyz.y, _1576.z ? _132.Color2.xyz.z : _132.Color3.xyz.z);
            bvec3 _1578 = bvec3(_1541 == 0u);
            imageStore(OutputTexture, ivec2(_1335), vec4(mix(vec3(_1561.x ? _132.Color0.xyz.x : _1560.x, _1561.y ? _132.Color0.xyz.y : _1560.y, _1561.z ? _132.Color0.xyz.z : _1560.z), vec3(_1578.x ? _132.Color1.xyz.x : _1577.x, _1578.y ? _132.Color1.xyz.y : _1577.y, _1578.z ? _132.Color1.xyz.z : _1577.z), vec3(_1537 - float(_1541))), clamp(_132.Opacity, 0.0, 1.0) * smoothstep(_1413 - _1416, _1413 + _1416, sqrt(_1390))));
            break;
        }
        if (_132.Style == 2u)
        {
            uint _1450 = _1347 * 2654435761u;
            uint _1590 = (_1450 << 13u) ^ _1450;
            float _1613 = clamp(float(((_1590 * (((_1590 * _1590) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1617 = min(uint(floor(_1613)), 2u);
            bvec3 _1635 = bvec3(_1617 == 1u);
            vec3 _1636 = vec3(_1635.x ? _132.Color1.xyz.x : _132.Color2.xyz.x, _1635.y ? _132.Color1.xyz.y : _132.Color2.xyz.y, _1635.z ? _132.Color1.xyz.z : _132.Color2.xyz.z);
            bvec3 _1637 = bvec3(_1617 == 0u);
            bvec3 _1652 = bvec3(_1617 == 1u);
            vec3 _1653 = vec3(_1652.x ? _132.Color2.xyz.x : _132.Color3.xyz.x, _1652.y ? _132.Color2.xyz.y : _132.Color3.xyz.y, _1652.z ? _132.Color2.xyz.z : _132.Color3.xyz.z);
            bvec3 _1654 = bvec3(_1617 == 0u);
            imageStore(OutputTexture, ivec2(_1335), vec4(mix(vec3(_1637.x ? _132.Color0.xyz.x : _1636.x, _1637.y ? _132.Color0.xyz.y : _1636.y, _1637.z ? _132.Color0.xyz.z : _1636.z), vec3(_1654.x ? _132.Color1.xyz.x : _1653.x, _1654.y ? _132.Color1.xyz.y : _1653.y, _1654.z ? _132.Color1.xyz.z : _1653.z), vec3(_1613 - float(_1617))), clamp(_132.Opacity, 0.0, 1.0)));
            break;
        }
        if (_132.Style == 3u)
        {
            vec3 _1478 = normalize(vec3(_1387.x, -_1387.y, -_1404));
            float _1699 = (0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _1481 = normalize(vec3((_1374.x - (0.5 * float(_132.OutputWidth))) / _1699, (-(_1374.y - (0.5 * float(_132.OutputHeight)))) / _1699, 1.0));
            uint _1714 = _1347 * 2654435761u;
            uint _1793 = (_1714 << 13u) ^ _1714;
            float _1816 = clamp(float(((_1793 * (((_1793 * _1793) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1820 = min(uint(floor(_1816)), 2u);
            bvec3 _1838 = bvec3(_1820 == 1u);
            vec3 _1839 = vec3(_1838.x ? _132.Color1.xyz.x : _132.Color2.xyz.x, _1838.y ? _132.Color1.xyz.y : _132.Color2.xyz.y, _1838.z ? _132.Color1.xyz.z : _132.Color2.xyz.z);
            bvec3 _1840 = bvec3(_1820 == 0u);
            bvec3 _1855 = bvec3(_1820 == 1u);
            vec3 _1856 = vec3(_1855.x ? _132.Color2.xyz.x : _132.Color3.xyz.x, _1855.y ? _132.Color2.xyz.y : _132.Color3.xyz.y, _1855.z ? _132.Color2.xyz.z : _132.Color3.xyz.z);
            bvec3 _1857 = bvec3(_1820 == 0u);
            vec3 _1721 = normalize(_132.SunDirection.xyz);
            vec3 _1779 = ((mix(vec3(_1840.x ? _132.Color0.xyz.x : _1839.x, _1840.y ? _132.Color0.xyz.y : _1839.y, _1840.z ? _132.Color0.xyz.z : _1839.z), vec3(_1857.x ? _132.Color1.xyz.x : _1856.x, _1857.y ? _132.Color1.xyz.y : _1856.y, _1857.z ? _132.Color1.xyz.z : _1856.z), vec3(_1816 - float(_1820))) * ((_132.SunColor.xyz * clamp(dot(_1478, _1721), 0.0, 1.0)) + (_132.SkyColor.xyz * (_132.Ambient * mix(0.3499999940395355224609375, 1.0, (dot(_1478, _132.WorldUp.xyz) * 0.5) + 0.5))))) + (_132.SunColor.xyz * (pow(clamp(dot(_1478, normalize(_1721 - _1481)), 0.0, 1.0), 48.0) * 0.4000000059604644775390625))) + (_132.SkyColor.xyz * (pow(1.0 - clamp(dot(_1478, -_1481), 0.0, 1.0), 4.0) * 0.300000011920928955078125));
            imageStore(OutputTexture, ivec2(_1335), vec4(_1779, clamp(_132.Opacity, 0.0, 1.0)));
            break;
        }
        ivec2 _1493 = ivec2(gl_GlobalInvocationID.xy);
        float _1501 = max(_1362.w - (_132.Radius * _1404), 0.0500000007450580596923828125);
        int _1903 = clamp(int((_1377 * 0.25) + 0.5), 1, 16);
        float _2728;
        _2728 = 0.0;
        float _2751;
        SPIRV_CROSS_UNROLL
        for (int _2727 = -2; _2727 <= 2; _2728 = _2751, _2727++)
        {
            float _2119 = (_2727 == 0) ? 6.0 : ((abs(_2727) == 1) ? 4.0 : 1.0);
            _2751 = _2728;
            float _2169;
            SPIRV_CROSS_UNROLL
            for (int _2747 = -2; _2747 <= 2; _2751 = _2169, _2747++)
            {
                ivec2 _2161 = _1493 + (ivec2(_2747, _2727) * ivec2(_1903));
                float _2749;
                do
                {
                    if ((((_2161.x < 0) || (_2161.y < 0)) || (_2161.x >= int(_132.OutputWidth))) || (_2161.y >= int(_132.OutputHeight)))
                    {
                        _2749 = 0.0;
                        break;
                    }
                    _2749 = float(ThicknessBits_1._data[uint(_2161.x) + (uint(_2161.y) * _132.OutputWidth)]) * 0.0009765625;
                    break;
                } while(false);
                _2169 = _2751 + ((((_2747 == 0) ? 6.0 : ((abs(_2747) == 1) ? 4.0 : 1.0)) * _2119) * _2749);
            }
        }
        float _1912 = max(_2728 * 0.00390625, 0.0) * max(_132.ThicknessScale, 0.0);
        float _2730;
        float _2731;
        _2731 = 0.0;
        _2730 = 0.0;
        float _2744;
        float _2746;
        SPIRV_CROSS_UNROLL
        for (int _2729 = 0; _2729 < 9; _2731 = _2746, _2730 = _2744, _2729++)
        {
            _2746 = _2731;
            _2744 = _2730;
            float _2292;
            float _2303;
            SPIRV_CROSS_UNROLL
            for (int _2738 = 0; _2738 < 9; _2746 = _2303, _2744 = _2292, _2738++)
            {
                ivec2 _2280 = _1493 + (ivec2(_2738 - 4, _2729 - 4) * ivec2(_1903));
                float _2740;
                do
                {
                    if ((((_2280.x < 0) || (_2280.y < 0)) || (_2280.x >= int(_132.OutputWidth))) || (_2280.y >= int(_132.OutputHeight)))
                    {
                        _2740 = 0.0;
                        break;
                    }
                    _2740 = float(ThicknessBits_1._data[uint(_2280.x) + (uint(_2280.y) * _132.OutputWidth)]) * 0.0009765625;
                    break;
                } while(false);
                _2292 = _2744 + ((_609[_2738] * _619[_2729]) * _2740);
                _2303 = _2746 + ((_619[_2738] * _609[_2729]) * _2740);
            }
        }
        vec2 _1923 = ((vec2(_2730, _2731) * (3.0517578125e-05 / float(_1903))) * 0.5) / vec2(_1501 / ((0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05)));
        vec3 _1930 = normalize(vec3(-_1923.x, _1923.y, -1.0));
        float _2412 = (0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec3 _2402 = vec3(((_1374.x - (0.5 * float(_132.OutputWidth))) * _1501) / _2412, ((-(_1374.y - (0.5 * float(_132.OutputHeight)))) * _1501) / _2412, _1501);
        vec3 _1935 = normalize(_2402);
        vec2 _1953 = _1923 / vec2(1.0 + (_1912 / (8.0 * _132.Radius)));
        vec3 _1962 = refract(_1935, normalize(vec3(-_1953.x, _1953.y, -1.0)), 0.75187969207763671875);
        bvec3 _2756 = bvec3(dot(_1962, _1962) < 9.9999999747524270787835121154785e-07);
        vec3 _1979 = _2402 + (vec3(_2756.x ? _1935.x : _1962.x, _2756.y ? _1935.y : _1962.y, _2756.z ? _1935.z : _1962.z) * min(_1912 * max(_132.Refraction, 0.0), 2.0));
        float _2452 = (0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec2 _2442 = vec2(((_1979.x * _2452) / _1979.z) + (0.5 * float(_132.OutputWidth)), (((-_1979.y) * _2452) / _1979.z) + (0.5 * float(_132.OutputHeight)));
        bvec2 _1986 = bvec2(_1979.z > 0.0500000007450580596923828125);
        vec2 _1992 = clamp(vec2(_1986.x ? _2442.x : _1374.x, _1986.y ? _2442.y : _1374.y), vec2(0.5), vec2(float(_132.OutputWidth), float(_132.OutputHeight)) - vec2(0.5));
        ivec2 _2467 = clamp(ivec2(_1992), ivec2(0), ivec2(int(_132.OutputWidth) - 1, int(_132.OutputHeight) - 1));
        vec4 _2493 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2467.x, int((_132.OutputHeight - 1u) - uint(_2467.y)), 0).xy, 0);
        float _2494 = _2493.x;
        bvec2 _2758 = bvec2((((_2494 > 0.0) && (abs(_2494 - _132.ProjA) > 9.999999717180685365747194737196e-10)) ? (_132.ProjB / (_2494 - _132.ProjA)) : 1000000000.0) < _1501);
        vec2 _2526 = clamp(vec2(_2758.x ? _1374.x : _1992.x, _2758.y ? _1374.y : _1992.y) / vec2(float(_132.OutputWidth), float(_132.OutputHeight)), vec2(0.0), vec2(1.0));
        vec3 _2010 = exp(max(_132.FluidTint.xyz, vec3(0.0)) * (-_1912));
        float _2735;
        vec3 _2736;
        _2736 = vec3(0.0);
        _2735 = 0.0;
        float _2753;
        vec3 _2754;
        SPIRV_CROSS_UNROLL
        for (int _2734 = 1; _2734 <= 3; _2736 = _2754, _2735 = _2753, _2734++)
        {
            ivec2 _2557 = ivec2((int(_132.OutputWidth) * _2734) / 4, 1);
            ivec2 _2601 = clamp(_2557, ivec2(0), ivec2(int(_132.OutputWidth) - 1, int(_132.OutputHeight) - 1));
            vec4 _2627 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2601.x, int((_132.OutputHeight - 1u) - uint(_2601.y)), 0).xy, 0);
            float _2628 = _2627.x;
            if ((((_2628 > 0.0) && (abs(_2628 - _132.ProjA) > 9.999999717180685365747194737196e-10)) ? (_132.ProjB / (_2628 - _132.ProjA)) : 1000000000.0) > 100000000.0)
            {
                vec2 _2660 = clamp((vec2(_2557) + vec2(0.5)) / vec2(float(_132.OutputWidth), float(_132.OutputHeight)), vec2(0.0), vec2(1.0));
                _2754 = _2736 + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_2660.x, 1.0 - _2660.y), 0.0).xyz;
                _2753 = _2735 + 1.0;
            }
            else
            {
                _2754 = _2736;
                _2753 = _2735;
            }
        }
        vec3 _2581 = _2736 / vec3(_2735);
        bvec3 _2585 = bvec3(_2735 > 0.0);
        vec3 _2586 = vec3(_2585.x ? _2581.x : _132.SkyColor.xyz.x, _2585.y ? _2581.y : _132.SkyColor.xyz.y, _2585.z ? _2581.z : _132.SkyColor.xyz.z);
        imageStore(OutputTexture, ivec2(_1335), vec4(mix((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_2526.x, 1.0 - _2526.y), 0.0).xyz * _2010) + ((vec3(1.0) - _2010) * _132.FluidColor.xyz), mix(_2586 * 0.25, _2586, vec3(smoothstep(-0.100000001490116119384765625, 0.20000000298023223876953125, dot(reflect(_1935, _1930), _132.WorldUp.xyz)))), vec3(clamp(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_1930, -_1935), 0.0, 1.0), max(_132.FresnelPower, 1.0))), 0.0, 1.0))) + ((_132.SunColor.xyz * pow(clamp(dot(_1930, normalize(normalize(_132.SunDirection.xyz) - _1935)), 0.0, 1.0), 96.0)) * _132.Specular), smoothstep(0.0, max((0.699999988079071044921875 * _132.Radius) * _132.InvPackingFactor, 9.9999997473787516355514526367188e-05), _1912) * clamp(_132.Opacity, 0.0, 1.0)));
        break;
    } while(false);
}

