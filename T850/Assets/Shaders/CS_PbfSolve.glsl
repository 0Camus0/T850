#version 430
// GENERATED from CS_PbfSolve.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=c28ab2154d85797ba9e78fe0bd444f095bdad13c9ee846e4780c7369d367a093; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

struct SolidBox
{
    vec4 lo;
    vec4 hi;
};

layout(binding = 0, std140) uniform PbfConstants
{
    uint ParticleCount;
    uint GridX;
    uint GridY;
    uint GridZ;
    float DeltaTime;
    float SmoothingRadius;
    float ParticleRadius;
    float RestDensity;
    float StiffnessEps;
    float ArtPressureK;
    float ArtPressureN;
    float ViscosityC;
    float Gravity;
    float MaxVelocity;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float BasinMaxX;
    float BasinMaxY;
    float BasinMaxZ;
    uint BoxCount;
    uint Iteration;
    uint SurfaceWidth;
    uint SurfaceDepth;
    float WaterPlaneY;
    float RestScale;
    float RestDenomReference;
    float MinNeighbours;
    float WallTopY;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
} _59;

layout(binding = 5, std430) readonly buffer SolidBoxes
{
    SolidBox _data[];
} SolidBoxes_1;

layout(binding = 1, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 2, std430) buffer PredictedNext
{
    vec4 _data[];
} PredictedNext_1;

layout(binding = 4, std430) readonly buffer Lambda
{
    vec4 _data[];
} Lambda_1;

layout(binding = 3, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _59.ParticleCount)
        {
            break;
        }
        uint _1286 = (_59.GridX * _59.GridY) * _59.GridZ;
        float _1289 = 1.0 / _59.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            PredictedNext_1._data[gl_GlobalInvocationID.x] = vec4(Predicted_1._data[gl_GlobalInvocationID.x].xyz, 0.0);
            break;
        }
        float _1315 = 0.300000011920928955078125 * _59.SmoothingRadius;
        float _1318 = _1315 * _1315;
        float _2637;
        do
        {
            if (_1318 >= (_59.SmoothingRadius * _59.SmoothingRadius))
            {
                _2637 = 0.0;
                break;
            }
            float _1623 = (_59.SmoothingRadius * _59.SmoothingRadius) - _1318;
            _2637 = ((((1.566681385040283203125 / pow(_59.SmoothingRadius, 9.0)) * _1623) * _1623) * _1623) / max(_59.RestScale, 9.9999999747524270787835121154785e-07);
            break;
        } while(false);
        float _1324 = (_2637 > 9.9999999600419720025001879548654e-13) ? (1.0 / _2637) : 0.0;
        int _1338 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].x - _59.GridOriginX) * _1289)), 0, int(_59.GridX) - 1);
        int _1352 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].y - _59.GridOriginY) * _1289)), 0, int(_59.GridY) - 1);
        int _1366 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].z - _59.GridOriginZ) * _1289)), 0, int(_59.GridZ) - 1);
        float _1371 = _59.SmoothingRadius * _59.SmoothingRadius;
        vec3 _2655;
        _2655 = vec3(0.0);
        vec3 _2751;
        SPIRV_CROSS_LOOP
        for (int _2638 = -1; _2638 <= 1; _2655 = _2751, _2638++)
        {
            _2751 = _2655;
            vec3 _2750;
            SPIRV_CROSS_LOOP
            for (int _2737 = -1; _2737 <= 1; _2737++, _2751 = _2750)
            {
                _2750 = _2751;
                vec3 _2777;
                SPIRV_CROSS_LOOP
                for (int _2739 = -1; _2739 <= 1; _2750 = _2777, _2739++)
                {
                    int _1389 = _1338 + _2739;
                    int _1392 = _1352 + _2737;
                    int _1395 = _1366 + _2638;
                    if ((((((_1389 < 0) || (_1392 < 0)) || (_1395 < 0)) || (_1389 >= int(_59.GridX))) || (_1392 >= int(_59.GridY))) || (_1395 >= int(_59.GridZ)))
                    {
                        _2777 = _2750;
                        continue;
                    }
                    uint _1437 = uint(_1389) + (_59.GridX * (uint(_1392) + (_59.GridY * uint(_1395))));
                    uint _1441 = min(GridBuffer_1._data[_1437], 64u);
                    vec3 _2749;
                    _2749 = _2750;
                    vec3 _2778;
                    SPIRV_CROSS_LOOP
                    for (uint _2742 = 0u; _2742 < _1441; _2749 = _2778, _2742++)
                    {
                        uint _1454 = (64u * (_1286 + _1437)) + _2742;
                        if ((GridBuffer_1._data[_1454] >= _59.ParticleCount) || (GridBuffer_1._data[_1454] == gl_GlobalInvocationID.x))
                        {
                            _2778 = _2749;
                            continue;
                        }
                        vec3 _1473 = Predicted_1._data[gl_GlobalInvocationID.x].xyz - Predicted_1._data[GridBuffer_1._data[_1454]].xyz;
                        vec3 _2743;
                        do
                        {
                            if (dot(_1473, _1473) > 9.9999999392252902907785028219223e-09)
                            {
                                _2743 = _1473;
                                break;
                            }
                            uint _1661 = (gl_GlobalInvocationID.x * 73856093u) ^ (GridBuffer_1._data[_1454] * 19349663u);
                            uint _1667 = (_1661 ^ (_1661 >> uint(13))) * 16777619u;
                            uint _1671 = _1667 ^ (_1667 >> uint(15));
                            vec3 _1686 = vec3(float(_1671 & 255u) - 127.5, float((_1671 >> uint(8)) & 255u) - 127.5, float((_1671 >> uint(16)) & 255u) - 127.5);
                            _2743 = _1686 * (9.9999997473787516355514526367188e-05 / max(length(_1686), 0.001000000047497451305389404296875));
                            break;
                        } while(false);
                        float _1479 = dot(_2743, _2743);
                        if (_1479 >= _1371)
                        {
                            _2778 = _2749;
                            continue;
                        }
                        vec3 _2744;
                        do
                        {
                            float _1704 = length(_2743);
                            if ((_1704 <= 9.9999999747524270787835121154785e-07) || (_1704 >= _59.SmoothingRadius))
                            {
                                _2744 = vec3(0.0);
                                break;
                            }
                            _2744 = _2743 * ((((((-14.323944091796875) / pow(_59.SmoothingRadius, 6.0)) * (_59.SmoothingRadius - _1704)) * (_59.SmoothingRadius - _1704)) / _1704) / max(_59.RestScale, 9.9999999747524270787835121154785e-07));
                            break;
                        } while(false);
                        float _2745;
                        do
                        {
                            if (_1479 >= (_59.SmoothingRadius * _59.SmoothingRadius))
                            {
                                _2745 = 0.0;
                                break;
                            }
                            float _1761 = (_59.SmoothingRadius * _59.SmoothingRadius) - _1479;
                            _2745 = ((((1.566681385040283203125 / pow(_59.SmoothingRadius, 9.0)) * _1761) * _1761) * _1761) / max(_59.RestScale, 9.9999999747524270787835121154785e-07);
                            break;
                        } while(false);
                        _2778 = _2749 + (_2744 * ((Lambda_1._data[gl_GlobalInvocationID.x].x + Lambda_1._data[GridBuffer_1._data[_1454]].x) + ((_1479 < ((0.810000002384185791015625 * _59.ParticleRadius) * _59.ParticleRadius)) ? ((-_59.ArtPressureK) * pow(_2745 * _1324, _59.ArtPressureN)) : 0.0)));
                    }
                    _2777 = _2749;
                }
            }
        }
        vec3 _2643;
        if ((((Predicted_1._data[gl_GlobalInvocationID.x].x > _59.BasinMinX) && (Predicted_1._data[gl_GlobalInvocationID.x].x < _59.BasinMaxX)) && (Predicted_1._data[gl_GlobalInvocationID.x].z > _59.BasinMinZ)) && (Predicted_1._data[gl_GlobalInvocationID.x].z < _59.BasinMaxZ))
        {
            float _1822 = Predicted_1._data[gl_GlobalInvocationID.x].y - _59.BasinMinY;
            vec3 _2639;
            do
            {
                if ((_1822 <= 0.0) || (_1822 >= _59.SmoothingRadius))
                {
                    _2639 = vec3(0.0);
                    break;
                }
                float _1916 = _1822 / _59.SmoothingRadius;
                float _1929 = 1.0 - (_1916 * _1916);
                float _1932 = _1929 * _1929;
                _2639 = vec3(0.0, 1.0, 0.0) * ((((-1.23046875) / _59.SmoothingRadius) * _1932) * _1932);
                break;
            } while(false);
            _2643 = _2639;
        }
        else
        {
            _2643 = vec3(0.0);
        }
        vec3 _2648;
        if (((Predicted_1._data[gl_GlobalInvocationID.x].y < _59.WallTopY) && (Predicted_1._data[gl_GlobalInvocationID.x].z > _59.BasinMinZ)) && (Predicted_1._data[gl_GlobalInvocationID.x].z < _59.BasinMaxZ))
        {
            float _1849 = Predicted_1._data[gl_GlobalInvocationID.x].x - _59.BasinMinX;
            vec3 _2641;
            do
            {
                if ((_1849 <= 0.0) || (_1849 >= _59.SmoothingRadius))
                {
                    _2641 = vec3(0.0);
                    break;
                }
                float _1960 = _1849 / _59.SmoothingRadius;
                float _1973 = 1.0 - (_1960 * _1960);
                float _1976 = _1973 * _1973;
                _2641 = vec3(1.0, 0.0, 0.0) * ((((-1.23046875) / _59.SmoothingRadius) * _1976) * _1976);
                break;
            } while(false);
            float _1857 = _59.BasinMaxX - Predicted_1._data[gl_GlobalInvocationID.x].x;
            vec3 _2644;
            do
            {
                if ((_1857 <= 0.0) || (_1857 >= _59.SmoothingRadius))
                {
                    _2644 = vec3(0.0);
                    break;
                }
                float _2004 = _1857 / _59.SmoothingRadius;
                float _2017 = 1.0 - (_2004 * _2004);
                float _2020 = _2017 * _2017;
                _2644 = vec3(-1.0, 0.0, 0.0) * ((((-1.23046875) / _59.SmoothingRadius) * _2020) * _2020);
                break;
            } while(false);
            _2648 = (_2643 + _2641) + _2644;
        }
        else
        {
            _2648 = _2643;
        }
        vec3 _2651;
        if (((Predicted_1._data[gl_GlobalInvocationID.x].y < _59.WallTopY) && (Predicted_1._data[gl_GlobalInvocationID.x].x > _59.BasinMinX)) && (Predicted_1._data[gl_GlobalInvocationID.x].x < _59.BasinMaxX))
        {
            float _1884 = Predicted_1._data[gl_GlobalInvocationID.x].z - _59.BasinMinZ;
            vec3 _2646;
            do
            {
                if ((_1884 <= 0.0) || (_1884 >= _59.SmoothingRadius))
                {
                    _2646 = vec3(0.0);
                    break;
                }
                float _2048 = _1884 / _59.SmoothingRadius;
                float _2061 = 1.0 - (_2048 * _2048);
                float _2064 = _2061 * _2061;
                _2646 = vec3(0.0, 0.0, 1.0) * ((((-1.23046875) / _59.SmoothingRadius) * _2064) * _2064);
                break;
            } while(false);
            float _1892 = _59.BasinMaxZ - Predicted_1._data[gl_GlobalInvocationID.x].z;
            vec3 _2649;
            do
            {
                if ((_1892 <= 0.0) || (_1892 >= _59.SmoothingRadius))
                {
                    _2649 = vec3(0.0);
                    break;
                }
                float _2092 = _1892 / _59.SmoothingRadius;
                float _2105 = 1.0 - (_2092 * _2092);
                float _2108 = _2105 * _2105;
                _2649 = vec3(0.0, 0.0, -1.0) * ((((-1.23046875) / _59.SmoothingRadius) * _2108) * _2108);
                break;
            } while(false);
            _2651 = (_2648 + _2646) + _2649;
        }
        else
        {
            _2651 = _2648;
        }
        vec3 _2662;
        _2662 = _2655 + (_2651 * (2.0 * Lambda_1._data[gl_GlobalInvocationID.x].x));
        vec3 _1565;
        SPIRV_CROSS_LOOP
        for (uint _2661 = 0u; _2661 < _59.BoxCount; _2662 = _1565, _2661++)
        {
            vec3 _2732;
            do
            {
                vec3 _2132 = Predicted_1._data[gl_GlobalInvocationID.x].xyz - clamp(Predicted_1._data[gl_GlobalInvocationID.x].xyz, SolidBoxes_1._data[_2661].lo.xyz, SolidBoxes_1._data[_2661].hi.xyz);
                float _2134 = length(_2132);
                if ((_2134 <= 9.9999997473787516355514526367188e-06) || (_2134 >= _59.SmoothingRadius))
                {
                    _2732 = vec3(0.0);
                    break;
                }
                vec3 _2731;
                do
                {
                    if ((_2134 <= 0.0) || (_2134 >= _59.SmoothingRadius))
                    {
                        _2731 = vec3(0.0);
                        break;
                    }
                    float _2170 = _2134 / _59.SmoothingRadius;
                    float _2183 = 1.0 - (_2170 * _2170);
                    float _2186 = _2183 * _2183;
                    _2731 = (_2132 / vec3(_2134)) * ((((-1.23046875) / _59.SmoothingRadius) * _2186) * _2186);
                    break;
                } while(false);
                _2732 = _2731;
                break;
            } while(false);
            _1565 = _2662 + (_2732 * (2.0 * Lambda_1._data[gl_GlobalInvocationID.x].x));
        }
        vec3 _1574 = _2662 * (1.0 / _59.RestDensity);
        float _1577 = _59.ParticleRadius * 0.100000001490116119384765625;
        float _1579 = length(_1574);
        vec3 _2663;
        if (_1579 > _1577)
        {
            _2663 = _1574 * (_1577 / _1579);
        }
        else
        {
            _2663 = _1574;
        }
        vec3 _1592 = Predicted_1._data[gl_GlobalInvocationID.x].xyz + _2663;
        float _2216 = _59.ParticleRadius * 0.5;
        vec3 _2666;
        if ((((_1592.x > (_59.BasinMinX - _2216)) && (_1592.x < (_59.BasinMaxX + _2216))) && (_1592.z > (_59.BasinMinZ - _2216))) && (_1592.z < (_59.BasinMaxZ + _2216)))
        {
            float _2256 = max(_1592.y, _59.BasinMinY + _2216);
            vec3 _2564 = _1592;
            _2564.y = _2256;
            vec3 _2667;
            if (_2256 < _59.WallTopY)
            {
                _2667 = vec3(clamp(_1592.x, _59.BasinMinX + _2216, _59.BasinMaxX - _2216), _2256, clamp(_1592.z, _59.BasinMinZ + _2216, _59.BasinMaxZ - _2216));
            }
            else
            {
                _2667 = _2564;
            }
            _2666 = _2667;
        }
        else
        {
            vec3 _2574 = _1592;
            _2574.y = max(_1592.y, (_59.WaterPlaneY + 0.1599999964237213134765625) + _2216);
            _2666 = _2574;
        }
        vec3 _2665;
        _2665 = _2666;
        vec3 _2762;
        SPIRV_CROSS_LOOP
        for (uint _2664 = 0u; _2664 < _59.BoxCount; _2665 = _2762, _2664++)
        {
            if ((((((_2665.x <= SolidBoxes_1._data[_2664].lo.x) || (_2665.x >= SolidBoxes_1._data[_2664].hi.x)) || (_2665.y <= SolidBoxes_1._data[_2664].lo.y)) || (_2665.y >= SolidBoxes_1._data[_2664].hi.y)) || (_2665.z <= SolidBoxes_1._data[_2664].lo.z)) || (_2665.z >= SolidBoxes_1._data[_2664].hi.z))
            {
                _2762 = _2665;
                continue;
            }
            int _2668;
            float _2686;
            float _2699;
            if (Predicted_1._data[gl_GlobalInvocationID.x].x <= SolidBoxes_1._data[_2664].lo.x)
            {
                _2699 = -1.0;
                _2686 = _2665.x - SolidBoxes_1._data[_2664].lo.x;
                _2668 = 0;
            }
            else
            {
                int _2669;
                float _2687;
                float _2700;
                if (Predicted_1._data[gl_GlobalInvocationID.x].x >= SolidBoxes_1._data[_2664].hi.x)
                {
                    _2700 = 1.0;
                    _2687 = SolidBoxes_1._data[_2664].hi.x - _2665.x;
                    _2669 = 0;
                }
                else
                {
                    int _2670;
                    float _2688;
                    float _2701;
                    if (Predicted_1._data[gl_GlobalInvocationID.x].y <= SolidBoxes_1._data[_2664].lo.y)
                    {
                        _2701 = -1.0;
                        _2688 = _2665.y - SolidBoxes_1._data[_2664].lo.y;
                        _2670 = 1;
                    }
                    else
                    {
                        int _2671;
                        float _2689;
                        float _2702;
                        if (Predicted_1._data[gl_GlobalInvocationID.x].y >= SolidBoxes_1._data[_2664].hi.y)
                        {
                            _2702 = 1.0;
                            _2689 = SolidBoxes_1._data[_2664].hi.y - _2665.y;
                            _2671 = 1;
                        }
                        else
                        {
                            int _2672;
                            float _2690;
                            float _2703;
                            if (Predicted_1._data[gl_GlobalInvocationID.x].z <= SolidBoxes_1._data[_2664].lo.z)
                            {
                                _2703 = -1.0;
                                _2690 = _2665.z - SolidBoxes_1._data[_2664].lo.z;
                                _2672 = 2;
                            }
                            else
                            {
                                bool _2417 = Predicted_1._data[gl_GlobalInvocationID.x].z >= SolidBoxes_1._data[_2664].hi.z;
                                float _2691;
                                if (_2417)
                                {
                                    _2691 = SolidBoxes_1._data[_2664].hi.z - _2665.z;
                                }
                                else
                                {
                                    _2691 = 0.0;
                                }
                                _2703 = float(_2417);
                                _2690 = _2691;
                                _2672 = _2417 ? 2 : (-1);
                            }
                            _2702 = _2703;
                            _2689 = _2690;
                            _2671 = _2672;
                        }
                        _2701 = _2702;
                        _2688 = _2689;
                        _2670 = _2671;
                    }
                    _2700 = _2701;
                    _2687 = _2688;
                    _2669 = _2670;
                }
                _2699 = _2700;
                _2686 = _2687;
                _2668 = _2669;
            }
            float _2685;
            int _2692;
            float _2698;
            if (_2668 < 0)
            {
                float _2437 = _2665.x - SolidBoxes_1._data[_2664].lo.x;
                float _2442 = SolidBoxes_1._data[_2664].hi.x - _2665.x;
                float _2447 = _2665.y - SolidBoxes_1._data[_2664].lo.y;
                float _2452 = SolidBoxes_1._data[_2664].hi.y - _2665.y;
                float _2457 = _2665.z - SolidBoxes_1._data[_2664].lo.z;
                float _2462 = SolidBoxes_1._data[_2664].hi.z - _2665.z;
                bool _2466 = _2442 < _2437;
                float _2782 = _2466 ? _2442 : _2437;
                bool _2472 = _2447 < _2782;
                float _2785 = _2472 ? _2447 : _2782;
                bool _2478 = _2452 < _2785;
                float _2788 = _2478 ? _2452 : _2785;
                bool _2484 = _2457 < _2788;
                float _2791 = _2484 ? _2457 : _2788;
                bool _2490 = _2462 < _2791;
                _2698 = _2490 ? 1.0 : (_2484 ? (-1.0) : (_2478 ? 1.0 : (_2472 ? (-1.0) : (_2466 ? 1.0 : (-1.0)))));
                _2692 = _2490 ? 2 : (_2484 ? 2 : (_2478 ? 1 : int(_2472)));
                _2685 = _2490 ? _2462 : _2791;
            }
            else
            {
                _2698 = _2699;
                _2692 = _2668;
                _2685 = _2686;
            }
            float _2502 = min(_2685 + _2216, _59.ParticleRadius * 0.0500000007450580596923828125);
            vec3 _2763;
            if (_2692 == 0)
            {
                vec3 _2625 = _2665;
                _2625.x = _2665.x + (_2698 * _2502);
                _2763 = _2625;
            }
            else
            {
                vec3 _2764;
                if (_2692 == 1)
                {
                    vec3 _2628 = _2665;
                    _2628.y = _2665.y + (_2698 * _2502);
                    _2764 = _2628;
                }
                else
                {
                    vec3 _2631 = _2665;
                    _2631.z = _2665.z + (_2698 * _2502);
                    _2764 = _2631;
                }
                _2763 = _2764;
            }
            _2762 = _2763;
        }
        PredictedNext_1._data[gl_GlobalInvocationID.x] = vec4(_2665, 1.0);
        break;
    } while(false);
}

