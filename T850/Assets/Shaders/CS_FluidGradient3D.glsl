#version 430
// GENERATED from CS_FluidGradient3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=c7f1b2701595d99e53ad93db4df98ec1399920f6fde955f5913522bc6f443687; defines=T850_SPIRV=1,T850_VULKAN=1
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
} _72;

layout(binding = 4, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 6, std140) uniform ObjectConstants
{
    uint BodyCount;
    uint ObjectPad0;
    uint ObjectPad1;
    uint ObjectPad2;
} _258;

layout(binding = 7, std430) readonly buffer BodyVelocity
{
    vec4 _data[];
} BodyVelocity_1;

layout(binding = 3, rgba16f) uniform writeonly image3D VelocityOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;
layout(binding = 5) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler3D SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _72.Width) || (gl_GlobalInvocationID.y >= _72.Height)) || (gl_GlobalInvocationID.z >= _72.Depth))
        {
            break;
        }
        ivec3 _711 = ivec3(gl_GlobalInvocationID);
        vec4 _725 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0);
        ivec3 _727 = _711 + ivec3(1, 0, 0);
        float _730 = _725.x;
        float _4861;
        do
        {
            bool _4829;
            do
            {
                if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                {
                    _4829 = true;
                    break;
                }
                _4829 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                break;
            } while(false);
            if (_4829)
            {
                float _4860;
                do
                {
                    bool _4858;
                    uint _4859;
                    do
                    {
                        if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                        {
                            _4859 = 0u;
                            _4858 = false;
                            break;
                        }
                        uint _1041 = uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))));
                        if (WallMask_1._data[_1041] < 2u)
                        {
                            _4859 = 0u;
                            _4858 = false;
                            break;
                        }
                        _4859 = min((WallMask_1._data[_1041] - 2u), (_258.BodyCount - 1u));
                        _4858 = true;
                        break;
                    } while(false);
                    if (_4858)
                    {
                        _4860 = BodyVelocity_1._data[_4859].x;
                        break;
                    }
                    _4860 = 0.0;
                    break;
                } while(false);
                _4861 = _4860;
                break;
            }
            bool _4830;
            do
            {
                if ((((((_727.x < 0) || (_727.y < 0)) || (_727.z < 0)) || (_727.x >= int(_72.Width))) || (_727.y >= int(_72.Height))) || (_727.z >= int(_72.Depth)))
                {
                    _4830 = true;
                    break;
                }
                _4830 = WallMask_1._data[uint(_727.x) + (_72.Width * (uint(_727.y) + (_72.Height * uint(_727.z))))] != 0u;
                break;
            } while(false);
            if (_4830 && (!((_72.OpenCeiling > 0.5) && (((((_727.y >= int(_72.Height)) && (_727.x >= 0)) && (_727.x < int(_72.Width))) && (_727.z >= 0)) && (_727.z < int(_72.Depth))))))
            {
                float _4857;
                do
                {
                    bool _4855;
                    uint _4856;
                    do
                    {
                        if ((((((_727.x < 0) || (_727.y < 0)) || (_727.z < 0)) || (_727.x >= int(_72.Width))) || (_727.y >= int(_72.Height))) || (_727.z >= int(_72.Depth)))
                        {
                            _4856 = 0u;
                            _4855 = false;
                            break;
                        }
                        uint _1232 = uint(_727.x) + (_72.Width * (uint(_727.y) + (_72.Height * uint(_727.z))));
                        if (WallMask_1._data[_1232] < 2u)
                        {
                            _4856 = 0u;
                            _4855 = false;
                            break;
                        }
                        _4856 = min((WallMask_1._data[_1232] - 2u), (_258.BodyCount - 1u));
                        _4855 = true;
                        break;
                    } while(false);
                    if (_4855)
                    {
                        _4857 = BodyVelocity_1._data[_4856].x;
                        break;
                    }
                    _4857 = 0.0;
                    break;
                } while(false);
                _4861 = _4857;
                break;
            }
            bool _4832;
            do
            {
                bool _4831;
                do
                {
                    if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                    {
                        _4831 = true;
                        break;
                    }
                    _4831 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                    break;
                } while(false);
                if (_4831)
                {
                    _4832 = false;
                    break;
                }
                _4832 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            bool _4834;
            do
            {
                bool _4833;
                do
                {
                    if ((((((_727.x < 0) || (_727.y < 0)) || (_727.z < 0)) || (_727.x >= int(_72.Width))) || (_727.y >= int(_72.Height))) || (_727.z >= int(_72.Depth)))
                    {
                        _4833 = true;
                        break;
                    }
                    _4833 = WallMask_1._data[uint(_727.x) + (_72.Width * (uint(_727.y) + (_72.Height * uint(_727.z))))] != 0u;
                    break;
                } while(false);
                if (_4833)
                {
                    _4834 = false;
                    break;
                }
                _4834 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if ((!_4832) && (!_4834))
            {
                _4861 = _730;
                break;
            }
            float _4848;
            if (_4832)
            {
                _4848 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x;
            }
            else
            {
                float _4839;
                do
                {
                    bool _4836;
                    do
                    {
                        bool _4835;
                        do
                        {
                            if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                            {
                                _4835 = true;
                                break;
                            }
                            _4835 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                            break;
                        } while(false);
                        if (_4835)
                        {
                            _4836 = false;
                            break;
                        }
                        _4836 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                        break;
                    } while(false);
                    if (_4836)
                    {
                        _4839 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x;
                        break;
                    }
                    float _4837;
                    do
                    {
                        if ((((((_727.x < 0) || (_727.y < 0)) || (_727.z < 0)) || (_727.x >= int(_72.Width))) || (_727.y >= int(_72.Height))) || (_727.z >= int(_72.Depth)))
                        {
                            _4837 = 0.0;
                            break;
                        }
                        _4837 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x, 0.0, 1.0);
                        break;
                    } while(false);
                    float _4838;
                    do
                    {
                        if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                        {
                            _4838 = 0.0;
                            break;
                        }
                        _4838 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0);
                        break;
                    } while(false);
                    _4839 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x * (1.0 - (1.0 / clamp((_4837 - 0.5) / max(_4837 - _4838, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0)));
                    break;
                } while(false);
                _4848 = _4839;
            }
            float _4845;
            if (_4834)
            {
                _4845 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x;
            }
            else
            {
                float _4846;
                if ((_72.OpenCeiling > 0.5) && (((((_727.y >= int(_72.Height)) && (_727.x >= 0)) && (_727.x < int(_72.Width))) && (_727.z >= 0)) && (_727.z < int(_72.Depth))))
                {
                    _4846 = 0.0;
                }
                else
                {
                    float _4844;
                    do
                    {
                        bool _4841;
                        do
                        {
                            bool _4840;
                            do
                            {
                                if ((((((_727.x < 0) || (_727.y < 0)) || (_727.z < 0)) || (_727.x >= int(_72.Width))) || (_727.y >= int(_72.Height))) || (_727.z >= int(_72.Depth)))
                                {
                                    _4840 = true;
                                    break;
                                }
                                _4840 = WallMask_1._data[uint(_727.x) + (_72.Width * (uint(_727.y) + (_72.Height * uint(_727.z))))] != 0u;
                                break;
                            } while(false);
                            if (_4840)
                            {
                                _4841 = false;
                                break;
                            }
                            _4841 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_4841)
                        {
                            _4844 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x;
                            break;
                        }
                        float _4842;
                        do
                        {
                            if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                            {
                                _4842 = 0.0;
                                break;
                            }
                            _4842 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0);
                            break;
                        } while(false);
                        float _4843;
                        do
                        {
                            if ((((((_727.x < 0) || (_727.y < 0)) || (_727.z < 0)) || (_727.x >= int(_72.Width))) || (_727.y >= int(_72.Height))) || (_727.z >= int(_72.Depth)))
                            {
                                _4843 = 0.0;
                                break;
                            }
                            _4843 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_727, 0).xyz, 0).x, 0.0, 1.0);
                            break;
                        } while(false);
                        _4844 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x * (1.0 - (1.0 / clamp((_4842 - 0.5) / max(_4842 - _4843, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0)));
                        break;
                    } while(false);
                    _4846 = _4844;
                }
                _4845 = _4846;
            }
            _4861 = _730 - ((_72.DeltaTime * (_4845 - _4848)) / _72.CellSize);
            break;
        } while(false);
        ivec3 _733 = _711 + ivec3(0, 1, 0);
        float _736 = _725.y;
        float _4894;
        do
        {
            bool _4862;
            do
            {
                if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                {
                    _4862 = true;
                    break;
                }
                _4862 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                break;
            } while(false);
            if (_4862)
            {
                float _4893;
                do
                {
                    bool _4891;
                    uint _4892;
                    do
                    {
                        if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                        {
                            _4892 = 0u;
                            _4891 = false;
                            break;
                        }
                        uint _2290 = uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))));
                        if (WallMask_1._data[_2290] < 2u)
                        {
                            _4892 = 0u;
                            _4891 = false;
                            break;
                        }
                        _4892 = min((WallMask_1._data[_2290] - 2u), (_258.BodyCount - 1u));
                        _4891 = true;
                        break;
                    } while(false);
                    if (_4891)
                    {
                        _4893 = BodyVelocity_1._data[_4892].y;
                        break;
                    }
                    _4893 = 0.0;
                    break;
                } while(false);
                _4894 = _4893;
                break;
            }
            bool _4863;
            do
            {
                if ((((((_733.x < 0) || (_733.y < 0)) || (_733.z < 0)) || (_733.x >= int(_72.Width))) || (_733.y >= int(_72.Height))) || (_733.z >= int(_72.Depth)))
                {
                    _4863 = true;
                    break;
                }
                _4863 = WallMask_1._data[uint(_733.x) + (_72.Width * (uint(_733.y) + (_72.Height * uint(_733.z))))] != 0u;
                break;
            } while(false);
            if (_4863 && (!((_72.OpenCeiling > 0.5) && (((((_733.y >= int(_72.Height)) && (_733.x >= 0)) && (_733.x < int(_72.Width))) && (_733.z >= 0)) && (_733.z < int(_72.Depth))))))
            {
                float _4890;
                do
                {
                    bool _4888;
                    uint _4889;
                    do
                    {
                        if ((((((_733.x < 0) || (_733.y < 0)) || (_733.z < 0)) || (_733.x >= int(_72.Width))) || (_733.y >= int(_72.Height))) || (_733.z >= int(_72.Depth)))
                        {
                            _4889 = 0u;
                            _4888 = false;
                            break;
                        }
                        uint _2481 = uint(_733.x) + (_72.Width * (uint(_733.y) + (_72.Height * uint(_733.z))));
                        if (WallMask_1._data[_2481] < 2u)
                        {
                            _4889 = 0u;
                            _4888 = false;
                            break;
                        }
                        _4889 = min((WallMask_1._data[_2481] - 2u), (_258.BodyCount - 1u));
                        _4888 = true;
                        break;
                    } while(false);
                    if (_4888)
                    {
                        _4890 = BodyVelocity_1._data[_4889].y;
                        break;
                    }
                    _4890 = 0.0;
                    break;
                } while(false);
                _4894 = _4890;
                break;
            }
            bool _4865;
            do
            {
                bool _4864;
                do
                {
                    if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                    {
                        _4864 = true;
                        break;
                    }
                    _4864 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                    break;
                } while(false);
                if (_4864)
                {
                    _4865 = false;
                    break;
                }
                _4865 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            bool _4867;
            do
            {
                bool _4866;
                do
                {
                    if ((((((_733.x < 0) || (_733.y < 0)) || (_733.z < 0)) || (_733.x >= int(_72.Width))) || (_733.y >= int(_72.Height))) || (_733.z >= int(_72.Depth)))
                    {
                        _4866 = true;
                        break;
                    }
                    _4866 = WallMask_1._data[uint(_733.x) + (_72.Width * (uint(_733.y) + (_72.Height * uint(_733.z))))] != 0u;
                    break;
                } while(false);
                if (_4866)
                {
                    _4867 = false;
                    break;
                }
                _4867 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if ((!_4865) && (!_4867))
            {
                _4894 = _736;
                break;
            }
            float _4881;
            if (_4865)
            {
                _4881 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x;
            }
            else
            {
                float _4872;
                do
                {
                    bool _4869;
                    do
                    {
                        bool _4868;
                        do
                        {
                            if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                            {
                                _4868 = true;
                                break;
                            }
                            _4868 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                            break;
                        } while(false);
                        if (_4868)
                        {
                            _4869 = false;
                            break;
                        }
                        _4869 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                        break;
                    } while(false);
                    if (_4869)
                    {
                        _4872 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x;
                        break;
                    }
                    float _4870;
                    do
                    {
                        if ((((((_733.x < 0) || (_733.y < 0)) || (_733.z < 0)) || (_733.x >= int(_72.Width))) || (_733.y >= int(_72.Height))) || (_733.z >= int(_72.Depth)))
                        {
                            _4870 = 0.0;
                            break;
                        }
                        _4870 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x, 0.0, 1.0);
                        break;
                    } while(false);
                    float _4871;
                    do
                    {
                        if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                        {
                            _4871 = 0.0;
                            break;
                        }
                        _4871 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0);
                        break;
                    } while(false);
                    _4872 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x * (1.0 - (1.0 / clamp((_4870 - 0.5) / max(_4870 - _4871, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0)));
                    break;
                } while(false);
                _4881 = _4872;
            }
            float _4878;
            if (_4867)
            {
                _4878 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x;
            }
            else
            {
                float _4879;
                if ((_72.OpenCeiling > 0.5) && (((((_733.y >= int(_72.Height)) && (_733.x >= 0)) && (_733.x < int(_72.Width))) && (_733.z >= 0)) && (_733.z < int(_72.Depth))))
                {
                    _4879 = 0.0;
                }
                else
                {
                    float _4877;
                    do
                    {
                        bool _4874;
                        do
                        {
                            bool _4873;
                            do
                            {
                                if ((((((_733.x < 0) || (_733.y < 0)) || (_733.z < 0)) || (_733.x >= int(_72.Width))) || (_733.y >= int(_72.Height))) || (_733.z >= int(_72.Depth)))
                                {
                                    _4873 = true;
                                    break;
                                }
                                _4873 = WallMask_1._data[uint(_733.x) + (_72.Width * (uint(_733.y) + (_72.Height * uint(_733.z))))] != 0u;
                                break;
                            } while(false);
                            if (_4873)
                            {
                                _4874 = false;
                                break;
                            }
                            _4874 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_4874)
                        {
                            _4877 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x;
                            break;
                        }
                        float _4875;
                        do
                        {
                            if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                            {
                                _4875 = 0.0;
                                break;
                            }
                            _4875 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0);
                            break;
                        } while(false);
                        float _4876;
                        do
                        {
                            if ((((((_733.x < 0) || (_733.y < 0)) || (_733.z < 0)) || (_733.x >= int(_72.Width))) || (_733.y >= int(_72.Height))) || (_733.z >= int(_72.Depth)))
                            {
                                _4876 = 0.0;
                                break;
                            }
                            _4876 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_733, 0).xyz, 0).x, 0.0, 1.0);
                            break;
                        } while(false);
                        _4877 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x * (1.0 - (1.0 / clamp((_4875 - 0.5) / max(_4875 - _4876, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0)));
                        break;
                    } while(false);
                    _4879 = _4877;
                }
                _4878 = _4879;
            }
            _4894 = _736 - ((_72.DeltaTime * (_4878 - _4881)) / _72.CellSize);
            break;
        } while(false);
        ivec3 _739 = _711 + ivec3(0, 0, 1);
        float _742 = _725.z;
        float _4927;
        do
        {
            bool _4895;
            do
            {
                if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                {
                    _4895 = true;
                    break;
                }
                _4895 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                break;
            } while(false);
            if (_4895)
            {
                float _4926;
                do
                {
                    bool _4924;
                    uint _4925;
                    do
                    {
                        if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                        {
                            _4925 = 0u;
                            _4924 = false;
                            break;
                        }
                        uint _3539 = uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))));
                        if (WallMask_1._data[_3539] < 2u)
                        {
                            _4925 = 0u;
                            _4924 = false;
                            break;
                        }
                        _4925 = min((WallMask_1._data[_3539] - 2u), (_258.BodyCount - 1u));
                        _4924 = true;
                        break;
                    } while(false);
                    if (_4924)
                    {
                        _4926 = BodyVelocity_1._data[_4925].z;
                        break;
                    }
                    _4926 = 0.0;
                    break;
                } while(false);
                _4927 = _4926;
                break;
            }
            bool _4896;
            do
            {
                if ((((((_739.x < 0) || (_739.y < 0)) || (_739.z < 0)) || (_739.x >= int(_72.Width))) || (_739.y >= int(_72.Height))) || (_739.z >= int(_72.Depth)))
                {
                    _4896 = true;
                    break;
                }
                _4896 = WallMask_1._data[uint(_739.x) + (_72.Width * (uint(_739.y) + (_72.Height * uint(_739.z))))] != 0u;
                break;
            } while(false);
            if (_4896 && (!((_72.OpenCeiling > 0.5) && (((((_739.y >= int(_72.Height)) && (_739.x >= 0)) && (_739.x < int(_72.Width))) && (_739.z >= 0)) && (_739.z < int(_72.Depth))))))
            {
                float _4923;
                do
                {
                    bool _4921;
                    uint _4922;
                    do
                    {
                        if ((((((_739.x < 0) || (_739.y < 0)) || (_739.z < 0)) || (_739.x >= int(_72.Width))) || (_739.y >= int(_72.Height))) || (_739.z >= int(_72.Depth)))
                        {
                            _4922 = 0u;
                            _4921 = false;
                            break;
                        }
                        uint _3730 = uint(_739.x) + (_72.Width * (uint(_739.y) + (_72.Height * uint(_739.z))));
                        if (WallMask_1._data[_3730] < 2u)
                        {
                            _4922 = 0u;
                            _4921 = false;
                            break;
                        }
                        _4922 = min((WallMask_1._data[_3730] - 2u), (_258.BodyCount - 1u));
                        _4921 = true;
                        break;
                    } while(false);
                    if (_4921)
                    {
                        _4923 = BodyVelocity_1._data[_4922].z;
                        break;
                    }
                    _4923 = 0.0;
                    break;
                } while(false);
                _4927 = _4923;
                break;
            }
            bool _4898;
            do
            {
                bool _4897;
                do
                {
                    if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                    {
                        _4897 = true;
                        break;
                    }
                    _4897 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                    break;
                } while(false);
                if (_4897)
                {
                    _4898 = false;
                    break;
                }
                _4898 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            bool _4900;
            do
            {
                bool _4899;
                do
                {
                    if ((((((_739.x < 0) || (_739.y < 0)) || (_739.z < 0)) || (_739.x >= int(_72.Width))) || (_739.y >= int(_72.Height))) || (_739.z >= int(_72.Depth)))
                    {
                        _4899 = true;
                        break;
                    }
                    _4899 = WallMask_1._data[uint(_739.x) + (_72.Width * (uint(_739.y) + (_72.Height * uint(_739.z))))] != 0u;
                    break;
                } while(false);
                if (_4899)
                {
                    _4900 = false;
                    break;
                }
                _4900 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if ((!_4898) && (!_4900))
            {
                _4927 = _742;
                break;
            }
            float _4914;
            if (_4898)
            {
                _4914 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x;
            }
            else
            {
                float _4905;
                do
                {
                    bool _4902;
                    do
                    {
                        bool _4901;
                        do
                        {
                            if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                            {
                                _4901 = true;
                                break;
                            }
                            _4901 = WallMask_1._data[uint(_711.x) + (_72.Width * (uint(_711.y) + (_72.Height * uint(_711.z))))] != 0u;
                            break;
                        } while(false);
                        if (_4901)
                        {
                            _4902 = false;
                            break;
                        }
                        _4902 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                        break;
                    } while(false);
                    if (_4902)
                    {
                        _4905 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x;
                        break;
                    }
                    float _4903;
                    do
                    {
                        if ((((((_739.x < 0) || (_739.y < 0)) || (_739.z < 0)) || (_739.x >= int(_72.Width))) || (_739.y >= int(_72.Height))) || (_739.z >= int(_72.Depth)))
                        {
                            _4903 = 0.0;
                            break;
                        }
                        _4903 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x, 0.0, 1.0);
                        break;
                    } while(false);
                    float _4904;
                    do
                    {
                        if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                        {
                            _4904 = 0.0;
                            break;
                        }
                        _4904 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0);
                        break;
                    } while(false);
                    _4905 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x * (1.0 - (1.0 / clamp((_4903 - 0.5) / max(_4903 - _4904, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0)));
                    break;
                } while(false);
                _4914 = _4905;
            }
            float _4911;
            if (_4900)
            {
                _4911 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x;
            }
            else
            {
                float _4912;
                if ((_72.OpenCeiling > 0.5) && (((((_739.y >= int(_72.Height)) && (_739.x >= 0)) && (_739.x < int(_72.Width))) && (_739.z >= 0)) && (_739.z < int(_72.Depth))))
                {
                    _4912 = 0.0;
                }
                else
                {
                    float _4910;
                    do
                    {
                        bool _4907;
                        do
                        {
                            bool _4906;
                            do
                            {
                                if ((((((_739.x < 0) || (_739.y < 0)) || (_739.z < 0)) || (_739.x >= int(_72.Width))) || (_739.y >= int(_72.Height))) || (_739.z >= int(_72.Depth)))
                                {
                                    _4906 = true;
                                    break;
                                }
                                _4906 = WallMask_1._data[uint(_739.x) + (_72.Width * (uint(_739.y) + (_72.Height * uint(_739.z))))] != 0u;
                                break;
                            } while(false);
                            if (_4906)
                            {
                                _4907 = false;
                                break;
                            }
                            _4907 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                            break;
                        } while(false);
                        if (_4907)
                        {
                            _4910 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x;
                            break;
                        }
                        float _4908;
                        do
                        {
                            if ((((((_711.x < 0) || (_711.y < 0)) || (_711.z < 0)) || (_711.x >= int(_72.Width))) || (_711.y >= int(_72.Height))) || (_711.z >= int(_72.Depth)))
                            {
                                _4908 = 0.0;
                                break;
                            }
                            _4908 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x, 0.0, 1.0);
                            break;
                        } while(false);
                        float _4909;
                        do
                        {
                            if ((((((_739.x < 0) || (_739.y < 0)) || (_739.z < 0)) || (_739.x >= int(_72.Width))) || (_739.y >= int(_72.Height))) || (_739.z >= int(_72.Depth)))
                            {
                                _4909 = 0.0;
                                break;
                            }
                            _4909 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_739, 0).xyz, 0).x, 0.0, 1.0);
                            break;
                        } while(false);
                        _4910 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_711, 0).xyz, 0).x * (1.0 - (1.0 / clamp((_4908 - 0.5) / max(_4908 - _4909, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0)));
                        break;
                    } while(false);
                    _4912 = _4910;
                }
                _4911 = _4912;
            }
            _4927 = _742 - ((_72.DeltaTime * (_4911 - _4914)) / _72.CellSize);
            break;
        } while(false);
        imageStore(VelocityOut, ivec3(gl_GlobalInvocationID), vec4(clamp(_4861, -12.0, 12.0), clamp(_4894, -12.0, 12.0), clamp(_4927, -12.0, 12.0), 0.0));
        break;
    } while(false);
}

