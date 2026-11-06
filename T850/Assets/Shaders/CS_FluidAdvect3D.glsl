#version 430
// GENERATED from CS_FluidAdvect3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=484209f2d01f3821b4c1d0b35a12c7a9b6f3551ea6c9272bb5bad15f4d378b15; defines=T850_SPIRV=1,T850_VULKAN=1
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
} _58;

layout(binding = 3, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 2, rgba16f) uniform writeonly image3D VelocityOut;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _58.Width) || (gl_GlobalInvocationID.y >= _58.Height)) || (gl_GlobalInvocationID.z >= _58.Depth))
        {
            break;
        }
        ivec3 _719 = ivec3(gl_GlobalInvocationID);
        bool _20590;
        do
        {
            bool _20588;
            do
            {
                if ((((((_719.x < 0) || (_719.y < 0)) || (_719.z < 0)) || (_719.x >= int(_58.Width))) || (_719.y >= int(_58.Height))) || (_719.z >= int(_58.Depth)))
                {
                    _20588 = true;
                    break;
                }
                _20588 = WallMask_1._data[uint(_719.x) + (_58.Width * (uint(_719.y) + (_58.Height * uint(_719.z))))] != 0u;
                break;
            } while(false);
            if (_20588)
            {
                _20590 = true;
                break;
            }
            int _839 = _719.x + 1;
            bool _20589;
            do
            {
                if ((((((_839 < 0) || (_719.y < 0)) || (_719.z < 0)) || (_839 >= int(_58.Width))) || (_719.y >= int(_58.Height))) || (_719.z >= int(_58.Depth)))
                {
                    _20589 = true;
                    break;
                }
                _20589 = WallMask_1._data[uint(_839) + (_58.Width * (uint(_719.y) + (_58.Height * uint(_719.z))))] != 0u;
                break;
            } while(false);
            _20590 = _20589;
            break;
        } while(false);
        float _20699;
        if (!_20590)
        {
            vec3 _736 = vec3(float(_719.x) + 1.0, float(_719.y) + 0.5, float(_719.z) + 0.5);
            vec3 _1047 = _736 - vec3(1.0, 0.5, 0.5);
            vec3 _1049 = floor(_1047);
            vec3 _1052 = _1047 - _1049;
            ivec3 _1054 = ivec3(_1049);
            float _20591;
            do
            {
                if ((_1054.x < 0) || (_1054.x > (int(_58.Width) - 1)))
                {
                    _20591 = 0.0;
                    break;
                }
                _20591 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1054.x, clamp(_1054.y, 0, int(_58.Height) - 1), clamp(_1054.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1060 = _1054 + ivec3(1, 0, 0);
            float _20592;
            do
            {
                if ((_1060.x < 0) || (_1060.x > (int(_58.Width) - 1)))
                {
                    _20592 = 0.0;
                    break;
                }
                _20592 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1060.x, clamp(_1060.y, 0, int(_58.Height) - 1), clamp(_1060.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1064 = _1054 + ivec3(0, 1, 0);
            float _20593;
            do
            {
                if ((_1064.x < 0) || (_1064.x > (int(_58.Width) - 1)))
                {
                    _20593 = 0.0;
                    break;
                }
                _20593 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1064.x, clamp(_1064.y, 0, int(_58.Height) - 1), clamp(_1064.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1068 = _1054 + ivec3(1, 1, 0);
            float _20594;
            do
            {
                if ((_1068.x < 0) || (_1068.x > (int(_58.Width) - 1)))
                {
                    _20594 = 0.0;
                    break;
                }
                _20594 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1068.x, clamp(_1068.y, 0, int(_58.Height) - 1), clamp(_1068.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1072 = _1054 + ivec3(0, 0, 1);
            float _20595;
            do
            {
                if ((_1072.x < 0) || (_1072.x > (int(_58.Width) - 1)))
                {
                    _20595 = 0.0;
                    break;
                }
                _20595 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1072.x, clamp(_1072.y, 0, int(_58.Height) - 1), clamp(_1072.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1076 = _1054 + ivec3(1, 0, 1);
            float _20596;
            do
            {
                if ((_1076.x < 0) || (_1076.x > (int(_58.Width) - 1)))
                {
                    _20596 = 0.0;
                    break;
                }
                _20596 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1076.x, clamp(_1076.y, 0, int(_58.Height) - 1), clamp(_1076.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1080 = _1054 + ivec3(0, 1, 1);
            float _20597;
            do
            {
                if ((_1080.x < 0) || (_1080.x > (int(_58.Width) - 1)))
                {
                    _20597 = 0.0;
                    break;
                }
                _20597 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1080.x, clamp(_1080.y, 0, int(_58.Height) - 1), clamp(_1080.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _1084 = _1054 + ivec3(1);
            float _20598;
            do
            {
                if ((_1084.x < 0) || (_1084.x > (int(_58.Width) - 1)))
                {
                    _20598 = 0.0;
                    break;
                }
                _20598 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_1084.x, clamp(_1084.y, 0, int(_58.Height) - 1), clamp(_1084.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            vec3 _2369 = _736 - vec3(0.5, 1.0, 0.5);
            vec3 _2371 = floor(_2369);
            vec3 _2374 = _2369 - _2371;
            ivec3 _2376 = ivec3(_2371);
            float _20599;
            do
            {
                if ((_2376.y < 0) || (_2376.y > (int(_58.Height) - 1)))
                {
                    _20599 = 0.0;
                    break;
                }
                _20599 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2376.x, 0, int(_58.Width) - 1), _2376.y, clamp(_2376.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2382 = _2376 + ivec3(1, 0, 0);
            float _20600;
            do
            {
                if ((_2382.y < 0) || (_2382.y > (int(_58.Height) - 1)))
                {
                    _20600 = 0.0;
                    break;
                }
                _20600 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2382.x, 0, int(_58.Width) - 1), _2382.y, clamp(_2382.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2386 = _2376 + ivec3(0, 1, 0);
            float _20601;
            do
            {
                if ((_2386.y < 0) || (_2386.y > (int(_58.Height) - 1)))
                {
                    _20601 = 0.0;
                    break;
                }
                _20601 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2386.x, 0, int(_58.Width) - 1), _2386.y, clamp(_2386.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2390 = _2376 + ivec3(1, 1, 0);
            float _20602;
            do
            {
                if ((_2390.y < 0) || (_2390.y > (int(_58.Height) - 1)))
                {
                    _20602 = 0.0;
                    break;
                }
                _20602 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2390.x, 0, int(_58.Width) - 1), _2390.y, clamp(_2390.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2394 = _2376 + ivec3(0, 0, 1);
            float _20603;
            do
            {
                if ((_2394.y < 0) || (_2394.y > (int(_58.Height) - 1)))
                {
                    _20603 = 0.0;
                    break;
                }
                _20603 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2394.x, 0, int(_58.Width) - 1), _2394.y, clamp(_2394.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2398 = _2376 + ivec3(1, 0, 1);
            float _20604;
            do
            {
                if ((_2398.y < 0) || (_2398.y > (int(_58.Height) - 1)))
                {
                    _20604 = 0.0;
                    break;
                }
                _20604 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2398.x, 0, int(_58.Width) - 1), _2398.y, clamp(_2398.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2402 = _2376 + ivec3(0, 1, 1);
            float _20605;
            do
            {
                if ((_2402.y < 0) || (_2402.y > (int(_58.Height) - 1)))
                {
                    _20605 = 0.0;
                    break;
                }
                _20605 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2402.x, 0, int(_58.Width) - 1), _2402.y, clamp(_2402.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _2406 = _2376 + ivec3(1);
            float _20606;
            do
            {
                if ((_2406.y < 0) || (_2406.y > (int(_58.Height) - 1)))
                {
                    _20606 = 0.0;
                    break;
                }
                _20606 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_2406.x, 0, int(_58.Width) - 1), _2406.y, clamp(_2406.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            vec3 _3691 = _736 - vec3(0.5, 0.5, 1.0);
            vec3 _3693 = floor(_3691);
            vec3 _3696 = _3691 - _3693;
            ivec3 _3698 = ivec3(_3693);
            float _20607;
            do
            {
                if ((_3698.z < 0) || (_3698.z > (int(_58.Depth) - 1)))
                {
                    _20607 = 0.0;
                    break;
                }
                _20607 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3698.x, 0, int(_58.Width) - 1), clamp(_3698.y, 0, int(_58.Height) - 1), _3698.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3704 = _3698 + ivec3(1, 0, 0);
            float _20608;
            do
            {
                if ((_3704.z < 0) || (_3704.z > (int(_58.Depth) - 1)))
                {
                    _20608 = 0.0;
                    break;
                }
                _20608 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3704.x, 0, int(_58.Width) - 1), clamp(_3704.y, 0, int(_58.Height) - 1), _3704.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3708 = _3698 + ivec3(0, 1, 0);
            float _20609;
            do
            {
                if ((_3708.z < 0) || (_3708.z > (int(_58.Depth) - 1)))
                {
                    _20609 = 0.0;
                    break;
                }
                _20609 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3708.x, 0, int(_58.Width) - 1), clamp(_3708.y, 0, int(_58.Height) - 1), _3708.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3712 = _3698 + ivec3(1, 1, 0);
            float _20610;
            do
            {
                if ((_3712.z < 0) || (_3712.z > (int(_58.Depth) - 1)))
                {
                    _20610 = 0.0;
                    break;
                }
                _20610 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3712.x, 0, int(_58.Width) - 1), clamp(_3712.y, 0, int(_58.Height) - 1), _3712.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3716 = _3698 + ivec3(0, 0, 1);
            float _20611;
            do
            {
                if ((_3716.z < 0) || (_3716.z > (int(_58.Depth) - 1)))
                {
                    _20611 = 0.0;
                    break;
                }
                _20611 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3716.x, 0, int(_58.Width) - 1), clamp(_3716.y, 0, int(_58.Height) - 1), _3716.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3720 = _3698 + ivec3(1, 0, 1);
            float _20612;
            do
            {
                if ((_3720.z < 0) || (_3720.z > (int(_58.Depth) - 1)))
                {
                    _20612 = 0.0;
                    break;
                }
                _20612 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3720.x, 0, int(_58.Width) - 1), clamp(_3720.y, 0, int(_58.Height) - 1), _3720.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3724 = _3698 + ivec3(0, 1, 1);
            float _20613;
            do
            {
                if ((_3724.z < 0) || (_3724.z > (int(_58.Depth) - 1)))
                {
                    _20613 = 0.0;
                    break;
                }
                _20613 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3724.x, 0, int(_58.Width) - 1), clamp(_3724.y, 0, int(_58.Height) - 1), _3724.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _3728 = _3698 + ivec3(1);
            float _20614;
            do
            {
                if ((_3728.z < 0) || (_3728.z > (int(_58.Depth) - 1)))
                {
                    _20614 = 0.0;
                    break;
                }
                _20614 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_3728.x, 0, int(_58.Width) - 1), clamp(_3728.y, 0, int(_58.Height) - 1), _3728.z, 0).xyz, 0).z;
                break;
            } while(false);
            vec3 _5013 = (_736 - (vec3(mix(mix(mix(_20591, _20592, _1052.x), mix(_20593, _20594, _1052.x), _1052.y), mix(mix(_20595, _20596, _1052.x), mix(_20597, _20598, _1052.x), _1052.y), _1052.z), mix(mix(mix(_20599, _20600, _2374.x), mix(_20601, _20602, _2374.x), _2374.y), mix(mix(_20603, _20604, _2374.x), mix(_20605, _20606, _2374.x), _2374.y), _2374.z), mix(mix(mix(_20607, _20608, _3696.x), mix(_20609, _20610, _3696.x), _3696.y), mix(mix(_20611, _20612, _3696.x), mix(_20613, _20614, _3696.x), _3696.y), _3696.z)) * (_58.DeltaTime / _58.CellSize))) - vec3(1.0, 0.5, 0.5);
            vec3 _5015 = floor(_5013);
            vec3 _5018 = _5013 - _5015;
            ivec3 _5020 = ivec3(_5015);
            float _20615;
            do
            {
                if ((_5020.x < 0) || (_5020.x > (int(_58.Width) - 1)))
                {
                    _20615 = 0.0;
                    break;
                }
                _20615 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5020.x, clamp(_5020.y, 0, int(_58.Height) - 1), clamp(_5020.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5026 = _5020 + ivec3(1, 0, 0);
            float _20616;
            do
            {
                if ((_5026.x < 0) || (_5026.x > (int(_58.Width) - 1)))
                {
                    _20616 = 0.0;
                    break;
                }
                _20616 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5026.x, clamp(_5026.y, 0, int(_58.Height) - 1), clamp(_5026.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5030 = _5020 + ivec3(0, 1, 0);
            float _20617;
            do
            {
                if ((_5030.x < 0) || (_5030.x > (int(_58.Width) - 1)))
                {
                    _20617 = 0.0;
                    break;
                }
                _20617 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5030.x, clamp(_5030.y, 0, int(_58.Height) - 1), clamp(_5030.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5034 = _5020 + ivec3(1, 1, 0);
            float _20618;
            do
            {
                if ((_5034.x < 0) || (_5034.x > (int(_58.Width) - 1)))
                {
                    _20618 = 0.0;
                    break;
                }
                _20618 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5034.x, clamp(_5034.y, 0, int(_58.Height) - 1), clamp(_5034.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5038 = _5020 + ivec3(0, 0, 1);
            float _20619;
            do
            {
                if ((_5038.x < 0) || (_5038.x > (int(_58.Width) - 1)))
                {
                    _20619 = 0.0;
                    break;
                }
                _20619 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5038.x, clamp(_5038.y, 0, int(_58.Height) - 1), clamp(_5038.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5042 = _5020 + ivec3(1, 0, 1);
            float _20620;
            do
            {
                if ((_5042.x < 0) || (_5042.x > (int(_58.Width) - 1)))
                {
                    _20620 = 0.0;
                    break;
                }
                _20620 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5042.x, clamp(_5042.y, 0, int(_58.Height) - 1), clamp(_5042.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5046 = _5020 + ivec3(0, 1, 1);
            float _20621;
            do
            {
                if ((_5046.x < 0) || (_5046.x > (int(_58.Width) - 1)))
                {
                    _20621 = 0.0;
                    break;
                }
                _20621 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5046.x, clamp(_5046.y, 0, int(_58.Height) - 1), clamp(_5046.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _5050 = _5020 + ivec3(1);
            float _20622;
            do
            {
                if ((_5050.x < 0) || (_5050.x > (int(_58.Width) - 1)))
                {
                    _20622 = 0.0;
                    break;
                }
                _20622 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_5050.x, clamp(_5050.y, 0, int(_58.Height) - 1), clamp(_5050.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            _20699 = mix(mix(mix(_20615, _20616, _5018.x), mix(_20617, _20618, _5018.x), _5018.y), mix(mix(_20619, _20620, _5018.x), mix(_20621, _20622, _5018.x), _5018.y), _5018.z);
        }
        else
        {
            _20699 = 0.0;
        }
        bool _20625;
        do
        {
            bool _20623;
            do
            {
                if ((((((_719.x < 0) || (_719.y < 0)) || (_719.z < 0)) || (_719.x >= int(_58.Width))) || (_719.y >= int(_58.Height))) || (_719.z >= int(_58.Depth)))
                {
                    _20623 = true;
                    break;
                }
                _20623 = WallMask_1._data[uint(_719.x) + (_58.Width * (uint(_719.y) + (_58.Height * uint(_719.z))))] != 0u;
                break;
            } while(false);
            if (_20623)
            {
                _20625 = true;
                break;
            }
            int _6321 = _719.y + 1;
            bool _20624;
            do
            {
                if ((((((_719.x < 0) || (_6321 < 0)) || (_719.z < 0)) || (_719.x >= int(_58.Width))) || (_6321 >= int(_58.Height))) || (_719.z >= int(_58.Depth)))
                {
                    _20624 = true;
                    break;
                }
                _20624 = WallMask_1._data[uint(_719.x) + (_58.Width * (uint(_6321) + (_58.Height * uint(_719.z))))] != 0u;
                break;
            } while(false);
            _20625 = _20624;
            break;
        } while(false);
        float _20772;
        if (!_20625)
        {
            vec3 _767 = vec3(float(_719.x) + 0.5, float(_719.y) + 1.0, float(_719.z) + 0.5);
            vec3 _6521 = _767 - vec3(1.0, 0.5, 0.5);
            vec3 _6523 = floor(_6521);
            vec3 _6526 = _6521 - _6523;
            ivec3 _6528 = ivec3(_6523);
            float _20626;
            do
            {
                if ((_6528.x < 0) || (_6528.x > (int(_58.Width) - 1)))
                {
                    _20626 = 0.0;
                    break;
                }
                _20626 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6528.x, clamp(_6528.y, 0, int(_58.Height) - 1), clamp(_6528.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6534 = _6528 + ivec3(1, 0, 0);
            float _20627;
            do
            {
                if ((_6534.x < 0) || (_6534.x > (int(_58.Width) - 1)))
                {
                    _20627 = 0.0;
                    break;
                }
                _20627 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6534.x, clamp(_6534.y, 0, int(_58.Height) - 1), clamp(_6534.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6538 = _6528 + ivec3(0, 1, 0);
            float _20628;
            do
            {
                if ((_6538.x < 0) || (_6538.x > (int(_58.Width) - 1)))
                {
                    _20628 = 0.0;
                    break;
                }
                _20628 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6538.x, clamp(_6538.y, 0, int(_58.Height) - 1), clamp(_6538.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6542 = _6528 + ivec3(1, 1, 0);
            float _20629;
            do
            {
                if ((_6542.x < 0) || (_6542.x > (int(_58.Width) - 1)))
                {
                    _20629 = 0.0;
                    break;
                }
                _20629 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6542.x, clamp(_6542.y, 0, int(_58.Height) - 1), clamp(_6542.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6546 = _6528 + ivec3(0, 0, 1);
            float _20630;
            do
            {
                if ((_6546.x < 0) || (_6546.x > (int(_58.Width) - 1)))
                {
                    _20630 = 0.0;
                    break;
                }
                _20630 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6546.x, clamp(_6546.y, 0, int(_58.Height) - 1), clamp(_6546.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6550 = _6528 + ivec3(1, 0, 1);
            float _20631;
            do
            {
                if ((_6550.x < 0) || (_6550.x > (int(_58.Width) - 1)))
                {
                    _20631 = 0.0;
                    break;
                }
                _20631 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6550.x, clamp(_6550.y, 0, int(_58.Height) - 1), clamp(_6550.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6554 = _6528 + ivec3(0, 1, 1);
            float _20632;
            do
            {
                if ((_6554.x < 0) || (_6554.x > (int(_58.Width) - 1)))
                {
                    _20632 = 0.0;
                    break;
                }
                _20632 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6554.x, clamp(_6554.y, 0, int(_58.Height) - 1), clamp(_6554.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _6558 = _6528 + ivec3(1);
            float _20633;
            do
            {
                if ((_6558.x < 0) || (_6558.x > (int(_58.Width) - 1)))
                {
                    _20633 = 0.0;
                    break;
                }
                _20633 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_6558.x, clamp(_6558.y, 0, int(_58.Height) - 1), clamp(_6558.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            vec3 _7843 = _767 - vec3(0.5, 1.0, 0.5);
            vec3 _7845 = floor(_7843);
            vec3 _7848 = _7843 - _7845;
            ivec3 _7850 = ivec3(_7845);
            float _20634;
            do
            {
                if ((_7850.y < 0) || (_7850.y > (int(_58.Height) - 1)))
                {
                    _20634 = 0.0;
                    break;
                }
                _20634 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7850.x, 0, int(_58.Width) - 1), _7850.y, clamp(_7850.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7856 = _7850 + ivec3(1, 0, 0);
            float _20635;
            do
            {
                if ((_7856.y < 0) || (_7856.y > (int(_58.Height) - 1)))
                {
                    _20635 = 0.0;
                    break;
                }
                _20635 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7856.x, 0, int(_58.Width) - 1), _7856.y, clamp(_7856.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7860 = _7850 + ivec3(0, 1, 0);
            float _20636;
            do
            {
                if ((_7860.y < 0) || (_7860.y > (int(_58.Height) - 1)))
                {
                    _20636 = 0.0;
                    break;
                }
                _20636 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7860.x, 0, int(_58.Width) - 1), _7860.y, clamp(_7860.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7864 = _7850 + ivec3(1, 1, 0);
            float _20637;
            do
            {
                if ((_7864.y < 0) || (_7864.y > (int(_58.Height) - 1)))
                {
                    _20637 = 0.0;
                    break;
                }
                _20637 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7864.x, 0, int(_58.Width) - 1), _7864.y, clamp(_7864.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7868 = _7850 + ivec3(0, 0, 1);
            float _20638;
            do
            {
                if ((_7868.y < 0) || (_7868.y > (int(_58.Height) - 1)))
                {
                    _20638 = 0.0;
                    break;
                }
                _20638 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7868.x, 0, int(_58.Width) - 1), _7868.y, clamp(_7868.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7872 = _7850 + ivec3(1, 0, 1);
            float _20639;
            do
            {
                if ((_7872.y < 0) || (_7872.y > (int(_58.Height) - 1)))
                {
                    _20639 = 0.0;
                    break;
                }
                _20639 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7872.x, 0, int(_58.Width) - 1), _7872.y, clamp(_7872.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7876 = _7850 + ivec3(0, 1, 1);
            float _20640;
            do
            {
                if ((_7876.y < 0) || (_7876.y > (int(_58.Height) - 1)))
                {
                    _20640 = 0.0;
                    break;
                }
                _20640 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7876.x, 0, int(_58.Width) - 1), _7876.y, clamp(_7876.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _7880 = _7850 + ivec3(1);
            float _20641;
            do
            {
                if ((_7880.y < 0) || (_7880.y > (int(_58.Height) - 1)))
                {
                    _20641 = 0.0;
                    break;
                }
                _20641 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_7880.x, 0, int(_58.Width) - 1), _7880.y, clamp(_7880.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            vec3 _9165 = _767 - vec3(0.5, 0.5, 1.0);
            vec3 _9167 = floor(_9165);
            vec3 _9170 = _9165 - _9167;
            ivec3 _9172 = ivec3(_9167);
            float _20642;
            do
            {
                if ((_9172.z < 0) || (_9172.z > (int(_58.Depth) - 1)))
                {
                    _20642 = 0.0;
                    break;
                }
                _20642 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9172.x, 0, int(_58.Width) - 1), clamp(_9172.y, 0, int(_58.Height) - 1), _9172.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9178 = _9172 + ivec3(1, 0, 0);
            float _20643;
            do
            {
                if ((_9178.z < 0) || (_9178.z > (int(_58.Depth) - 1)))
                {
                    _20643 = 0.0;
                    break;
                }
                _20643 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9178.x, 0, int(_58.Width) - 1), clamp(_9178.y, 0, int(_58.Height) - 1), _9178.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9182 = _9172 + ivec3(0, 1, 0);
            float _20644;
            do
            {
                if ((_9182.z < 0) || (_9182.z > (int(_58.Depth) - 1)))
                {
                    _20644 = 0.0;
                    break;
                }
                _20644 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9182.x, 0, int(_58.Width) - 1), clamp(_9182.y, 0, int(_58.Height) - 1), _9182.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9186 = _9172 + ivec3(1, 1, 0);
            float _20645;
            do
            {
                if ((_9186.z < 0) || (_9186.z > (int(_58.Depth) - 1)))
                {
                    _20645 = 0.0;
                    break;
                }
                _20645 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9186.x, 0, int(_58.Width) - 1), clamp(_9186.y, 0, int(_58.Height) - 1), _9186.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9190 = _9172 + ivec3(0, 0, 1);
            float _20646;
            do
            {
                if ((_9190.z < 0) || (_9190.z > (int(_58.Depth) - 1)))
                {
                    _20646 = 0.0;
                    break;
                }
                _20646 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9190.x, 0, int(_58.Width) - 1), clamp(_9190.y, 0, int(_58.Height) - 1), _9190.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9194 = _9172 + ivec3(1, 0, 1);
            float _20647;
            do
            {
                if ((_9194.z < 0) || (_9194.z > (int(_58.Depth) - 1)))
                {
                    _20647 = 0.0;
                    break;
                }
                _20647 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9194.x, 0, int(_58.Width) - 1), clamp(_9194.y, 0, int(_58.Height) - 1), _9194.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9198 = _9172 + ivec3(0, 1, 1);
            float _20648;
            do
            {
                if ((_9198.z < 0) || (_9198.z > (int(_58.Depth) - 1)))
                {
                    _20648 = 0.0;
                    break;
                }
                _20648 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9198.x, 0, int(_58.Width) - 1), clamp(_9198.y, 0, int(_58.Height) - 1), _9198.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _9202 = _9172 + ivec3(1);
            float _20649;
            do
            {
                if ((_9202.z < 0) || (_9202.z > (int(_58.Depth) - 1)))
                {
                    _20649 = 0.0;
                    break;
                }
                _20649 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_9202.x, 0, int(_58.Width) - 1), clamp(_9202.y, 0, int(_58.Height) - 1), _9202.z, 0).xyz, 0).z;
                break;
            } while(false);
            vec3 _10487 = (_767 - (vec3(mix(mix(mix(_20626, _20627, _6526.x), mix(_20628, _20629, _6526.x), _6526.y), mix(mix(_20630, _20631, _6526.x), mix(_20632, _20633, _6526.x), _6526.y), _6526.z), mix(mix(mix(_20634, _20635, _7848.x), mix(_20636, _20637, _7848.x), _7848.y), mix(mix(_20638, _20639, _7848.x), mix(_20640, _20641, _7848.x), _7848.y), _7848.z), mix(mix(mix(_20642, _20643, _9170.x), mix(_20644, _20645, _9170.x), _9170.y), mix(mix(_20646, _20647, _9170.x), mix(_20648, _20649, _9170.x), _9170.y), _9170.z)) * (_58.DeltaTime / _58.CellSize))) - vec3(0.5, 1.0, 0.5);
            vec3 _10489 = floor(_10487);
            vec3 _10492 = _10487 - _10489;
            ivec3 _10494 = ivec3(_10489);
            float _20650;
            do
            {
                if ((_10494.y < 0) || (_10494.y > (int(_58.Height) - 1)))
                {
                    _20650 = 0.0;
                    break;
                }
                _20650 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10494.x, 0, int(_58.Width) - 1), _10494.y, clamp(_10494.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10500 = _10494 + ivec3(1, 0, 0);
            float _20651;
            do
            {
                if ((_10500.y < 0) || (_10500.y > (int(_58.Height) - 1)))
                {
                    _20651 = 0.0;
                    break;
                }
                _20651 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10500.x, 0, int(_58.Width) - 1), _10500.y, clamp(_10500.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10504 = _10494 + ivec3(0, 1, 0);
            float _20652;
            do
            {
                if ((_10504.y < 0) || (_10504.y > (int(_58.Height) - 1)))
                {
                    _20652 = 0.0;
                    break;
                }
                _20652 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10504.x, 0, int(_58.Width) - 1), _10504.y, clamp(_10504.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10508 = _10494 + ivec3(1, 1, 0);
            float _20653;
            do
            {
                if ((_10508.y < 0) || (_10508.y > (int(_58.Height) - 1)))
                {
                    _20653 = 0.0;
                    break;
                }
                _20653 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10508.x, 0, int(_58.Width) - 1), _10508.y, clamp(_10508.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10512 = _10494 + ivec3(0, 0, 1);
            float _20654;
            do
            {
                if ((_10512.y < 0) || (_10512.y > (int(_58.Height) - 1)))
                {
                    _20654 = 0.0;
                    break;
                }
                _20654 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10512.x, 0, int(_58.Width) - 1), _10512.y, clamp(_10512.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10516 = _10494 + ivec3(1, 0, 1);
            float _20655;
            do
            {
                if ((_10516.y < 0) || (_10516.y > (int(_58.Height) - 1)))
                {
                    _20655 = 0.0;
                    break;
                }
                _20655 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10516.x, 0, int(_58.Width) - 1), _10516.y, clamp(_10516.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10520 = _10494 + ivec3(0, 1, 1);
            float _20656;
            do
            {
                if ((_10520.y < 0) || (_10520.y > (int(_58.Height) - 1)))
                {
                    _20656 = 0.0;
                    break;
                }
                _20656 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10520.x, 0, int(_58.Width) - 1), _10520.y, clamp(_10520.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _10524 = _10494 + ivec3(1);
            float _20657;
            do
            {
                if ((_10524.y < 0) || (_10524.y > (int(_58.Height) - 1)))
                {
                    _20657 = 0.0;
                    break;
                }
                _20657 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_10524.x, 0, int(_58.Width) - 1), _10524.y, clamp(_10524.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            _20772 = mix(mix(mix(_20650, _20651, _10492.x), mix(_20652, _20653, _10492.x), _10492.y), mix(mix(_20654, _20655, _10492.x), mix(_20656, _20657, _10492.x), _10492.y), _10492.z);
        }
        else
        {
            _20772 = 0.0;
        }
        bool _20660;
        do
        {
            bool _20658;
            do
            {
                if ((((((_719.x < 0) || (_719.y < 0)) || (_719.z < 0)) || (_719.x >= int(_58.Width))) || (_719.y >= int(_58.Height))) || (_719.z >= int(_58.Depth)))
                {
                    _20658 = true;
                    break;
                }
                _20658 = WallMask_1._data[uint(_719.x) + (_58.Width * (uint(_719.y) + (_58.Height * uint(_719.z))))] != 0u;
                break;
            } while(false);
            if (_20658)
            {
                _20660 = true;
                break;
            }
            int _11800 = _719.z + 1;
            bool _20659;
            do
            {
                if ((((((_719.x < 0) || (_719.y < 0)) || (_11800 < 0)) || (_719.x >= int(_58.Width))) || (_719.y >= int(_58.Height))) || (_11800 >= int(_58.Depth)))
                {
                    _20659 = true;
                    break;
                }
                _20659 = WallMask_1._data[uint(_719.x) + (_58.Width * (uint(_719.y) + (_58.Height * uint(_11800))))] != 0u;
                break;
            } while(false);
            _20660 = _20659;
            break;
        } while(false);
        float _20845;
        if (!_20660)
        {
            vec3 _798 = vec3(float(_719.x) + 0.5, float(_719.y) + 0.5, float(_719.z) + 1.0);
            vec3 _11995 = _798 - vec3(1.0, 0.5, 0.5);
            vec3 _11997 = floor(_11995);
            vec3 _12000 = _11995 - _11997;
            ivec3 _12002 = ivec3(_11997);
            float _20661;
            do
            {
                if ((_12002.x < 0) || (_12002.x > (int(_58.Width) - 1)))
                {
                    _20661 = 0.0;
                    break;
                }
                _20661 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12002.x, clamp(_12002.y, 0, int(_58.Height) - 1), clamp(_12002.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12008 = _12002 + ivec3(1, 0, 0);
            float _20662;
            do
            {
                if ((_12008.x < 0) || (_12008.x > (int(_58.Width) - 1)))
                {
                    _20662 = 0.0;
                    break;
                }
                _20662 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12008.x, clamp(_12008.y, 0, int(_58.Height) - 1), clamp(_12008.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12012 = _12002 + ivec3(0, 1, 0);
            float _20663;
            do
            {
                if ((_12012.x < 0) || (_12012.x > (int(_58.Width) - 1)))
                {
                    _20663 = 0.0;
                    break;
                }
                _20663 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12012.x, clamp(_12012.y, 0, int(_58.Height) - 1), clamp(_12012.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12016 = _12002 + ivec3(1, 1, 0);
            float _20664;
            do
            {
                if ((_12016.x < 0) || (_12016.x > (int(_58.Width) - 1)))
                {
                    _20664 = 0.0;
                    break;
                }
                _20664 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12016.x, clamp(_12016.y, 0, int(_58.Height) - 1), clamp(_12016.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12020 = _12002 + ivec3(0, 0, 1);
            float _20665;
            do
            {
                if ((_12020.x < 0) || (_12020.x > (int(_58.Width) - 1)))
                {
                    _20665 = 0.0;
                    break;
                }
                _20665 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12020.x, clamp(_12020.y, 0, int(_58.Height) - 1), clamp(_12020.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12024 = _12002 + ivec3(1, 0, 1);
            float _20666;
            do
            {
                if ((_12024.x < 0) || (_12024.x > (int(_58.Width) - 1)))
                {
                    _20666 = 0.0;
                    break;
                }
                _20666 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12024.x, clamp(_12024.y, 0, int(_58.Height) - 1), clamp(_12024.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12028 = _12002 + ivec3(0, 1, 1);
            float _20667;
            do
            {
                if ((_12028.x < 0) || (_12028.x > (int(_58.Width) - 1)))
                {
                    _20667 = 0.0;
                    break;
                }
                _20667 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12028.x, clamp(_12028.y, 0, int(_58.Height) - 1), clamp(_12028.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            ivec3 _12032 = _12002 + ivec3(1);
            float _20668;
            do
            {
                if ((_12032.x < 0) || (_12032.x > (int(_58.Width) - 1)))
                {
                    _20668 = 0.0;
                    break;
                }
                _20668 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_12032.x, clamp(_12032.y, 0, int(_58.Height) - 1), clamp(_12032.z, 0, int(_58.Depth) - 1), 0).xyz, 0).x;
                break;
            } while(false);
            vec3 _13317 = _798 - vec3(0.5, 1.0, 0.5);
            vec3 _13319 = floor(_13317);
            vec3 _13322 = _13317 - _13319;
            ivec3 _13324 = ivec3(_13319);
            float _20669;
            do
            {
                if ((_13324.y < 0) || (_13324.y > (int(_58.Height) - 1)))
                {
                    _20669 = 0.0;
                    break;
                }
                _20669 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13324.x, 0, int(_58.Width) - 1), _13324.y, clamp(_13324.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13330 = _13324 + ivec3(1, 0, 0);
            float _20670;
            do
            {
                if ((_13330.y < 0) || (_13330.y > (int(_58.Height) - 1)))
                {
                    _20670 = 0.0;
                    break;
                }
                _20670 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13330.x, 0, int(_58.Width) - 1), _13330.y, clamp(_13330.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13334 = _13324 + ivec3(0, 1, 0);
            float _20671;
            do
            {
                if ((_13334.y < 0) || (_13334.y > (int(_58.Height) - 1)))
                {
                    _20671 = 0.0;
                    break;
                }
                _20671 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13334.x, 0, int(_58.Width) - 1), _13334.y, clamp(_13334.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13338 = _13324 + ivec3(1, 1, 0);
            float _20672;
            do
            {
                if ((_13338.y < 0) || (_13338.y > (int(_58.Height) - 1)))
                {
                    _20672 = 0.0;
                    break;
                }
                _20672 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13338.x, 0, int(_58.Width) - 1), _13338.y, clamp(_13338.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13342 = _13324 + ivec3(0, 0, 1);
            float _20673;
            do
            {
                if ((_13342.y < 0) || (_13342.y > (int(_58.Height) - 1)))
                {
                    _20673 = 0.0;
                    break;
                }
                _20673 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13342.x, 0, int(_58.Width) - 1), _13342.y, clamp(_13342.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13346 = _13324 + ivec3(1, 0, 1);
            float _20674;
            do
            {
                if ((_13346.y < 0) || (_13346.y > (int(_58.Height) - 1)))
                {
                    _20674 = 0.0;
                    break;
                }
                _20674 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13346.x, 0, int(_58.Width) - 1), _13346.y, clamp(_13346.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13350 = _13324 + ivec3(0, 1, 1);
            float _20675;
            do
            {
                if ((_13350.y < 0) || (_13350.y > (int(_58.Height) - 1)))
                {
                    _20675 = 0.0;
                    break;
                }
                _20675 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13350.x, 0, int(_58.Width) - 1), _13350.y, clamp(_13350.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            ivec3 _13354 = _13324 + ivec3(1);
            float _20676;
            do
            {
                if ((_13354.y < 0) || (_13354.y > (int(_58.Height) - 1)))
                {
                    _20676 = 0.0;
                    break;
                }
                _20676 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_13354.x, 0, int(_58.Width) - 1), _13354.y, clamp(_13354.z, 0, int(_58.Depth) - 1), 0).xyz, 0).y;
                break;
            } while(false);
            vec3 _14639 = _798 - vec3(0.5, 0.5, 1.0);
            vec3 _14641 = floor(_14639);
            vec3 _14644 = _14639 - _14641;
            ivec3 _14646 = ivec3(_14641);
            float _20677;
            do
            {
                if ((_14646.z < 0) || (_14646.z > (int(_58.Depth) - 1)))
                {
                    _20677 = 0.0;
                    break;
                }
                _20677 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14646.x, 0, int(_58.Width) - 1), clamp(_14646.y, 0, int(_58.Height) - 1), _14646.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14652 = _14646 + ivec3(1, 0, 0);
            float _20678;
            do
            {
                if ((_14652.z < 0) || (_14652.z > (int(_58.Depth) - 1)))
                {
                    _20678 = 0.0;
                    break;
                }
                _20678 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14652.x, 0, int(_58.Width) - 1), clamp(_14652.y, 0, int(_58.Height) - 1), _14652.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14656 = _14646 + ivec3(0, 1, 0);
            float _20679;
            do
            {
                if ((_14656.z < 0) || (_14656.z > (int(_58.Depth) - 1)))
                {
                    _20679 = 0.0;
                    break;
                }
                _20679 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14656.x, 0, int(_58.Width) - 1), clamp(_14656.y, 0, int(_58.Height) - 1), _14656.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14660 = _14646 + ivec3(1, 1, 0);
            float _20680;
            do
            {
                if ((_14660.z < 0) || (_14660.z > (int(_58.Depth) - 1)))
                {
                    _20680 = 0.0;
                    break;
                }
                _20680 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14660.x, 0, int(_58.Width) - 1), clamp(_14660.y, 0, int(_58.Height) - 1), _14660.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14664 = _14646 + ivec3(0, 0, 1);
            float _20681;
            do
            {
                if ((_14664.z < 0) || (_14664.z > (int(_58.Depth) - 1)))
                {
                    _20681 = 0.0;
                    break;
                }
                _20681 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14664.x, 0, int(_58.Width) - 1), clamp(_14664.y, 0, int(_58.Height) - 1), _14664.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14668 = _14646 + ivec3(1, 0, 1);
            float _20682;
            do
            {
                if ((_14668.z < 0) || (_14668.z > (int(_58.Depth) - 1)))
                {
                    _20682 = 0.0;
                    break;
                }
                _20682 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14668.x, 0, int(_58.Width) - 1), clamp(_14668.y, 0, int(_58.Height) - 1), _14668.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14672 = _14646 + ivec3(0, 1, 1);
            float _20683;
            do
            {
                if ((_14672.z < 0) || (_14672.z > (int(_58.Depth) - 1)))
                {
                    _20683 = 0.0;
                    break;
                }
                _20683 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14672.x, 0, int(_58.Width) - 1), clamp(_14672.y, 0, int(_58.Height) - 1), _14672.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _14676 = _14646 + ivec3(1);
            float _20684;
            do
            {
                if ((_14676.z < 0) || (_14676.z > (int(_58.Depth) - 1)))
                {
                    _20684 = 0.0;
                    break;
                }
                _20684 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_14676.x, 0, int(_58.Width) - 1), clamp(_14676.y, 0, int(_58.Height) - 1), _14676.z, 0).xyz, 0).z;
                break;
            } while(false);
            vec3 _15961 = (_798 - (vec3(mix(mix(mix(_20661, _20662, _12000.x), mix(_20663, _20664, _12000.x), _12000.y), mix(mix(_20665, _20666, _12000.x), mix(_20667, _20668, _12000.x), _12000.y), _12000.z), mix(mix(mix(_20669, _20670, _13322.x), mix(_20671, _20672, _13322.x), _13322.y), mix(mix(_20673, _20674, _13322.x), mix(_20675, _20676, _13322.x), _13322.y), _13322.z), mix(mix(mix(_20677, _20678, _14644.x), mix(_20679, _20680, _14644.x), _14644.y), mix(mix(_20681, _20682, _14644.x), mix(_20683, _20684, _14644.x), _14644.y), _14644.z)) * (_58.DeltaTime / _58.CellSize))) - vec3(0.5, 0.5, 1.0);
            vec3 _15963 = floor(_15961);
            vec3 _15966 = _15961 - _15963;
            ivec3 _15968 = ivec3(_15963);
            float _20685;
            do
            {
                if ((_15968.z < 0) || (_15968.z > (int(_58.Depth) - 1)))
                {
                    _20685 = 0.0;
                    break;
                }
                _20685 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15968.x, 0, int(_58.Width) - 1), clamp(_15968.y, 0, int(_58.Height) - 1), _15968.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15974 = _15968 + ivec3(1, 0, 0);
            float _20686;
            do
            {
                if ((_15974.z < 0) || (_15974.z > (int(_58.Depth) - 1)))
                {
                    _20686 = 0.0;
                    break;
                }
                _20686 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15974.x, 0, int(_58.Width) - 1), clamp(_15974.y, 0, int(_58.Height) - 1), _15974.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15978 = _15968 + ivec3(0, 1, 0);
            float _20687;
            do
            {
                if ((_15978.z < 0) || (_15978.z > (int(_58.Depth) - 1)))
                {
                    _20687 = 0.0;
                    break;
                }
                _20687 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15978.x, 0, int(_58.Width) - 1), clamp(_15978.y, 0, int(_58.Height) - 1), _15978.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15982 = _15968 + ivec3(1, 1, 0);
            float _20688;
            do
            {
                if ((_15982.z < 0) || (_15982.z > (int(_58.Depth) - 1)))
                {
                    _20688 = 0.0;
                    break;
                }
                _20688 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15982.x, 0, int(_58.Width) - 1), clamp(_15982.y, 0, int(_58.Height) - 1), _15982.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15986 = _15968 + ivec3(0, 0, 1);
            float _20689;
            do
            {
                if ((_15986.z < 0) || (_15986.z > (int(_58.Depth) - 1)))
                {
                    _20689 = 0.0;
                    break;
                }
                _20689 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15986.x, 0, int(_58.Width) - 1), clamp(_15986.y, 0, int(_58.Height) - 1), _15986.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15990 = _15968 + ivec3(1, 0, 1);
            float _20690;
            do
            {
                if ((_15990.z < 0) || (_15990.z > (int(_58.Depth) - 1)))
                {
                    _20690 = 0.0;
                    break;
                }
                _20690 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15990.x, 0, int(_58.Width) - 1), clamp(_15990.y, 0, int(_58.Height) - 1), _15990.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15994 = _15968 + ivec3(0, 1, 1);
            float _20691;
            do
            {
                if ((_15994.z < 0) || (_15994.z > (int(_58.Depth) - 1)))
                {
                    _20691 = 0.0;
                    break;
                }
                _20691 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15994.x, 0, int(_58.Width) - 1), clamp(_15994.y, 0, int(_58.Height) - 1), _15994.z, 0).xyz, 0).z;
                break;
            } while(false);
            ivec3 _15998 = _15968 + ivec3(1);
            float _20692;
            do
            {
                if ((_15998.z < 0) || (_15998.z > (int(_58.Depth) - 1)))
                {
                    _20692 = 0.0;
                    break;
                }
                _20692 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(clamp(_15998.x, 0, int(_58.Width) - 1), clamp(_15998.y, 0, int(_58.Height) - 1), _15998.z, 0).xyz, 0).z;
                break;
            } while(false);
            _20845 = mix(mix(mix(_20685, _20686, _15966.x), mix(_20687, _20688, _15966.x), _15966.y), mix(mix(_20689, _20690, _15966.x), mix(_20691, _20692, _15966.x), _15966.y), _15966.z);
        }
        else
        {
            _20845 = 0.0;
        }
        imageStore(VelocityOut, ivec3(gl_GlobalInvocationID), vec4(_20699, _20772, _20845, 0.0));
        break;
    } while(false);
}

