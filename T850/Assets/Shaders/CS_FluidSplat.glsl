#version 430
// GENERATED from CS_FluidSplat.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=2c9dc87d21f829d60cb1e095cc831135f603d8e1faecec36de448a8a821feff5; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float Color1Pad;
    vec3 Color2;
    float Color2Pad;
    vec3 Color3;
    float Color3Pad;
    vec3 EmitterPosition;
    float Anisotropic;
    vec4 ViewProjection[4];
} _80;

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

layout(binding = 2) uniform sampler2D SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _80.ParticleCount)
        {
            break;
        }
        vec4 _1320 = Projected_1._data[gl_GlobalInvocationID.x];
        if (_1320.z <= 0.0)
        {
            break;
        }
        vec2 _1338 = _1320.xy * vec2(float(_80.OutputWidth), float(_80.OutputHeight));
        float _1341 = min(_1320.z, 64.0);
        float _1344 = max(_80.Radius, 9.9999997473787516355514526367188e-05);
        ivec2 _1353 = ivec2(int(_80.OutputWidth) - 1, int(_80.OutputHeight) - 1);
        if (!(_80.ScreenSpace > 0.5))
        {
            if (_1341 < 0.5)
            {
                break;
            }
            ivec2 _1367 = max(ivec2(floor(_1338 - vec2(_1341))), ivec2(0));
            ivec2 _1375 = min(ivec2(ceil(_1338 + vec2(_1341))), _1353);
            if ((_1367.x > _1375.x) || (_1367.y > _1375.y))
            {
                break;
            }
            float _1392 = max(_1341 * _1341, 9.9999999747524270787835121154785e-07);
            int _1394 = _1367.y;
            for (int _2588 = _1394; _2588 <= _1375.y; _2588++)
            {
                int _1403 = _1367.x;
                for (int _2589 = _1403; _2589 <= _1375.x; _2589++)
                {
                    vec2 _1419 = vec2(float(_2589) + 0.5, float(_2588) + 0.5) - _1338;
                    float _1422 = dot(_1419, _1419);
                    if (_1422 > _1392)
                    {
                        continue;
                    }
                    float _1433 = sqrt(max(1.0 - (_1422 / _1392), 0.0));
                    float _1439 = _1320.w - (_1433 * _1344);
                    vec4 _2122 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2589, int((_80.OutputHeight - 1u) - uint(_2588)), 0).xy, 0);
                    float _2123 = _2122.x;
                    if (_1439 >= (((_2123 > 0.0) && (abs(_2123 - _80.ProjA) > 9.999999717180685365747194737196e-10)) ? (_80.ProjB / (_2123 - _80.ProjA)) : 1000000015047466219876688855040.0))
                    {
                        continue;
                    }
                    uint _1454 = uint(_2589) + (uint(_2588) * _80.OutputWidth);
                    uint _1460 = atomicMin(DepthBits_1._data[_1454], (uint(clamp((log2(max(_1439, 0.0500000007450580596923828125)) + 4.3219280242919921875) * 0.069990210235118865966796875, 0.0, 1.0) * 32766.0) << 17u) | (gl_GlobalInvocationID.x & 131071u));
                    uint _1472 = atomicAdd(ThicknessBits_1._data[_1454], uint((((2.0 * _1433) * _1344) * _80.InvPackingFactor) * 1024.0));
                }
            }
            break;
        }
        uint _1486 = gl_GlobalInvocationID.x * 3u;
        vec4 _1489 = Anisotropy_1._data[_1486];
        bvec4 _1490 = bvec4(_80.Anisotropic > 0.5);
        vec4 _1491 = vec4(_1490.x ? _1489.x : vec4(0.0).x, _1490.y ? _1489.y : vec4(0.0).y, _1490.z ? _1489.z : vec4(0.0).z, _1490.w ? _1489.w : vec4(0.0).w);
        if (_1491.w < 0.0)
        {
            break;
        }
        if (_1341 >= 0.5)
        {
            ivec2 _1506 = max(ivec2(floor(_1338 - vec2(_1341))), ivec2(0));
            ivec2 _1514 = min(ivec2(ceil(_1338 + vec2(_1341))), _1353);
            float _1518 = max(_1341 * _1341, 9.9999999747524270787835121154785e-07);
            int _1520 = _1506.y;
            for (int _2576 = _1520; _2576 <= _1514.y; _2576++)
            {
                int _1529 = _1506.x;
                for (int _2585 = _1529; _2585 <= _1514.x; _2585++)
                {
                    vec2 _1545 = vec2(float(_2585) + 0.5, float(_2576) + 0.5) - _1338;
                    float _1548 = dot(_1545, _1545);
                    if (_1548 > _1518)
                    {
                        continue;
                    }
                    float _1559 = sqrt(max(1.0 - (_1548 / _1518), 0.0));
                    vec4 _2181 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2585, int((_80.OutputHeight - 1u) - uint(_2576)), 0).xy, 0);
                    float _2182 = _2181.x;
                    if ((_1320.w - (_1559 * _1344)) >= (((_2182 > 0.0) && (abs(_2182 - _80.ProjA) > 9.999999717180685365747194737196e-10)) ? (_80.ProjB / (_2182 - _80.ProjA)) : 1000000015047466219876688855040.0))
                    {
                        continue;
                    }
                    uint _1590 = atomicAdd(ThicknessBits_1._data[uint(_2585) + (uint(_2576) * _80.OutputWidth)], uint((((2.0 * _1559) * _1344) * _80.InvPackingFactor) * 1024.0));
                }
            }
        }
        float _1607 = (0.5 * float(_80.OutputHeight)) * max(_80.ProjScaleY, 9.9999997473787516355514526367188e-05);
        float _1615 = (0.5 * float(_80.OutputWidth)) * max(_80.ProjScaleX, 9.9999997473787516355514526367188e-05);
        vec2 _1623 = vec2(float(_80.OutputWidth), float(_80.OutputHeight)) * 0.5;
        if (_1491.w > 0.0)
        {
            uint _1630 = (gl_GlobalInvocationID.x * 3u) + 1u;
            uint _1635 = (gl_GlobalInvocationID.x * 3u) + 2u;
            mat3 _1690 = mat3(vec3(_80.ViewProjection[0].x / max(_80.ProjScaleX, 9.9999999747524270787835121154785e-07), _80.ViewProjection[1].x / max(_80.ProjScaleX, 9.9999999747524270787835121154785e-07), _80.ViewProjection[2].x / max(_80.ProjScaleX, 9.9999999747524270787835121154785e-07)), vec3(_80.ViewProjection[0].y / max(_80.ProjScaleY, 9.9999999747524270787835121154785e-07), _80.ViewProjection[1].y / max(_80.ProjScaleY, 9.9999999747524270787835121154785e-07), _80.ViewProjection[2].y / max(_80.ProjScaleY, 9.9999999747524270787835121154785e-07)), vec3(_80.ViewProjection[0].w, _80.ViewProjection[1].w, _80.ViewProjection[2].w));
            mat3 _1696 = transpose(_1690) * (mat3(vec3(Anisotropy_1._data[_1630].xyz), vec3(Anisotropy_1._data[_1630].yw, Anisotropy_1._data[_1635].x), vec3(Anisotropy_1._data[_1630].z, Anisotropy_1._data[_1635].xy)) * _1690);
            vec4 _2273 = (((_80.ViewProjection[0] * _1491.x) + (_80.ViewProjection[1] * _1491.y)) + (_80.ViewProjection[2] * _1491.z)) + _80.ViewProjection[3];
            float _2282 = _2273.x / max(_80.ProjScaleX, 9.9999999747524270787835121154785e-07);
            float _2288 = _2273.y / max(_80.ProjScaleY, 9.9999999747524270787835121154785e-07);
            float _2290 = _2273.w;
            vec3 _2291 = vec3(_2282, _2288, _2290);
            float _1701 = _1491.w;
            if ((_2290 - _1701) <= 0.0500000007450580596923828125)
            {
                break;
            }
            vec2 _1726 = vec2((_2282 * _1615) / _2290, ((-_2288) * _1607) / _2290) + _1623;
            float _1735 = min((_1701 * _1607) / (_2290 - _1701), 64.0);
            if (_1735 < 0.5)
            {
                break;
            }
            float _2308 = _1696[0].x;
            float _2310 = _1696[0].y;
            float _2312 = _1696[0].z;
            float _2314 = _1696[1].y;
            float _2316 = _1696[1].z;
            float _2318 = _1696[2].z;
            float _2325 = (_2314 * _2318) - (_2316 * _2316);
            float _2332 = (_2312 * _2316) - (_2310 * _2318);
            float _2339 = (_2310 * _2316) - (_2312 * _2314);
            float _2353 = (_2310 * _2312) - (_2308 * _2316);
            mat3 _2387 = mat3(vec3(_2325, _2332, _2339), vec3(_2332, (_2308 * _2318) - (_2312 * _2312), _2353), vec3(_2339, _2353, (_2308 * _2314) - (_2310 * _2310))) * (1.0 / (((_2308 * _2325) + (_2310 * _2332)) + (_2312 * _2339)));
            float _1765 = _2387[2].z - (_2290 * _2290);
            float _1773 = _2387[0].z - (_2282 * _2290);
            float _1781 = _2387[1].z - (_2288 * _2290);
            float _1790 = sqrt(max((_1773 * _1773) - ((_2387[0].x - (_2282 * _2282)) * _1765), 0.0));
            float _1799 = sqrt(max((_1781 * _1781) - ((_2387[1].y - (_2288 * _2288)) * _1765), 0.0));
            vec2 _1809 = vec2(_1773 + _1790, _1773 - _1790) / vec2(_1765);
            vec2 _1819 = vec2(_1781 + _1799, _1781 - _1799) / vec2(_1765);
            ivec2 _1866 = max(max(ivec2(floor(_1726 - vec2(_1735))), ivec2(floor(vec2(min(_1809.x, _1809.y) * _1615, (-max(_1819.x, _1819.y)) * _1607) + _1623))), ivec2(0));
            ivec2 _1878 = min(min(ivec2(ceil(_1726 + vec2(_1735))), ivec2(ceil(vec2(max(_1809.x, _1809.y) * _1615, (-min(_1819.x, _1819.y)) * _1607) + _1623))), _1353);
            int _1880 = _1866.y;
            for (int _2581 = _1880; _2581 <= _1878.y; _2581++)
            {
                int _1889 = _1866.x;
                for (int _2582 = _1889; _2582 <= _1878.x; _2582++)
                {
                    vec3 _1914 = vec3(((float(_2582) + 0.5) - _1623.x) / _1615, (-((float(_2581) + 0.5) - _1623.y)) / _1607, 1.0);
                    vec3 _1920 = (_1914 * _2290) - _2291;
                    vec3 _1923 = _1914 * _1696;
                    float _1926 = dot(_1914, _1923);
                    float _1929 = dot(_1920, _1923);
                    float _1942 = (_1929 * _1929) - (_1926 * (dot(_1920, _1920 * _1696) - 1.0));
                    if ((_1942 < 0.0) || (_1926 <= 9.9999999600419720025001879548654e-13))
                    {
                        continue;
                    }
                    float _1959 = _2290 + (((-_1929) - sqrt(_1942)) / _1926);
                    vec4 _2412 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2582, int((_80.OutputHeight - 1u) - uint(_2581)), 0).xy, 0);
                    float _2413 = _2412.x;
                    if ((_1959 <= 0.0500000007450580596923828125) || (_1959 >= (((_2413 > 0.0) && (abs(_2413 - _80.ProjA) > 9.999999717180685365747194737196e-10)) ? (_80.ProjB / (_2413 - _80.ProjA)) : 1000000015047466219876688855040.0)))
                    {
                        continue;
                    }
                    uint _1982 = atomicMin(DepthBits_1._data[uint(_2582) + (uint(_2581) * _80.OutputWidth)], floatBitsToUint(_1959));
                }
            }
            break;
        }
        float _1995 = _1341 * _80.DepthRadiusScale;
        if (_1995 < 0.5)
        {
            break;
        }
        float _2005 = max(_80.Radius * _80.DepthRadiusScale, 9.9999997473787516355514526367188e-05);
        ivec2 _2012 = max(ivec2(floor(_1338 - vec2(_1995))), ivec2(0));
        ivec2 _2020 = min(ivec2(ceil(_1338 + vec2(_1995))), _1353);
        float _2023 = _1995 * _1995;
        int _2025 = _2012.y;
        for (int _2577 = _2025; _2577 <= _2020.y; _2577++)
        {
            int _2034 = _2012.x;
            for (int _2578 = _2034; _2578 <= _2020.x; _2578++)
            {
                vec2 _2050 = vec2(float(_2578) + 0.5, float(_2577) + 0.5) - _1338;
                float _2053 = dot(_2050, _2050);
                if (_2053 > _2023)
                {
                    continue;
                }
                float _2069 = _1320.w - (sqrt(max(1.0 - (_2053 / _2023), 0.0)) * _2005);
                vec4 _2455 = texelFetch(SPIRV_Cross_CombinedSceneDepthSPIRV_Cross_DummySampler, ivec3(_2578, int((_80.OutputHeight - 1u) - uint(_2577)), 0).xy, 0);
                float _2456 = _2455.x;
                if (_2069 >= (((_2456 > 0.0) && (abs(_2456 - _80.ProjA) > 9.999999717180685365747194737196e-10)) ? (_80.ProjB / (_2456 - _80.ProjA)) : 1000000015047466219876688855040.0))
                {
                    continue;
                }
                uint _2088 = atomicMin(DepthBits_1._data[uint(_2578) + (uint(_2577) * _80.OutputWidth)], floatBitsToUint(_2069));
            }
        }
        break;
    } while(false);
}

