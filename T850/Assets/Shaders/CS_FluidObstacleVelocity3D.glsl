#version 430
// GENERATED from CS_FluidObstacleVelocity3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=4dbb7bc97b370a4763712ded9b7b852bc08b0bddef65131fca85f5e77ddb8aea; defines=T850_SPIRV=1,T850_VULKAN=1
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

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
} _52;

layout(binding = 3, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 2, std140) uniform ObjectConstants
{
    uint BodyCount;
    uint ObjectPad0;
    uint ObjectPad1;
    uint ObjectPad2;
} _174;

layout(binding = 5, std430) readonly buffer BodyVelocity
{
    vec4 _data[];
} BodyVelocity_1;

layout(binding = 4, rgba16f) uniform writeonly image3D VelocityOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _52.Width) || (gl_GlobalInvocationID.y >= _52.Height)) || (gl_GlobalInvocationID.z >= _52.Depth))
        {
            break;
        }
        ivec3 _391 = ivec3(gl_GlobalInvocationID);
        vec4 _405 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_391, 0).xyz, 0);
        ivec3 _407 = _391 + ivec3(1, 0, 0);
        float _1604;
        do
        {
            bool _1596;
            do
            {
                if ((((((_391.x < 0) || (_391.y < 0)) || (_391.z < 0)) || (_391.x >= int(_52.Width))) || (_391.y >= int(_52.Height))) || (_391.z >= int(_52.Depth)))
                {
                    _1596 = true;
                    break;
                }
                _1596 = WallMask_1._data[uint(_391.x) + (_52.Width * (uint(_391.y) + (_52.Height * uint(_391.z))))] != 0u;
                break;
            } while(false);
            if (_1596)
            {
                float _1603;
                do
                {
                    bool _1601;
                    uint _1602;
                    do
                    {
                        if ((((((_391.x < 0) || (_391.y < 0)) || (_391.z < 0)) || (_391.x >= int(_52.Width))) || (_391.y >= int(_52.Height))) || (_391.z >= int(_52.Depth)))
                        {
                            _1602 = 0u;
                            _1601 = false;
                            break;
                        }
                        uint _620 = uint(_391.x) + (_52.Width * (uint(_391.y) + (_52.Height * uint(_391.z))));
                        if (WallMask_1._data[_620] < 2u)
                        {
                            _1602 = 0u;
                            _1601 = false;
                            break;
                        }
                        _1602 = min((WallMask_1._data[_620] - 2u), (_174.BodyCount - 1u));
                        _1601 = true;
                        break;
                    } while(false);
                    if (_1601)
                    {
                        _1603 = BodyVelocity_1._data[_1602].x;
                        break;
                    }
                    _1603 = 0.0;
                    break;
                } while(false);
                _1604 = _1603;
                break;
            }
            bool _1597;
            do
            {
                if ((((((_407.x < 0) || (_407.y < 0)) || (_407.z < 0)) || (_407.x >= int(_52.Width))) || (_407.y >= int(_52.Height))) || (_407.z >= int(_52.Depth)))
                {
                    _1597 = true;
                    break;
                }
                _1597 = WallMask_1._data[uint(_407.x) + (_52.Width * (uint(_407.y) + (_52.Height * uint(_407.z))))] != 0u;
                break;
            } while(false);
            if (_1597)
            {
                float _1600;
                do
                {
                    bool _1598;
                    uint _1599;
                    do
                    {
                        if ((((((_407.x < 0) || (_407.y < 0)) || (_407.z < 0)) || (_407.x >= int(_52.Width))) || (_407.y >= int(_52.Height))) || (_407.z >= int(_52.Depth)))
                        {
                            _1599 = 0u;
                            _1598 = false;
                            break;
                        }
                        uint _781 = uint(_407.x) + (_52.Width * (uint(_407.y) + (_52.Height * uint(_407.z))));
                        if (WallMask_1._data[_781] < 2u)
                        {
                            _1599 = 0u;
                            _1598 = false;
                            break;
                        }
                        _1599 = min((WallMask_1._data[_781] - 2u), (_174.BodyCount - 1u));
                        _1598 = true;
                        break;
                    } while(false);
                    if (_1598)
                    {
                        _1600 = BodyVelocity_1._data[_1599].x;
                        break;
                    }
                    _1600 = 0.0;
                    break;
                } while(false);
                _1604 = _1600;
                break;
            }
            _1604 = _405.x;
            break;
        } while(false);
        ivec3 _413 = _391 + ivec3(0, 1, 0);
        float _1613;
        do
        {
            bool _1605;
            do
            {
                if ((((((_391.x < 0) || (_391.y < 0)) || (_391.z < 0)) || (_391.x >= int(_52.Width))) || (_391.y >= int(_52.Height))) || (_391.z >= int(_52.Depth)))
                {
                    _1605 = true;
                    break;
                }
                _1605 = WallMask_1._data[uint(_391.x) + (_52.Width * (uint(_391.y) + (_52.Height * uint(_391.z))))] != 0u;
                break;
            } while(false);
            if (_1605)
            {
                float _1612;
                do
                {
                    bool _1610;
                    uint _1611;
                    do
                    {
                        if ((((((_391.x < 0) || (_391.y < 0)) || (_391.z < 0)) || (_391.x >= int(_52.Width))) || (_391.y >= int(_52.Height))) || (_391.z >= int(_52.Depth)))
                        {
                            _1611 = 0u;
                            _1610 = false;
                            break;
                        }
                        uint _970 = uint(_391.x) + (_52.Width * (uint(_391.y) + (_52.Height * uint(_391.z))));
                        if (WallMask_1._data[_970] < 2u)
                        {
                            _1611 = 0u;
                            _1610 = false;
                            break;
                        }
                        _1611 = min((WallMask_1._data[_970] - 2u), (_174.BodyCount - 1u));
                        _1610 = true;
                        break;
                    } while(false);
                    if (_1610)
                    {
                        _1612 = BodyVelocity_1._data[_1611].y;
                        break;
                    }
                    _1612 = 0.0;
                    break;
                } while(false);
                _1613 = _1612;
                break;
            }
            bool _1606;
            do
            {
                if ((((((_413.x < 0) || (_413.y < 0)) || (_413.z < 0)) || (_413.x >= int(_52.Width))) || (_413.y >= int(_52.Height))) || (_413.z >= int(_52.Depth)))
                {
                    _1606 = true;
                    break;
                }
                _1606 = WallMask_1._data[uint(_413.x) + (_52.Width * (uint(_413.y) + (_52.Height * uint(_413.z))))] != 0u;
                break;
            } while(false);
            if (_1606)
            {
                float _1609;
                do
                {
                    bool _1607;
                    uint _1608;
                    do
                    {
                        if ((((((_413.x < 0) || (_413.y < 0)) || (_413.z < 0)) || (_413.x >= int(_52.Width))) || (_413.y >= int(_52.Height))) || (_413.z >= int(_52.Depth)))
                        {
                            _1608 = 0u;
                            _1607 = false;
                            break;
                        }
                        uint _1131 = uint(_413.x) + (_52.Width * (uint(_413.y) + (_52.Height * uint(_413.z))));
                        if (WallMask_1._data[_1131] < 2u)
                        {
                            _1608 = 0u;
                            _1607 = false;
                            break;
                        }
                        _1608 = min((WallMask_1._data[_1131] - 2u), (_174.BodyCount - 1u));
                        _1607 = true;
                        break;
                    } while(false);
                    if (_1607)
                    {
                        _1609 = BodyVelocity_1._data[_1608].y;
                        break;
                    }
                    _1609 = 0.0;
                    break;
                } while(false);
                _1613 = _1609;
                break;
            }
            _1613 = _405.y;
            break;
        } while(false);
        ivec3 _419 = _391 + ivec3(0, 0, 1);
        float _1622;
        do
        {
            bool _1614;
            do
            {
                if ((((((_391.x < 0) || (_391.y < 0)) || (_391.z < 0)) || (_391.x >= int(_52.Width))) || (_391.y >= int(_52.Height))) || (_391.z >= int(_52.Depth)))
                {
                    _1614 = true;
                    break;
                }
                _1614 = WallMask_1._data[uint(_391.x) + (_52.Width * (uint(_391.y) + (_52.Height * uint(_391.z))))] != 0u;
                break;
            } while(false);
            if (_1614)
            {
                float _1621;
                do
                {
                    bool _1619;
                    uint _1620;
                    do
                    {
                        if ((((((_391.x < 0) || (_391.y < 0)) || (_391.z < 0)) || (_391.x >= int(_52.Width))) || (_391.y >= int(_52.Height))) || (_391.z >= int(_52.Depth)))
                        {
                            _1620 = 0u;
                            _1619 = false;
                            break;
                        }
                        uint _1320 = uint(_391.x) + (_52.Width * (uint(_391.y) + (_52.Height * uint(_391.z))));
                        if (WallMask_1._data[_1320] < 2u)
                        {
                            _1620 = 0u;
                            _1619 = false;
                            break;
                        }
                        _1620 = min((WallMask_1._data[_1320] - 2u), (_174.BodyCount - 1u));
                        _1619 = true;
                        break;
                    } while(false);
                    if (_1619)
                    {
                        _1621 = BodyVelocity_1._data[_1620].z;
                        break;
                    }
                    _1621 = 0.0;
                    break;
                } while(false);
                _1622 = _1621;
                break;
            }
            bool _1615;
            do
            {
                if ((((((_419.x < 0) || (_419.y < 0)) || (_419.z < 0)) || (_419.x >= int(_52.Width))) || (_419.y >= int(_52.Height))) || (_419.z >= int(_52.Depth)))
                {
                    _1615 = true;
                    break;
                }
                _1615 = WallMask_1._data[uint(_419.x) + (_52.Width * (uint(_419.y) + (_52.Height * uint(_419.z))))] != 0u;
                break;
            } while(false);
            if (_1615)
            {
                float _1618;
                do
                {
                    bool _1616;
                    uint _1617;
                    do
                    {
                        if ((((((_419.x < 0) || (_419.y < 0)) || (_419.z < 0)) || (_419.x >= int(_52.Width))) || (_419.y >= int(_52.Height))) || (_419.z >= int(_52.Depth)))
                        {
                            _1617 = 0u;
                            _1616 = false;
                            break;
                        }
                        uint _1481 = uint(_419.x) + (_52.Width * (uint(_419.y) + (_52.Height * uint(_419.z))));
                        if (WallMask_1._data[_1481] < 2u)
                        {
                            _1617 = 0u;
                            _1616 = false;
                            break;
                        }
                        _1617 = min((WallMask_1._data[_1481] - 2u), (_174.BodyCount - 1u));
                        _1616 = true;
                        break;
                    } while(false);
                    if (_1616)
                    {
                        _1618 = BodyVelocity_1._data[_1617].z;
                        break;
                    }
                    _1618 = 0.0;
                    break;
                } while(false);
                _1622 = _1618;
                break;
            }
            _1622 = _405.z;
            break;
        } while(false);
        imageStore(VelocityOut, ivec3(gl_GlobalInvocationID), vec4(_1604, _1613, _1622, 0.0));
        break;
    } while(false);
}

