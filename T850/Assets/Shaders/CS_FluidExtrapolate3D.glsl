#version 430
// GENERATED from CS_FluidExtrapolate3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=e37bc9b3547c19e18fa890a548dd38c15bafab605cbedebecb6a3d9275c804ac; defines=T850_SPIRV=1,T850_VULKAN=1
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
} _66;

layout(binding = 4, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 3, rgba16f) uniform writeonly image3D VelocityOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _66.Width) || (gl_GlobalInvocationID.y >= _66.Height)) || (gl_GlobalInvocationID.z >= _66.Depth))
        {
            break;
        }
        ivec3 _730 = ivec3(gl_GlobalInvocationID);
        vec4 _744 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_730, 0).xyz, 0);
        bool _15944;
        do
        {
            bool _15942;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _15942 = true;
                    break;
                }
                _15942 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                break;
            } while(false);
            if (_15942)
            {
                _15944 = true;
                break;
            }
            int _810 = _730.x + 1;
            bool _15943;
            do
            {
                if (!((((((_810 >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_810 < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _15943 = true;
                    break;
                }
                _15943 = WallMask_1._data[uint(_810) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                break;
            } while(false);
            _15944 = _15943;
            break;
        } while(false);
        bool _15950;
        do
        {
            bool _15947;
            do
            {
                bool _15945;
                do
                {
                    if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                    {
                        _15945 = true;
                        break;
                    }
                    _15945 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                    break;
                } while(false);
                if (_15945)
                {
                    _15947 = true;
                    break;
                }
                int _1039 = _730.x + 1;
                bool _15946;
                do
                {
                    if (!((((((_1039 >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_1039 < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                    {
                        _15946 = true;
                        break;
                    }
                    _15946 = WallMask_1._data[uint(_1039) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                    break;
                } while(false);
                _15947 = _15946;
                break;
            } while(false);
            if (_15947)
            {
                _15950 = false;
                break;
            }
            bool _15948;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _15948 = false;
                    break;
                }
                if (WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u)
                {
                    _15948 = false;
                    break;
                }
                _15948 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_730, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_15948)
            {
                _15950 = true;
                break;
            }
            int _1000 = _730.x + 1;
            bool _15949;
            do
            {
                if (!((((((_1000 >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_1000 < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _15949 = false;
                    break;
                }
                if (WallMask_1._data[uint(_1000) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u)
                {
                    _15949 = false;
                    break;
                }
                _15949 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1000, _730.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            _15950 = _15949;
            break;
        } while(false);
        float _16398;
        if ((!_15944) && (!_15950))
        {
            float _16089;
            do
            {
                ivec3 _1418 = _730 + ivec3(1, 0, 0);
                float _15972;
                uint _15973;
                do
                {
                    if (!((((((_1418.x >= 0) && (_1418.y >= 0)) && (_1418.z >= 0)) && (_1418.x < int(_66.Width))) && (_1418.y < int(_66.Height))) && (_1418.z < int(_66.Depth))))
                    {
                        _15973 = 0u;
                        _15972 = 0.0;
                        break;
                    }
                    bool _15956;
                    do
                    {
                        bool _15953;
                        do
                        {
                            bool _15951;
                            do
                            {
                                if (!((((((_1418.x >= 0) && (_1418.y >= 0)) && (_1418.z >= 0)) && (_1418.x < int(_66.Width))) && (_1418.y < int(_66.Height))) && (_1418.z < int(_66.Depth))))
                                {
                                    _15951 = true;
                                    break;
                                }
                                _15951 = WallMask_1._data[uint(_1418.x) + (_66.Width * (uint(_1418.y) + (_66.Height * uint(_1418.z))))] != 0u;
                                break;
                            } while(false);
                            if (_15951)
                            {
                                _15953 = true;
                                break;
                            }
                            int _1600 = _1418.x + 1;
                            bool _15952;
                            do
                            {
                                if (!((((((_1600 >= 0) && (_1418.y >= 0)) && (_1418.z >= 0)) && (_1600 < int(_66.Width))) && (_1418.y < int(_66.Height))) && (_1418.z < int(_66.Depth))))
                                {
                                    _15952 = true;
                                    break;
                                }
                                _15952 = WallMask_1._data[uint(_1600) + (_66.Width * (uint(_1418.y) + (_66.Height * uint(_1418.z))))] != 0u;
                                break;
                            } while(false);
                            _15953 = _15952;
                            break;
                        } while(false);
                        if (_15953)
                        {
                            _15956 = false;
                            break;
                        }
                        bool _15954;
                        do
                        {
                            if (!((((((_1418.x >= 0) && (_1418.y >= 0)) && (_1418.z >= 0)) && (_1418.x < int(_66.Width))) && (_1418.y < int(_66.Height))) && (_1418.z < int(_66.Depth))))
                            {
                                _15954 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1418.x) + (_66.Width * (uint(_1418.y) + (_66.Height * uint(_1418.z))))] != 0u)
                            {
                                _15954 = false;
                                break;
                            }
                            _15954 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1418, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_15954)
                        {
                            _15956 = true;
                            break;
                        }
                        int _1561 = _1418.x + 1;
                        bool _15955;
                        do
                        {
                            if (!((((((_1561 >= 0) && (_1418.y >= 0)) && (_1418.z >= 0)) && (_1561 < int(_66.Width))) && (_1418.y < int(_66.Height))) && (_1418.z < int(_66.Depth))))
                            {
                                _15955 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1561) + (_66.Width * (uint(_1418.y) + (_66.Height * uint(_1418.z))))] != 0u)
                            {
                                _15955 = false;
                                break;
                            }
                            _15955 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1561, _1418.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _15956 = _15955;
                        break;
                    } while(false);
                    if (!_15956)
                    {
                        _15973 = 0u;
                        _15972 = 0.0;
                        break;
                    }
                    float _15957;
                    do
                    {
                        if ((_1418.x < 0) || (_1418.x > (int(_66.Width) - 1)))
                        {
                            _15957 = 0.0;
                            break;
                        }
                        _15957 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1418.x, clamp(_1418.y, 0, int(_66.Height) - 1), clamp(_1418.z, 0, int(_66.Depth) - 1), 0).xyz, 0).x;
                        break;
                    } while(false);
                    _15973 = 1u;
                    _15972 = _15957;
                    break;
                } while(false);
                ivec3 _1426 = _730 - ivec3(1, 0, 0);
                float _15995;
                uint _15996;
                do
                {
                    if (!((((((_1426.x >= 0) && (_1426.y >= 0)) && (_1426.z >= 0)) && (_1426.x < int(_66.Width))) && (_1426.y < int(_66.Height))) && (_1426.z < int(_66.Depth))))
                    {
                        _15996 = _15973;
                        _15995 = _15972;
                        break;
                    }
                    bool _15979;
                    do
                    {
                        bool _15976;
                        do
                        {
                            bool _15974;
                            do
                            {
                                if (!((((((_1426.x >= 0) && (_1426.y >= 0)) && (_1426.z >= 0)) && (_1426.x < int(_66.Width))) && (_1426.y < int(_66.Height))) && (_1426.z < int(_66.Depth))))
                                {
                                    _15974 = true;
                                    break;
                                }
                                _15974 = WallMask_1._data[uint(_1426.x) + (_66.Width * (uint(_1426.y) + (_66.Height * uint(_1426.z))))] != 0u;
                                break;
                            } while(false);
                            if (_15974)
                            {
                                _15976 = true;
                                break;
                            }
                            int _2221 = _1426.x + 1;
                            bool _15975;
                            do
                            {
                                if (!((((((_2221 >= 0) && (_1426.y >= 0)) && (_1426.z >= 0)) && (_2221 < int(_66.Width))) && (_1426.y < int(_66.Height))) && (_1426.z < int(_66.Depth))))
                                {
                                    _15975 = true;
                                    break;
                                }
                                _15975 = WallMask_1._data[uint(_2221) + (_66.Width * (uint(_1426.y) + (_66.Height * uint(_1426.z))))] != 0u;
                                break;
                            } while(false);
                            _15976 = _15975;
                            break;
                        } while(false);
                        if (_15976)
                        {
                            _15979 = false;
                            break;
                        }
                        bool _15977;
                        do
                        {
                            if (!((((((_1426.x >= 0) && (_1426.y >= 0)) && (_1426.z >= 0)) && (_1426.x < int(_66.Width))) && (_1426.y < int(_66.Height))) && (_1426.z < int(_66.Depth))))
                            {
                                _15977 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1426.x) + (_66.Width * (uint(_1426.y) + (_66.Height * uint(_1426.z))))] != 0u)
                            {
                                _15977 = false;
                                break;
                            }
                            _15977 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1426, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_15977)
                        {
                            _15979 = true;
                            break;
                        }
                        int _2182 = _1426.x + 1;
                        bool _15978;
                        do
                        {
                            if (!((((((_2182 >= 0) && (_1426.y >= 0)) && (_1426.z >= 0)) && (_2182 < int(_66.Width))) && (_1426.y < int(_66.Height))) && (_1426.z < int(_66.Depth))))
                            {
                                _15978 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_2182) + (_66.Width * (uint(_1426.y) + (_66.Height * uint(_1426.z))))] != 0u)
                            {
                                _15978 = false;
                                break;
                            }
                            _15978 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_2182, _1426.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _15979 = _15978;
                        break;
                    } while(false);
                    if (!_15979)
                    {
                        _15996 = _15973;
                        _15995 = _15972;
                        break;
                    }
                    float _15980;
                    do
                    {
                        if ((_1426.x < 0) || (_1426.x > (int(_66.Width) - 1)))
                        {
                            _15980 = 0.0;
                            break;
                        }
                        _15980 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1426.x, clamp(_1426.y, 0, int(_66.Height) - 1), clamp(_1426.z, 0, int(_66.Depth) - 1), 0).xyz, 0).x;
                        break;
                    } while(false);
                    _15996 = _15973 + uint(1);
                    _15995 = _15972 + _15980;
                    break;
                } while(false);
                ivec3 _1434 = _730 + ivec3(0, 1, 0);
                float _16018;
                uint _16019;
                do
                {
                    if (!((((((_1434.x >= 0) && (_1434.y >= 0)) && (_1434.z >= 0)) && (_1434.x < int(_66.Width))) && (_1434.y < int(_66.Height))) && (_1434.z < int(_66.Depth))))
                    {
                        _16019 = _15996;
                        _16018 = _15995;
                        break;
                    }
                    bool _16002;
                    do
                    {
                        bool _15999;
                        do
                        {
                            bool _15997;
                            do
                            {
                                if (!((((((_1434.x >= 0) && (_1434.y >= 0)) && (_1434.z >= 0)) && (_1434.x < int(_66.Width))) && (_1434.y < int(_66.Height))) && (_1434.z < int(_66.Depth))))
                                {
                                    _15997 = true;
                                    break;
                                }
                                _15997 = WallMask_1._data[uint(_1434.x) + (_66.Width * (uint(_1434.y) + (_66.Height * uint(_1434.z))))] != 0u;
                                break;
                            } while(false);
                            if (_15997)
                            {
                                _15999 = true;
                                break;
                            }
                            int _2842 = _1434.x + 1;
                            bool _15998;
                            do
                            {
                                if (!((((((_2842 >= 0) && (_1434.y >= 0)) && (_1434.z >= 0)) && (_2842 < int(_66.Width))) && (_1434.y < int(_66.Height))) && (_1434.z < int(_66.Depth))))
                                {
                                    _15998 = true;
                                    break;
                                }
                                _15998 = WallMask_1._data[uint(_2842) + (_66.Width * (uint(_1434.y) + (_66.Height * uint(_1434.z))))] != 0u;
                                break;
                            } while(false);
                            _15999 = _15998;
                            break;
                        } while(false);
                        if (_15999)
                        {
                            _16002 = false;
                            break;
                        }
                        bool _16000;
                        do
                        {
                            if (!((((((_1434.x >= 0) && (_1434.y >= 0)) && (_1434.z >= 0)) && (_1434.x < int(_66.Width))) && (_1434.y < int(_66.Height))) && (_1434.z < int(_66.Depth))))
                            {
                                _16000 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1434.x) + (_66.Width * (uint(_1434.y) + (_66.Height * uint(_1434.z))))] != 0u)
                            {
                                _16000 = false;
                                break;
                            }
                            _16000 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1434, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16000)
                        {
                            _16002 = true;
                            break;
                        }
                        int _2803 = _1434.x + 1;
                        bool _16001;
                        do
                        {
                            if (!((((((_2803 >= 0) && (_1434.y >= 0)) && (_1434.z >= 0)) && (_2803 < int(_66.Width))) && (_1434.y < int(_66.Height))) && (_1434.z < int(_66.Depth))))
                            {
                                _16001 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_2803) + (_66.Width * (uint(_1434.y) + (_66.Height * uint(_1434.z))))] != 0u)
                            {
                                _16001 = false;
                                break;
                            }
                            _16001 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_2803, _1434.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16002 = _16001;
                        break;
                    } while(false);
                    if (!_16002)
                    {
                        _16019 = _15996;
                        _16018 = _15995;
                        break;
                    }
                    float _16003;
                    do
                    {
                        if ((_1434.x < 0) || (_1434.x > (int(_66.Width) - 1)))
                        {
                            _16003 = 0.0;
                            break;
                        }
                        _16003 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1434.x, clamp(_1434.y, 0, int(_66.Height) - 1), clamp(_1434.z, 0, int(_66.Depth) - 1), 0).xyz, 0).x;
                        break;
                    } while(false);
                    _16019 = _15996 + uint(1);
                    _16018 = _15995 + _16003;
                    break;
                } while(false);
                ivec3 _1442 = _730 - ivec3(0, 1, 0);
                float _16041;
                uint _16042;
                do
                {
                    if (!((((((_1442.x >= 0) && (_1442.y >= 0)) && (_1442.z >= 0)) && (_1442.x < int(_66.Width))) && (_1442.y < int(_66.Height))) && (_1442.z < int(_66.Depth))))
                    {
                        _16042 = _16019;
                        _16041 = _16018;
                        break;
                    }
                    bool _16025;
                    do
                    {
                        bool _16022;
                        do
                        {
                            bool _16020;
                            do
                            {
                                if (!((((((_1442.x >= 0) && (_1442.y >= 0)) && (_1442.z >= 0)) && (_1442.x < int(_66.Width))) && (_1442.y < int(_66.Height))) && (_1442.z < int(_66.Depth))))
                                {
                                    _16020 = true;
                                    break;
                                }
                                _16020 = WallMask_1._data[uint(_1442.x) + (_66.Width * (uint(_1442.y) + (_66.Height * uint(_1442.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16020)
                            {
                                _16022 = true;
                                break;
                            }
                            int _3463 = _1442.x + 1;
                            bool _16021;
                            do
                            {
                                if (!((((((_3463 >= 0) && (_1442.y >= 0)) && (_1442.z >= 0)) && (_3463 < int(_66.Width))) && (_1442.y < int(_66.Height))) && (_1442.z < int(_66.Depth))))
                                {
                                    _16021 = true;
                                    break;
                                }
                                _16021 = WallMask_1._data[uint(_3463) + (_66.Width * (uint(_1442.y) + (_66.Height * uint(_1442.z))))] != 0u;
                                break;
                            } while(false);
                            _16022 = _16021;
                            break;
                        } while(false);
                        if (_16022)
                        {
                            _16025 = false;
                            break;
                        }
                        bool _16023;
                        do
                        {
                            if (!((((((_1442.x >= 0) && (_1442.y >= 0)) && (_1442.z >= 0)) && (_1442.x < int(_66.Width))) && (_1442.y < int(_66.Height))) && (_1442.z < int(_66.Depth))))
                            {
                                _16023 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1442.x) + (_66.Width * (uint(_1442.y) + (_66.Height * uint(_1442.z))))] != 0u)
                            {
                                _16023 = false;
                                break;
                            }
                            _16023 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1442, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16023)
                        {
                            _16025 = true;
                            break;
                        }
                        int _3424 = _1442.x + 1;
                        bool _16024;
                        do
                        {
                            if (!((((((_3424 >= 0) && (_1442.y >= 0)) && (_1442.z >= 0)) && (_3424 < int(_66.Width))) && (_1442.y < int(_66.Height))) && (_1442.z < int(_66.Depth))))
                            {
                                _16024 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_3424) + (_66.Width * (uint(_1442.y) + (_66.Height * uint(_1442.z))))] != 0u)
                            {
                                _16024 = false;
                                break;
                            }
                            _16024 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_3424, _1442.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16025 = _16024;
                        break;
                    } while(false);
                    if (!_16025)
                    {
                        _16042 = _16019;
                        _16041 = _16018;
                        break;
                    }
                    float _16026;
                    do
                    {
                        if ((_1442.x < 0) || (_1442.x > (int(_66.Width) - 1)))
                        {
                            _16026 = 0.0;
                            break;
                        }
                        _16026 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1442.x, clamp(_1442.y, 0, int(_66.Height) - 1), clamp(_1442.z, 0, int(_66.Depth) - 1), 0).xyz, 0).x;
                        break;
                    } while(false);
                    _16042 = _16019 + uint(1);
                    _16041 = _16018 + _16026;
                    break;
                } while(false);
                ivec3 _1450 = _730 + ivec3(0, 0, 1);
                float _16064;
                uint _16065;
                do
                {
                    if (!((((((_1450.x >= 0) && (_1450.y >= 0)) && (_1450.z >= 0)) && (_1450.x < int(_66.Width))) && (_1450.y < int(_66.Height))) && (_1450.z < int(_66.Depth))))
                    {
                        _16065 = _16042;
                        _16064 = _16041;
                        break;
                    }
                    bool _16048;
                    do
                    {
                        bool _16045;
                        do
                        {
                            bool _16043;
                            do
                            {
                                if (!((((((_1450.x >= 0) && (_1450.y >= 0)) && (_1450.z >= 0)) && (_1450.x < int(_66.Width))) && (_1450.y < int(_66.Height))) && (_1450.z < int(_66.Depth))))
                                {
                                    _16043 = true;
                                    break;
                                }
                                _16043 = WallMask_1._data[uint(_1450.x) + (_66.Width * (uint(_1450.y) + (_66.Height * uint(_1450.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16043)
                            {
                                _16045 = true;
                                break;
                            }
                            int _4084 = _1450.x + 1;
                            bool _16044;
                            do
                            {
                                if (!((((((_4084 >= 0) && (_1450.y >= 0)) && (_1450.z >= 0)) && (_4084 < int(_66.Width))) && (_1450.y < int(_66.Height))) && (_1450.z < int(_66.Depth))))
                                {
                                    _16044 = true;
                                    break;
                                }
                                _16044 = WallMask_1._data[uint(_4084) + (_66.Width * (uint(_1450.y) + (_66.Height * uint(_1450.z))))] != 0u;
                                break;
                            } while(false);
                            _16045 = _16044;
                            break;
                        } while(false);
                        if (_16045)
                        {
                            _16048 = false;
                            break;
                        }
                        bool _16046;
                        do
                        {
                            if (!((((((_1450.x >= 0) && (_1450.y >= 0)) && (_1450.z >= 0)) && (_1450.x < int(_66.Width))) && (_1450.y < int(_66.Height))) && (_1450.z < int(_66.Depth))))
                            {
                                _16046 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1450.x) + (_66.Width * (uint(_1450.y) + (_66.Height * uint(_1450.z))))] != 0u)
                            {
                                _16046 = false;
                                break;
                            }
                            _16046 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1450, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16046)
                        {
                            _16048 = true;
                            break;
                        }
                        int _4045 = _1450.x + 1;
                        bool _16047;
                        do
                        {
                            if (!((((((_4045 >= 0) && (_1450.y >= 0)) && (_1450.z >= 0)) && (_4045 < int(_66.Width))) && (_1450.y < int(_66.Height))) && (_1450.z < int(_66.Depth))))
                            {
                                _16047 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_4045) + (_66.Width * (uint(_1450.y) + (_66.Height * uint(_1450.z))))] != 0u)
                            {
                                _16047 = false;
                                break;
                            }
                            _16047 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4045, _1450.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16048 = _16047;
                        break;
                    } while(false);
                    if (!_16048)
                    {
                        _16065 = _16042;
                        _16064 = _16041;
                        break;
                    }
                    float _16049;
                    do
                    {
                        if ((_1450.x < 0) || (_1450.x > (int(_66.Width) - 1)))
                        {
                            _16049 = 0.0;
                            break;
                        }
                        _16049 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1450.x, clamp(_1450.y, 0, int(_66.Height) - 1), clamp(_1450.z, 0, int(_66.Depth) - 1), 0).xyz, 0).x;
                        break;
                    } while(false);
                    _16065 = _16042 + uint(1);
                    _16064 = _16041 + _16049;
                    break;
                } while(false);
                ivec3 _1458 = _730 - ivec3(0, 0, 1);
                float _16087;
                uint _16088;
                do
                {
                    if (!((((((_1458.x >= 0) && (_1458.y >= 0)) && (_1458.z >= 0)) && (_1458.x < int(_66.Width))) && (_1458.y < int(_66.Height))) && (_1458.z < int(_66.Depth))))
                    {
                        _16088 = _16065;
                        _16087 = _16064;
                        break;
                    }
                    bool _16071;
                    do
                    {
                        bool _16068;
                        do
                        {
                            bool _16066;
                            do
                            {
                                if (!((((((_1458.x >= 0) && (_1458.y >= 0)) && (_1458.z >= 0)) && (_1458.x < int(_66.Width))) && (_1458.y < int(_66.Height))) && (_1458.z < int(_66.Depth))))
                                {
                                    _16066 = true;
                                    break;
                                }
                                _16066 = WallMask_1._data[uint(_1458.x) + (_66.Width * (uint(_1458.y) + (_66.Height * uint(_1458.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16066)
                            {
                                _16068 = true;
                                break;
                            }
                            int _4705 = _1458.x + 1;
                            bool _16067;
                            do
                            {
                                if (!((((((_4705 >= 0) && (_1458.y >= 0)) && (_1458.z >= 0)) && (_4705 < int(_66.Width))) && (_1458.y < int(_66.Height))) && (_1458.z < int(_66.Depth))))
                                {
                                    _16067 = true;
                                    break;
                                }
                                _16067 = WallMask_1._data[uint(_4705) + (_66.Width * (uint(_1458.y) + (_66.Height * uint(_1458.z))))] != 0u;
                                break;
                            } while(false);
                            _16068 = _16067;
                            break;
                        } while(false);
                        if (_16068)
                        {
                            _16071 = false;
                            break;
                        }
                        bool _16069;
                        do
                        {
                            if (!((((((_1458.x >= 0) && (_1458.y >= 0)) && (_1458.z >= 0)) && (_1458.x < int(_66.Width))) && (_1458.y < int(_66.Height))) && (_1458.z < int(_66.Depth))))
                            {
                                _16069 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_1458.x) + (_66.Width * (uint(_1458.y) + (_66.Height * uint(_1458.z))))] != 0u)
                            {
                                _16069 = false;
                                break;
                            }
                            _16069 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1458, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16069)
                        {
                            _16071 = true;
                            break;
                        }
                        int _4666 = _1458.x + 1;
                        bool _16070;
                        do
                        {
                            if (!((((((_4666 >= 0) && (_1458.y >= 0)) && (_1458.z >= 0)) && (_4666 < int(_66.Width))) && (_1458.y < int(_66.Height))) && (_1458.z < int(_66.Depth))))
                            {
                                _16070 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_4666) + (_66.Width * (uint(_1458.y) + (_66.Height * uint(_1458.z))))] != 0u)
                            {
                                _16070 = false;
                                break;
                            }
                            _16070 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4666, _1458.yz, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16071 = _16070;
                        break;
                    } while(false);
                    if (!_16071)
                    {
                        _16088 = _16065;
                        _16087 = _16064;
                        break;
                    }
                    float _16072;
                    do
                    {
                        if ((_1458.x < 0) || (_1458.x > (int(_66.Width) - 1)))
                        {
                            _16072 = 0.0;
                            break;
                        }
                        _16072 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1458.x, clamp(_1458.y, 0, int(_66.Height) - 1), clamp(_1458.z, 0, int(_66.Depth) - 1), 0).xyz, 0).x;
                        break;
                    } while(false);
                    _16088 = _16065 + uint(1);
                    _16087 = _16064 + _16072;
                    break;
                } while(false);
                if (_16088 > 0u)
                {
                    _16089 = _16087 / float(_16088);
                    break;
                }
                _16089 = 0.0;
                break;
            } while(false);
            _16398 = _16089;
        }
        else
        {
            _16398 = _744.x;
        }
        bool _16092;
        do
        {
            bool _16090;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _16090 = true;
                    break;
                }
                _16090 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                break;
            } while(false);
            if (_16090)
            {
                _16092 = true;
                break;
            }
            int _5227 = _730.y + 1;
            bool _16091;
            do
            {
                if (!((((((_730.x >= 0) && (_5227 >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_5227 < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _16091 = true;
                    break;
                }
                _16091 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_5227) + (_66.Height * uint(_730.z))))] != 0u;
                break;
            } while(false);
            _16092 = _16091;
            break;
        } while(false);
        bool _16098;
        do
        {
            bool _16095;
            do
            {
                bool _16093;
                do
                {
                    if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                    {
                        _16093 = true;
                        break;
                    }
                    _16093 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                    break;
                } while(false);
                if (_16093)
                {
                    _16095 = true;
                    break;
                }
                int _5456 = _730.y + 1;
                bool _16094;
                do
                {
                    if (!((((((_730.x >= 0) && (_5456 >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_5456 < int(_66.Height))) && (_730.z < int(_66.Depth))))
                    {
                        _16094 = true;
                        break;
                    }
                    _16094 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_5456) + (_66.Height * uint(_730.z))))] != 0u;
                    break;
                } while(false);
                _16095 = _16094;
                break;
            } while(false);
            if (_16095)
            {
                _16098 = false;
                break;
            }
            bool _16096;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _16096 = false;
                    break;
                }
                if (WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u)
                {
                    _16096 = false;
                    break;
                }
                _16096 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_730, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_16096)
            {
                _16098 = true;
                break;
            }
            int _5417 = _730.y + 1;
            bool _16097;
            do
            {
                if (!((((((_730.x >= 0) && (_5417 >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_5417 < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _16097 = false;
                    break;
                }
                if (WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_5417) + (_66.Height * uint(_730.z))))] != 0u)
                {
                    _16097 = false;
                    break;
                }
                _16097 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_730.x, _5417, _730.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            _16098 = _16097;
            break;
        } while(false);
        float _16520;
        if ((!_16092) && (!_16098))
        {
            float _16237;
            do
            {
                ivec3 _5827 = _730 + ivec3(1, 0, 0);
                float _16120;
                uint _16121;
                do
                {
                    if (!((((((_5827.x >= 0) && (_5827.y >= 0)) && (_5827.z >= 0)) && (_5827.x < int(_66.Width))) && (_5827.y < int(_66.Height))) && (_5827.z < int(_66.Depth))))
                    {
                        _16121 = 0u;
                        _16120 = 0.0;
                        break;
                    }
                    bool _16104;
                    do
                    {
                        bool _16101;
                        do
                        {
                            bool _16099;
                            do
                            {
                                if (!((((((_5827.x >= 0) && (_5827.y >= 0)) && (_5827.z >= 0)) && (_5827.x < int(_66.Width))) && (_5827.y < int(_66.Height))) && (_5827.z < int(_66.Depth))))
                                {
                                    _16099 = true;
                                    break;
                                }
                                _16099 = WallMask_1._data[uint(_5827.x) + (_66.Width * (uint(_5827.y) + (_66.Height * uint(_5827.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16099)
                            {
                                _16101 = true;
                                break;
                            }
                            int _6017 = _5827.y + 1;
                            bool _16100;
                            do
                            {
                                if (!((((((_5827.x >= 0) && (_6017 >= 0)) && (_5827.z >= 0)) && (_5827.x < int(_66.Width))) && (_6017 < int(_66.Height))) && (_5827.z < int(_66.Depth))))
                                {
                                    _16100 = true;
                                    break;
                                }
                                _16100 = WallMask_1._data[uint(_5827.x) + (_66.Width * (uint(_6017) + (_66.Height * uint(_5827.z))))] != 0u;
                                break;
                            } while(false);
                            _16101 = _16100;
                            break;
                        } while(false);
                        if (_16101)
                        {
                            _16104 = false;
                            break;
                        }
                        bool _16102;
                        do
                        {
                            if (!((((((_5827.x >= 0) && (_5827.y >= 0)) && (_5827.z >= 0)) && (_5827.x < int(_66.Width))) && (_5827.y < int(_66.Height))) && (_5827.z < int(_66.Depth))))
                            {
                                _16102 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5827.x) + (_66.Width * (uint(_5827.y) + (_66.Height * uint(_5827.z))))] != 0u)
                            {
                                _16102 = false;
                                break;
                            }
                            _16102 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5827, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16102)
                        {
                            _16104 = true;
                            break;
                        }
                        int _5978 = _5827.y + 1;
                        bool _16103;
                        do
                        {
                            if (!((((((_5827.x >= 0) && (_5978 >= 0)) && (_5827.z >= 0)) && (_5827.x < int(_66.Width))) && (_5978 < int(_66.Height))) && (_5827.z < int(_66.Depth))))
                            {
                                _16103 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5827.x) + (_66.Width * (uint(_5978) + (_66.Height * uint(_5827.z))))] != 0u)
                            {
                                _16103 = false;
                                break;
                            }
                            _16103 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5827.x, _5978, _5827.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16104 = _16103;
                        break;
                    } while(false);
                    if (!_16104)
                    {
                        _16121 = 0u;
                        _16120 = 0.0;
                        break;
                    }
                    float _16105;
                    do
                    {
                        if ((_5827.y < 0) || (_5827.y > (int(_66.Height) - 1)))
                        {
                            _16105 = 0.0;
                            break;
                        }
                        _16105 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_5827.x, 0, int(_66.Width) - 1), _5827.y, clamp(_5827.z, 0, int(_66.Depth) - 1), 0).xyz, 0).y;
                        break;
                    } while(false);
                    _16121 = 1u;
                    _16120 = _16105;
                    break;
                } while(false);
                ivec3 _5835 = _730 - ivec3(1, 0, 0);
                float _16143;
                uint _16144;
                do
                {
                    if (!((((((_5835.x >= 0) && (_5835.y >= 0)) && (_5835.z >= 0)) && (_5835.x < int(_66.Width))) && (_5835.y < int(_66.Height))) && (_5835.z < int(_66.Depth))))
                    {
                        _16144 = _16121;
                        _16143 = _16120;
                        break;
                    }
                    bool _16127;
                    do
                    {
                        bool _16124;
                        do
                        {
                            bool _16122;
                            do
                            {
                                if (!((((((_5835.x >= 0) && (_5835.y >= 0)) && (_5835.z >= 0)) && (_5835.x < int(_66.Width))) && (_5835.y < int(_66.Height))) && (_5835.z < int(_66.Depth))))
                                {
                                    _16122 = true;
                                    break;
                                }
                                _16122 = WallMask_1._data[uint(_5835.x) + (_66.Width * (uint(_5835.y) + (_66.Height * uint(_5835.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16122)
                            {
                                _16124 = true;
                                break;
                            }
                            int _6638 = _5835.y + 1;
                            bool _16123;
                            do
                            {
                                if (!((((((_5835.x >= 0) && (_6638 >= 0)) && (_5835.z >= 0)) && (_5835.x < int(_66.Width))) && (_6638 < int(_66.Height))) && (_5835.z < int(_66.Depth))))
                                {
                                    _16123 = true;
                                    break;
                                }
                                _16123 = WallMask_1._data[uint(_5835.x) + (_66.Width * (uint(_6638) + (_66.Height * uint(_5835.z))))] != 0u;
                                break;
                            } while(false);
                            _16124 = _16123;
                            break;
                        } while(false);
                        if (_16124)
                        {
                            _16127 = false;
                            break;
                        }
                        bool _16125;
                        do
                        {
                            if (!((((((_5835.x >= 0) && (_5835.y >= 0)) && (_5835.z >= 0)) && (_5835.x < int(_66.Width))) && (_5835.y < int(_66.Height))) && (_5835.z < int(_66.Depth))))
                            {
                                _16125 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5835.x) + (_66.Width * (uint(_5835.y) + (_66.Height * uint(_5835.z))))] != 0u)
                            {
                                _16125 = false;
                                break;
                            }
                            _16125 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5835, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16125)
                        {
                            _16127 = true;
                            break;
                        }
                        int _6599 = _5835.y + 1;
                        bool _16126;
                        do
                        {
                            if (!((((((_5835.x >= 0) && (_6599 >= 0)) && (_5835.z >= 0)) && (_5835.x < int(_66.Width))) && (_6599 < int(_66.Height))) && (_5835.z < int(_66.Depth))))
                            {
                                _16126 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5835.x) + (_66.Width * (uint(_6599) + (_66.Height * uint(_5835.z))))] != 0u)
                            {
                                _16126 = false;
                                break;
                            }
                            _16126 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5835.x, _6599, _5835.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16127 = _16126;
                        break;
                    } while(false);
                    if (!_16127)
                    {
                        _16144 = _16121;
                        _16143 = _16120;
                        break;
                    }
                    float _16128;
                    do
                    {
                        if ((_5835.y < 0) || (_5835.y > (int(_66.Height) - 1)))
                        {
                            _16128 = 0.0;
                            break;
                        }
                        _16128 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_5835.x, 0, int(_66.Width) - 1), _5835.y, clamp(_5835.z, 0, int(_66.Depth) - 1), 0).xyz, 0).y;
                        break;
                    } while(false);
                    _16144 = _16121 + uint(1);
                    _16143 = _16120 + _16128;
                    break;
                } while(false);
                ivec3 _5843 = _730 + ivec3(0, 1, 0);
                float _16166;
                uint _16167;
                do
                {
                    if (!((((((_5843.x >= 0) && (_5843.y >= 0)) && (_5843.z >= 0)) && (_5843.x < int(_66.Width))) && (_5843.y < int(_66.Height))) && (_5843.z < int(_66.Depth))))
                    {
                        _16167 = _16144;
                        _16166 = _16143;
                        break;
                    }
                    bool _16150;
                    do
                    {
                        bool _16147;
                        do
                        {
                            bool _16145;
                            do
                            {
                                if (!((((((_5843.x >= 0) && (_5843.y >= 0)) && (_5843.z >= 0)) && (_5843.x < int(_66.Width))) && (_5843.y < int(_66.Height))) && (_5843.z < int(_66.Depth))))
                                {
                                    _16145 = true;
                                    break;
                                }
                                _16145 = WallMask_1._data[uint(_5843.x) + (_66.Width * (uint(_5843.y) + (_66.Height * uint(_5843.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16145)
                            {
                                _16147 = true;
                                break;
                            }
                            int _7259 = _5843.y + 1;
                            bool _16146;
                            do
                            {
                                if (!((((((_5843.x >= 0) && (_7259 >= 0)) && (_5843.z >= 0)) && (_5843.x < int(_66.Width))) && (_7259 < int(_66.Height))) && (_5843.z < int(_66.Depth))))
                                {
                                    _16146 = true;
                                    break;
                                }
                                _16146 = WallMask_1._data[uint(_5843.x) + (_66.Width * (uint(_7259) + (_66.Height * uint(_5843.z))))] != 0u;
                                break;
                            } while(false);
                            _16147 = _16146;
                            break;
                        } while(false);
                        if (_16147)
                        {
                            _16150 = false;
                            break;
                        }
                        bool _16148;
                        do
                        {
                            if (!((((((_5843.x >= 0) && (_5843.y >= 0)) && (_5843.z >= 0)) && (_5843.x < int(_66.Width))) && (_5843.y < int(_66.Height))) && (_5843.z < int(_66.Depth))))
                            {
                                _16148 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5843.x) + (_66.Width * (uint(_5843.y) + (_66.Height * uint(_5843.z))))] != 0u)
                            {
                                _16148 = false;
                                break;
                            }
                            _16148 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5843, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16148)
                        {
                            _16150 = true;
                            break;
                        }
                        int _7220 = _5843.y + 1;
                        bool _16149;
                        do
                        {
                            if (!((((((_5843.x >= 0) && (_7220 >= 0)) && (_5843.z >= 0)) && (_5843.x < int(_66.Width))) && (_7220 < int(_66.Height))) && (_5843.z < int(_66.Depth))))
                            {
                                _16149 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5843.x) + (_66.Width * (uint(_7220) + (_66.Height * uint(_5843.z))))] != 0u)
                            {
                                _16149 = false;
                                break;
                            }
                            _16149 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5843.x, _7220, _5843.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16150 = _16149;
                        break;
                    } while(false);
                    if (!_16150)
                    {
                        _16167 = _16144;
                        _16166 = _16143;
                        break;
                    }
                    float _16151;
                    do
                    {
                        if ((_5843.y < 0) || (_5843.y > (int(_66.Height) - 1)))
                        {
                            _16151 = 0.0;
                            break;
                        }
                        _16151 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_5843.x, 0, int(_66.Width) - 1), _5843.y, clamp(_5843.z, 0, int(_66.Depth) - 1), 0).xyz, 0).y;
                        break;
                    } while(false);
                    _16167 = _16144 + uint(1);
                    _16166 = _16143 + _16151;
                    break;
                } while(false);
                ivec3 _5851 = _730 - ivec3(0, 1, 0);
                float _16189;
                uint _16190;
                do
                {
                    if (!((((((_5851.x >= 0) && (_5851.y >= 0)) && (_5851.z >= 0)) && (_5851.x < int(_66.Width))) && (_5851.y < int(_66.Height))) && (_5851.z < int(_66.Depth))))
                    {
                        _16190 = _16167;
                        _16189 = _16166;
                        break;
                    }
                    bool _16173;
                    do
                    {
                        bool _16170;
                        do
                        {
                            bool _16168;
                            do
                            {
                                if (!((((((_5851.x >= 0) && (_5851.y >= 0)) && (_5851.z >= 0)) && (_5851.x < int(_66.Width))) && (_5851.y < int(_66.Height))) && (_5851.z < int(_66.Depth))))
                                {
                                    _16168 = true;
                                    break;
                                }
                                _16168 = WallMask_1._data[uint(_5851.x) + (_66.Width * (uint(_5851.y) + (_66.Height * uint(_5851.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16168)
                            {
                                _16170 = true;
                                break;
                            }
                            int _7880 = _5851.y + 1;
                            bool _16169;
                            do
                            {
                                if (!((((((_5851.x >= 0) && (_7880 >= 0)) && (_5851.z >= 0)) && (_5851.x < int(_66.Width))) && (_7880 < int(_66.Height))) && (_5851.z < int(_66.Depth))))
                                {
                                    _16169 = true;
                                    break;
                                }
                                _16169 = WallMask_1._data[uint(_5851.x) + (_66.Width * (uint(_7880) + (_66.Height * uint(_5851.z))))] != 0u;
                                break;
                            } while(false);
                            _16170 = _16169;
                            break;
                        } while(false);
                        if (_16170)
                        {
                            _16173 = false;
                            break;
                        }
                        bool _16171;
                        do
                        {
                            if (!((((((_5851.x >= 0) && (_5851.y >= 0)) && (_5851.z >= 0)) && (_5851.x < int(_66.Width))) && (_5851.y < int(_66.Height))) && (_5851.z < int(_66.Depth))))
                            {
                                _16171 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5851.x) + (_66.Width * (uint(_5851.y) + (_66.Height * uint(_5851.z))))] != 0u)
                            {
                                _16171 = false;
                                break;
                            }
                            _16171 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5851, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16171)
                        {
                            _16173 = true;
                            break;
                        }
                        int _7841 = _5851.y + 1;
                        bool _16172;
                        do
                        {
                            if (!((((((_5851.x >= 0) && (_7841 >= 0)) && (_5851.z >= 0)) && (_5851.x < int(_66.Width))) && (_7841 < int(_66.Height))) && (_5851.z < int(_66.Depth))))
                            {
                                _16172 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5851.x) + (_66.Width * (uint(_7841) + (_66.Height * uint(_5851.z))))] != 0u)
                            {
                                _16172 = false;
                                break;
                            }
                            _16172 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5851.x, _7841, _5851.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16173 = _16172;
                        break;
                    } while(false);
                    if (!_16173)
                    {
                        _16190 = _16167;
                        _16189 = _16166;
                        break;
                    }
                    float _16174;
                    do
                    {
                        if ((_5851.y < 0) || (_5851.y > (int(_66.Height) - 1)))
                        {
                            _16174 = 0.0;
                            break;
                        }
                        _16174 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_5851.x, 0, int(_66.Width) - 1), _5851.y, clamp(_5851.z, 0, int(_66.Depth) - 1), 0).xyz, 0).y;
                        break;
                    } while(false);
                    _16190 = _16167 + uint(1);
                    _16189 = _16166 + _16174;
                    break;
                } while(false);
                ivec3 _5859 = _730 + ivec3(0, 0, 1);
                float _16212;
                uint _16213;
                do
                {
                    if (!((((((_5859.x >= 0) && (_5859.y >= 0)) && (_5859.z >= 0)) && (_5859.x < int(_66.Width))) && (_5859.y < int(_66.Height))) && (_5859.z < int(_66.Depth))))
                    {
                        _16213 = _16190;
                        _16212 = _16189;
                        break;
                    }
                    bool _16196;
                    do
                    {
                        bool _16193;
                        do
                        {
                            bool _16191;
                            do
                            {
                                if (!((((((_5859.x >= 0) && (_5859.y >= 0)) && (_5859.z >= 0)) && (_5859.x < int(_66.Width))) && (_5859.y < int(_66.Height))) && (_5859.z < int(_66.Depth))))
                                {
                                    _16191 = true;
                                    break;
                                }
                                _16191 = WallMask_1._data[uint(_5859.x) + (_66.Width * (uint(_5859.y) + (_66.Height * uint(_5859.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16191)
                            {
                                _16193 = true;
                                break;
                            }
                            int _8501 = _5859.y + 1;
                            bool _16192;
                            do
                            {
                                if (!((((((_5859.x >= 0) && (_8501 >= 0)) && (_5859.z >= 0)) && (_5859.x < int(_66.Width))) && (_8501 < int(_66.Height))) && (_5859.z < int(_66.Depth))))
                                {
                                    _16192 = true;
                                    break;
                                }
                                _16192 = WallMask_1._data[uint(_5859.x) + (_66.Width * (uint(_8501) + (_66.Height * uint(_5859.z))))] != 0u;
                                break;
                            } while(false);
                            _16193 = _16192;
                            break;
                        } while(false);
                        if (_16193)
                        {
                            _16196 = false;
                            break;
                        }
                        bool _16194;
                        do
                        {
                            if (!((((((_5859.x >= 0) && (_5859.y >= 0)) && (_5859.z >= 0)) && (_5859.x < int(_66.Width))) && (_5859.y < int(_66.Height))) && (_5859.z < int(_66.Depth))))
                            {
                                _16194 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5859.x) + (_66.Width * (uint(_5859.y) + (_66.Height * uint(_5859.z))))] != 0u)
                            {
                                _16194 = false;
                                break;
                            }
                            _16194 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5859, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16194)
                        {
                            _16196 = true;
                            break;
                        }
                        int _8462 = _5859.y + 1;
                        bool _16195;
                        do
                        {
                            if (!((((((_5859.x >= 0) && (_8462 >= 0)) && (_5859.z >= 0)) && (_5859.x < int(_66.Width))) && (_8462 < int(_66.Height))) && (_5859.z < int(_66.Depth))))
                            {
                                _16195 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5859.x) + (_66.Width * (uint(_8462) + (_66.Height * uint(_5859.z))))] != 0u)
                            {
                                _16195 = false;
                                break;
                            }
                            _16195 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5859.x, _8462, _5859.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16196 = _16195;
                        break;
                    } while(false);
                    if (!_16196)
                    {
                        _16213 = _16190;
                        _16212 = _16189;
                        break;
                    }
                    float _16197;
                    do
                    {
                        if ((_5859.y < 0) || (_5859.y > (int(_66.Height) - 1)))
                        {
                            _16197 = 0.0;
                            break;
                        }
                        _16197 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_5859.x, 0, int(_66.Width) - 1), _5859.y, clamp(_5859.z, 0, int(_66.Depth) - 1), 0).xyz, 0).y;
                        break;
                    } while(false);
                    _16213 = _16190 + uint(1);
                    _16212 = _16189 + _16197;
                    break;
                } while(false);
                ivec3 _5867 = _730 - ivec3(0, 0, 1);
                float _16235;
                uint _16236;
                do
                {
                    if (!((((((_5867.x >= 0) && (_5867.y >= 0)) && (_5867.z >= 0)) && (_5867.x < int(_66.Width))) && (_5867.y < int(_66.Height))) && (_5867.z < int(_66.Depth))))
                    {
                        _16236 = _16213;
                        _16235 = _16212;
                        break;
                    }
                    bool _16219;
                    do
                    {
                        bool _16216;
                        do
                        {
                            bool _16214;
                            do
                            {
                                if (!((((((_5867.x >= 0) && (_5867.y >= 0)) && (_5867.z >= 0)) && (_5867.x < int(_66.Width))) && (_5867.y < int(_66.Height))) && (_5867.z < int(_66.Depth))))
                                {
                                    _16214 = true;
                                    break;
                                }
                                _16214 = WallMask_1._data[uint(_5867.x) + (_66.Width * (uint(_5867.y) + (_66.Height * uint(_5867.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16214)
                            {
                                _16216 = true;
                                break;
                            }
                            int _9122 = _5867.y + 1;
                            bool _16215;
                            do
                            {
                                if (!((((((_5867.x >= 0) && (_9122 >= 0)) && (_5867.z >= 0)) && (_5867.x < int(_66.Width))) && (_9122 < int(_66.Height))) && (_5867.z < int(_66.Depth))))
                                {
                                    _16215 = true;
                                    break;
                                }
                                _16215 = WallMask_1._data[uint(_5867.x) + (_66.Width * (uint(_9122) + (_66.Height * uint(_5867.z))))] != 0u;
                                break;
                            } while(false);
                            _16216 = _16215;
                            break;
                        } while(false);
                        if (_16216)
                        {
                            _16219 = false;
                            break;
                        }
                        bool _16217;
                        do
                        {
                            if (!((((((_5867.x >= 0) && (_5867.y >= 0)) && (_5867.z >= 0)) && (_5867.x < int(_66.Width))) && (_5867.y < int(_66.Height))) && (_5867.z < int(_66.Depth))))
                            {
                                _16217 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5867.x) + (_66.Width * (uint(_5867.y) + (_66.Height * uint(_5867.z))))] != 0u)
                            {
                                _16217 = false;
                                break;
                            }
                            _16217 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5867, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16217)
                        {
                            _16219 = true;
                            break;
                        }
                        int _9083 = _5867.y + 1;
                        bool _16218;
                        do
                        {
                            if (!((((((_5867.x >= 0) && (_9083 >= 0)) && (_5867.z >= 0)) && (_5867.x < int(_66.Width))) && (_9083 < int(_66.Height))) && (_5867.z < int(_66.Depth))))
                            {
                                _16218 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_5867.x) + (_66.Width * (uint(_9083) + (_66.Height * uint(_5867.z))))] != 0u)
                            {
                                _16218 = false;
                                break;
                            }
                            _16218 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5867.x, _9083, _5867.z, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16219 = _16218;
                        break;
                    } while(false);
                    if (!_16219)
                    {
                        _16236 = _16213;
                        _16235 = _16212;
                        break;
                    }
                    float _16220;
                    do
                    {
                        if ((_5867.y < 0) || (_5867.y > (int(_66.Height) - 1)))
                        {
                            _16220 = 0.0;
                            break;
                        }
                        _16220 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_5867.x, 0, int(_66.Width) - 1), _5867.y, clamp(_5867.z, 0, int(_66.Depth) - 1), 0).xyz, 0).y;
                        break;
                    } while(false);
                    _16236 = _16213 + uint(1);
                    _16235 = _16212 + _16220;
                    break;
                } while(false);
                if (_16236 > 0u)
                {
                    _16237 = _16235 / float(_16236);
                    break;
                }
                _16237 = 0.0;
                break;
            } while(false);
            _16520 = _16237;
        }
        else
        {
            _16520 = _744.y;
        }
        bool _16240;
        do
        {
            bool _16238;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _16238 = true;
                    break;
                }
                _16238 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                break;
            } while(false);
            if (_16238)
            {
                _16240 = true;
                break;
            }
            int _9641 = _730.z + 1;
            bool _16239;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_9641 >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_9641 < int(_66.Depth))))
                {
                    _16239 = true;
                    break;
                }
                _16239 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_9641))))] != 0u;
                break;
            } while(false);
            _16240 = _16239;
            break;
        } while(false);
        bool _16246;
        do
        {
            bool _16243;
            do
            {
                bool _16241;
                do
                {
                    if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                    {
                        _16241 = true;
                        break;
                    }
                    _16241 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u;
                    break;
                } while(false);
                if (_16241)
                {
                    _16243 = true;
                    break;
                }
                int _9870 = _730.z + 1;
                bool _16242;
                do
                {
                    if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_9870 >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_9870 < int(_66.Depth))))
                    {
                        _16242 = true;
                        break;
                    }
                    _16242 = WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_9870))))] != 0u;
                    break;
                } while(false);
                _16243 = _16242;
                break;
            } while(false);
            if (_16243)
            {
                _16246 = false;
                break;
            }
            bool _16244;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_730.z >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_730.z < int(_66.Depth))))
                {
                    _16244 = false;
                    break;
                }
                if (WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_730.z))))] != 0u)
                {
                    _16244 = false;
                    break;
                }
                _16244 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_730, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_16244)
            {
                _16246 = true;
                break;
            }
            int _9831 = _730.z + 1;
            bool _16245;
            do
            {
                if (!((((((_730.x >= 0) && (_730.y >= 0)) && (_9831 >= 0)) && (_730.x < int(_66.Width))) && (_730.y < int(_66.Height))) && (_9831 < int(_66.Depth))))
                {
                    _16245 = false;
                    break;
                }
                if (WallMask_1._data[uint(_730.x) + (_66.Width * (uint(_730.y) + (_66.Height * uint(_9831))))] != 0u)
                {
                    _16245 = false;
                    break;
                }
                _16245 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_730.xy, _9831, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            _16246 = _16245;
            break;
        } while(false);
        float _16642;
        if ((!_16240) && (!_16246))
        {
            float _16385;
            do
            {
                ivec3 _10236 = _730 + ivec3(1, 0, 0);
                float _16268;
                uint _16269;
                do
                {
                    if (!((((((_10236.x >= 0) && (_10236.y >= 0)) && (_10236.z >= 0)) && (_10236.x < int(_66.Width))) && (_10236.y < int(_66.Height))) && (_10236.z < int(_66.Depth))))
                    {
                        _16269 = 0u;
                        _16268 = 0.0;
                        break;
                    }
                    bool _16252;
                    do
                    {
                        bool _16249;
                        do
                        {
                            bool _16247;
                            do
                            {
                                if (!((((((_10236.x >= 0) && (_10236.y >= 0)) && (_10236.z >= 0)) && (_10236.x < int(_66.Width))) && (_10236.y < int(_66.Height))) && (_10236.z < int(_66.Depth))))
                                {
                                    _16247 = true;
                                    break;
                                }
                                _16247 = WallMask_1._data[uint(_10236.x) + (_66.Width * (uint(_10236.y) + (_66.Height * uint(_10236.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16247)
                            {
                                _16249 = true;
                                break;
                            }
                            int _10431 = _10236.z + 1;
                            bool _16248;
                            do
                            {
                                if (!((((((_10236.x >= 0) && (_10236.y >= 0)) && (_10431 >= 0)) && (_10236.x < int(_66.Width))) && (_10236.y < int(_66.Height))) && (_10431 < int(_66.Depth))))
                                {
                                    _16248 = true;
                                    break;
                                }
                                _16248 = WallMask_1._data[uint(_10236.x) + (_66.Width * (uint(_10236.y) + (_66.Height * uint(_10431))))] != 0u;
                                break;
                            } while(false);
                            _16249 = _16248;
                            break;
                        } while(false);
                        if (_16249)
                        {
                            _16252 = false;
                            break;
                        }
                        bool _16250;
                        do
                        {
                            if (!((((((_10236.x >= 0) && (_10236.y >= 0)) && (_10236.z >= 0)) && (_10236.x < int(_66.Width))) && (_10236.y < int(_66.Height))) && (_10236.z < int(_66.Depth))))
                            {
                                _16250 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10236.x) + (_66.Width * (uint(_10236.y) + (_66.Height * uint(_10236.z))))] != 0u)
                            {
                                _16250 = false;
                                break;
                            }
                            _16250 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10236, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16250)
                        {
                            _16252 = true;
                            break;
                        }
                        int _10392 = _10236.z + 1;
                        bool _16251;
                        do
                        {
                            if (!((((((_10236.x >= 0) && (_10236.y >= 0)) && (_10392 >= 0)) && (_10236.x < int(_66.Width))) && (_10236.y < int(_66.Height))) && (_10392 < int(_66.Depth))))
                            {
                                _16251 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10236.x) + (_66.Width * (uint(_10236.y) + (_66.Height * uint(_10392))))] != 0u)
                            {
                                _16251 = false;
                                break;
                            }
                            _16251 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10236.xy, _10392, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16252 = _16251;
                        break;
                    } while(false);
                    if (!_16252)
                    {
                        _16269 = 0u;
                        _16268 = 0.0;
                        break;
                    }
                    float _16253;
                    do
                    {
                        if ((_10236.z < 0) || (_10236.z > (int(_66.Depth) - 1)))
                        {
                            _16253 = 0.0;
                            break;
                        }
                        _16253 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10236.x, 0, int(_66.Width) - 1), clamp(_10236.y, 0, int(_66.Height) - 1), _10236.z, 0).xyz, 0).z;
                        break;
                    } while(false);
                    _16269 = 1u;
                    _16268 = _16253;
                    break;
                } while(false);
                ivec3 _10244 = _730 - ivec3(1, 0, 0);
                float _16291;
                uint _16292;
                do
                {
                    if (!((((((_10244.x >= 0) && (_10244.y >= 0)) && (_10244.z >= 0)) && (_10244.x < int(_66.Width))) && (_10244.y < int(_66.Height))) && (_10244.z < int(_66.Depth))))
                    {
                        _16292 = _16269;
                        _16291 = _16268;
                        break;
                    }
                    bool _16275;
                    do
                    {
                        bool _16272;
                        do
                        {
                            bool _16270;
                            do
                            {
                                if (!((((((_10244.x >= 0) && (_10244.y >= 0)) && (_10244.z >= 0)) && (_10244.x < int(_66.Width))) && (_10244.y < int(_66.Height))) && (_10244.z < int(_66.Depth))))
                                {
                                    _16270 = true;
                                    break;
                                }
                                _16270 = WallMask_1._data[uint(_10244.x) + (_66.Width * (uint(_10244.y) + (_66.Height * uint(_10244.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16270)
                            {
                                _16272 = true;
                                break;
                            }
                            int _11052 = _10244.z + 1;
                            bool _16271;
                            do
                            {
                                if (!((((((_10244.x >= 0) && (_10244.y >= 0)) && (_11052 >= 0)) && (_10244.x < int(_66.Width))) && (_10244.y < int(_66.Height))) && (_11052 < int(_66.Depth))))
                                {
                                    _16271 = true;
                                    break;
                                }
                                _16271 = WallMask_1._data[uint(_10244.x) + (_66.Width * (uint(_10244.y) + (_66.Height * uint(_11052))))] != 0u;
                                break;
                            } while(false);
                            _16272 = _16271;
                            break;
                        } while(false);
                        if (_16272)
                        {
                            _16275 = false;
                            break;
                        }
                        bool _16273;
                        do
                        {
                            if (!((((((_10244.x >= 0) && (_10244.y >= 0)) && (_10244.z >= 0)) && (_10244.x < int(_66.Width))) && (_10244.y < int(_66.Height))) && (_10244.z < int(_66.Depth))))
                            {
                                _16273 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10244.x) + (_66.Width * (uint(_10244.y) + (_66.Height * uint(_10244.z))))] != 0u)
                            {
                                _16273 = false;
                                break;
                            }
                            _16273 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10244, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16273)
                        {
                            _16275 = true;
                            break;
                        }
                        int _11013 = _10244.z + 1;
                        bool _16274;
                        do
                        {
                            if (!((((((_10244.x >= 0) && (_10244.y >= 0)) && (_11013 >= 0)) && (_10244.x < int(_66.Width))) && (_10244.y < int(_66.Height))) && (_11013 < int(_66.Depth))))
                            {
                                _16274 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10244.x) + (_66.Width * (uint(_10244.y) + (_66.Height * uint(_11013))))] != 0u)
                            {
                                _16274 = false;
                                break;
                            }
                            _16274 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10244.xy, _11013, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16275 = _16274;
                        break;
                    } while(false);
                    if (!_16275)
                    {
                        _16292 = _16269;
                        _16291 = _16268;
                        break;
                    }
                    float _16276;
                    do
                    {
                        if ((_10244.z < 0) || (_10244.z > (int(_66.Depth) - 1)))
                        {
                            _16276 = 0.0;
                            break;
                        }
                        _16276 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10244.x, 0, int(_66.Width) - 1), clamp(_10244.y, 0, int(_66.Height) - 1), _10244.z, 0).xyz, 0).z;
                        break;
                    } while(false);
                    _16292 = _16269 + uint(1);
                    _16291 = _16268 + _16276;
                    break;
                } while(false);
                ivec3 _10252 = _730 + ivec3(0, 1, 0);
                float _16314;
                uint _16315;
                do
                {
                    if (!((((((_10252.x >= 0) && (_10252.y >= 0)) && (_10252.z >= 0)) && (_10252.x < int(_66.Width))) && (_10252.y < int(_66.Height))) && (_10252.z < int(_66.Depth))))
                    {
                        _16315 = _16292;
                        _16314 = _16291;
                        break;
                    }
                    bool _16298;
                    do
                    {
                        bool _16295;
                        do
                        {
                            bool _16293;
                            do
                            {
                                if (!((((((_10252.x >= 0) && (_10252.y >= 0)) && (_10252.z >= 0)) && (_10252.x < int(_66.Width))) && (_10252.y < int(_66.Height))) && (_10252.z < int(_66.Depth))))
                                {
                                    _16293 = true;
                                    break;
                                }
                                _16293 = WallMask_1._data[uint(_10252.x) + (_66.Width * (uint(_10252.y) + (_66.Height * uint(_10252.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16293)
                            {
                                _16295 = true;
                                break;
                            }
                            int _11673 = _10252.z + 1;
                            bool _16294;
                            do
                            {
                                if (!((((((_10252.x >= 0) && (_10252.y >= 0)) && (_11673 >= 0)) && (_10252.x < int(_66.Width))) && (_10252.y < int(_66.Height))) && (_11673 < int(_66.Depth))))
                                {
                                    _16294 = true;
                                    break;
                                }
                                _16294 = WallMask_1._data[uint(_10252.x) + (_66.Width * (uint(_10252.y) + (_66.Height * uint(_11673))))] != 0u;
                                break;
                            } while(false);
                            _16295 = _16294;
                            break;
                        } while(false);
                        if (_16295)
                        {
                            _16298 = false;
                            break;
                        }
                        bool _16296;
                        do
                        {
                            if (!((((((_10252.x >= 0) && (_10252.y >= 0)) && (_10252.z >= 0)) && (_10252.x < int(_66.Width))) && (_10252.y < int(_66.Height))) && (_10252.z < int(_66.Depth))))
                            {
                                _16296 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10252.x) + (_66.Width * (uint(_10252.y) + (_66.Height * uint(_10252.z))))] != 0u)
                            {
                                _16296 = false;
                                break;
                            }
                            _16296 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10252, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16296)
                        {
                            _16298 = true;
                            break;
                        }
                        int _11634 = _10252.z + 1;
                        bool _16297;
                        do
                        {
                            if (!((((((_10252.x >= 0) && (_10252.y >= 0)) && (_11634 >= 0)) && (_10252.x < int(_66.Width))) && (_10252.y < int(_66.Height))) && (_11634 < int(_66.Depth))))
                            {
                                _16297 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10252.x) + (_66.Width * (uint(_10252.y) + (_66.Height * uint(_11634))))] != 0u)
                            {
                                _16297 = false;
                                break;
                            }
                            _16297 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10252.xy, _11634, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16298 = _16297;
                        break;
                    } while(false);
                    if (!_16298)
                    {
                        _16315 = _16292;
                        _16314 = _16291;
                        break;
                    }
                    float _16299;
                    do
                    {
                        if ((_10252.z < 0) || (_10252.z > (int(_66.Depth) - 1)))
                        {
                            _16299 = 0.0;
                            break;
                        }
                        _16299 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10252.x, 0, int(_66.Width) - 1), clamp(_10252.y, 0, int(_66.Height) - 1), _10252.z, 0).xyz, 0).z;
                        break;
                    } while(false);
                    _16315 = _16292 + uint(1);
                    _16314 = _16291 + _16299;
                    break;
                } while(false);
                ivec3 _10260 = _730 - ivec3(0, 1, 0);
                float _16337;
                uint _16338;
                do
                {
                    if (!((((((_10260.x >= 0) && (_10260.y >= 0)) && (_10260.z >= 0)) && (_10260.x < int(_66.Width))) && (_10260.y < int(_66.Height))) && (_10260.z < int(_66.Depth))))
                    {
                        _16338 = _16315;
                        _16337 = _16314;
                        break;
                    }
                    bool _16321;
                    do
                    {
                        bool _16318;
                        do
                        {
                            bool _16316;
                            do
                            {
                                if (!((((((_10260.x >= 0) && (_10260.y >= 0)) && (_10260.z >= 0)) && (_10260.x < int(_66.Width))) && (_10260.y < int(_66.Height))) && (_10260.z < int(_66.Depth))))
                                {
                                    _16316 = true;
                                    break;
                                }
                                _16316 = WallMask_1._data[uint(_10260.x) + (_66.Width * (uint(_10260.y) + (_66.Height * uint(_10260.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16316)
                            {
                                _16318 = true;
                                break;
                            }
                            int _12294 = _10260.z + 1;
                            bool _16317;
                            do
                            {
                                if (!((((((_10260.x >= 0) && (_10260.y >= 0)) && (_12294 >= 0)) && (_10260.x < int(_66.Width))) && (_10260.y < int(_66.Height))) && (_12294 < int(_66.Depth))))
                                {
                                    _16317 = true;
                                    break;
                                }
                                _16317 = WallMask_1._data[uint(_10260.x) + (_66.Width * (uint(_10260.y) + (_66.Height * uint(_12294))))] != 0u;
                                break;
                            } while(false);
                            _16318 = _16317;
                            break;
                        } while(false);
                        if (_16318)
                        {
                            _16321 = false;
                            break;
                        }
                        bool _16319;
                        do
                        {
                            if (!((((((_10260.x >= 0) && (_10260.y >= 0)) && (_10260.z >= 0)) && (_10260.x < int(_66.Width))) && (_10260.y < int(_66.Height))) && (_10260.z < int(_66.Depth))))
                            {
                                _16319 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10260.x) + (_66.Width * (uint(_10260.y) + (_66.Height * uint(_10260.z))))] != 0u)
                            {
                                _16319 = false;
                                break;
                            }
                            _16319 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10260, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16319)
                        {
                            _16321 = true;
                            break;
                        }
                        int _12255 = _10260.z + 1;
                        bool _16320;
                        do
                        {
                            if (!((((((_10260.x >= 0) && (_10260.y >= 0)) && (_12255 >= 0)) && (_10260.x < int(_66.Width))) && (_10260.y < int(_66.Height))) && (_12255 < int(_66.Depth))))
                            {
                                _16320 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10260.x) + (_66.Width * (uint(_10260.y) + (_66.Height * uint(_12255))))] != 0u)
                            {
                                _16320 = false;
                                break;
                            }
                            _16320 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10260.xy, _12255, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16321 = _16320;
                        break;
                    } while(false);
                    if (!_16321)
                    {
                        _16338 = _16315;
                        _16337 = _16314;
                        break;
                    }
                    float _16322;
                    do
                    {
                        if ((_10260.z < 0) || (_10260.z > (int(_66.Depth) - 1)))
                        {
                            _16322 = 0.0;
                            break;
                        }
                        _16322 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10260.x, 0, int(_66.Width) - 1), clamp(_10260.y, 0, int(_66.Height) - 1), _10260.z, 0).xyz, 0).z;
                        break;
                    } while(false);
                    _16338 = _16315 + uint(1);
                    _16337 = _16314 + _16322;
                    break;
                } while(false);
                ivec3 _10268 = _730 + ivec3(0, 0, 1);
                float _16360;
                uint _16361;
                do
                {
                    if (!((((((_10268.x >= 0) && (_10268.y >= 0)) && (_10268.z >= 0)) && (_10268.x < int(_66.Width))) && (_10268.y < int(_66.Height))) && (_10268.z < int(_66.Depth))))
                    {
                        _16361 = _16338;
                        _16360 = _16337;
                        break;
                    }
                    bool _16344;
                    do
                    {
                        bool _16341;
                        do
                        {
                            bool _16339;
                            do
                            {
                                if (!((((((_10268.x >= 0) && (_10268.y >= 0)) && (_10268.z >= 0)) && (_10268.x < int(_66.Width))) && (_10268.y < int(_66.Height))) && (_10268.z < int(_66.Depth))))
                                {
                                    _16339 = true;
                                    break;
                                }
                                _16339 = WallMask_1._data[uint(_10268.x) + (_66.Width * (uint(_10268.y) + (_66.Height * uint(_10268.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16339)
                            {
                                _16341 = true;
                                break;
                            }
                            int _12915 = _10268.z + 1;
                            bool _16340;
                            do
                            {
                                if (!((((((_10268.x >= 0) && (_10268.y >= 0)) && (_12915 >= 0)) && (_10268.x < int(_66.Width))) && (_10268.y < int(_66.Height))) && (_12915 < int(_66.Depth))))
                                {
                                    _16340 = true;
                                    break;
                                }
                                _16340 = WallMask_1._data[uint(_10268.x) + (_66.Width * (uint(_10268.y) + (_66.Height * uint(_12915))))] != 0u;
                                break;
                            } while(false);
                            _16341 = _16340;
                            break;
                        } while(false);
                        if (_16341)
                        {
                            _16344 = false;
                            break;
                        }
                        bool _16342;
                        do
                        {
                            if (!((((((_10268.x >= 0) && (_10268.y >= 0)) && (_10268.z >= 0)) && (_10268.x < int(_66.Width))) && (_10268.y < int(_66.Height))) && (_10268.z < int(_66.Depth))))
                            {
                                _16342 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10268.x) + (_66.Width * (uint(_10268.y) + (_66.Height * uint(_10268.z))))] != 0u)
                            {
                                _16342 = false;
                                break;
                            }
                            _16342 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10268, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16342)
                        {
                            _16344 = true;
                            break;
                        }
                        int _12876 = _10268.z + 1;
                        bool _16343;
                        do
                        {
                            if (!((((((_10268.x >= 0) && (_10268.y >= 0)) && (_12876 >= 0)) && (_10268.x < int(_66.Width))) && (_10268.y < int(_66.Height))) && (_12876 < int(_66.Depth))))
                            {
                                _16343 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10268.x) + (_66.Width * (uint(_10268.y) + (_66.Height * uint(_12876))))] != 0u)
                            {
                                _16343 = false;
                                break;
                            }
                            _16343 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10268.xy, _12876, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16344 = _16343;
                        break;
                    } while(false);
                    if (!_16344)
                    {
                        _16361 = _16338;
                        _16360 = _16337;
                        break;
                    }
                    float _16345;
                    do
                    {
                        if ((_10268.z < 0) || (_10268.z > (int(_66.Depth) - 1)))
                        {
                            _16345 = 0.0;
                            break;
                        }
                        _16345 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10268.x, 0, int(_66.Width) - 1), clamp(_10268.y, 0, int(_66.Height) - 1), _10268.z, 0).xyz, 0).z;
                        break;
                    } while(false);
                    _16361 = _16338 + uint(1);
                    _16360 = _16337 + _16345;
                    break;
                } while(false);
                ivec3 _10276 = _730 - ivec3(0, 0, 1);
                float _16383;
                uint _16384;
                do
                {
                    if (!((((((_10276.x >= 0) && (_10276.y >= 0)) && (_10276.z >= 0)) && (_10276.x < int(_66.Width))) && (_10276.y < int(_66.Height))) && (_10276.z < int(_66.Depth))))
                    {
                        _16384 = _16361;
                        _16383 = _16360;
                        break;
                    }
                    bool _16367;
                    do
                    {
                        bool _16364;
                        do
                        {
                            bool _16362;
                            do
                            {
                                if (!((((((_10276.x >= 0) && (_10276.y >= 0)) && (_10276.z >= 0)) && (_10276.x < int(_66.Width))) && (_10276.y < int(_66.Height))) && (_10276.z < int(_66.Depth))))
                                {
                                    _16362 = true;
                                    break;
                                }
                                _16362 = WallMask_1._data[uint(_10276.x) + (_66.Width * (uint(_10276.y) + (_66.Height * uint(_10276.z))))] != 0u;
                                break;
                            } while(false);
                            if (_16362)
                            {
                                _16364 = true;
                                break;
                            }
                            int _13536 = _10276.z + 1;
                            bool _16363;
                            do
                            {
                                if (!((((((_10276.x >= 0) && (_10276.y >= 0)) && (_13536 >= 0)) && (_10276.x < int(_66.Width))) && (_10276.y < int(_66.Height))) && (_13536 < int(_66.Depth))))
                                {
                                    _16363 = true;
                                    break;
                                }
                                _16363 = WallMask_1._data[uint(_10276.x) + (_66.Width * (uint(_10276.y) + (_66.Height * uint(_13536))))] != 0u;
                                break;
                            } while(false);
                            _16364 = _16363;
                            break;
                        } while(false);
                        if (_16364)
                        {
                            _16367 = false;
                            break;
                        }
                        bool _16365;
                        do
                        {
                            if (!((((((_10276.x >= 0) && (_10276.y >= 0)) && (_10276.z >= 0)) && (_10276.x < int(_66.Width))) && (_10276.y < int(_66.Height))) && (_10276.z < int(_66.Depth))))
                            {
                                _16365 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10276.x) + (_66.Width * (uint(_10276.y) + (_66.Height * uint(_10276.z))))] != 0u)
                            {
                                _16365 = false;
                                break;
                            }
                            _16365 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10276, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_16365)
                        {
                            _16367 = true;
                            break;
                        }
                        int _13497 = _10276.z + 1;
                        bool _16366;
                        do
                        {
                            if (!((((((_10276.x >= 0) && (_10276.y >= 0)) && (_13497 >= 0)) && (_10276.x < int(_66.Width))) && (_10276.y < int(_66.Height))) && (_13497 < int(_66.Depth))))
                            {
                                _16366 = false;
                                break;
                            }
                            if (WallMask_1._data[uint(_10276.x) + (_66.Width * (uint(_10276.y) + (_66.Height * uint(_13497))))] != 0u)
                            {
                                _16366 = false;
                                break;
                            }
                            _16366 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_10276.xy, _13497, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        _16367 = _16366;
                        break;
                    } while(false);
                    if (!_16367)
                    {
                        _16384 = _16361;
                        _16383 = _16360;
                        break;
                    }
                    float _16368;
                    do
                    {
                        if ((_10276.z < 0) || (_10276.z > (int(_66.Depth) - 1)))
                        {
                            _16368 = 0.0;
                            break;
                        }
                        _16368 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10276.x, 0, int(_66.Width) - 1), clamp(_10276.y, 0, int(_66.Height) - 1), _10276.z, 0).xyz, 0).z;
                        break;
                    } while(false);
                    _16384 = _16361 + uint(1);
                    _16383 = _16360 + _16368;
                    break;
                } while(false);
                if (_16384 > 0u)
                {
                    _16385 = _16383 / float(_16384);
                    break;
                }
                _16385 = 0.0;
                break;
            } while(false);
            _16642 = _16385;
        }
        else
        {
            _16642 = _744.z;
        }
        imageStore(VelocityOut, ivec3(gl_GlobalInvocationID), vec4(_16398, _16520, _16642, 0.0));
        break;
    } while(false);
}

