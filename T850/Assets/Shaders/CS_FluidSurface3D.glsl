#version 430
// GENERATED from CS_FluidSurface3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=d00eb0552b3c27359f23dff6e334c548da62beaf55a7f606561492401c27ba83; defines=T850_SPIRV=1,T850_VULKAN=1
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

const ivec2 _318[8] = ivec2[](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1), ivec2(1), ivec2(-1, 1), ivec2(1, -1), ivec2(-1));

layout(binding = 0, std140) uniform FluidConstants
{
    uint Width;
    uint Height;
    uint Depth;
    uint Iteration;
    float DeltaTime;
    float CellSize;
    float Viscosity;
    float Gravity;
    float WindX;
    float WindY;
    float WindZ;
    float Damping;
    float SurfaceLevel;
    float TimeValue;
    float PressureScale;
    float Padding0;
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
} _61;

layout(binding = 5, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 3, std140) uniform GeometryConstants
{
    float BasinMinY;
    float WaterPlaneY;
    float WorldOriginX;
    float WorldOriginZ;
} _118;

layout(binding = 4, rgba16f) uniform writeonly image2D SurfaceOut;
layout(binding = 2) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

shared float s_elevation[10][10];

void main()
{
    do
    {
        uint _874 = gl_LocalInvocationID.x + (8u * gl_LocalInvocationID.y);
        for (uint _2217 = _874; _2217 < 100u; _2217 += 64u)
        {
            int _883 = int(_2217 % 10u);
            int _886 = int(_2217 / 10u);
            int _895 = ((int(gl_WorkGroupID.x) * 8) + _883) - 1;
            int _902 = ((int(gl_WorkGroupID.y) * 8) + _886) - 1;
            float _2366;
            do
            {
                bool _2304;
                float _2305;
                float _2306;
                float _2307;
                do
                {
                    if ((((_895 < 0) || (_902 < 0)) || (_895 >= int(_61.Width))) || (_902 >= int(_61.Depth)))
                    {
                        _2307 = _118.WaterPlaneY;
                        _2306 = _118.WaterPlaneY;
                        _2305 = 0.0;
                        _2304 = false;
                        break;
                    }
                    bool _2273;
                    float _2275;
                    float _2280;
                    float _2283;
                    _2283 = _118.WaterPlaneY;
                    _2280 = 0.0;
                    _2275 = 0.0;
                    _2273 = false;
                    bool _2388;
                    float _2393;
                    float _2397;
                    float _2401;
                    float _2411;
                    bool _2413;
                    bool _2415;
                    uint _2272 = 0u;
                    float _2291 = 0.0;
                    bool _2293 = false;
                    bool _2296 = false;
                    for (; _2272 < _61.Height; _2296 = _2415, _2293 = _2413, _2291 = _2411, _2283 = _2401, _2280 = _2397, _2275 = _2393, _2273 = _2388, _2272++)
                    {
                        int _1233 = int(_2272);
                        bool _2287;
                        do
                        {
                            if ((((((_895 < 0) || (_1233 < 0)) || (_902 < 0)) || (_895 >= int(_61.Width))) || (_1233 >= int(_61.Height))) || (_902 >= int(_61.Depth)))
                            {
                                _2287 = true;
                                break;
                            }
                            _2287 = WallMask_1._data[uint(_895) + (_61.Width * (_2272 + (_61.Height * uint(_902))))] != 0u;
                            break;
                        } while(false);
                        if (_2287)
                        {
                            bool _2439 = _2293 ? true : _2296;
                            float _2402;
                            if (_2439)
                            {
                                _2402 = _118.BasinMinY + (float(_2272 + 1u) * _61.CellSize);
                            }
                            else
                            {
                                _2402 = _2283;
                            }
                            _2415 = _2439;
                            _2413 = false;
                            _2411 = _2291 + 1.0;
                            _2401 = _2402;
                            _2397 = _2293 ? 0.0 : _2280;
                            _2393 = _2275;
                            _2388 = _2293 ? true : _2273;
                            continue;
                        }
                        float _1275 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_895, _1233, _902, 0).xyz, 0).x, 0.0, 1.0);
                        float _1278 = _2275 + _1275;
                        bool _1285 = _1275 >= 0.5;
                        float _2396;
                        if (_1285)
                        {
                            _2396 = _1278 + _2291;
                        }
                        else
                        {
                            _2396 = _1278;
                        }
                        _2415 = false;
                        _2413 = true;
                        _2411 = _1285 ? 0.0 : _2291;
                        _2401 = _2283;
                        _2397 = _2280 + _1275;
                        _2393 = _2396;
                        _2388 = _2273;
                    }
                    float _2284;
                    if (_2275 > 0.001000000047497451305389404296875)
                    {
                        _2284 = _118.BasinMinY + (_2275 * _61.CellSize);
                    }
                    else
                    {
                        _2284 = _118.WaterPlaneY;
                    }
                    _2307 = _2284;
                    _2306 = _2283;
                    _2305 = (!_2273) ? 0.0 : _2280;
                    _2304 = _2273;
                    break;
                } while(false);
                if (!_2304)
                {
                    _2366 = _2307;
                    break;
                }
                float _2309;
                float _2310;
                _2310 = 0.0;
                _2309 = 0.0;
                float _2418;
                float _2420;
                SPIRV_CROSS_LOOP
                for (int _2308 = 0; _2308 < 8; _2310 = _2420, _2309 = _2418, _2308++)
                {
                    int _2311 = 1;
                    SPIRV_CROSS_LOOP
                    for (;;)
                    {
                        if (_2311 <= 8)
                        {
                            int _1126 = _895 + (_318[_2308].x * _2311);
                            int _1133 = _902 + (_318[_2308].y * _2311);
                            bool _2331;
                            bool _2332;
                            float _2333;
                            do
                            {
                                if ((((_1126 < 0) || (_1133 < 0)) || (_1126 >= int(_61.Width))) || (_1133 >= int(_61.Depth)))
                                {
                                    _2333 = _118.WaterPlaneY;
                                    _2332 = false;
                                    _2331 = false;
                                    break;
                                }
                                float _2314;
                                bool _2317;
                                _2317 = false;
                                _2314 = 0.0;
                                float _2422;
                                bool _2425;
                                float _2433;
                                bool _2435;
                                uint _2313 = 0u;
                                float _2323 = 0.0;
                                bool _2325 = false;
                                for (; _2313 < _61.Height; _2325 = _2435, _2323 = _2433, _2317 = _2425, _2314 = _2422, _2313++)
                                {
                                    int _1432 = int(_2313);
                                    bool _2320;
                                    do
                                    {
                                        if ((((((_1126 < 0) || (_1432 < 0)) || (_1133 < 0)) || (_1126 >= int(_61.Width))) || (_1432 >= int(_61.Height))) || (_1133 >= int(_61.Depth)))
                                        {
                                            _2320 = true;
                                            break;
                                        }
                                        _2320 = WallMask_1._data[uint(_1126) + (_61.Width * (_2313 + (_61.Height * uint(_1133))))] != 0u;
                                        break;
                                    } while(false);
                                    if (_2320)
                                    {
                                        _2435 = false;
                                        _2433 = _2323 + 1.0;
                                        _2425 = _2325 ? true : _2317;
                                        _2422 = _2314;
                                        continue;
                                    }
                                    float _1474 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1126, _1432, _1133, 0).xyz, 0).x, 0.0, 1.0);
                                    float _1477 = _2314 + _1474;
                                    bool _1484 = _1474 >= 0.5;
                                    float _2424;
                                    if (_1484)
                                    {
                                        _2424 = _1477 + _2323;
                                    }
                                    else
                                    {
                                        _2424 = _1477;
                                    }
                                    _2435 = true;
                                    _2433 = _1484 ? 0.0 : _2323;
                                    _2425 = _2317;
                                    _2422 = _2424;
                                }
                                bool _1501 = _2314 > 0.001000000047497451305389404296875;
                                float _2318;
                                if (_1501)
                                {
                                    _2318 = _118.BasinMinY + (_2314 * _61.CellSize);
                                }
                                else
                                {
                                    _2318 = _118.WaterPlaneY;
                                }
                                _2333 = _2318;
                                _2332 = _2317;
                                _2331 = _1501;
                                break;
                            } while(false);
                            if (_2332)
                            {
                                _2311++;
                                continue;
                            }
                            float _2419;
                            float _2421;
                            if (_2331)
                            {
                                _2421 = _2310 + _2333;
                                _2419 = _2309 + 1.0;
                            }
                            else
                            {
                                _2421 = _2310;
                                _2419 = _2309;
                            }
                            _2420 = _2421;
                            _2418 = _2419;
                            break;
                        }
                        else
                        {
                            _2420 = _2310;
                            _2418 = _2309;
                            break;
                        }
                    }
                }
                _2366 = mix((_2309 > 0.0) ? (_2310 / _2309) : _2307, _2306 + (_2305 * _61.CellSize), clamp(_2305, 0.0, 1.0));
                break;
            } while(false);
            s_elevation[_886][_883] = _2366;
        }
        barrier();
        if ((gl_GlobalInvocationID.x >= _61.Width) || (gl_GlobalInvocationID.y >= _61.Depth))
        {
            break;
        }
        int _924 = int(gl_GlobalInvocationID.x);
        int _927 = int(gl_GlobalInvocationID.y);
        int _2218;
        _2218 = -1;
        float _850[3][3];
        for (; _2218 <= 1; _2218++)
        {
            for (int _2270 = -1; _2270 <= 1; )
            {
                _850[_2218 + 1][_2270 + 1] = s_elevation[(int(gl_LocalInvocationID.y) + 1) + _2218][(int(gl_LocalInvocationID.x) + 1) + _2270];
                _2270++;
                continue;
            }
        }
        float _996 = 0.0625 * ((((((((_850[0][0] + (2.0 * _850[0][1])) + _850[0][2]) + (2.0 * _850[1][0])) + (4.0 * _850[1][1])) + (2.0 * _850[1][2])) + _850[2][0]) + (2.0 * _850[2][1])) + _850[2][2]);
        int _1053 = clamp(int((_996 - _118.BasinMinY) / _61.CellSize), 1, int(_61.Height) - 1);
        int _1596 = int(_61.Height) - 1;
        int _1605 = clamp(_924 - 1, 0, int(_61.Width) - 1);
        int _1609 = clamp(_927, 0, int(_61.Depth) - 1);
        int _1611 = _1053 - 2;
        float _2220;
        _2220 = 0.0;
        float _1724;
        for (int _2219 = _1611; _2219 <= _1053; _2220 = _1724, _2219++)
        {
            int _1622 = clamp(_2219, 0, _1596);
            ivec3 _1624 = ivec3(_1605, _1622, _1609);
            vec4 _1638 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1605, _1622, _1609, 0).xyz, 0);
            float _2257;
            if (_1605 > 0)
            {
                _2257 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1624 - ivec3(1, 0, 0), 0).xyz, 0).x;
            }
            else
            {
                _2257 = 0.0;
            }
            float _2259;
            if (_1622 > 0)
            {
                _2259 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1624 - ivec3(0, 1, 0), 0).xyz, 0).y;
            }
            else
            {
                _2259 = 0.0;
            }
            float _2261;
            if (_1609 > 0)
            {
                _2261 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1624 - ivec3(0, 0, 1), 0).xyz, 0).z;
            }
            else
            {
                _2261 = 0.0;
            }
            _1724 = _2220 + ((((_1638.x - _2257) + (_1638.y - _2259)) + (_1638.z - _2261)) / _61.CellSize);
        }
        int _1749 = int(_61.Height) - 1;
        int _1758 = clamp(_924, 0, int(_61.Width) - 1);
        int _1762 = clamp(_927, 0, int(_61.Depth) - 1);
        int _1764 = _1053 - 2;
        float _2222;
        _2222 = 0.0;
        float _1877;
        for (int _2221 = _1764; _2221 <= _1053; _2222 = _1877, _2221++)
        {
            int _1775 = clamp(_2221, 0, _1749);
            ivec3 _1777 = ivec3(_1758, _1775, _1762);
            vec4 _1791 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1758, _1775, _1762, 0).xyz, 0);
            float _2242;
            if (_1758 > 0)
            {
                _2242 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1777 - ivec3(1, 0, 0), 0).xyz, 0).x;
            }
            else
            {
                _2242 = 0.0;
            }
            float _2244;
            if (_1775 > 0)
            {
                _2244 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1777 - ivec3(0, 1, 0), 0).xyz, 0).y;
            }
            else
            {
                _2244 = 0.0;
            }
            float _2246;
            if (_1762 > 0)
            {
                _2246 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1777 - ivec3(0, 0, 1), 0).xyz, 0).z;
            }
            else
            {
                _2246 = 0.0;
            }
            _1877 = _2222 + ((((_1791.x - _2242) + (_1791.y - _2244)) + (_1791.z - _2246)) / _61.CellSize);
        }
        int _1902 = int(_61.Height) - 1;
        int _1911 = clamp(_924 + 1, 0, int(_61.Width) - 1);
        int _1915 = clamp(_927, 0, int(_61.Depth) - 1);
        int _1917 = _1053 - 2;
        float _2224;
        _2224 = 0.0;
        float _2030;
        for (int _2223 = _1917; _2223 <= _1053; _2224 = _2030, _2223++)
        {
            int _1928 = clamp(_2223, 0, _1902);
            ivec3 _1930 = ivec3(_1911, _1928, _1915);
            vec4 _1944 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1911, _1928, _1915, 0).xyz, 0);
            float _2227;
            if (_1911 > 0)
            {
                _2227 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1930 - ivec3(1, 0, 0), 0).xyz, 0).x;
            }
            else
            {
                _2227 = 0.0;
            }
            float _2229;
            if (_1928 > 0)
            {
                _2229 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1930 - ivec3(0, 1, 0), 0).xyz, 0).y;
            }
            else
            {
                _2229 = 0.0;
            }
            float _2231;
            if (_1915 > 0)
            {
                _2231 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1930 - ivec3(0, 0, 1), 0).xyz, 0).z;
            }
            else
            {
                _2231 = 0.0;
            }
            _2030 = _2224 + ((((_1944.x - _2227) + (_1944.y - _2229)) + (_1944.z - _2231)) / _61.CellSize);
        }
        imageStore(SurfaceOut, ivec2(gl_GlobalInvocationID.xy), vec4(_996 - _118.WaterPlaneY, ((clamp(_2220 * 0.3499999940395355224609375, 0.0, 1.0) + clamp(_2222 * 0.3499999940395355224609375, 0.0, 1.0)) + clamp(_2224 * 0.3499999940395355224609375, 0.0, 1.0)) * 0.3333333432674407958984375, 0.0625 * (((_850[0][2] + (2.0 * _850[1][2])) + _850[2][2]) - ((_850[0][0] + (2.0 * _850[1][0])) + _850[2][0])), 0.0625 * (((_850[2][0] + (2.0 * _850[2][1])) + _850[2][2]) - ((_850[0][0] + (2.0 * _850[0][1])) + _850[0][2]))));
        break;
    } while(false);
}

