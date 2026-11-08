#version 430
// GENERATED from CS_FluidPressure3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=1056c7a95fe745ba2ecab637fc1ecc6e67ff7bd81887f02695888f84a7a2c70c; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _56;

layout(binding = 4, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 3, r32f) uniform writeonly image3D PressureOut;
layout(binding = 5) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler;
layout(binding = 2) uniform sampler3D SPIRV_Cross_CombinedDivergenceInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _56.Width) || (gl_GlobalInvocationID.y >= _56.Height)) || (gl_GlobalInvocationID.z >= _56.Depth))
        {
            break;
        }
        ivec3 _611 = ivec3(gl_GlobalInvocationID);
        bool _2974;
        do
        {
            bool _2973;
            do
            {
                if ((((((_611.x < 0) || (_611.y < 0)) || (_611.z < 0)) || (_611.x >= int(_56.Width))) || (_611.y >= int(_56.Height))) || (_611.z >= int(_56.Depth)))
                {
                    _2973 = true;
                    break;
                }
                _2973 = WallMask_1._data[uint(_611.x) + (_56.Width * (uint(_611.y) + (_56.Height * uint(_611.z))))] != 0u;
                break;
            } while(false);
            if (_2973)
            {
                _2974 = false;
                break;
            }
            _2974 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_611, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
            break;
        } while(false);
        if (!_2974)
        {
            imageStore(PressureOut, ivec3(gl_GlobalInvocationID), vec4(0.0));
            break;
        }
        vec4 _633 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_611, 0).xyz, 0);
        float _634 = _633.x;
        if ((((_611.x + _611.y) + _611.z) & 1) != int(_56.Iteration & 1u))
        {
            imageStore(PressureOut, ivec3(gl_GlobalInvocationID), vec4(_634));
            break;
        }
        float _2975;
        do
        {
            if ((((((_611.x < 0) || (_611.y < 0)) || (_611.z < 0)) || (_611.x >= int(_56.Width))) || (_611.y >= int(_56.Height))) || (_611.z >= int(_56.Depth)))
            {
                _2975 = 0.0;
                break;
            }
            _2975 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_611, 0).xyz, 0).x, 0.0, 1.0);
            break;
        } while(false);
        ivec3 _658 = _611 + ivec3(1, 0, 0);
        float _2987;
        float _2989;
        do
        {
            if ((_56.OpenCeiling > 0.5) && (((((_658.y >= int(_56.Height)) && (_658.x >= 0)) && (_658.x < int(_56.Width))) && (_658.z >= 0)) && (_658.z < int(_56.Depth))))
            {
                _2989 = 1.0;
                _2987 = 0.0;
                break;
            }
            bool _2976;
            do
            {
                if ((((((_658.x < 0) || (_658.y < 0)) || (_658.z < 0)) || (_658.x >= int(_56.Width))) || (_658.y >= int(_56.Height))) || (_658.z >= int(_56.Depth)))
                {
                    _2976 = true;
                    break;
                }
                _2976 = WallMask_1._data[uint(_658.x) + (_56.Width * (uint(_658.y) + (_56.Height * uint(_658.z))))] != 0u;
                break;
            } while(false);
            if (_2976)
            {
                _2989 = 0.0;
                _2987 = 0.0;
                break;
            }
            bool _2978;
            do
            {
                bool _2977;
                do
                {
                    if ((((((_658.x < 0) || (_658.y < 0)) || (_658.z < 0)) || (_658.x >= int(_56.Width))) || (_658.y >= int(_56.Height))) || (_658.z >= int(_56.Depth)))
                    {
                        _2977 = true;
                        break;
                    }
                    _2977 = WallMask_1._data[uint(_658.x) + (_56.Width * (uint(_658.y) + (_56.Height * uint(_658.z))))] != 0u;
                    break;
                } while(false);
                if (_2977)
                {
                    _2978 = false;
                    break;
                }
                _2978 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_658, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_2978)
            {
                _2989 = 1.0;
                _2987 = texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_658, 0).xyz, 0).x;
                break;
            }
            float _2979;
            do
            {
                if ((((((_658.x < 0) || (_658.y < 0)) || (_658.z < 0)) || (_658.x >= int(_56.Width))) || (_658.y >= int(_56.Height))) || (_658.z >= int(_56.Depth)))
                {
                    _2979 = 0.0;
                    break;
                }
                _2979 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_658, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            _2989 = 1.0 / clamp((_2975 - 0.5) / max(_2975 - _2979, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0);
            _2987 = 0.0;
            break;
        } while(false);
        ivec3 _666 = _611 + ivec3(-1, 0, 0);
        float _3001;
        float _3003;
        do
        {
            if ((_56.OpenCeiling > 0.5) && (((((_666.y >= int(_56.Height)) && (_666.x >= 0)) && (_666.x < int(_56.Width))) && (_666.z >= 0)) && (_666.z < int(_56.Depth))))
            {
                _3003 = _2989 + 1.0;
                _3001 = _2987;
                break;
            }
            bool _2990;
            do
            {
                if ((((((_666.x < 0) || (_666.y < 0)) || (_666.z < 0)) || (_666.x >= int(_56.Width))) || (_666.y >= int(_56.Height))) || (_666.z >= int(_56.Depth)))
                {
                    _2990 = true;
                    break;
                }
                _2990 = WallMask_1._data[uint(_666.x) + (_56.Width * (uint(_666.y) + (_56.Height * uint(_666.z))))] != 0u;
                break;
            } while(false);
            if (_2990)
            {
                _3003 = _2989;
                _3001 = _2987;
                break;
            }
            bool _2992;
            do
            {
                bool _2991;
                do
                {
                    if ((((((_666.x < 0) || (_666.y < 0)) || (_666.z < 0)) || (_666.x >= int(_56.Width))) || (_666.y >= int(_56.Height))) || (_666.z >= int(_56.Depth)))
                    {
                        _2991 = true;
                        break;
                    }
                    _2991 = WallMask_1._data[uint(_666.x) + (_56.Width * (uint(_666.y) + (_56.Height * uint(_666.z))))] != 0u;
                    break;
                } while(false);
                if (_2991)
                {
                    _2992 = false;
                    break;
                }
                _2992 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_666, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_2992)
            {
                _3003 = _2989 + 1.0;
                _3001 = _2987 + texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_666, 0).xyz, 0).x;
                break;
            }
            float _2993;
            do
            {
                if ((((((_666.x < 0) || (_666.y < 0)) || (_666.z < 0)) || (_666.x >= int(_56.Width))) || (_666.y >= int(_56.Height))) || (_666.z >= int(_56.Depth)))
                {
                    _2993 = 0.0;
                    break;
                }
                _2993 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_666, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            _3003 = _2989 + (1.0 / clamp((_2975 - 0.5) / max(_2975 - _2993, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0));
            _3001 = _2987;
            break;
        } while(false);
        ivec3 _674 = _611 + ivec3(0, 1, 0);
        float _3015;
        float _3017;
        do
        {
            if ((_56.OpenCeiling > 0.5) && (((((_674.y >= int(_56.Height)) && (_674.x >= 0)) && (_674.x < int(_56.Width))) && (_674.z >= 0)) && (_674.z < int(_56.Depth))))
            {
                _3017 = _3003 + 1.0;
                _3015 = _3001;
                break;
            }
            bool _3004;
            do
            {
                if ((((((_674.x < 0) || (_674.y < 0)) || (_674.z < 0)) || (_674.x >= int(_56.Width))) || (_674.y >= int(_56.Height))) || (_674.z >= int(_56.Depth)))
                {
                    _3004 = true;
                    break;
                }
                _3004 = WallMask_1._data[uint(_674.x) + (_56.Width * (uint(_674.y) + (_56.Height * uint(_674.z))))] != 0u;
                break;
            } while(false);
            if (_3004)
            {
                _3017 = _3003;
                _3015 = _3001;
                break;
            }
            bool _3006;
            do
            {
                bool _3005;
                do
                {
                    if ((((((_674.x < 0) || (_674.y < 0)) || (_674.z < 0)) || (_674.x >= int(_56.Width))) || (_674.y >= int(_56.Height))) || (_674.z >= int(_56.Depth)))
                    {
                        _3005 = true;
                        break;
                    }
                    _3005 = WallMask_1._data[uint(_674.x) + (_56.Width * (uint(_674.y) + (_56.Height * uint(_674.z))))] != 0u;
                    break;
                } while(false);
                if (_3005)
                {
                    _3006 = false;
                    break;
                }
                _3006 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_674, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_3006)
            {
                _3017 = _3003 + 1.0;
                _3015 = _3001 + texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_674, 0).xyz, 0).x;
                break;
            }
            float _3007;
            do
            {
                if ((((((_674.x < 0) || (_674.y < 0)) || (_674.z < 0)) || (_674.x >= int(_56.Width))) || (_674.y >= int(_56.Height))) || (_674.z >= int(_56.Depth)))
                {
                    _3007 = 0.0;
                    break;
                }
                _3007 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_674, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            _3017 = _3003 + (1.0 / clamp((_2975 - 0.5) / max(_2975 - _3007, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0));
            _3015 = _3001;
            break;
        } while(false);
        ivec3 _682 = _611 + ivec3(0, -1, 0);
        float _3029;
        float _3031;
        do
        {
            if ((_56.OpenCeiling > 0.5) && (((((_682.y >= int(_56.Height)) && (_682.x >= 0)) && (_682.x < int(_56.Width))) && (_682.z >= 0)) && (_682.z < int(_56.Depth))))
            {
                _3031 = _3017 + 1.0;
                _3029 = _3015;
                break;
            }
            bool _3018;
            do
            {
                if ((((((_682.x < 0) || (_682.y < 0)) || (_682.z < 0)) || (_682.x >= int(_56.Width))) || (_682.y >= int(_56.Height))) || (_682.z >= int(_56.Depth)))
                {
                    _3018 = true;
                    break;
                }
                _3018 = WallMask_1._data[uint(_682.x) + (_56.Width * (uint(_682.y) + (_56.Height * uint(_682.z))))] != 0u;
                break;
            } while(false);
            if (_3018)
            {
                _3031 = _3017;
                _3029 = _3015;
                break;
            }
            bool _3020;
            do
            {
                bool _3019;
                do
                {
                    if ((((((_682.x < 0) || (_682.y < 0)) || (_682.z < 0)) || (_682.x >= int(_56.Width))) || (_682.y >= int(_56.Height))) || (_682.z >= int(_56.Depth)))
                    {
                        _3019 = true;
                        break;
                    }
                    _3019 = WallMask_1._data[uint(_682.x) + (_56.Width * (uint(_682.y) + (_56.Height * uint(_682.z))))] != 0u;
                    break;
                } while(false);
                if (_3019)
                {
                    _3020 = false;
                    break;
                }
                _3020 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_682, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_3020)
            {
                _3031 = _3017 + 1.0;
                _3029 = _3015 + texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_682, 0).xyz, 0).x;
                break;
            }
            float _3021;
            do
            {
                if ((((((_682.x < 0) || (_682.y < 0)) || (_682.z < 0)) || (_682.x >= int(_56.Width))) || (_682.y >= int(_56.Height))) || (_682.z >= int(_56.Depth)))
                {
                    _3021 = 0.0;
                    break;
                }
                _3021 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_682, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            _3031 = _3017 + (1.0 / clamp((_2975 - 0.5) / max(_2975 - _3021, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0));
            _3029 = _3015;
            break;
        } while(false);
        ivec3 _690 = _611 + ivec3(0, 0, 1);
        float _3043;
        float _3045;
        do
        {
            if ((_56.OpenCeiling > 0.5) && (((((_690.y >= int(_56.Height)) && (_690.x >= 0)) && (_690.x < int(_56.Width))) && (_690.z >= 0)) && (_690.z < int(_56.Depth))))
            {
                _3045 = _3031 + 1.0;
                _3043 = _3029;
                break;
            }
            bool _3032;
            do
            {
                if ((((((_690.x < 0) || (_690.y < 0)) || (_690.z < 0)) || (_690.x >= int(_56.Width))) || (_690.y >= int(_56.Height))) || (_690.z >= int(_56.Depth)))
                {
                    _3032 = true;
                    break;
                }
                _3032 = WallMask_1._data[uint(_690.x) + (_56.Width * (uint(_690.y) + (_56.Height * uint(_690.z))))] != 0u;
                break;
            } while(false);
            if (_3032)
            {
                _3045 = _3031;
                _3043 = _3029;
                break;
            }
            bool _3034;
            do
            {
                bool _3033;
                do
                {
                    if ((((((_690.x < 0) || (_690.y < 0)) || (_690.z < 0)) || (_690.x >= int(_56.Width))) || (_690.y >= int(_56.Height))) || (_690.z >= int(_56.Depth)))
                    {
                        _3033 = true;
                        break;
                    }
                    _3033 = WallMask_1._data[uint(_690.x) + (_56.Width * (uint(_690.y) + (_56.Height * uint(_690.z))))] != 0u;
                    break;
                } while(false);
                if (_3033)
                {
                    _3034 = false;
                    break;
                }
                _3034 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_690, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_3034)
            {
                _3045 = _3031 + 1.0;
                _3043 = _3029 + texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_690, 0).xyz, 0).x;
                break;
            }
            float _3035;
            do
            {
                if ((((((_690.x < 0) || (_690.y < 0)) || (_690.z < 0)) || (_690.x >= int(_56.Width))) || (_690.y >= int(_56.Height))) || (_690.z >= int(_56.Depth)))
                {
                    _3035 = 0.0;
                    break;
                }
                _3035 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_690, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            _3045 = _3031 + (1.0 / clamp((_2975 - 0.5) / max(_2975 - _3035, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0));
            _3043 = _3029;
            break;
        } while(false);
        ivec3 _698 = _611 + ivec3(0, 0, -1);
        float _3057;
        float _3059;
        do
        {
            if ((_56.OpenCeiling > 0.5) && (((((_698.y >= int(_56.Height)) && (_698.x >= 0)) && (_698.x < int(_56.Width))) && (_698.z >= 0)) && (_698.z < int(_56.Depth))))
            {
                _3059 = _3045 + 1.0;
                _3057 = _3043;
                break;
            }
            bool _3046;
            do
            {
                if ((((((_698.x < 0) || (_698.y < 0)) || (_698.z < 0)) || (_698.x >= int(_56.Width))) || (_698.y >= int(_56.Height))) || (_698.z >= int(_56.Depth)))
                {
                    _3046 = true;
                    break;
                }
                _3046 = WallMask_1._data[uint(_698.x) + (_56.Width * (uint(_698.y) + (_56.Height * uint(_698.z))))] != 0u;
                break;
            } while(false);
            if (_3046)
            {
                _3059 = _3045;
                _3057 = _3043;
                break;
            }
            bool _3048;
            do
            {
                bool _3047;
                do
                {
                    if ((((((_698.x < 0) || (_698.y < 0)) || (_698.z < 0)) || (_698.x >= int(_56.Width))) || (_698.y >= int(_56.Height))) || (_698.z >= int(_56.Depth)))
                    {
                        _3047 = true;
                        break;
                    }
                    _3047 = WallMask_1._data[uint(_698.x) + (_56.Width * (uint(_698.y) + (_56.Height * uint(_698.z))))] != 0u;
                    break;
                } while(false);
                if (_3047)
                {
                    _3048 = false;
                    break;
                }
                _3048 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_698, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
                break;
            } while(false);
            if (_3048)
            {
                _3059 = _3045 + 1.0;
                _3057 = _3043 + texelFetch(SPIRV_Cross_CombinedPressureInSPIRV_Cross_DummySampler, ivec4(_698, 0).xyz, 0).x;
                break;
            }
            float _3049;
            do
            {
                if ((((((_698.x < 0) || (_698.y < 0)) || (_698.z < 0)) || (_698.x >= int(_56.Width))) || (_698.y >= int(_56.Height))) || (_698.z >= int(_56.Depth)))
                {
                    _3049 = 0.0;
                    break;
                }
                _3049 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_698, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            _3059 = _3045 + (1.0 / clamp((_2975 - 0.5) / max(_2975 - _3049, 9.9999997473787516355514526367188e-05), 0.100000001490116119384765625, 1.0));
            _3057 = _3043;
            break;
        } while(false);
        imageStore(PressureOut, ivec3(gl_GlobalInvocationID), vec4(mix(_634, (_3059 > 0.0) ? ((_3057 - (texelFetch(SPIRV_Cross_CombinedDivergenceInSPIRV_Cross_DummySampler, ivec4(_611, 0).xyz, 0).x * _56.PressureScale)) / _3059) : 0.0, clamp(_56.Padding0, 0.0500000007450580596923828125, 1.9900000095367431640625))));
        break;
    } while(false);
}

