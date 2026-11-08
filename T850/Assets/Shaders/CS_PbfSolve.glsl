#version 430
// GENERATED from CS_PbfSolve.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=bc6d8b0a11a573ef34cf46306286c23c8832f322db1711298a5776ea972dbdac; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float Pad6;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
} _35;

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
        if (gl_GlobalInvocationID.x >= _35.ParticleCount)
        {
            break;
        }
        uint _942 = (_35.GridX * _35.GridY) * _35.GridZ;
        float _945 = 1.0 / _35.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            PredictedNext_1._data[gl_GlobalInvocationID.x] = vec4(Predicted_1._data[gl_GlobalInvocationID.x].xyz, 0.0);
            break;
        }
        float _971 = 0.300000011920928955078125 * _35.SmoothingRadius;
        float _974 = _971 * _971;
        float _1773;
        do
        {
            if (_974 >= (_35.SmoothingRadius * _35.SmoothingRadius))
            {
                _1773 = 0.0;
                break;
            }
            float _1243 = (_35.SmoothingRadius * _35.SmoothingRadius) - _974;
            _1773 = ((((1.566681385040283203125 / pow(_35.SmoothingRadius, 9.0)) * _1243) * _1243) * _1243) / max(_35.RestScale, 9.9999999747524270787835121154785e-07);
            break;
        } while(false);
        float _980 = (_1773 > 9.9999999600419720025001879548654e-13) ? (1.0 / _1773) : 0.0;
        int _994 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].x - _35.GridOriginX) * _945)), 0, int(_35.GridX) - 1);
        int _1008 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].y - _35.GridOriginY) * _945)), 0, int(_35.GridY) - 1);
        int _1022 = clamp(int(floor((Predicted_1._data[gl_GlobalInvocationID.x].z - _35.GridOriginZ) * _945)), 0, int(_35.GridZ) - 1);
        float _1027 = _35.SmoothingRadius * _35.SmoothingRadius;
        vec3 _1775;
        _1775 = vec3(0.0);
        vec3 _1856;
        SPIRV_CROSS_LOOP
        for (int _1774 = -1; _1774 <= 1; _1775 = _1856, _1774++)
        {
            _1856 = _1775;
            vec3 _1855;
            SPIRV_CROSS_LOOP
            for (int _1844 = -1; _1844 <= 1; _1844++, _1856 = _1855)
            {
                _1855 = _1856;
                vec3 _1878;
                SPIRV_CROSS_LOOP
                for (int _1846 = -1; _1846 <= 1; _1855 = _1878, _1846++)
                {
                    int _1045 = _994 + _1846;
                    int _1048 = _1008 + _1844;
                    int _1051 = _1022 + _1774;
                    if ((((((_1045 < 0) || (_1048 < 0)) || (_1051 < 0)) || (_1045 >= int(_35.GridX))) || (_1048 >= int(_35.GridY))) || (_1051 >= int(_35.GridZ)))
                    {
                        _1878 = _1855;
                        continue;
                    }
                    uint _1093 = uint(_1045) + (_35.GridX * (uint(_1048) + (_35.GridY * uint(_1051))));
                    uint _1097 = min(GridBuffer_1._data[_1093], 64u);
                    vec3 _1854;
                    _1854 = _1855;
                    vec3 _1879;
                    SPIRV_CROSS_LOOP
                    for (uint _1849 = 0u; _1849 < _1097; _1854 = _1879, _1849++)
                    {
                        uint _1110 = (64u * (_942 + _1093)) + _1849;
                        if ((GridBuffer_1._data[_1110] >= _35.ParticleCount) || (GridBuffer_1._data[_1110] == gl_GlobalInvocationID.x))
                        {
                            _1879 = _1854;
                            continue;
                        }
                        vec3 _1129 = Predicted_1._data[gl_GlobalInvocationID.x].xyz - Predicted_1._data[GridBuffer_1._data[_1110]].xyz;
                        float _1132 = dot(_1129, _1129);
                        if (_1132 >= _1027)
                        {
                            _1879 = _1854;
                            continue;
                        }
                        vec3 _1850;
                        do
                        {
                            float _1272 = length(_1129);
                            if ((_1272 <= 9.9999999747524270787835121154785e-07) || (_1272 >= _35.SmoothingRadius))
                            {
                                _1850 = vec3(0.0);
                                break;
                            }
                            _1850 = _1129 * ((((((-14.323944091796875) / pow(_35.SmoothingRadius, 6.0)) * (_35.SmoothingRadius - _1272)) * (_35.SmoothingRadius - _1272)) / _1272) / max(_35.RestScale, 9.9999999747524270787835121154785e-07));
                            break;
                        } while(false);
                        float _1851;
                        do
                        {
                            if (_1132 >= (_35.SmoothingRadius * _35.SmoothingRadius))
                            {
                                _1851 = 0.0;
                                break;
                            }
                            float _1329 = (_35.SmoothingRadius * _35.SmoothingRadius) - _1132;
                            _1851 = ((((1.566681385040283203125 / pow(_35.SmoothingRadius, 9.0)) * _1329) * _1329) * _1329) / max(_35.RestScale, 9.9999999747524270787835121154785e-07);
                            break;
                        } while(false);
                        _1879 = _1854 + (_1850 * ((Lambda_1._data[gl_GlobalInvocationID.x].x + Lambda_1._data[GridBuffer_1._data[_1110]].x) + ((_1132 < ((0.810000002384185791015625 * _35.ParticleRadius) * _35.ParticleRadius)) ? ((-_35.ArtPressureK) * pow(_1851 * _980, _35.ArtPressureN)) : 0.0)));
                    }
                    _1878 = _1854;
                }
            }
        }
        vec3 _1194 = _1775 * (1.0 / _35.RestDensity);
        float _1197 = _35.ParticleRadius * 0.100000001490116119384765625;
        float _1199 = length(_1194);
        vec3 _1776;
        if (_1199 > _1197)
        {
            _1776 = _1194 * (_1197 / _1199);
        }
        else
        {
            _1776 = _1194;
        }
        vec3 _1212 = Predicted_1._data[gl_GlobalInvocationID.x].xyz + _1776;
        float _1369 = _35.ParticleRadius * 0.5;
        vec3 _1779;
        if ((((_1212.x > (_35.BasinMinX - _1369)) && (_1212.x < (_35.BasinMaxX + _1369))) && (_1212.z > (_35.BasinMinZ - _1369))) && (_1212.z < (_35.BasinMaxZ + _1369)))
        {
            float _1409 = max(_1212.y, _35.BasinMinY + _1369);
            vec3 _1702 = _1212;
            _1702.y = _1409;
            vec3 _1780;
            if (_1409 < _35.WaterPlaneY)
            {
                _1780 = vec3(clamp(_1212.x, _35.BasinMinX + _1369, _35.BasinMaxX - _1369), _1409, clamp(_1212.z, _35.BasinMinZ + _1369, _35.BasinMaxZ - _1369));
            }
            else
            {
                _1780 = _1702;
            }
            _1779 = _1780;
        }
        else
        {
            vec3 _1712 = _1212;
            _1712.y = max(_1212.y, (_35.WaterPlaneY + 0.1599999964237213134765625) + _1369);
            _1779 = _1712;
        }
        vec3 _1778;
        _1778 = _1779;
        vec3 _1865;
        SPIRV_CROSS_LOOP
        for (uint _1777 = 0u; _1777 < _35.BoxCount; _1778 = _1865, _1777++)
        {
            if ((((((_1778.x <= SolidBoxes_1._data[_1777].lo.x) || (_1778.x >= SolidBoxes_1._data[_1777].hi.x)) || (_1778.y <= SolidBoxes_1._data[_1777].lo.y)) || (_1778.y >= SolidBoxes_1._data[_1777].hi.y)) || (_1778.z <= SolidBoxes_1._data[_1777].lo.z)) || (_1778.z >= SolidBoxes_1._data[_1777].hi.z))
            {
                _1865 = _1778;
                continue;
            }
            int _1781;
            float _1799;
            float _1812;
            if (Predicted_1._data[gl_GlobalInvocationID.x].x <= SolidBoxes_1._data[_1777].lo.x)
            {
                _1812 = -1.0;
                _1799 = _1778.x - SolidBoxes_1._data[_1777].lo.x;
                _1781 = 0;
            }
            else
            {
                int _1782;
                float _1800;
                float _1813;
                if (Predicted_1._data[gl_GlobalInvocationID.x].x >= SolidBoxes_1._data[_1777].hi.x)
                {
                    _1813 = 1.0;
                    _1800 = SolidBoxes_1._data[_1777].hi.x - _1778.x;
                    _1782 = 0;
                }
                else
                {
                    int _1783;
                    float _1801;
                    float _1814;
                    if (Predicted_1._data[gl_GlobalInvocationID.x].y <= SolidBoxes_1._data[_1777].lo.y)
                    {
                        _1814 = -1.0;
                        _1801 = _1778.y - SolidBoxes_1._data[_1777].lo.y;
                        _1783 = 1;
                    }
                    else
                    {
                        int _1784;
                        float _1802;
                        float _1815;
                        if (Predicted_1._data[gl_GlobalInvocationID.x].y >= SolidBoxes_1._data[_1777].hi.y)
                        {
                            _1815 = 1.0;
                            _1802 = SolidBoxes_1._data[_1777].hi.y - _1778.y;
                            _1784 = 1;
                        }
                        else
                        {
                            int _1785;
                            float _1803;
                            float _1816;
                            if (Predicted_1._data[gl_GlobalInvocationID.x].z <= SolidBoxes_1._data[_1777].lo.z)
                            {
                                _1816 = -1.0;
                                _1803 = _1778.z - SolidBoxes_1._data[_1777].lo.z;
                                _1785 = 2;
                            }
                            else
                            {
                                bool _1570 = Predicted_1._data[gl_GlobalInvocationID.x].z >= SolidBoxes_1._data[_1777].hi.z;
                                float _1804;
                                if (_1570)
                                {
                                    _1804 = SolidBoxes_1._data[_1777].hi.z - _1778.z;
                                }
                                else
                                {
                                    _1804 = 0.0;
                                }
                                _1816 = float(_1570);
                                _1803 = _1804;
                                _1785 = _1570 ? 2 : (-1);
                            }
                            _1815 = _1816;
                            _1802 = _1803;
                            _1784 = _1785;
                        }
                        _1814 = _1815;
                        _1801 = _1802;
                        _1783 = _1784;
                    }
                    _1813 = _1814;
                    _1800 = _1801;
                    _1782 = _1783;
                }
                _1812 = _1813;
                _1799 = _1800;
                _1781 = _1782;
            }
            float _1798;
            int _1805;
            float _1811;
            if (_1781 < 0)
            {
                float _1590 = _1778.x - SolidBoxes_1._data[_1777].lo.x;
                float _1595 = SolidBoxes_1._data[_1777].hi.x - _1778.x;
                float _1600 = _1778.y - SolidBoxes_1._data[_1777].lo.y;
                float _1605 = SolidBoxes_1._data[_1777].hi.y - _1778.y;
                float _1610 = _1778.z - SolidBoxes_1._data[_1777].lo.z;
                float _1615 = SolidBoxes_1._data[_1777].hi.z - _1778.z;
                bool _1619 = _1595 < _1590;
                float _1883 = _1619 ? _1595 : _1590;
                bool _1625 = _1600 < _1883;
                float _1886 = _1625 ? _1600 : _1883;
                bool _1631 = _1605 < _1886;
                float _1889 = _1631 ? _1605 : _1886;
                bool _1637 = _1610 < _1889;
                float _1892 = _1637 ? _1610 : _1889;
                bool _1643 = _1615 < _1892;
                _1811 = _1643 ? 1.0 : (_1637 ? (-1.0) : (_1631 ? 1.0 : (_1625 ? (-1.0) : (_1619 ? 1.0 : (-1.0)))));
                _1805 = _1643 ? 2 : (_1637 ? 2 : (_1631 ? 1 : int(_1625)));
                _1798 = _1643 ? _1615 : _1892;
            }
            else
            {
                _1811 = _1812;
                _1805 = _1781;
                _1798 = _1799;
            }
            float _1655 = min(_1798 + _1369, _35.ParticleRadius * 0.0500000007450580596923828125);
            vec3 _1866;
            if (_1805 == 0)
            {
                vec3 _1763 = _1778;
                _1763.x = _1778.x + (_1811 * _1655);
                _1866 = _1763;
            }
            else
            {
                vec3 _1867;
                if (_1805 == 1)
                {
                    vec3 _1766 = _1778;
                    _1766.y = _1778.y + (_1811 * _1655);
                    _1867 = _1766;
                }
                else
                {
                    vec3 _1769 = _1778;
                    _1769.z = _1778.z + (_1811 * _1655);
                    _1867 = _1769;
                }
                _1866 = _1867;
            }
            _1865 = _1866;
        }
        PredictedNext_1._data[gl_GlobalInvocationID.x] = vec4(_1778, 1.0);
        break;
    } while(false);
}

