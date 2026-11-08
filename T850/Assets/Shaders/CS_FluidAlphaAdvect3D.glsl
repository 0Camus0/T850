#version 430
// GENERATED from CS_FluidAlphaAdvect3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=54b7fe06efad100b0bbe002ed068dea0d47c428a94991961de5a3a6b7acbecd7; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;

ivec3 _5828;

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
} _50;

layout(binding = 4, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 3, r32f) uniform writeonly image3D AlphaOut;
layout(binding = 2) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _50.Width) || (gl_GlobalInvocationID.y >= _50.Height)) || (gl_GlobalInvocationID.z >= _50.Depth))
        {
            break;
        }
        ivec3 _680 = ivec3(gl_GlobalInvocationID);
        bool _5829;
        do
        {
            if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
            {
                _5829 = true;
                break;
            }
            _5829 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
            break;
        } while(false);
        if (_5829)
        {
            imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(0.0));
            break;
        }
        float _5831;
        do
        {
            bool _5830;
            do
            {
                if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                {
                    _5830 = true;
                    break;
                }
                _5830 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                break;
            } while(false);
            if (_5830)
            {
                _5831 = 0.0;
                break;
            }
            _5831 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_680, 0).xyz, 0).x, 0.0, 1.0);
            break;
        } while(false);
        float _5842;
        do
        {
            ivec3 _933 = _680.xyz;
            ivec3 _936 = _933 + ivec3(1, 0, 0);
            bool _5832;
            do
            {
                if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                {
                    _5832 = true;
                    break;
                }
                _5832 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                break;
            } while(false);
            bool _5833;
            do
            {
                if ((((((_936.x < 0) || (_936.y < 0)) || (_936.z < 0)) || (_936.x >= int(_50.Width))) || (_936.y >= int(_50.Height))) || (_936.z >= int(_50.Depth)))
                {
                    _5833 = true;
                    break;
                }
                _5833 = WallMask_1._data[uint(_936.x) + (_50.Width * (uint(_936.y) + (_50.Height * uint(_936.z))))] != 0u;
                break;
            } while(false);
            if (_5832 || _5833)
            {
                _5842 = 0.0;
                break;
            }
            float _5834;
            do
            {
                if ((_680.x < 0) || (_680.x > (int(_50.Width) - 1)))
                {
                    _5834 = 0.0;
                    break;
                }
                _5834 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_680.x, clamp(_680.y, 0, int(_50.Height) - 1), clamp(_680.z, 0, int(_50.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            float _5836;
            do
            {
                bool _5835;
                do
                {
                    if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                    {
                        _5835 = true;
                        break;
                    }
                    _5835 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                    break;
                } while(false);
                if (_5835)
                {
                    _5836 = 0.0;
                    break;
                }
                _5836 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_680, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5838;
            do
            {
                bool _5837;
                do
                {
                    if ((((((_936.x < 0) || (_936.y < 0)) || (_936.z < 0)) || (_936.x >= int(_50.Width))) || (_936.y >= int(_50.Height))) || (_936.z >= int(_50.Depth)))
                    {
                        _5837 = true;
                        break;
                    }
                    _5837 = WallMask_1._data[uint(_936.x) + (_50.Width * (uint(_936.y) + (_50.Height * uint(_936.z))))] != 0u;
                    break;
                } while(false);
                if (_5837)
                {
                    _5838 = 0.0;
                    break;
                }
                _5838 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_936, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5841;
            if (_5834 != 0.0)
            {
                float _962 = min((abs(_5834) * _50.DeltaTime) / _50.CellSize, 1.0);
                bool _964 = _5834 > 0.0;
                float _968 = _964 ? _5836 : _5838;
                ivec3 _976 = _933 - ivec3(1, 0, 0);
                ivec3 _979 = _933 + ivec3(2, 0, 0);
                bvec3 _980 = bvec3(_964);
                ivec3 _981 = ivec3(_980.x ? _976.x : _979.x, _980.y ? _976.y : _979.y, _980.z ? _976.z : _979.z);
                float _5840;
                do
                {
                    bool _5839;
                    do
                    {
                        if ((((((_981.x < 0) || (_981.y < 0)) || (_981.z < 0)) || (_981.x >= int(_50.Width))) || (_981.y >= int(_50.Height))) || (_981.z >= int(_50.Depth)))
                        {
                            _5839 = true;
                            break;
                        }
                        _5839 = WallMask_1._data[uint(_981.x) + (_50.Width * (uint(_981.y) + (_50.Height * uint(_981.z))))] != 0u;
                        break;
                    } while(false);
                    if (_5839)
                    {
                        _5840 = 0.0;
                        break;
                    }
                    _5840 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_981, 0).xyz, 0).x, 0.0, 1.0);
                    break;
                } while(false);
                float _986 = _968 - _5840;
                float _989 = (_964 ? _5838 : _5836) - _968;
                float _1023 = (min(_962 * clamp(_968 + ((0.5 * (((_986 * _989) > 0.0) ? ((abs(_986) < abs(_989)) ? _986 : _989) : 0.0)) * (1.0 - _962)), 0.0, 1.0), _968) * _50.CellSize) / max(_50.DeltaTime, 9.9999999392252902907785028219223e-09);
                _5841 = _964 ? _1023 : (-_1023);
            }
            else
            {
                _5841 = 0.0;
            }
            float _1035 = 0.5 * (_5836 + _5838);
            _5842 = _5841 + ((((_50.Padding0 * abs(_5834)) * _1035) * (1.0 - _1035)) * clamp((_5838 - _5836) * 4.0, -1.0, 1.0));
            break;
        } while(false);
        float _5853;
        do
        {
            ivec3 _1673 = _680 - ivec3(1, 0, 0);
            ivec3 _1675 = ivec3(_1673.x, _1673.y, _1673.z);
            ivec3 _1678 = _1675 + ivec3(1, 0, 0);
            bool _5843;
            do
            {
                if ((((((_1673.x < 0) || (_1673.y < 0)) || (_1673.z < 0)) || (_1673.x >= int(_50.Width))) || (_1673.y >= int(_50.Height))) || (_1673.z >= int(_50.Depth)))
                {
                    _5843 = true;
                    break;
                }
                _5843 = WallMask_1._data[uint(_1673.x) + (_50.Width * (uint(_1673.y) + (_50.Height * uint(_1673.z))))] != 0u;
                break;
            } while(false);
            bool _5844;
            do
            {
                if ((((((_1678.x < 0) || (_1678.y < 0)) || (_1678.z < 0)) || (_1678.x >= int(_50.Width))) || (_1678.y >= int(_50.Height))) || (_1678.z >= int(_50.Depth)))
                {
                    _5844 = true;
                    break;
                }
                _5844 = WallMask_1._data[uint(_1678.x) + (_50.Width * (uint(_1678.y) + (_50.Height * uint(_1678.z))))] != 0u;
                break;
            } while(false);
            if (_5843 || _5844)
            {
                _5853 = 0.0;
                break;
            }
            float _5845;
            do
            {
                if ((_1673.x < 0) || (_1673.x > (int(_50.Width) - 1)))
                {
                    _5845 = 0.0;
                    break;
                }
                _5845 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1673.x, clamp(_1673.y, 0, int(_50.Height) - 1), clamp(_1673.z, 0, int(_50.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            float _5847;
            do
            {
                bool _5846;
                do
                {
                    if ((((((_1673.x < 0) || (_1673.y < 0)) || (_1673.z < 0)) || (_1673.x >= int(_50.Width))) || (_1673.y >= int(_50.Height))) || (_1673.z >= int(_50.Depth)))
                    {
                        _5846 = true;
                        break;
                    }
                    _5846 = WallMask_1._data[uint(_1673.x) + (_50.Width * (uint(_1673.y) + (_50.Height * uint(_1673.z))))] != 0u;
                    break;
                } while(false);
                if (_5846)
                {
                    _5847 = 0.0;
                    break;
                }
                _5847 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1673, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5849;
            do
            {
                bool _5848;
                do
                {
                    if ((((((_1678.x < 0) || (_1678.y < 0)) || (_1678.z < 0)) || (_1678.x >= int(_50.Width))) || (_1678.y >= int(_50.Height))) || (_1678.z >= int(_50.Depth)))
                    {
                        _5848 = true;
                        break;
                    }
                    _5848 = WallMask_1._data[uint(_1678.x) + (_50.Width * (uint(_1678.y) + (_50.Height * uint(_1678.z))))] != 0u;
                    break;
                } while(false);
                if (_5848)
                {
                    _5849 = 0.0;
                    break;
                }
                _5849 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1678, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5852;
            if (_5845 != 0.0)
            {
                float _1704 = min((abs(_5845) * _50.DeltaTime) / _50.CellSize, 1.0);
                bool _1706 = _5845 > 0.0;
                float _1710 = _1706 ? _5847 : _5849;
                ivec3 _1718 = _1675 - ivec3(1, 0, 0);
                ivec3 _1721 = _1675 + ivec3(2, 0, 0);
                bvec3 _1722 = bvec3(_1706);
                ivec3 _1723 = ivec3(_1722.x ? _1718.x : _1721.x, _1722.y ? _1718.y : _1721.y, _1722.z ? _1718.z : _1721.z);
                float _5851;
                do
                {
                    bool _5850;
                    do
                    {
                        if ((((((_1723.x < 0) || (_1723.y < 0)) || (_1723.z < 0)) || (_1723.x >= int(_50.Width))) || (_1723.y >= int(_50.Height))) || (_1723.z >= int(_50.Depth)))
                        {
                            _5850 = true;
                            break;
                        }
                        _5850 = WallMask_1._data[uint(_1723.x) + (_50.Width * (uint(_1723.y) + (_50.Height * uint(_1723.z))))] != 0u;
                        break;
                    } while(false);
                    if (_5850)
                    {
                        _5851 = 0.0;
                        break;
                    }
                    _5851 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_1723, 0).xyz, 0).x, 0.0, 1.0);
                    break;
                } while(false);
                float _1728 = _1710 - _5851;
                float _1731 = (_1706 ? _5849 : _5847) - _1710;
                float _1765 = (min(_1704 * clamp(_1710 + ((0.5 * (((_1728 * _1731) > 0.0) ? ((abs(_1728) < abs(_1731)) ? _1728 : _1731) : 0.0)) * (1.0 - _1704)), 0.0, 1.0), _1710) * _50.CellSize) / max(_50.DeltaTime, 9.9999999392252902907785028219223e-09);
                _5852 = _1706 ? _1765 : (-_1765);
            }
            else
            {
                _5852 = 0.0;
            }
            float _1777 = 0.5 * (_5847 + _5849);
            _5853 = -(_5852 + ((((_50.Padding0 * abs(_5845)) * _1777) * (1.0 - _1777)) * clamp((_5849 - _5847) * 4.0, -1.0, 1.0)));
            break;
        } while(false);
        float _5864;
        do
        {
            ivec3 _2417 = _680.xyz;
            ivec3 _2420 = _2417 + ivec3(0, 1, 0);
            bool _5854;
            do
            {
                if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                {
                    _5854 = true;
                    break;
                }
                _5854 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                break;
            } while(false);
            bool _5855;
            do
            {
                if ((((((_2420.x < 0) || (_2420.y < 0)) || (_2420.z < 0)) || (_2420.x >= int(_50.Width))) || (_2420.y >= int(_50.Height))) || (_2420.z >= int(_50.Depth)))
                {
                    _5855 = true;
                    break;
                }
                _5855 = WallMask_1._data[uint(_2420.x) + (_50.Width * (uint(_2420.y) + (_50.Height * uint(_2420.z))))] != 0u;
                break;
            } while(false);
            if (_5854 || _5855)
            {
                _5864 = 0.0;
                break;
            }
            float _5856;
            do
            {
                if ((_680.y < 0) || (_680.y > (int(_50.Height) - 1)))
                {
                    _5856 = 0.0;
                    break;
                }
                _5856 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_680.x, 0, int(_50.Width) - 1), _680.y, clamp(_680.z, 0, int(_50.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            float _5858;
            do
            {
                bool _5857;
                do
                {
                    if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                    {
                        _5857 = true;
                        break;
                    }
                    _5857 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                    break;
                } while(false);
                if (_5857)
                {
                    _5858 = 0.0;
                    break;
                }
                _5858 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_680, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5860;
            do
            {
                bool _5859;
                do
                {
                    if ((((((_2420.x < 0) || (_2420.y < 0)) || (_2420.z < 0)) || (_2420.x >= int(_50.Width))) || (_2420.y >= int(_50.Height))) || (_2420.z >= int(_50.Depth)))
                    {
                        _5859 = true;
                        break;
                    }
                    _5859 = WallMask_1._data[uint(_2420.x) + (_50.Width * (uint(_2420.y) + (_50.Height * uint(_2420.z))))] != 0u;
                    break;
                } while(false);
                if (_5859)
                {
                    _5860 = 0.0;
                    break;
                }
                _5860 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_2420, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5863;
            if (_5856 != 0.0)
            {
                float _2446 = min((abs(_5856) * _50.DeltaTime) / _50.CellSize, 1.0);
                bool _2448 = _5856 > 0.0;
                float _2452 = _2448 ? _5858 : _5860;
                ivec3 _2460 = _2417 - ivec3(0, 1, 0);
                ivec3 _2463 = _2417 + ivec3(0, 2, 0);
                bvec3 _2464 = bvec3(_2448);
                ivec3 _2465 = ivec3(_2464.x ? _2460.x : _2463.x, _2464.y ? _2460.y : _2463.y, _2464.z ? _2460.z : _2463.z);
                float _5862;
                do
                {
                    bool _5861;
                    do
                    {
                        if ((((((_2465.x < 0) || (_2465.y < 0)) || (_2465.z < 0)) || (_2465.x >= int(_50.Width))) || (_2465.y >= int(_50.Height))) || (_2465.z >= int(_50.Depth)))
                        {
                            _5861 = true;
                            break;
                        }
                        _5861 = WallMask_1._data[uint(_2465.x) + (_50.Width * (uint(_2465.y) + (_50.Height * uint(_2465.z))))] != 0u;
                        break;
                    } while(false);
                    if (_5861)
                    {
                        _5862 = 0.0;
                        break;
                    }
                    _5862 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_2465, 0).xyz, 0).x, 0.0, 1.0);
                    break;
                } while(false);
                float _2470 = _2452 - _5862;
                float _2473 = (_2448 ? _5860 : _5858) - _2452;
                float _2507 = (min(_2446 * clamp(_2452 + ((0.5 * (((_2470 * _2473) > 0.0) ? ((abs(_2470) < abs(_2473)) ? _2470 : _2473) : 0.0)) * (1.0 - _2446)), 0.0, 1.0), _2452) * _50.CellSize) / max(_50.DeltaTime, 9.9999999392252902907785028219223e-09);
                _5863 = _2448 ? _2507 : (-_2507);
            }
            else
            {
                _5863 = 0.0;
            }
            float _2519 = 0.5 * (_5858 + _5860);
            _5864 = _5863 + ((((_50.Padding0 * abs(_5856)) * _2519) * (1.0 - _2519)) * clamp((_5860 - _5858) * 4.0, -1.0, 1.0));
            break;
        } while(false);
        float _5875;
        do
        {
            ivec3 _3157 = _680 - ivec3(0, 1, 0);
            ivec3 _3159 = ivec3(_3157.x, _3157.y, _3157.z);
            ivec3 _3162 = _3159 + ivec3(0, 1, 0);
            bool _5865;
            do
            {
                if ((((((_3157.x < 0) || (_3157.y < 0)) || (_3157.z < 0)) || (_3157.x >= int(_50.Width))) || (_3157.y >= int(_50.Height))) || (_3157.z >= int(_50.Depth)))
                {
                    _5865 = true;
                    break;
                }
                _5865 = WallMask_1._data[uint(_3157.x) + (_50.Width * (uint(_3157.y) + (_50.Height * uint(_3157.z))))] != 0u;
                break;
            } while(false);
            bool _5866;
            do
            {
                if ((((((_3162.x < 0) || (_3162.y < 0)) || (_3162.z < 0)) || (_3162.x >= int(_50.Width))) || (_3162.y >= int(_50.Height))) || (_3162.z >= int(_50.Depth)))
                {
                    _5866 = true;
                    break;
                }
                _5866 = WallMask_1._data[uint(_3162.x) + (_50.Width * (uint(_3162.y) + (_50.Height * uint(_3162.z))))] != 0u;
                break;
            } while(false);
            if (_5865 || _5866)
            {
                _5875 = 0.0;
                break;
            }
            float _5867;
            do
            {
                if ((_3157.y < 0) || (_3157.y > (int(_50.Height) - 1)))
                {
                    _5867 = 0.0;
                    break;
                }
                _5867 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3157.x, 0, int(_50.Width) - 1), _3157.y, clamp(_3157.z, 0, int(_50.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            float _5869;
            do
            {
                bool _5868;
                do
                {
                    if ((((((_3157.x < 0) || (_3157.y < 0)) || (_3157.z < 0)) || (_3157.x >= int(_50.Width))) || (_3157.y >= int(_50.Height))) || (_3157.z >= int(_50.Depth)))
                    {
                        _5868 = true;
                        break;
                    }
                    _5868 = WallMask_1._data[uint(_3157.x) + (_50.Width * (uint(_3157.y) + (_50.Height * uint(_3157.z))))] != 0u;
                    break;
                } while(false);
                if (_5868)
                {
                    _5869 = 0.0;
                    break;
                }
                _5869 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_3157, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5871;
            do
            {
                bool _5870;
                do
                {
                    if ((((((_3162.x < 0) || (_3162.y < 0)) || (_3162.z < 0)) || (_3162.x >= int(_50.Width))) || (_3162.y >= int(_50.Height))) || (_3162.z >= int(_50.Depth)))
                    {
                        _5870 = true;
                        break;
                    }
                    _5870 = WallMask_1._data[uint(_3162.x) + (_50.Width * (uint(_3162.y) + (_50.Height * uint(_3162.z))))] != 0u;
                    break;
                } while(false);
                if (_5870)
                {
                    _5871 = 0.0;
                    break;
                }
                _5871 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_3162, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5874;
            if (_5867 != 0.0)
            {
                float _3188 = min((abs(_5867) * _50.DeltaTime) / _50.CellSize, 1.0);
                bool _3190 = _5867 > 0.0;
                float _3194 = _3190 ? _5869 : _5871;
                ivec3 _3202 = _3159 - ivec3(0, 1, 0);
                ivec3 _3205 = _3159 + ivec3(0, 2, 0);
                bvec3 _3206 = bvec3(_3190);
                ivec3 _3207 = ivec3(_3206.x ? _3202.x : _3205.x, _3206.y ? _3202.y : _3205.y, _3206.z ? _3202.z : _3205.z);
                float _5873;
                do
                {
                    bool _5872;
                    do
                    {
                        if ((((((_3207.x < 0) || (_3207.y < 0)) || (_3207.z < 0)) || (_3207.x >= int(_50.Width))) || (_3207.y >= int(_50.Height))) || (_3207.z >= int(_50.Depth)))
                        {
                            _5872 = true;
                            break;
                        }
                        _5872 = WallMask_1._data[uint(_3207.x) + (_50.Width * (uint(_3207.y) + (_50.Height * uint(_3207.z))))] != 0u;
                        break;
                    } while(false);
                    if (_5872)
                    {
                        _5873 = 0.0;
                        break;
                    }
                    _5873 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_3207, 0).xyz, 0).x, 0.0, 1.0);
                    break;
                } while(false);
                float _3212 = _3194 - _5873;
                float _3215 = (_3190 ? _5871 : _5869) - _3194;
                float _3249 = (min(_3188 * clamp(_3194 + ((0.5 * (((_3212 * _3215) > 0.0) ? ((abs(_3212) < abs(_3215)) ? _3212 : _3215) : 0.0)) * (1.0 - _3188)), 0.0, 1.0), _3194) * _50.CellSize) / max(_50.DeltaTime, 9.9999999392252902907785028219223e-09);
                _5874 = _3190 ? _3249 : (-_3249);
            }
            else
            {
                _5874 = 0.0;
            }
            float _3261 = 0.5 * (_5869 + _5871);
            _5875 = -(_5874 + ((((_50.Padding0 * abs(_5867)) * _3261) * (1.0 - _3261)) * clamp((_5871 - _5869) * 4.0, -1.0, 1.0)));
            break;
        } while(false);
        float _5886;
        do
        {
            ivec3 _3901 = _680.xyz;
            ivec3 _3904 = _3901 + ivec3(0, 0, 1);
            bool _5876;
            do
            {
                if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                {
                    _5876 = true;
                    break;
                }
                _5876 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                break;
            } while(false);
            bool _5877;
            do
            {
                if ((((((_3904.x < 0) || (_3904.y < 0)) || (_3904.z < 0)) || (_3904.x >= int(_50.Width))) || (_3904.y >= int(_50.Height))) || (_3904.z >= int(_50.Depth)))
                {
                    _5877 = true;
                    break;
                }
                _5877 = WallMask_1._data[uint(_3904.x) + (_50.Width * (uint(_3904.y) + (_50.Height * uint(_3904.z))))] != 0u;
                break;
            } while(false);
            if (_5876 || _5877)
            {
                _5886 = 0.0;
                break;
            }
            float _5878;
            do
            {
                if ((_680.z < 0) || (_680.z > (int(_50.Depth) - 1)))
                {
                    _5878 = 0.0;
                    break;
                }
                _5878 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_680.x, 0, int(_50.Width) - 1), clamp(_680.y, 0, int(_50.Height) - 1), _680.z, 0).xyz, 0).z;
                break;
            } while(false);
            float _5880;
            do
            {
                bool _5879;
                do
                {
                    if ((((((_680.x < 0) || (_680.y < 0)) || (_680.z < 0)) || (_680.x >= int(_50.Width))) || (_680.y >= int(_50.Height))) || (_680.z >= int(_50.Depth)))
                    {
                        _5879 = true;
                        break;
                    }
                    _5879 = WallMask_1._data[uint(_680.x) + (_50.Width * (uint(_680.y) + (_50.Height * uint(_680.z))))] != 0u;
                    break;
                } while(false);
                if (_5879)
                {
                    _5880 = 0.0;
                    break;
                }
                _5880 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_680, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5882;
            do
            {
                bool _5881;
                do
                {
                    if ((((((_3904.x < 0) || (_3904.y < 0)) || (_3904.z < 0)) || (_3904.x >= int(_50.Width))) || (_3904.y >= int(_50.Height))) || (_3904.z >= int(_50.Depth)))
                    {
                        _5881 = true;
                        break;
                    }
                    _5881 = WallMask_1._data[uint(_3904.x) + (_50.Width * (uint(_3904.y) + (_50.Height * uint(_3904.z))))] != 0u;
                    break;
                } while(false);
                if (_5881)
                {
                    _5882 = 0.0;
                    break;
                }
                _5882 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_3904, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5885;
            if (_5878 != 0.0)
            {
                float _3930 = min((abs(_5878) * _50.DeltaTime) / _50.CellSize, 1.0);
                bool _3932 = _5878 > 0.0;
                float _3936 = _3932 ? _5880 : _5882;
                ivec3 _3944 = _3901 - ivec3(0, 0, 1);
                ivec3 _3947 = _3901 + ivec3(0, 0, 2);
                bvec3 _3948 = bvec3(_3932);
                ivec3 _3949 = ivec3(_3948.x ? _3944.x : _3947.x, _3948.y ? _3944.y : _3947.y, _3948.z ? _3944.z : _3947.z);
                float _5884;
                do
                {
                    bool _5883;
                    do
                    {
                        if ((((((_3949.x < 0) || (_3949.y < 0)) || (_3949.z < 0)) || (_3949.x >= int(_50.Width))) || (_3949.y >= int(_50.Height))) || (_3949.z >= int(_50.Depth)))
                        {
                            _5883 = true;
                            break;
                        }
                        _5883 = WallMask_1._data[uint(_3949.x) + (_50.Width * (uint(_3949.y) + (_50.Height * uint(_3949.z))))] != 0u;
                        break;
                    } while(false);
                    if (_5883)
                    {
                        _5884 = 0.0;
                        break;
                    }
                    _5884 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_3949, 0).xyz, 0).x, 0.0, 1.0);
                    break;
                } while(false);
                float _3954 = _3936 - _5884;
                float _3957 = (_3932 ? _5882 : _5880) - _3936;
                float _3991 = (min(_3930 * clamp(_3936 + ((0.5 * (((_3954 * _3957) > 0.0) ? ((abs(_3954) < abs(_3957)) ? _3954 : _3957) : 0.0)) * (1.0 - _3930)), 0.0, 1.0), _3936) * _50.CellSize) / max(_50.DeltaTime, 9.9999999392252902907785028219223e-09);
                _5885 = _3932 ? _3991 : (-_3991);
            }
            else
            {
                _5885 = 0.0;
            }
            float _4003 = 0.5 * (_5880 + _5882);
            _5886 = _5885 + ((((_50.Padding0 * abs(_5878)) * _4003) * (1.0 - _4003)) * clamp((_5882 - _5880) * 4.0, -1.0, 1.0));
            break;
        } while(false);
        float _5897;
        do
        {
            ivec3 _4641 = _680 - ivec3(0, 0, 1);
            ivec3 _4643 = ivec3(_4641.x, _4641.y, _4641.z);
            ivec3 _4646 = _4643 + ivec3(0, 0, 1);
            bool _5887;
            do
            {
                if ((((((_4641.x < 0) || (_4641.y < 0)) || (_4641.z < 0)) || (_4641.x >= int(_50.Width))) || (_4641.y >= int(_50.Height))) || (_4641.z >= int(_50.Depth)))
                {
                    _5887 = true;
                    break;
                }
                _5887 = WallMask_1._data[uint(_4641.x) + (_50.Width * (uint(_4641.y) + (_50.Height * uint(_4641.z))))] != 0u;
                break;
            } while(false);
            bool _5888;
            do
            {
                if ((((((_4646.x < 0) || (_4646.y < 0)) || (_4646.z < 0)) || (_4646.x >= int(_50.Width))) || (_4646.y >= int(_50.Height))) || (_4646.z >= int(_50.Depth)))
                {
                    _5888 = true;
                    break;
                }
                _5888 = WallMask_1._data[uint(_4646.x) + (_50.Width * (uint(_4646.y) + (_50.Height * uint(_4646.z))))] != 0u;
                break;
            } while(false);
            if (_5887 || _5888)
            {
                _5897 = 0.0;
                break;
            }
            float _5889;
            do
            {
                if ((_4641.z < 0) || (_4641.z > (int(_50.Depth) - 1)))
                {
                    _5889 = 0.0;
                    break;
                }
                _5889 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_4641.x, 0, int(_50.Width) - 1), clamp(_4641.y, 0, int(_50.Height) - 1), _4641.z, 0).xyz, 0).z;
                break;
            } while(false);
            float _5891;
            do
            {
                bool _5890;
                do
                {
                    if ((((((_4641.x < 0) || (_4641.y < 0)) || (_4641.z < 0)) || (_4641.x >= int(_50.Width))) || (_4641.y >= int(_50.Height))) || (_4641.z >= int(_50.Depth)))
                    {
                        _5890 = true;
                        break;
                    }
                    _5890 = WallMask_1._data[uint(_4641.x) + (_50.Width * (uint(_4641.y) + (_50.Height * uint(_4641.z))))] != 0u;
                    break;
                } while(false);
                if (_5890)
                {
                    _5891 = 0.0;
                    break;
                }
                _5891 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4641, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5893;
            do
            {
                bool _5892;
                do
                {
                    if ((((((_4646.x < 0) || (_4646.y < 0)) || (_4646.z < 0)) || (_4646.x >= int(_50.Width))) || (_4646.y >= int(_50.Height))) || (_4646.z >= int(_50.Depth)))
                    {
                        _5892 = true;
                        break;
                    }
                    _5892 = WallMask_1._data[uint(_4646.x) + (_50.Width * (uint(_4646.y) + (_50.Height * uint(_4646.z))))] != 0u;
                    break;
                } while(false);
                if (_5892)
                {
                    _5893 = 0.0;
                    break;
                }
                _5893 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4646, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _5896;
            if (_5889 != 0.0)
            {
                float _4672 = min((abs(_5889) * _50.DeltaTime) / _50.CellSize, 1.0);
                bool _4674 = _5889 > 0.0;
                float _4678 = _4674 ? _5891 : _5893;
                ivec3 _4686 = _4643 - ivec3(0, 0, 1);
                ivec3 _4689 = _4643 + ivec3(0, 0, 2);
                bvec3 _4690 = bvec3(_4674);
                ivec3 _4691 = ivec3(_4690.x ? _4686.x : _4689.x, _4690.y ? _4686.y : _4689.y, _4690.z ? _4686.z : _4689.z);
                float _5895;
                do
                {
                    bool _5894;
                    do
                    {
                        if ((((((_4691.x < 0) || (_4691.y < 0)) || (_4691.z < 0)) || (_4691.x >= int(_50.Width))) || (_4691.y >= int(_50.Height))) || (_4691.z >= int(_50.Depth)))
                        {
                            _5894 = true;
                            break;
                        }
                        _5894 = WallMask_1._data[uint(_4691.x) + (_50.Width * (uint(_4691.y) + (_50.Height * uint(_4691.z))))] != 0u;
                        break;
                    } while(false);
                    if (_5894)
                    {
                        _5895 = 0.0;
                        break;
                    }
                    _5895 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4691, 0).xyz, 0).x, 0.0, 1.0);
                    break;
                } while(false);
                float _4696 = _4678 - _5895;
                float _4699 = (_4674 ? _5893 : _5891) - _4678;
                float _4733 = (min(_4672 * clamp(_4678 + ((0.5 * (((_4696 * _4699) > 0.0) ? ((abs(_4696) < abs(_4699)) ? _4696 : _4699) : 0.0)) * (1.0 - _4672)), 0.0, 1.0), _4678) * _50.CellSize) / max(_50.DeltaTime, 9.9999999392252902907785028219223e-09);
                _5896 = _4674 ? _4733 : (-_4733);
            }
            else
            {
                _5896 = 0.0;
            }
            float _4745 = 0.5 * (_5891 + _5893);
            _5897 = -(_5896 + ((((_50.Padding0 * abs(_5889)) * _4745) * (1.0 - _4745)) * clamp((_5893 - _5891) * 4.0, -1.0, 1.0)));
            break;
        } while(false);
        imageStore(AlphaOut, ivec3(gl_GlobalInvocationID), vec4(clamp(_5831 - ((_50.DeltaTime / _50.CellSize) * (((((_5842 + _5853) + _5864) + _5875) + _5886) + _5897)), 0.0, 1.0)));
        break;
    } while(false);
}

