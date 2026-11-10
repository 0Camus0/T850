#version 430
// GENERATED from /home/camus000/testBitso/T850/Assets/Shaders/CS_FluidSphereShade.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=e94e3ea34aab8bb4279b6b5dcef1fa1feb9a72cae12bb8dec99aeb58ea3360ff; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
        uvec2 _1250 = uvec2(gl_GlobalInvocationID.x, (_132.OutputHeight - 1u) - gl_GlobalInvocationID.y);
        uint _1258 = gl_GlobalInvocationID.x + (gl_GlobalInvocationID.y * _132.OutputWidth);
        uint _1260 = Keys_1._data[_1258];
        uint _1262 = _1260 & 131071u;
        if ((_1260 == 4294967295u) || (_1262 >= _132.ParticleCount))
        {
            imageStore(OutputTexture, ivec2(_1250), vec4(0.0));
            break;
        }
        vec4 _1277 = Projected_1._data[_1262];
        vec2 _1289 = vec2(gl_GlobalInvocationID.xy) + vec2(0.5);
        float _1292 = min(_1277.z, 64.0);
        vec2 _1302 = (_1289 - (_1277.xy * vec2(float(_132.OutputWidth), float(_132.OutputHeight)))) / vec2(max(_1292, 0.001000000047497451305389404296875));
        float _1305 = dot(_1302, _1302);
        if ((_1277.z <= 0.0) || (_1305 > 1.0))
        {
            imageStore(OutputTexture, ivec2(_1250), vec4(0.0));
            break;
        }
        float _1319 = sqrt(1.0 - _1305);
        if (_132.Style == 3u)
        {
            vec3 _1332 = normalize(vec3(_1302.x, -_1302.y, -_1319));
            float _1401 = (0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05);
            vec3 _1335 = normalize(vec3((_1289.x - (0.5 * float(_132.OutputWidth))) / _1401, (-(_1289.y - (0.5 * float(_132.OutputHeight)))) / _1401, 1.0));
            uint _1416 = _1262 * 2654435761u;
            uint _1495 = (_1416 << 13u) ^ _1416;
            float _1518 = clamp(float(((_1495 * (((_1495 * _1495) * 15731u) + 789221u)) + 1376312589u) & 2147483647u) * 1.6298144778215117867148364894092e-10, 0.0, 1.0) * 3.0;
            uint _1522 = min(uint(floor(_1518)), 2u);
            bvec3 _1540 = bvec3(_1522 == 1u);
            vec3 _1541 = vec3(_1540.x ? _132.Color1.xyz.x : _132.Color2.xyz.x, _1540.y ? _132.Color1.xyz.y : _132.Color2.xyz.y, _1540.z ? _132.Color1.xyz.z : _132.Color2.xyz.z);
            bvec3 _1542 = bvec3(_1522 == 0u);
            bvec3 _1557 = bvec3(_1522 == 1u);
            vec3 _1558 = vec3(_1557.x ? _132.Color2.xyz.x : _132.Color3.xyz.x, _1557.y ? _132.Color2.xyz.y : _132.Color3.xyz.y, _1557.z ? _132.Color2.xyz.z : _132.Color3.xyz.z);
            bvec3 _1559 = bvec3(_1522 == 0u);
            vec3 _1423 = normalize(_132.SunDirection.xyz);
            vec3 _1481 = ((mix(vec3(_1542.x ? _132.Color0.xyz.x : _1541.x, _1542.y ? _132.Color0.xyz.y : _1541.y, _1542.z ? _132.Color0.xyz.z : _1541.z), vec3(_1559.x ? _132.Color1.xyz.x : _1558.x, _1559.y ? _132.Color1.xyz.y : _1558.y, _1559.z ? _132.Color1.xyz.z : _1558.z), vec3(_1518 - float(_1522))) * ((_132.SunColor.xyz * clamp(dot(_1332, _1423), 0.0, 1.0)) + (_132.SkyColor.xyz * (_132.Ambient * mix(0.3499999940395355224609375, 1.0, (dot(_1332, _132.WorldUp.xyz) * 0.5) + 0.5))))) + (_132.SunColor.xyz * (pow(clamp(dot(_1332, normalize(_1423 - _1335)), 0.0, 1.0), 48.0) * 0.4000000059604644775390625))) + (_132.SkyColor.xyz * (pow(1.0 - clamp(dot(_1332, -_1335), 0.0, 1.0), 4.0) * 0.300000011920928955078125));
            imageStore(OutputTexture, ivec2(_1250), vec4(_1481, clamp(_132.Opacity, 0.0, 1.0)));
            break;
        }
        ivec2 _1347 = ivec2(gl_GlobalInvocationID.xy);
        float _1355 = max(_1277.w - (_132.Radius * _1319), 0.0500000007450580596923828125);
        int _1605 = clamp(int((_1292 * 0.25) + 0.5), 1, 16);
        float _2430;
        _2430 = 0.0;
        float _2453;
        SPIRV_CROSS_UNROLL
        for (int _2429 = -2; _2429 <= 2; _2430 = _2453, _2429++)
        {
            float _1821 = (_2429 == 0) ? 6.0 : ((abs(_2429) == 1) ? 4.0 : 1.0);
            _2453 = _2430;
            float _1871;
            SPIRV_CROSS_UNROLL
            for (int _2449 = -2; _2449 <= 2; _2453 = _1871, _2449++)
            {
                ivec2 _1863 = _1347 + (ivec2(_2449, _2429) * ivec2(_1605));
                float _2451;
                do
                {
                    if ((((_1863.x < 0) || (_1863.y < 0)) || (_1863.x >= int(_132.OutputWidth))) || (_1863.y >= int(_132.OutputHeight)))
                    {
                        _2451 = 0.0;
                        break;
                    }
                    _2451 = float(ThicknessBits_1._data[uint(_1863.x) + (uint(_1863.y) * _132.OutputWidth)]) * 0.0009765625;
                    break;
                } while(false);
                _1871 = _2453 + ((((_2449 == 0) ? 6.0 : ((abs(_2449) == 1) ? 4.0 : 1.0)) * _1821) * _2451);
            }
        }
        float _1614 = max(_2430 * 0.00390625, 0.0) * max(_132.ThicknessScale, 0.0);
        float _2432;
        float _2433;
        _2433 = 0.0;
        _2432 = 0.0;
        float _2446;
        float _2448;
        SPIRV_CROSS_UNROLL
        for (int _2431 = 0; _2431 < 9; _2433 = _2448, _2432 = _2446, _2431++)
        {
            _2448 = _2433;
            _2446 = _2432;
            float _1994;
            float _2005;
            SPIRV_CROSS_UNROLL
            for (int _2440 = 0; _2440 < 9; _2448 = _2005, _2446 = _1994, _2440++)
            {
                ivec2 _1982 = _1347 + (ivec2(_2440 - 4, _2431 - 4) * ivec2(_1605));
                float _2442;
                do
                {
                    if ((((_1982.x < 0) || (_1982.y < 0)) || (_1982.x >= int(_132.OutputWidth))) || (_1982.y >= int(_132.OutputHeight)))
                    {
                        _2442 = 0.0;
                        break;
                    }
                    _2442 = float(ThicknessBits_1._data[uint(_1982.x) + (uint(_1982.y) * _132.OutputWidth)]) * 0.0009765625;
                    break;
                } while(false);
                _1994 = _2446 + ((_609[_2440] * _619[_2431]) * _2442);
                _2005 = _2448 + ((_619[_2440] * _609[_2431]) * _2442);
            }
        }
        vec2 _1625 = ((vec2(_2432, _2433) * (3.0517578125e-05 / float(_1605))) * 0.5) / vec2(_1355 / ((0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05)));
        vec3 _1632 = normalize(vec3(-_1625.x, _1625.y, -1.0));
        float _2114 = (0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec3 _2104 = vec3(((_1289.x - (0.5 * float(_132.OutputWidth))) * _1355) / _2114, ((-(_1289.y - (0.5 * float(_132.OutputHeight)))) * _1355) / _2114, _1355);
        vec3 _1637 = normalize(_2104);
        vec2 _1655 = _1625 / vec2(1.0 + (_1614 / (8.0 * _132.Radius)));
        vec3 _1664 = refract(_1637, normalize(vec3(-_1655.x, _1655.y, -1.0)), 0.75187969207763671875);
        bvec3 _2458 = bvec3(dot(_1664, _1664) < 9.9999999747524270787835121154785e-07);
        vec3 _1681 = _2104 + (vec3(_2458.x ? _1637.x : _1664.x, _2458.y ? _1637.y : _1664.y, _2458.z ? _1637.z : _1664.z) * min(_1614 * max(_132.Refraction, 0.0), 2.0));
        float _2154 = (0.5 * float(_132.OutputHeight)) * max(_132.ProjScaleY, 9.9999997473787516355514526367188e-05);
        vec2 _2144 = vec2(((_1681.x * _2154) / _1681.z) + (0.5 * float(_132.OutputWidth)), (((-_1681.y) * _2154) / _1681.z) + (0.5 * float(_132.OutputHeight)));
        bvec2 _1688 = bvec2(_1681.z > 0.0500000007450580596923828125);
        vec2 _1694 = clamp(vec2(_1688.x ? _2144.x : _1289.x, _1688.y ? _2144.y : _1289.y), vec2(0.5), vec2(float(_132.OutputWidth), float(_132.OutputHeight)) - vec2(0.5));
        ivec2 _2169 = clamp(ivec2(_1694), ivec2(0), ivec2(int(_132.OutputWidth) - 1, int(_132.OutputHeight) - 1));
        vec4 _2195 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2169.x, int((_132.OutputHeight - 1u) - uint(_2169.y)), 0).xy, 0);
        float _2196 = _2195.x;
        bvec2 _2460 = bvec2((((_2196 > 0.0) && (abs(_2196 - _132.ProjA) > 9.999999717180685365747194737196e-10)) ? (_132.ProjB / (_2196 - _132.ProjA)) : 1000000000.0) < _1355);
        vec2 _2228 = clamp(vec2(_2460.x ? _1289.x : _1694.x, _2460.y ? _1289.y : _1694.y) / vec2(float(_132.OutputWidth), float(_132.OutputHeight)), vec2(0.0), vec2(1.0));
        vec3 _1712 = exp(max(_132.FluidTint.xyz, vec3(0.0)) * (-_1614));
        float _2437;
        vec3 _2438;
        _2438 = vec3(0.0);
        _2437 = 0.0;
        float _2455;
        vec3 _2456;
        SPIRV_CROSS_UNROLL
        for (int _2436 = 1; _2436 <= 3; _2438 = _2456, _2437 = _2455, _2436++)
        {
            ivec2 _2259 = ivec2((int(_132.OutputWidth) * _2436) / 4, 1);
            ivec2 _2303 = clamp(_2259, ivec2(0), ivec2(int(_132.OutputWidth) - 1, int(_132.OutputHeight) - 1));
            vec4 _2329 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2303.x, int((_132.OutputHeight - 1u) - uint(_2303.y)), 0).xy, 0);
            float _2330 = _2329.x;
            if ((((_2330 > 0.0) && (abs(_2330 - _132.ProjA) > 9.999999717180685365747194737196e-10)) ? (_132.ProjB / (_2330 - _132.ProjA)) : 1000000000.0) > 100000000.0)
            {
                vec2 _2362 = clamp((vec2(_2259) + vec2(0.5)) / vec2(float(_132.OutputWidth), float(_132.OutputHeight)), vec2(0.0), vec2(1.0));
                _2456 = _2438 + textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_2362.x, 1.0 - _2362.y), 0.0).xyz;
                _2455 = _2437 + 1.0;
            }
            else
            {
                _2456 = _2438;
                _2455 = _2437;
            }
        }
        vec3 _2283 = _2438 / vec3(_2437);
        bvec3 _2287 = bvec3(_2437 > 0.0);
        vec3 _2288 = vec3(_2287.x ? _2283.x : _132.SkyColor.xyz.x, _2287.y ? _2283.y : _132.SkyColor.xyz.y, _2287.z ? _2283.z : _132.SkyColor.xyz.z);
        imageStore(OutputTexture, ivec2(_1250), vec4(mix((textureLod(SPIRV_Cross_CombinedSceneColorLinearSampler, vec2(_2228.x, 1.0 - _2228.y), 0.0).xyz * _1712) + ((vec3(1.0) - _1712) * _132.FluidColor.xyz), mix(_2288 * 0.25, _2288, vec3(smoothstep(-0.100000001490116119384765625, 0.20000000298023223876953125, dot(reflect(_1637, _1632), _132.WorldUp.xyz)))), vec3(clamp(0.0199999995529651641845703125 + (0.980000019073486328125 * pow(1.0 - clamp(dot(_1632, -_1637), 0.0, 1.0), max(_132.FresnelPower, 1.0))), 0.0, 1.0))) + ((_132.SunColor.xyz * pow(clamp(dot(_1632, normalize(normalize(_132.SunDirection.xyz) - _1637)), 0.0, 1.0), 96.0)) * _132.Specular), smoothstep(0.0, max((0.699999988079071044921875 * _132.Radius) * _132.InvPackingFactor, 9.9999997473787516355514526367188e-05), _1614) * clamp(_132.Opacity, 0.0, 1.0)));
        break;
    } while(false);
}

