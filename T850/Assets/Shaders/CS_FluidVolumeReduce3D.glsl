#version 430
// GENERATED from CS_FluidVolumeReduce3D.hlsl by scripts/shaders/generate_gl_compute_twins.ps1, do not edit; hlsl-sha256=e26cec0f4c2518b7202bd1e6ef0e4cb3c2c1aa0300dabe643d16e743122ad404; defines=T850_SPIRV=1,T850_VULKAN=1,T850_GL=1
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

layout(binding = 2, std430) buffer Sums
{
    uint _data[];
} Sums_1;

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
} _87;

layout(binding = 1) uniform sampler3D SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler;

shared vec3 g_partial[64];

void main()
{
    vec3 _496;
    if (((gl_GlobalInvocationID.x < _87.Width) && (gl_GlobalInvocationID.y < _87.Height)) && (gl_GlobalInvocationID.z < _87.Depth))
    {
        vec4 _284 = texelFetch(SPIRV_Cross_CombinedAlphaInSPIRV_Cross_DummySampler, ivec4(ivec3(gl_GlobalInvocationID), 0).xyz, 0);
        float _285 = _284.x;
        vec3 _485 = vec3(0.0);
        _485.x = _285;
        vec3 _497;
        if ((_285 >= 0.5) && (_285 < 0.999000012874603271484375))
        {
            _497 = vec3(_285, 1.0 - _285, _285);
        }
        else
        {
            _497 = _485;
        }
        _496 = _497;
    }
    else
    {
        _496 = vec3(0.0);
    }
    g_partial[gl_LocalInvocationIndex] = _496;
    barrier();
    SPIRV_CROSS_UNROLL
    for (uint _498 = 32u; _498 > 0u; _498 = _498 >> 1u)
    {
        if (gl_LocalInvocationIndex < _498)
        {
            g_partial[gl_LocalInvocationIndex] += g_partial[gl_LocalInvocationIndex + _498];
        }
        barrier();
    }
    if (gl_LocalInvocationIndex == 0u)
    {
        uint _336 = _87.Iteration & 1u;
        vec3 _339 = g_partial[0];
        vec3 _340 = max(_339, vec3(0.0));
        float _343 = _340.x;
        float _360 = floor(_343);
        uint _376 = atomicAdd(Sums_1._data[_336 * 8u], uint(_360));
        uint _382 = atomicAdd(Sums_1._data[(_336 * 8u) + 1u], uint((_343 - _360) * 65536.0));
        float _347 = _340.y;
        float _402 = floor(_347);
        uint _431 = atomicAdd(Sums_1._data[(_336 * 8u) + 2u], uint(_402));
        uint _437 = atomicAdd(Sums_1._data[(_336 * 8u) + 3u], uint((_347 - _402) * 65536.0));
        uint _350 = (_336 * 2u) + 1u;
        float _352 = _340.z;
        float _444 = floor(_352);
        uint _460 = atomicAdd(Sums_1._data[_350 * 4u], uint(_444));
        uint _466 = atomicAdd(Sums_1._data[(_350 * 4u) + 1u], uint((_352 - _444) * 65536.0));
    }
}

