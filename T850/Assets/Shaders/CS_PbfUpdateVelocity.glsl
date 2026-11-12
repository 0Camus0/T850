#version 430
// GENERATED from CS_PbfUpdateVelocity.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=5e1410a5c20cf712d87e4bea65adfed85fcb9354a8d947ad5626906e2e029226; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float WallFriction;
} _37;

layout(binding = 2, std430) readonly buffer Predicted
{
    vec4 _data[];
} Predicted_1;

layout(binding = 4, std430) buffer VelocitiesNext
{
    vec4 _data[];
} VelocitiesNext_1;

layout(binding = 1, std430) buffer Positions
{
    vec4 _data[];
} Positions_1;

layout(binding = 5, std430) readonly buffer GridBuffer
{
    uint _data[];
} GridBuffer_1;

layout(binding = 3, std430) readonly buffer Velocities
{
    vec4 _data[];
} Velocities_1;

layout(binding = 6, std430) readonly buffer SolidBoxes
{
    SolidBox _data[];
} SolidBoxes_1;

void main()
{
    do
    {
        if (gl_GlobalInvocationID.x >= _37.ParticleCount)
        {
            break;
        }
        uint _834 = (_37.GridX * _37.GridY) * _37.GridZ;
        float _837 = 1.0 / _37.SmoothingRadius;
        if (Predicted_1._data[gl_GlobalInvocationID.x].w < 0.5)
        {
            VelocitiesNext_1._data[gl_GlobalInvocationID.x] = vec4(0.0);
            Positions_1._data[gl_GlobalInvocationID.x] = vec4(Predicted_1._data[gl_GlobalInvocationID.x].xyz, 0.0);
            break;
        }
        vec4 _866 = Predicted_1._data[gl_GlobalInvocationID.x];
        vec3 _872 = (_866.xyz - Positions_1._data[gl_GlobalInvocationID.x].xyz) * (1.0 / max(_37.DeltaTime, 9.9999999747524270787835121154785e-07));
        float _874 = length(_872);
        vec3 _884 = _872 * (_37.MaxVelocity / _874);
        bvec3 _886 = bvec3(_874 > _37.MaxVelocity);
        vec3 _887 = vec3(_886.x ? _884.x : _872.x, _886.y ? _884.y : _872.y, _886.z ? _884.z : _872.z);
        int _901 = clamp(int(floor((_866.x - _37.GridOriginX) * _837)), 0, int(_37.GridX) - 1);
        int _915 = clamp(int(floor((_866.y - _37.GridOriginY) * _837)), 0, int(_37.GridY) - 1);
        int _929 = clamp(int(floor((_866.z - _37.GridOriginZ) * _837)), 0, int(_37.GridZ) - 1);
        float _934 = _37.SmoothingRadius * _37.SmoothingRadius;
        float _1721;
        vec3 _1722;
        _1722 = vec3(0.0);
        _1721 = 0.0;
        vec3 _1756;
        float _1760;
        SPIRV_CROSS_LOOP
        for (int _1720 = -1; _1720 <= 1; _1722 = _1756, _1721 = _1760, _1720++)
        {
            _1760 = _1721;
            _1756 = _1722;
            vec3 _1755;
            float _1759;
            SPIRV_CROSS_LOOP
            for (int _1742 = -1; _1742 <= 1; _1742++, _1760 = _1759, _1756 = _1755)
            {
                _1759 = _1760;
                _1755 = _1756;
                vec3 _1777;
                float _1779;
                SPIRV_CROSS_LOOP
                for (int _1744 = -1; _1744 <= 1; _1759 = _1779, _1755 = _1777, _1744++)
                {
                    int _952 = _901 + _1744;
                    int _955 = _915 + _1742;
                    int _958 = _929 + _1720;
                    if ((((((_952 < 0) || (_955 < 0)) || (_958 < 0)) || (_952 >= int(_37.GridX))) || (_955 >= int(_37.GridY))) || (_958 >= int(_37.GridZ)))
                    {
                        _1779 = _1759;
                        _1777 = _1755;
                        continue;
                    }
                    uint _1000 = uint(_952) + (_37.GridX * (uint(_955) + (_37.GridY * uint(_958))));
                    uint _1004 = min(GridBuffer_1._data[_1000], 64u);
                    vec3 _1754;
                    float _1758;
                    _1758 = _1759;
                    _1754 = _1755;
                    vec3 _1778;
                    float _1780;
                    SPIRV_CROSS_LOOP
                    for (uint _1747 = 0u; _1747 < _1004; _1758 = _1780, _1754 = _1778, _1747++)
                    {
                        uint _1017 = (64u * (_834 + _1000)) + _1747;
                        if ((GridBuffer_1._data[_1017] >= _37.ParticleCount) || (GridBuffer_1._data[_1017] == gl_GlobalInvocationID.x))
                        {
                            _1780 = _1758;
                            _1778 = _1754;
                            continue;
                        }
                        vec3 _1036 = _866.xyz - Predicted_1._data[GridBuffer_1._data[_1017]].xyz;
                        float _1039 = dot(_1036, _1036);
                        if (_1039 >= _934)
                        {
                            _1780 = _1758;
                            _1778 = _1754;
                            continue;
                        }
                        float _1748;
                        do
                        {
                            if (_1039 >= (_37.SmoothingRadius * _37.SmoothingRadius))
                            {
                                _1748 = 0.0;
                                break;
                            }
                            float _1246 = (_37.SmoothingRadius * _37.SmoothingRadius) - _1039;
                            _1748 = (((1.566681385040283203125 / pow(_37.SmoothingRadius, 9.0)) * _1246) * _1246) * _1246;
                            break;
                        } while(false);
                        _1780 = _1758 + _1748;
                        _1778 = _1754 + ((Velocities_1._data[GridBuffer_1._data[_1017]].xyz - _887) * _1748);
                    }
                    _1779 = _1758;
                    _1777 = _1754;
                }
            }
        }
        vec3 _1733;
        if (_1721 > 9.9999999600419720025001879548654e-13)
        {
            _1733 = _887 + ((_1722 / vec3(_1721)) * _37.ViscosityC);
        }
        else
        {
            _1733 = _887;
        }
        float _1724;
        if ((((_866.x > _37.BasinMinX) && (_866.x < _37.BasinMaxX)) && (_866.z > _37.BasinMinZ)) && (_866.z < _37.BasinMaxZ))
        {
            float _1384 = clamp((_866.y - _37.BasinMinY) / _37.SmoothingRadius, 0.0, 1.0);
            float _1387 = _1384 * _1384;
            float _1390 = _1387 * _1384;
            float _1393 = _1390 * _1387;
            float _1396 = _1393 * _1387;
            _1724 = (((((1.0 - _1384) - (1.33333337306976318359375 * (1.0 - _1390))) + (1.2000000476837158203125 * (1.0 - _1393))) - (0.571428596973419189453125 * (1.0 - _1396))) + (0.111111111938953399658203125 * (1.0 - (_1396 * _1387)))) * 1.23046875;
        }
        else
        {
            _1724 = 0.0;
        }
        float _1725;
        if (((_866.y < _37.WallTopY) && (_866.z > _37.BasinMinZ)) && (_866.z < _37.BasinMaxZ))
        {
            float _1434 = clamp((_866.x - _37.BasinMinX) / _37.SmoothingRadius, 0.0, 1.0);
            float _1437 = _1434 * _1434;
            float _1440 = _1437 * _1434;
            float _1443 = _1440 * _1437;
            float _1446 = _1443 * _1437;
            float _1484 = clamp((_37.BasinMaxX - _866.x) / _37.SmoothingRadius, 0.0, 1.0);
            float _1487 = _1484 * _1484;
            float _1490 = _1487 * _1484;
            float _1493 = _1490 * _1487;
            float _1496 = _1493 * _1487;
            _1725 = (_1724 + ((((((1.0 - _1434) - (1.33333337306976318359375 * (1.0 - _1440))) + (1.2000000476837158203125 * (1.0 - _1443))) - (0.571428596973419189453125 * (1.0 - _1446))) + (0.111111111938953399658203125 * (1.0 - (_1446 * _1437)))) * 1.23046875)) + ((((((1.0 - _1484) - (1.33333337306976318359375 * (1.0 - _1490))) + (1.2000000476837158203125 * (1.0 - _1493))) - (0.571428596973419189453125 * (1.0 - _1496))) + (0.111111111938953399658203125 * (1.0 - (_1496 * _1487)))) * 1.23046875);
        }
        else
        {
            _1725 = _1724;
        }
        float _1726;
        if (((_866.y < _37.WallTopY) && (_866.x > _37.BasinMinX)) && (_866.x < _37.BasinMaxX))
        {
            float _1534 = clamp((_866.z - _37.BasinMinZ) / _37.SmoothingRadius, 0.0, 1.0);
            float _1537 = _1534 * _1534;
            float _1540 = _1537 * _1534;
            float _1543 = _1540 * _1537;
            float _1546 = _1543 * _1537;
            float _1584 = clamp((_37.BasinMaxZ - _866.z) / _37.SmoothingRadius, 0.0, 1.0);
            float _1587 = _1584 * _1584;
            float _1590 = _1587 * _1584;
            float _1593 = _1590 * _1587;
            float _1596 = _1593 * _1587;
            _1726 = (_1725 + ((((((1.0 - _1534) - (1.33333337306976318359375 * (1.0 - _1540))) + (1.2000000476837158203125 * (1.0 - _1543))) - (0.571428596973419189453125 * (1.0 - _1546))) + (0.111111111938953399658203125 * (1.0 - (_1546 * _1537)))) * 1.23046875)) + ((((((1.0 - _1584) - (1.33333337306976318359375 * (1.0 - _1590))) + (1.2000000476837158203125 * (1.0 - _1593))) - (0.571428596973419189453125 * (1.0 - _1596))) + (0.111111111938953399658203125 * (1.0 - (_1596 * _1587)))) * 1.23046875);
        }
        else
        {
            _1726 = _1725;
        }
        float _1728;
        _1728 = _1726;
        SPIRV_CROSS_LOOP
        for (uint _1727 = 0u; _1727 < _37.BoxCount; )
        {
            float _1645 = clamp(length(_866.xyz - clamp(_866.xyz, SolidBoxes_1._data[_1727].lo.xyz, SolidBoxes_1._data[_1727].hi.xyz)) / _37.SmoothingRadius, 0.0, 1.0);
            float _1648 = _1645 * _1645;
            float _1651 = _1648 * _1645;
            float _1654 = _1651 * _1648;
            float _1657 = _1654 * _1648;
            _1728 += ((((((1.0 - _1645) - (1.33333337306976318359375 * (1.0 - _1651))) + (1.2000000476837158203125 * (1.0 - _1654))) - (0.571428596973419189453125 * (1.0 - _1657))) + (0.111111111938953399658203125 * (1.0 - (_1657 * _1648)))) * 1.23046875);
            _1727++;
            continue;
        }
        vec3 _1734;
        if (_1728 > 0.0)
        {
            _1734 = _1733 * max(0.0, 1.0 - ((_37.WallFriction * _37.ViscosityC) * _1728));
        }
        else
        {
            _1734 = _1733;
        }
        float _1132 = _37.ParticleRadius * 0.5;
        vec3 _1735;
        if (_866.y <= ((_37.BasinMinY + _1132) + 9.9999997473787516355514526367188e-05))
        {
            vec3 _1703 = _1734;
            _1703.y = 0.0;
            _1735 = _1703;
        }
        else
        {
            _1735 = _1734;
        }
        vec3 _1738;
        if (_866.y < _37.WallTopY)
        {
            vec3 _1736;
            if (_866.x <= ((_37.BasinMinX + _1132) + 9.9999997473787516355514526367188e-05))
            {
                vec3 _1707 = _1735;
                _1707.x = 0.0;
                _1736 = _1707;
            }
            else
            {
                vec3 _1737;
                if (_866.x >= ((_37.BasinMaxX - _1132) - 9.9999997473787516355514526367188e-05))
                {
                    vec3 _1710 = _1735;
                    _1710.x = 0.0;
                    _1737 = _1710;
                }
                else
                {
                    _1737 = _1735;
                }
                _1736 = _1737;
            }
            vec3 _1739;
            if (_866.z <= ((_37.BasinMinZ + _1132) + 9.9999997473787516355514526367188e-05))
            {
                vec3 _1713 = _1736;
                _1713.z = 0.0;
                _1739 = _1713;
            }
            else
            {
                vec3 _1740;
                if (_866.z >= ((_37.BasinMaxZ - _1132) - 9.9999997473787516355514526367188e-05))
                {
                    vec3 _1716 = _1736;
                    _1716.z = 0.0;
                    _1740 = _1716;
                }
                else
                {
                    _1740 = _1736;
                }
                _1739 = _1740;
            }
            _1738 = _1739;
        }
        else
        {
            _1738 = _1735;
        }
        float _1198 = length(_1738);
        vec3 _1741;
        if (_1198 > _37.MaxVelocity)
        {
            _1741 = _1738 * (_37.MaxVelocity / _1198);
        }
        else
        {
            _1741 = _1738;
        }
        VelocitiesNext_1._data[gl_GlobalInvocationID.x] = vec4(_1741, 0.0);
        Positions_1._data[gl_GlobalInvocationID.x] = vec4(_866.xyz, 1.0);
        break;
    } while(false);
}

