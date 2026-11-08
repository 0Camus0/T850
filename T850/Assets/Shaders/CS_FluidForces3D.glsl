#version 430
// GENERATED from CS_FluidForces3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=643bf4ffd1f534782244b3947889f34431d599c88d362fa04abc341f1ee258c6; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
    float ForcingAmplitude;
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
} _59;

layout(binding = 3, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 2, rgba16f) uniform writeonly image3D VelocityOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;
layout(binding = 4) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _59.Width) || (gl_GlobalInvocationID.y >= _59.Height)) || (gl_GlobalInvocationID.z >= _59.Depth))
        {
            break;
        }
        ivec3 _895 = ivec3(gl_GlobalInvocationID);
        vec4 _909 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_895, 0).xyz, 0);
        float _921 = min((_59.Viscosity * _59.DeltaTime) / (_59.CellSize * _59.CellSize), 0.16666667163372039794921875);
        bool _7356;
        do
        {
            bool _7354;
            do
            {
                if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7354 = true;
                    break;
                }
                _7354 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            if (_7354)
            {
                _7356 = true;
                break;
            }
            int _1157 = _895.x + 1;
            bool _7355;
            do
            {
                if ((((((_1157 < 0) || (_895.y < 0)) || (_895.z < 0)) || (_1157 >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7355 = true;
                    break;
                }
                _7355 = WallMask_1._data[uint(_1157) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            _7356 = _7355;
            break;
        } while(false);
        float _7459;
        if (!_7356)
        {
            float _930 = _909.x;
            ivec3 _1332 = _895 + ivec3(1, 0, 0);
            float _7357;
            do
            {
                if ((_1332.x < 0) || (_1332.x > (int(_59.Width) - 1)))
                {
                    _7357 = _930;
                    break;
                }
                _7357 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1332.x, clamp(_1332.y, 0, int(_59.Height) - 1), clamp(_1332.z, 0, int(_59.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1339 = _895 - ivec3(1, 0, 0);
            float _7359;
            do
            {
                if ((_1339.x < 0) || (_1339.x > (int(_59.Width) - 1)))
                {
                    _7359 = _930;
                    break;
                }
                _7359 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1339.x, clamp(_1339.y, 0, int(_59.Height) - 1), clamp(_1339.z, 0, int(_59.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1346 = _895 + ivec3(0, 1, 0);
            float _7361;
            do
            {
                if ((_1346.x < 0) || (_1346.x > (int(_59.Width) - 1)))
                {
                    _7361 = _930;
                    break;
                }
                _7361 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1346.x, clamp(_1346.y, 0, int(_59.Height) - 1), clamp(_1346.z, 0, int(_59.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1353 = _895 - ivec3(0, 1, 0);
            float _7363;
            do
            {
                if ((_1353.x < 0) || (_1353.x > (int(_59.Width) - 1)))
                {
                    _7363 = _930;
                    break;
                }
                _7363 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1353.x, clamp(_1353.y, 0, int(_59.Height) - 1), clamp(_1353.z, 0, int(_59.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1360 = _895 + ivec3(0, 0, 1);
            float _7365;
            do
            {
                if ((_1360.x < 0) || (_1360.x > (int(_59.Width) - 1)))
                {
                    _7365 = _930;
                    break;
                }
                _7365 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1360.x, clamp(_1360.y, 0, int(_59.Height) - 1), clamp(_1360.z, 0, int(_59.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1367 = _895 - ivec3(0, 0, 1);
            float _7367;
            do
            {
                if ((_1367.x < 0) || (_1367.x > (int(_59.Width) - 1)))
                {
                    _7367 = _930;
                    break;
                }
                _7367 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1367.x, clamp(_1367.y, 0, int(_59.Height) - 1), clamp(_1367.z, 0, int(_59.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            _7459 = _909.x + (((((((_7357 + _7359) + _7361) + _7363) + _7365) + _7367) - (6.0 * _930)) * _921);
        }
        else
        {
            _7459 = 0.0;
        }
        bool _7371;
        do
        {
            bool _7369;
            do
            {
                if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7369 = true;
                    break;
                }
                _7369 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            if (_7369)
            {
                _7371 = true;
                break;
            }
            int _2315 = _895.y + 1;
            bool _7370;
            do
            {
                if ((((((_895.x < 0) || (_2315 < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_2315 >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7370 = true;
                    break;
                }
                _7370 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_2315) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            _7371 = _7370;
            break;
        } while(false);
        float _7407;
        if (!_7371)
        {
            float _944 = _909.y;
            ivec3 _2482 = _895 + ivec3(1, 0, 0);
            float _7372;
            do
            {
                if ((_2482.y < 0) || (_2482.y > (int(_59.Height) - 1)))
                {
                    _7372 = _944;
                    break;
                }
                _7372 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2482.x, 0, int(_59.Width) - 1), _2482.y, clamp(_2482.z, 0, int(_59.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2489 = _895 - ivec3(1, 0, 0);
            float _7374;
            do
            {
                if ((_2489.y < 0) || (_2489.y > (int(_59.Height) - 1)))
                {
                    _7374 = _944;
                    break;
                }
                _7374 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2489.x, 0, int(_59.Width) - 1), _2489.y, clamp(_2489.z, 0, int(_59.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2496 = _895 + ivec3(0, 1, 0);
            float _7376;
            do
            {
                if ((_2496.y < 0) || (_2496.y > (int(_59.Height) - 1)))
                {
                    _7376 = _944;
                    break;
                }
                _7376 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2496.x, 0, int(_59.Width) - 1), _2496.y, clamp(_2496.z, 0, int(_59.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2503 = _895 - ivec3(0, 1, 0);
            float _7378;
            do
            {
                if ((_2503.y < 0) || (_2503.y > (int(_59.Height) - 1)))
                {
                    _7378 = _944;
                    break;
                }
                _7378 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2503.x, 0, int(_59.Width) - 1), _2503.y, clamp(_2503.z, 0, int(_59.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2510 = _895 + ivec3(0, 0, 1);
            float _7380;
            do
            {
                if ((_2510.y < 0) || (_2510.y > (int(_59.Height) - 1)))
                {
                    _7380 = _944;
                    break;
                }
                _7380 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2510.x, 0, int(_59.Width) - 1), _2510.y, clamp(_2510.z, 0, int(_59.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2517 = _895 - ivec3(0, 0, 1);
            float _7382;
            do
            {
                if ((_2517.y < 0) || (_2517.y > (int(_59.Height) - 1)))
                {
                    _7382 = _944;
                    break;
                }
                _7382 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2517.x, 0, int(_59.Width) - 1), _2517.y, clamp(_2517.z, 0, int(_59.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            _7407 = _909.y + (((((((_7372 + _7374) + _7376) + _7378) + _7380) + _7382) - (6.0 * _944)) * _921);
        }
        else
        {
            _7407 = 0.0;
        }
        bool _7386;
        do
        {
            bool _7384;
            do
            {
                if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7384 = true;
                    break;
                }
                _7384 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            if (_7384)
            {
                _7386 = true;
                break;
            }
            int _3470 = _895.z + 1;
            bool _7385;
            do
            {
                if ((((((_895.x < 0) || (_895.y < 0)) || (_3470 < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_3470 >= int(_59.Depth)))
                {
                    _7385 = true;
                    break;
                }
                _7385 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_3470))))] != 0u;
                break;
            } while(false);
            _7386 = _7385;
            break;
        } while(false);
        float _7514;
        if (!_7386)
        {
            float _958 = _909.z;
            ivec3 _3632 = _895 + ivec3(1, 0, 0);
            float _7387;
            do
            {
                if ((_3632.z < 0) || (_3632.z > (int(_59.Depth) - 1)))
                {
                    _7387 = _958;
                    break;
                }
                _7387 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3632.x, 0, int(_59.Width) - 1), clamp(_3632.y, 0, int(_59.Height) - 1), _3632.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3639 = _895 - ivec3(1, 0, 0);
            float _7389;
            do
            {
                if ((_3639.z < 0) || (_3639.z > (int(_59.Depth) - 1)))
                {
                    _7389 = _958;
                    break;
                }
                _7389 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3639.x, 0, int(_59.Width) - 1), clamp(_3639.y, 0, int(_59.Height) - 1), _3639.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3646 = _895 + ivec3(0, 1, 0);
            float _7391;
            do
            {
                if ((_3646.z < 0) || (_3646.z > (int(_59.Depth) - 1)))
                {
                    _7391 = _958;
                    break;
                }
                _7391 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3646.x, 0, int(_59.Width) - 1), clamp(_3646.y, 0, int(_59.Height) - 1), _3646.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3653 = _895 - ivec3(0, 1, 0);
            float _7393;
            do
            {
                if ((_3653.z < 0) || (_3653.z > (int(_59.Depth) - 1)))
                {
                    _7393 = _958;
                    break;
                }
                _7393 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3653.x, 0, int(_59.Width) - 1), clamp(_3653.y, 0, int(_59.Height) - 1), _3653.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3660 = _895 + ivec3(0, 0, 1);
            float _7395;
            do
            {
                if ((_3660.z < 0) || (_3660.z > (int(_59.Depth) - 1)))
                {
                    _7395 = _958;
                    break;
                }
                _7395 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3660.x, 0, int(_59.Width) - 1), clamp(_3660.y, 0, int(_59.Height) - 1), _3660.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3667 = _895 - ivec3(0, 0, 1);
            float _7397;
            do
            {
                if ((_3667.z < 0) || (_3667.z > (int(_59.Depth) - 1)))
                {
                    _7397 = _958;
                    break;
                }
                _7397 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3667.x, 0, int(_59.Width) - 1), clamp(_3667.y, 0, int(_59.Height) - 1), _3667.z, 0).xyz, 0).z;
                break;
            } while(false);
            _7514 = _909.z + (((((((_7387 + _7389) + _7391) + _7393) + _7395) + _7397) - (6.0 * _958)) * _921);
        }
        else
        {
            _7514 = 0.0;
        }
        bool _7401;
        do
        {
            bool _7399;
            do
            {
                if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7399 = true;
                    break;
                }
                _7399 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            if (_7399)
            {
                _7401 = true;
                break;
            }
            int _4615 = _895.y + 1;
            bool _7400;
            do
            {
                if ((((((_895.x < 0) || (_4615 < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_4615 >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                {
                    _7400 = true;
                    break;
                }
                _7400 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_4615) + (_59.Height * uint(_895.z))))] != 0u;
                break;
            } while(false);
            _7401 = _7400;
            break;
        } while(false);
        float _7585;
        if (!_7401)
        {
            _7585 = _7407 - (_59.Gravity * _59.DeltaTime);
        }
        else
        {
            _7585 = _7407;
        }
        float _981 = length(vec2(_59.WindX, _59.WindZ));
        float _7546;
        float _7569;
        if (_981 > 0.0)
        {
            float _990 = (0.00156000000424683094024658203125 * _981) / (1000.0 * _59.CellSize);
            bool _7431;
            do
            {
                bool _7429;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7429 = true;
                        break;
                    }
                    _7429 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7429)
                {
                    _7431 = true;
                    break;
                }
                int _4778 = _895.x + 1;
                bool _7430;
                do
                {
                    if ((((((_4778 < 0) || (_895.y < 0)) || (_895.z < 0)) || (_4778 >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7430 = true;
                        break;
                    }
                    _7430 = WallMask_1._data[uint(_4778) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                _7431 = _7430;
                break;
            } while(false);
            float _7433;
            do
            {
                bool _7432;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7432 = true;
                        break;
                    }
                    _7432 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7432)
                {
                    _7433 = 0.0;
                    break;
                }
                _7433 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_895, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            ivec3 _4943 = _895 + ivec3(1, 0, 0);
            float _7435;
            do
            {
                bool _7434;
                do
                {
                    if ((((((_4943.x < 0) || (_4943.y < 0)) || (_4943.z < 0)) || (_4943.x >= int(_59.Width))) || (_4943.y >= int(_59.Height))) || (_4943.z >= int(_59.Depth)))
                    {
                        _7434 = true;
                        break;
                    }
                    _7434 = WallMask_1._data[uint(_4943.x) + (_59.Width * (uint(_4943.y) + (_59.Height * uint(_4943.z))))] != 0u;
                    break;
                } while(false);
                if (_7434)
                {
                    _7435 = 0.0;
                    break;
                }
                _7435 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4943, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            ivec3 _4948 = _895 + ivec3(0, 1, 0);
            float _7437;
            do
            {
                bool _7436;
                do
                {
                    if ((((((_4948.x < 0) || (_4948.y < 0)) || (_4948.z < 0)) || (_4948.x >= int(_59.Width))) || (_4948.y >= int(_59.Height))) || (_4948.z >= int(_59.Depth)))
                    {
                        _7436 = true;
                        break;
                    }
                    _7436 = WallMask_1._data[uint(_4948.x) + (_59.Width * (uint(_4948.y) + (_59.Height * uint(_4948.z))))] != 0u;
                    break;
                } while(false);
                if (_7436)
                {
                    _7437 = 0.0;
                    break;
                }
                _7437 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4948, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            ivec3 _4953 = _895 + ivec3(1, 1, 0);
            float _7439;
            do
            {
                bool _7438;
                do
                {
                    if ((((((_4953.x < 0) || (_4953.y < 0)) || (_4953.z < 0)) || (_4953.x >= int(_59.Width))) || (_4953.y >= int(_59.Height))) || (_4953.z >= int(_59.Depth)))
                    {
                        _7438 = true;
                        break;
                    }
                    _7438 = WallMask_1._data[uint(_4953.x) + (_59.Width * (uint(_4953.y) + (_59.Height * uint(_4953.z))))] != 0u;
                    break;
                } while(false);
                if (_7438)
                {
                    _7439 = 0.0;
                    break;
                }
                _7439 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_4953, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _7558;
            if ((!_7431) && (((0.5 * (_7433 + _7435)) >= 0.5) && ((0.5 * (_7437 + _7439)) < 0.5)))
            {
                _7558 = _7459 + ((_990 * _59.WindX) * _59.DeltaTime);
            }
            else
            {
                _7558 = _7459;
            }
            bool _7481;
            do
            {
                bool _7479;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7479 = true;
                        break;
                    }
                    _7479 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7479)
                {
                    _7481 = true;
                    break;
                }
                int _5369 = _895.z + 1;
                bool _7480;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_5369 < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_5369 >= int(_59.Depth)))
                    {
                        _7480 = true;
                        break;
                    }
                    _7480 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_5369))))] != 0u;
                    break;
                } while(false);
                _7481 = _7480;
                break;
            } while(false);
            float _7483;
            do
            {
                bool _7482;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7482 = true;
                        break;
                    }
                    _7482 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7482)
                {
                    _7483 = 0.0;
                    break;
                }
                _7483 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_895, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            ivec3 _5521 = _895 + ivec3(0, 0, 1);
            float _7485;
            do
            {
                bool _7484;
                do
                {
                    if ((((((_5521.x < 0) || (_5521.y < 0)) || (_5521.z < 0)) || (_5521.x >= int(_59.Width))) || (_5521.y >= int(_59.Height))) || (_5521.z >= int(_59.Depth)))
                    {
                        _7484 = true;
                        break;
                    }
                    _7484 = WallMask_1._data[uint(_5521.x) + (_59.Width * (uint(_5521.y) + (_59.Height * uint(_5521.z))))] != 0u;
                    break;
                } while(false);
                if (_7484)
                {
                    _7485 = 0.0;
                    break;
                }
                _7485 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5521, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            ivec3 _5526 = _895 + ivec3(0, 1, 0);
            float _7487;
            do
            {
                bool _7486;
                do
                {
                    if ((((((_5526.x < 0) || (_5526.y < 0)) || (_5526.z < 0)) || (_5526.x >= int(_59.Width))) || (_5526.y >= int(_59.Height))) || (_5526.z >= int(_59.Depth)))
                    {
                        _7486 = true;
                        break;
                    }
                    _7486 = WallMask_1._data[uint(_5526.x) + (_59.Width * (uint(_5526.y) + (_59.Height * uint(_5526.z))))] != 0u;
                    break;
                } while(false);
                if (_7486)
                {
                    _7487 = 0.0;
                    break;
                }
                _7487 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5526, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            ivec3 _5531 = _895 + ivec3(0, 1, 1);
            float _7489;
            do
            {
                bool _7488;
                do
                {
                    if ((((((_5531.x < 0) || (_5531.y < 0)) || (_5531.z < 0)) || (_5531.x >= int(_59.Width))) || (_5531.y >= int(_59.Height))) || (_5531.z >= int(_59.Depth)))
                    {
                        _7488 = true;
                        break;
                    }
                    _7488 = WallMask_1._data[uint(_5531.x) + (_59.Width * (uint(_5531.y) + (_59.Height * uint(_5531.z))))] != 0u;
                    break;
                } while(false);
                if (_7488)
                {
                    _7489 = 0.0;
                    break;
                }
                _7489 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_5531, 0).xyz, 0).x, 0.0, 1.0);
                break;
            } while(false);
            float _7570;
            if ((!_7481) && (((0.5 * (_7483 + _7485)) >= 0.5) && ((0.5 * (_7487 + _7489)) < 0.5)))
            {
                _7570 = _7514 + ((_990 * _59.WindZ) * _59.DeltaTime);
            }
            else
            {
                _7570 = _7514;
            }
            _7569 = _7570;
            _7546 = _7558;
        }
        else
        {
            _7569 = _7514;
            _7546 = _7459;
        }
        float _7613;
        float _7623;
        float _7625;
        if (float(gl_GlobalInvocationID.y) > ((_59.SurfaceLevel * float(_59.Height)) - 2.0))
        {
            float _1044 = max(_59.ForcingAmplitude, 0.0) * _59.DeltaTime;
            float _1058 = sin(((_59.TimeValue * 0.89999997615814208984375) + (float(gl_GlobalInvocationID.x) * 0.310000002384185791015625)) + (float(gl_GlobalInvocationID.z) * 0.17000000178813934326171875));
            float _1072 = cos(((_59.TimeValue * 0.699999988079071044921875) + (float(gl_GlobalInvocationID.z) * 0.2899999916553497314453125)) + (float(gl_GlobalInvocationID.x) * 0.12999999523162841796875));
            bool _7543;
            do
            {
                bool _7541;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7541 = true;
                        break;
                    }
                    _7541 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7541)
                {
                    _7543 = true;
                    break;
                }
                int _5934 = _895.x + 1;
                bool _7542;
                do
                {
                    if ((((((_5934 < 0) || (_895.y < 0)) || (_895.z < 0)) || (_5934 >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7542 = true;
                        break;
                    }
                    _7542 = WallMask_1._data[uint(_5934) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                _7543 = _7542;
                break;
            } while(false);
            float _7620;
            if (!_7543)
            {
                _7620 = _7546 + ((_1058 * 0.449999988079071044921875) * _1044);
            }
            else
            {
                _7620 = _7546;
            }
            bool _7563;
            do
            {
                bool _7561;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7561 = true;
                        break;
                    }
                    _7561 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7561)
                {
                    _7563 = true;
                    break;
                }
                int _6118 = _895.z + 1;
                bool _7562;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_6118 < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_6118 >= int(_59.Depth)))
                    {
                        _7562 = true;
                        break;
                    }
                    _7562 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_6118))))] != 0u;
                    break;
                } while(false);
                _7563 = _7562;
                break;
            } while(false);
            float _7629;
            if (!_7563)
            {
                _7629 = _7569 + ((_1072 * 0.449999988079071044921875) * _1044);
            }
            else
            {
                _7629 = _7569;
            }
            bool _7575;
            do
            {
                bool _7573;
                do
                {
                    if ((((((_895.x < 0) || (_895.y < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_895.y >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7573 = true;
                        break;
                    }
                    _7573 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_895.y) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                if (_7573)
                {
                    _7575 = true;
                    break;
                }
                int _6284 = _895.y + 1;
                bool _7574;
                do
                {
                    if ((((((_895.x < 0) || (_6284 < 0)) || (_895.z < 0)) || (_895.x >= int(_59.Width))) || (_6284 >= int(_59.Height))) || (_895.z >= int(_59.Depth)))
                    {
                        _7574 = true;
                        break;
                    }
                    _7574 = WallMask_1._data[uint(_895.x) + (_59.Width * (uint(_6284) + (_59.Height * uint(_895.z))))] != 0u;
                    break;
                } while(false);
                _7575 = _7574;
                break;
            } while(false);
            float _7624;
            if (!_7575)
            {
                _7624 = _7585 + (((_1058 * _1072) * 0.25) * _1044);
            }
            else
            {
                _7624 = _7585;
            }
            _7625 = _7629;
            _7623 = _7624;
            _7613 = _7620;
        }
        else
        {
            _7625 = _7569;
            _7623 = _7585;
            _7613 = _7546;
        }
        float _1115 = clamp(1.0 - (_59.Damping * _59.DeltaTime), 0.0, 1.0);
        imageStore(VelocityOut, ivec3(gl_GlobalInvocationID), vec4(clamp(_7613 * _1115, -12.0, 12.0), clamp(_7623 * _1115, -12.0, 12.0), clamp(_7625 * _1115, -12.0, 12.0), 0.0));
        break;
    } while(false);
}

