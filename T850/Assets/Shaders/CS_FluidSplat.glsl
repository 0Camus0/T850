#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=af2ccbf84581520c4051958b103693e123d41534d418176550219f998793acf8; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0, std140) uniform ParticleConstants
{
    uint ParticleCount;
    uint OutputWidth;
    uint OutputHeight;
    uint Iteration;
    float Radius;
    float Opacity;
    float Intensity;
    float Softness;
    float DepthBias;
    float Lifetime;
    float Spread;
    float Rise;
    float TimeSeconds;
    float SizeScaleMin;
    float SizeScaleMax;
    uint Shape;
    uint Blend;
    uint Source;
    float ScreenSpace;
    float TileCountX;
    float TileCountY;
    uint FrameIndex;
    float ProjScaleY;
    float Style;
    float DepthRadiusScale;
    float InvPackingFactor;
    float ProjA;
    float ProjB;
    vec3 Color0;
    float ProjScaleX;
    vec3 Color1;
    float AuxEnabled;
    vec3 Color2;
    float Color2Pad;
    vec3 Color3;
    float Color3Pad;
    vec3 EmitterPosition;
    float Anisotropic;
    vec4 ViewProjection[4];
} _117;

layout(binding = 1, std430) readonly buffer Projected
{
    vec4 _data[];
} Projected_1;

layout(binding = 3, std430) buffer DepthBits
{
    uint _data[];
} DepthBits_1;

layout(binding = 4, std430) buffer ThicknessBits
{
    uint _data[];
} ThicknessBits_1;

layout(binding = 5, std430) readonly buffer Anisotropy
{
    vec4 _data[];
} Anisotropy_1;

layout(binding = 6, std430) readonly buffer Whitewater
{
    vec4 _data[];
} Whitewater_1;

layout(binding = 7, std430) buffer AuxBits
{
    uint _data[];
} AuxBits_1;

layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _117.ParticleCount)
        {
            break;
        }
        vec4 _1553 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_1553.z <= 0.0)
        {
            break;
        }
        uvec2 _1560 = uvec2(textureSize(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, 0));
        uint _1562 = _1560.x;
        uint _1565 = _1560.y;
        vec2 _1579 = _1553.xy * vec2(float(_117.OutputWidth), float(_117.OutputHeight));
        float _1591 = min((_1553.z * float(_117.OutputHeight)) / float(max(_1565, 1u)), 64.0);
        float _1594 = max(_117.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _1603 = ivec2(int(_117.OutputWidth) - 1, int(_117.OutputHeight) - 1);
        if (!(_117.ScreenSpace > 0.5))
        {
            if (_1591 < 0.5)
            {
                break;
            }
            ivec2 _1617 = max(ivec2(floor(_1579 - vec2(_1591))), ivec2(0));
            ivec2 _1625 = min(ivec2(ceil(_1579 + vec2(_1591))), _1603);
            if ((_1617.x > _1625.x) || (_1617.y > _1625.y))
            {
                break;
            }
            float _1642 = max(_1591 * _1591, 9.9999999747524270787835121154785e-07);
            int _1644 = _1617.y;
            for (int _2970 = _1644; _2970 <= _1625.y; _2970++)
            {
                int _1653 = _1617.x;
                for (int _2971 = _1653; _2971 <= _1625.x; _2971++)
                {
                    vec2 _1669 = vec2(float(_2971) + 0.5, float(_2970) + 0.5) - _1579;
                    float _1672 = dot(_1669, _1669);
                    if (_1672 > _1642)
                    {
                        continue;
                    }
                    float _1683 = sqrt(max(1.0 - (_1672 / _1642), 0.0));
                    float _1689 = _1553.w - (_1683 * _1594);
                    float _2978;
                    do
                    {
                        if ((_1562 == _117.OutputWidth) && (_1565 == _117.OutputHeight))
                        {
                            vec4 _2563 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2971, int((_1565 - 1u) - uint(_2970)), 0).xy, 0);
                            float _2564 = _2563.x;
                            _2978 = ((_2564 > 0.0) && (abs(_2564 - _117.ProjA) > 9.999999717180685365747194737196e-10)) ? (_117.ProjB / (_2564 - _117.ProjA)) : 1000000015047466219876688855040.0;
                            break;
                        }
                        vec2 _2469 = vec2(_1560) / vec2(float(_117.OutputWidth), float(_117.OutputHeight));
                        ivec2 _2478 = ivec2(floor(vec2(float(_2971), float(_2970)) * _2469));
                        ivec2 _2496 = min((ivec2(ceil(vec2(float(_2971 + 1), float(_2970 + 1)) * _2469)) - ivec2(1)), (ivec2(_1560) - ivec2(1)));
                        int _2973;
                        float _2974;
                        _2974 = 0.0;
                        _2973 = _2478.y;
                        float _2977;
                        SPIRV_CROSS_LOOP
                        for (; _2973 <= min(_2496.y, (_2478.y + 2)); _2974 = _2977, _2973++)
                        {
                            int _2511 = _2478.x;
                            _2977 = _2974;
                            SPIRV_CROSS_LOOP
                            for (int _2975 = _2511; _2975 <= min(_2496.x, (_2478.x + 2)); )
                            {
                                vec4 _2604 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2975, int((_1565 - 1u) - uint(_2973)), 0).xy, 0);
                                float _2605 = _2604.x;
                                _2977 = max(_2977, ((_2605 > 0.0) && (abs(_2605 - _117.ProjA) > 9.999999717180685365747194737196e-10)) ? (_117.ProjB / (_2605 - _117.ProjA)) : 1000000015047466219876688855040.0);
                                _2975++;
                                continue;
                            }
                        }
                        _2978 = _2974;
                        break;
                    } while(false);
                    if (_1689 >= _2978)
                    {
                        continue;
                    }
                    uint _1705 = uint(_2971) + (uint(_2970) * _117.OutputWidth);
                    uint _1711 = atomicMin(DepthBits_1._data[_1705], (uint(clamp((log2(max(_1689, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _1723 = atomicAdd(ThicknessBits_1._data[_1705], uint((((2.0 * _1683) * _1594) * _117.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _1737 = gl_GlobalInvocationID.x * 3u;
        vec4 _1740 = Anisotropy_1._data[_1737];
        bvec4 _1741 = bvec4(_117.Anisotropic > 0.5);
        vec4 _1742 = vec4(_1741.x ? _1740.x : vec4(0.0).x, _1741.y ? _1740.y : vec4(0.0).y, _1741.z ? _1740.z : vec4(0.0).z, _1741.w ? _1740.w : vec4(0.0).w);
        if (_1742.w < 0.0)
        {
            break;
        }
        vec4 _1753 = Whitewater_1._data[gl_GlobalInvocationID.x];
        bvec4 _1754 = bvec4(_117.AuxEnabled > 0.5);
        vec4 _1755 = vec4(_1754.x ? _1753.x : vec4(0.0).x, _1754.y ? _1753.y : vec4(0.0).y, _1754.z ? _1753.z : vec4(0.0).z, _1754.w ? _1753.w : vec4(0.0).w);
        float _1767 = Anisotropy_1._data[(gl_GlobalInvocationID.x * 3u) + 2u].w;
        float _1768 = ((_117.AuxEnabled > 0.5) && (_117.Anisotropic > 0.5)) ? _1767 : 0.0;
        bool _1771 = _1755.x > 0.0199999995529651641845703125;
        bool _1774 = _1755.y > 0.0199999995529651641845703125;
        bool _1777 = _1755.z > 0.0199999995529651641845703125;
        bool _1779 = _1768 > 0.0199999995529651641845703125;
        bool _1786 = ((_1771 || _1774) || _1777) || _1779;
        if (_1591 >= 0.5)
        {
            ivec2 _1796 = max(ivec2(floor(_1579 - vec2(_1591))), ivec2(0));
            ivec2 _1804 = min(ivec2(ceil(_1579 + vec2(_1591))), _1603);
            float _1808 = max(_1591 * _1591, 9.9999999747524270787835121154785e-07);
            int _1810 = _1796.y;
            for (int _2953 = _1810; _2953 <= _1804.y; _2953++)
            {
                int _1819 = _1796.x;
                for (int _2962 = _1819; _2962 <= _1804.x; _2962++)
                {
                    vec2 _1835 = vec2(float(_2962) + 0.5, float(_2953) + 0.5) - _1579;
                    float _1838 = dot(_1835, _1835);
                    if (_1838 > _1808)
                    {
                        continue;
                    }
                    float _1849 = sqrt(max(1.0 - (_1838 / _1808), 0.0));
                    uint _1857 = uint(_2962) + (uint(_2953) * _117.OutputWidth);
                    float _1864 = ((2.0 * _1849) * _1594) * _117.InvPackingFactor;
                    uint _1870 = atomicAdd(ThicknessBits_1._data[_1857], uint(_1864 * 1024.0));
                    if (_1786)
                    {
                        if (_1771)
                        {
                            uint _1886 = atomicMax(AuxBits_1._data[_1857 * 4u], uint(clamp(_1755.x * _1849, 0.0, 1.0) * 65535.0));
                        }
                        if (_1774)
                        {
                            uint _1901 = atomicAdd(AuxBits_1._data[(_1857 * 4u) + 1u], uint((min(_1755.y, 2.0) * _1864) * 1024.0));
                        }
                        if (_1777)
                        {
                            uint _1915 = atomicAdd(AuxBits_1._data[(_1857 * 4u) + 2u], uint((_1755.z * _1864) * 1024.0));
                        }
                        if (_1779)
                        {
                            uint _1928 = atomicAdd(AuxBits_1._data[(_1857 * 4u) + 3u], uint((_1768 * _1864) * 1024.0));
                        }
                    }
                }
            }
        }
        float _1947 = (0.5 * float(_117.OutputHeight)) * max(_117.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _1955 = (0.5 * float(_117.OutputWidth)) * max(_117.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _1963 = vec2(float(_117.OutputWidth), float(_117.OutputHeight)) * 0.5;
        if (_1742.w > 0.0)
        {
            uint _1970 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _1975 = (gl_GlobalInvocationID.x * 3u) + 2u;
            mat3 _2030 = mat3(vec3(_117.ViewProjection[0].x / max(_117.ProjScaleX, 9.9999999747524270787835121154785e-07), _117.ViewProjection[1].x / max(_117.ProjScaleX, 9.9999999747524270787835121154785e-07), _117.ViewProjection[2].x / max(_117.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_117.ViewProjection[0].y / max(_117.ProjScaleY, 9.9999999747524270787835121154785e-07), _117.ViewProjection[1].y / max(_117.ProjScaleY, 9.9999999747524270787835121154785e-07), _117.ViewProjection[2].y / max(_117.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_117.ViewProjection[0].w, _117.ViewProjection[1].w, _117.ViewProjection[2].w));
            mat3 _2036 = transpose(_2030) * (mat3(vec3(Anisotropy_1._data[_1970].xyz), vec3(Anisotropy_1._data[_1970].yw, Anisotropy_1._data[_1975].x), vec3(Anisotropy_1._data[_1970].z, Anisotropy_1._data[_1975].xy)) * _2030);
            vec4 _2712 = (((_117.ViewProjection[0] * _1742.x) + (_117.ViewProjection[1] * _1742.y)) + (_117.ViewProjection[2] * _1742.z)) + _117.ViewProjection[3];
            float _2721 = _2712.x / max(_117.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _2727 = _2712.y / max(_117.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _2729 = _2712.w;
            vec3 _2730 = vec3(_2721, _2727, _2729);
            float _2041 = _1742.w;
            if ((_2729 - _2041) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _2066 = vec2((_2721 * _1955) / _2729, ((-_2727) * _1947) / _2729) + _1963;
            float _2075 = min((_2041 * _1947) / (_2729 - _2041), 64.0);
            if (_2075 < 0.5)
            {
                break;
            }
            float _2747 = _2036[0].x;
            float _2749 = _2036[0].y;
            float _2751 = _2036[0].z;
            float _2753 = _2036[1].y;
            float _2755 = _2036[1].z;
            float _2757 = _2036[2].z;
            float _2764 = (_2753 * _2757) - (_2755 * _2755);
            float _2771 = (_2751 * _2755) - (_2749 * _2757);
            float _2778 = (_2749 * _2755) - (_2751 * _2753);
            float _2792 = (_2749 * _2751) - (_2747 * _2755);
            mat3 _2826 = mat3(vec3(_2764, _2771, _2778), vec3(_2771, (_2747 * _2757) - (_2751 * _2751), _2792), vec3(_2778, _2792, (_2747 * _2753) - (_2749 * _2749))) * (1.0 / (((_2747 * _2764) + (_2749 * _2771)) + (_2751 * _2778)));
            float _2105 = _2826[2].z - (_2729 * _2729);
            float _2113 = _2826[0].z - (_2721 * _2729);
            float _2121 = _2826[1].z - (_2727 * _2729);
            float _2130 = sqrt(max((_2113 * _2113) - ((_2826[0].x - (_2721 * _2721)) * _2105), 0.0));
            float _2139 = sqrt(max((_2121 * _2121) - ((_2826[1].y - (_2727 * _2727)) * _2105), 0.0));
            vec2 _2149 = vec2(_2113 + _2130, _2113 - _2130) / vec2(_2105);
            vec2 _2159 = vec2(_2121 + _2139, _2121 - _2139) / vec2(_2105);
            ivec2 _2206 = max(max(ivec2(floor(_2066 - vec2(_2075))), ivec2(floor(vec2(min(_2149.x, _2149.y) * _1955, (-max(_2159.x, _2159.y)) * _1947) + _1963))), ivec2(0));
            ivec2 _2218 = min(min(ivec2(ceil(_2066 + vec2(_2075))), ivec2(ceil(vec2(max(_2149.x, _2149.y) * _1955, (-min(_2159.x, _2159.y)) * _1947) + _1963))), _1603);
            int _2220 = _2206.y;
            for (int _2958 = _2220; _2958 <= _2218.y; _2958++)
            {
                int _2229 = _2206.x;
                for (int _2959 = _2229; _2959 <= _2218.x; _2959++)
                {
                    vec3 _2254 = vec3(((float(_2959) + 0.5) - _1963.x) / _1955, (-((float(_2958) + 0.5) - _1963.y)) / _1947, 1.0);
                    vec3 _2260 = (_2254 * _2729) - _2730;
                    vec3 _2263 = _2254 * _2036;
                    float _2266 = dot(_2254, _2263);
                    float _2269 = dot(_2260, _2263);
                    float _2282 = (_2269 * _2269) - (_2266 * (dot(_2260, _2260 * _2036) - 1.0));
                    if ((_2282 < 0.0) || (_2266 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _2299 = _2729 + (((-_2269) - sqrt(_2282)) / _2266);
                    if (_2299 <= 0.0500000007450580596923828125)
                    {
                        continue;
                    }
                    uint _2316 = atomicMin(DepthBits_1._data[uint(_2959) + (uint(_2958) * _117.OutputWidth)], floatBitsToUint(_2299));
                }
            }
            break;
        }
        float _2329 = _1591 * _117.DepthRadiusScale;
        if (_2329 < 0.5)
        {
            break;
        }
        float _2339 = max(_117.Radius * _117.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _2346 = max(ivec2(floor(_1579 - vec2(_2329))), ivec2(0));
        ivec2 _2354 = min(ivec2(ceil(_1579 + vec2(_2329))), _1603);
        float _2357 = _2329 * _2329;
        int _2359 = _2346.y;
        for (int _2954 = _2359; _2954 <= _2354.y; _2954++)
        {
            int _2368 = _2346.x;
            for (int _2955 = _2368; _2955 <= _2354.x; _2955++)
            {
                vec2 _2384 = vec2(float(_2955) + 0.5, float(_2954) + 0.5) - _1579;
                float _2387 = dot(_2384, _2384);
                if (_2387 > _2357)
                {
                    continue;
                }
                uint _2415 = atomicMin(DepthBits_1._data[uint(_2955) + (uint(_2954) * _117.OutputWidth)], floatBitsToUint(_1553.w - (sqrt(max(1.0 - (_2387 / _2357), 0.0)) * _2339)));
            }
        }
        break;
    } while(false);
}

