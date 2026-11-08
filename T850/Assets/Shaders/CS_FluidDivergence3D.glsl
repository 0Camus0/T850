#version 430
// GENERATED from CS_FluidDivergence3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=00c2bf11b77be5d64f8b19ef2d65080a2b2da807e514841b859e69adad70fb10; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _41;

layout(binding = 3, std430) readonly buffer WallMask
{
    uint _data[];
} WallMask_1;

layout(binding = 2, r32f) uniform writeonly image3D DivergenceOut;
layout(binding = 4) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;
layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

void main()
{
    do
    {
        if (((gl_GlobalInvocationID.x >= _41.Width) || (gl_GlobalInvocationID.y >= _41.Height)) || (gl_GlobalInvocationID.z >= _41.Depth))
        {
            break;
        }
        ivec3 _390 = ivec3(gl_GlobalInvocationID);
        bool _1008;
        do
        {
            bool _1007;
            do
            {
                if ((((((_390.x < 0) || (_390.y < 0)) || (_390.z < 0)) || (_390.x >= int(_41.Width))) || (_390.y >= int(_41.Height))) || (_390.z >= int(_41.Depth)))
                {
                    _1007 = true;
                    break;
                }
                _1007 = WallMask_1._data[uint(_390.x) + (_41.Width * (uint(_390.y) + (_41.Height * uint(_390.z))))] != 0u;
                break;
            } while(false);
            if (_1007)
            {
                _1008 = false;
                break;
            }
            _1008 = clamp(texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(_390, 0).xyz, 0).x, 0.0, 1.0) >= 0.5;
            break;
        } while(false);
        if (!_1008)
        {
            imageStore(DivergenceOut, ivec3(gl_GlobalInvocationID), vec4(0.0));
            break;
        }
        float _1009;
        do
        {
            if (_390.x < 0)
            {
                _1009 = 0.0;
                break;
            }
            _1009 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_390, 0).xyz, 0).x;
            break;
        } while(false);
        ivec3 _402 = _390 + ivec3(-1, 0, 0);
        float _1010;
        do
        {
            if (_402.x < 0)
            {
                _1010 = 0.0;
                break;
            }
            _1010 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_402, 0).xyz, 0).x;
            break;
        } while(false);
        float _1011;
        do
        {
            if (_390.y < 0)
            {
                _1011 = 0.0;
                break;
            }
            _1011 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_390, 0).xyz, 0).y;
            break;
        } while(false);
        ivec3 _407 = _390 + ivec3(0, -1, 0);
        float _1012;
        do
        {
            if (_407.y < 0)
            {
                _1012 = 0.0;
                break;
            }
            _1012 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_407, 0).xyz, 0).y;
            break;
        } while(false);
        float _1013;
        do
        {
            if (_390.z < 0)
            {
                _1013 = 0.0;
                break;
            }
            _1013 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_390, 0).xyz, 0).z;
            break;
        } while(false);
        ivec3 _412 = _390 + ivec3(0, 0, -1);
        float _1014;
        do
        {
            if (_412.z < 0)
            {
                _1014 = 0.0;
                break;
            }
            _1014 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(_412, 0).xyz, 0).z;
            break;
        } while(false);
        imageStore(DivergenceOut, ivec3(gl_GlobalInvocationID), vec4((((((_1009 - _1010) + _1011) - _1012) + _1013) - _1014) / _41.CellSize));
        break;
    } while(false);
}

