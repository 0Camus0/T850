#version 430
// GENERATED from CS_FluidMaxVelocity3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=577faf30925c04dd93358f2c96f5cf3c10a946a486a9143b7c0162268a03c32d; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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
} _18;

layout(binding = 2, std430) buffer MaxSpeedOut
{
    uint _data[];
} MaxSpeedOut_1;

layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler;

shared float g_partial[64];

void main()
{
    do
    {
        if (_18.Iteration == 4294967295u)
        {
            if (((gl_GlobalInvocationID.x == 0u) && (gl_GlobalInvocationID.y == 0u)) && (gl_GlobalInvocationID.z == 0u))
            {
                MaxSpeedOut_1._data[0] = 0u;
            }
            break;
        }
        float _314;
        if (((gl_GlobalInvocationID.x < _18.Width) && (gl_GlobalInvocationID.y < _18.Height)) && (gl_GlobalInvocationID.z < _18.Depth))
        {
            vec4 _245 = texelFetch(SPIRV_Cross_CombinedVelocityInSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0);
            float _257 = (abs(_245.x) + abs(_245.y)) + abs(_245.z);
            _314 = ((!(_257 >= 0.0)) || (_257 > 1000000.0)) ? 1000000.0 : _257;
        }
        else
        {
            _314 = 0.0;
        }
        g_partial[gl_LocalInvocationIndex] = _314;
        barrier();
        SPIRV_CROSS_UNROLL
        for (uint _316 = 32u; _316 > 0u; _316 = _316 >> 1u)
        {
            if (gl_LocalInvocationIndex < _316)
            {
                g_partial[gl_LocalInvocationIndex] = max(g_partial[gl_LocalInvocationIndex], g_partial[gl_LocalInvocationIndex + _316]);
            }
            barrier();
        }
        if (gl_LocalInvocationIndex == 0u)
        {
            uint _302 = atomicMax(MaxSpeedOut_1._data[0], floatBitsToUint(g_partial[0]));
        }
        break;
    } while(false);
}

