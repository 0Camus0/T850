#version 430
// GENERATED from CS_PbfFields.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=ef9dc118ea328f130d1ab609f0081531d62650f6ddf2565fb68e6d3ef0785736; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 0, std140) uniform PbfWhitewaterConstants
{
    uint ParticleCount;
    uint DiffuseCapacity;
    uint GridX;
    uint GridY;
    uint GridZ;
    uint FrameSeed;
    uint SurfaceWidth;
    uint SurfaceDepth;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float CellSize;
    float DeltaTime;
    float Spacing;
    float Gravity;
    float SurfaceNeighbours;
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float BasinMaxX;
    float BasinMaxY;
    float BasinMaxZ;
    float WaterPlaneY;
    float DeckY;
    float TrappedAirMin;
    float TrappedAirMax;
    float CrestMin;
    float CrestMax;
    float EnergyMin;
    float EnergyMax;
    float FoamGain;
    float FoamDecay;
    float AerationGain;
    float AerationDecay;
    float SprayRate;
    float CrestRate;
    float DiffuseLifetime;
    float BubbleBuoyancy;
    float BubbleDrag;
    float SprayDrag;
    float WetOriginX;
    float WetOriginZ;
    float WetTexelSize;
    float WetDecay;
    uint WetWidth;
    uint WetHeight;
    uint Mode;
    float CausticGain;
} _61;

layout(binding = 1, std430) readonly buffer ColumnHeights
{
    int _data[];
} ColumnHeights_1;

layout(binding = 2, std430) buffer Wetness
{
    uint _data[];
} Wetness_1;

layout(binding = 3, std430) buffer CausticField
{
    float _data[];
} CausticField_1;

void main()
{
    do
    {
        if (_61.Mode == 0u)
        {
            if (gl_GlobalInvocationID.x >= (_61.WetWidth * _61.WetHeight))
            {
                break;
            }
            if (Wetness_1._data[gl_GlobalInvocationID.x] != 0u)
            {
                Wetness_1._data[gl_GlobalInvocationID.x] = uint(float(Wetness_1._data[gl_GlobalInvocationID.x]) * _61.WetDecay);
            }
            break;
        }
        if (gl_GlobalInvocationID.x >= (_61.SurfaceWidth * _61.SurfaceDepth))
        {
            break;
        }
        int _439 = int(gl_GlobalInvocationID.x % _61.SurfaceWidth);
        int _444 = int(gl_GlobalInvocationID.x / _61.SurfaceWidth);
        float _455 = (_61.BasinMaxX - _61.BasinMinX) / max(float(_61.SurfaceWidth) - 1.0, 1.0);
        float _466 = (_61.BasinMaxZ - _61.BasinMinZ) / max(float(_61.SurfaceDepth) - 1.0, 1.0);
        float _1041;
        float _1042;
        _1042 = 0.0;
        _1041 = 0.0;
        float _1077;
        float _1078;
        SPIRV_CROSS_UNROLL
        for (int _1040 = -2; _1040 <= 2; _1042 = _1077, _1041 = _1078, _1040++)
        {
            _1078 = _1041;
            _1077 = _1042;
            float _1093;
            float _1094;
            SPIRV_CROSS_UNROLL
            for (int _1075 = -2; _1075 <= 2; _1078 = _1094, _1077 = _1093, _1075++)
            {
                int _571 = _439 + _1075;
                int _574 = _444 + _1040;
                if ((((_571 < 0) || (_574 < 0)) || (_571 >= int(_61.SurfaceWidth))) || (_574 >= int(_61.SurfaceDepth)))
                {
                    _1094 = _1078;
                    _1093 = _1077;
                    continue;
                }
                uint _601 = uint(_571) + (_61.SurfaceWidth * uint(_574));
                if (ColumnHeights_1._data[(_601 * 2u) + 1u] <= 0)
                {
                    _1094 = _1078;
                    _1093 = _1077;
                    continue;
                }
                float _619 = exp((-0.3499999940395355224609375) * float((_1075 * _1075) + (_1040 * _1040)));
                _1094 = _1078 + _619;
                _1093 = _1077 + (intBitsToFloat(ColumnHeights_1._data[_601 * 2u]) * _619);
            }
        }
        float _646 = (_1041 > 0.0) ? (_1042 / _1041) : (-1.0);
        if (_646 <= 0.0)
        {
            CausticField_1._data[gl_GlobalInvocationID.x] = 1.0;
            break;
        }
        int _477 = _439 - 2;
        float _1044;
        float _1045;
        _1045 = 0.0;
        _1044 = 0.0;
        float _1072;
        float _1073;
        SPIRV_CROSS_UNROLL
        for (int _1043 = -2; _1043 <= 2; _1045 = _1072, _1044 = _1073, _1043++)
        {
            _1073 = _1044;
            _1072 = _1045;
            float _1090;
            float _1091;
            SPIRV_CROSS_UNROLL
            for (int _1070 = -2; _1070 <= 2; _1073 = _1091, _1072 = _1090, _1070++)
            {
                int _669 = _477 + _1070;
                int _672 = _444 + _1043;
                if ((((_669 < 0) || (_672 < 0)) || (_669 >= int(_61.SurfaceWidth))) || (_672 >= int(_61.SurfaceDepth)))
                {
                    _1091 = _1073;
                    _1090 = _1072;
                    continue;
                }
                uint _699 = uint(_669) + (_61.SurfaceWidth * uint(_672));
                if (ColumnHeights_1._data[(_699 * 2u) + 1u] <= 0)
                {
                    _1091 = _1073;
                    _1090 = _1072;
                    continue;
                }
                float _717 = exp((-0.3499999940395355224609375) * float((_1070 * _1070) + (_1043 * _1043)));
                _1091 = _1073 + _717;
                _1090 = _1072 + (intBitsToFloat(ColumnHeights_1._data[_699 * 2u]) * _717);
            }
        }
        float _744 = (_1044 > 0.0) ? (_1045 / _1044) : (-1.0);
        int _481 = _439 + 2;
        float _1047;
        float _1048;
        _1048 = 0.0;
        _1047 = 0.0;
        float _1067;
        float _1068;
        SPIRV_CROSS_UNROLL
        for (int _1046 = -2; _1046 <= 2; _1048 = _1067, _1047 = _1068, _1046++)
        {
            _1068 = _1047;
            _1067 = _1048;
            float _1087;
            float _1088;
            SPIRV_CROSS_UNROLL
            for (int _1065 = -2; _1065 <= 2; _1068 = _1088, _1067 = _1087, _1065++)
            {
                int _767 = _481 + _1065;
                int _770 = _444 + _1046;
                if ((((_767 < 0) || (_770 < 0)) || (_767 >= int(_61.SurfaceWidth))) || (_770 >= int(_61.SurfaceDepth)))
                {
                    _1088 = _1068;
                    _1087 = _1067;
                    continue;
                }
                uint _797 = uint(_767) + (_61.SurfaceWidth * uint(_770));
                if (ColumnHeights_1._data[(_797 * 2u) + 1u] <= 0)
                {
                    _1088 = _1068;
                    _1087 = _1067;
                    continue;
                }
                float _815 = exp((-0.3499999940395355224609375) * float((_1065 * _1065) + (_1046 * _1046)));
                _1088 = _1068 + _815;
                _1087 = _1067 + (intBitsToFloat(ColumnHeights_1._data[_797 * 2u]) * _815);
            }
        }
        float _842 = (_1047 > 0.0) ? (_1048 / _1047) : (-1.0);
        int _485 = _444 - 2;
        float _1050;
        float _1051;
        _1051 = 0.0;
        _1050 = 0.0;
        float _1062;
        float _1063;
        SPIRV_CROSS_UNROLL
        for (int _1049 = -2; _1049 <= 2; _1051 = _1062, _1050 = _1063, _1049++)
        {
            _1063 = _1050;
            _1062 = _1051;
            float _1084;
            float _1085;
            SPIRV_CROSS_UNROLL
            for (int _1060 = -2; _1060 <= 2; _1063 = _1085, _1062 = _1084, _1060++)
            {
                int _865 = _439 + _1060;
                int _868 = _485 + _1049;
                if ((((_865 < 0) || (_868 < 0)) || (_865 >= int(_61.SurfaceWidth))) || (_868 >= int(_61.SurfaceDepth)))
                {
                    _1085 = _1063;
                    _1084 = _1062;
                    continue;
                }
                uint _895 = uint(_865) + (_61.SurfaceWidth * uint(_868));
                if (ColumnHeights_1._data[(_895 * 2u) + 1u] <= 0)
                {
                    _1085 = _1063;
                    _1084 = _1062;
                    continue;
                }
                float _913 = exp((-0.3499999940395355224609375) * float((_1060 * _1060) + (_1049 * _1049)));
                _1085 = _1063 + _913;
                _1084 = _1062 + (intBitsToFloat(ColumnHeights_1._data[_895 * 2u]) * _913);
            }
        }
        float _940 = (_1050 > 0.0) ? (_1051 / _1050) : (-1.0);
        int _489 = _444 + 2;
        float _1053;
        float _1054;
        _1054 = 0.0;
        _1053 = 0.0;
        float _1057;
        float _1058;
        SPIRV_CROSS_UNROLL
        for (int _1052 = -2; _1052 <= 2; _1054 = _1057, _1053 = _1058, _1052++)
        {
            _1058 = _1053;
            _1057 = _1054;
            float _1081;
            float _1082;
            SPIRV_CROSS_UNROLL
            for (int _1055 = -2; _1055 <= 2; _1058 = _1082, _1057 = _1081, _1055++)
            {
                int _963 = _439 + _1055;
                int _966 = _489 + _1052;
                if ((((_963 < 0) || (_966 < 0)) || (_963 >= int(_61.SurfaceWidth))) || (_966 >= int(_61.SurfaceDepth)))
                {
                    _1082 = _1058;
                    _1081 = _1057;
                    continue;
                }
                uint _993 = uint(_963) + (_61.SurfaceWidth * uint(_966));
                if (ColumnHeights_1._data[(_993 * 2u) + 1u] <= 0)
                {
                    _1082 = _1058;
                    _1081 = _1057;
                    continue;
                }
                float _1011 = exp((-0.3499999940395355224609375) * float((_1055 * _1055) + (_1052 * _1052)));
                _1082 = _1058 + _1011;
                _1081 = _1057 + (intBitsToFloat(ColumnHeights_1._data[_993 * 2u]) * _1011);
            }
        }
        float _1038 = (_1053 > 0.0) ? (_1054 / _1053) : (-1.0);
        CausticField_1._data[gl_GlobalInvocationID.x] = clamp(1.0 / max(1.0 + ((_61.CausticGain * _646) * ((((((_842 > 0.0) ? _842 : _646) + ((_744 > 0.0) ? _744 : _646)) - (2.0 * _646)) / ((4.0 * _455) * _455)) + (((((_1038 > 0.0) ? _1038 : _646) + ((_940 > 0.0) ? _940 : _646)) - (2.0 * _646)) / ((4.0 * _466) * _466)))), 0.3499999940395355224609375), 0.3499999940395355224609375, 2.5);
        break;
    } while(false);
}

