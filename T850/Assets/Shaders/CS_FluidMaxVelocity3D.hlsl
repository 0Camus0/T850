// T850 pool fluid: the largest |u|+|v|+|w| in the velocity volume (W-31).
//
// The alpha transport is an unsplit upwind scheme whose stability limit is
// (|u|+|v|+|w|) * dt / h <= 1, so the substep size has to follow the flow instead of a fixed
// worst case. CS_FluidDiagnostics3D already measures speed, but it issues four atomics per voxel and
// exists for the probes; this kernel reduces to ONE value per dispatch with a groupshared max and a
// single atomic per group.
//
// The result is a max, so it must be cleared before accumulating. Same trick as the diagnostics
// kernel: the Iteration slot carries T8_MAX_VELOCITY_RESET and one thread zeroes the accumulator.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
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
    // 1 when the rows above the domain are open air, 0 for a closed ceiling.
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> VelocityIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
// One word: the max as a uint bit pattern. `asuint` of a non-negative float orders the same way the
// float does, so InterlockedMax on the bits is a max on the value.
RWStructuredBuffer<uint> MaxSpeedOut : register(u0);

#define T8_MAX_VELOCITY_RESET 0xFFFFFFFFu

groupshared float g_partial[64];

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    if (Iteration == T8_MAX_VELOCITY_RESET) {
        if (id.x == 0u && id.y == 0u && id.z == 0u)
            MaxSpeedOut[0] = 0u;
        return;
    }

    float sum = 0.0f;
    if (id.x < Width && id.y < Height && id.z < Depth) {
        // The MAC grid stores velocities on the cell faces; the transport sees their sum, so the
        // bound is the sum of absolute face velocities, not the vector magnitude.
        const float3 velocity = VelocityIn.Load(int4(int3(id), 0)).xyz;
        sum = abs(velocity.x) + abs(velocity.y) + abs(velocity.z);
        // A NaN would win no comparison and silently shrink the substep count; map it to a large
        // finite value so a broken frame gets caught by the gates rather than hidden here.
        if (!(sum >= 0.0f) || sum > 1.0e6f)
            sum = 1.0e6f;
    }

    g_partial[index] = sum;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (index < stride)
            g_partial[index] = max(g_partial[index], g_partial[index + stride]);
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u)
        InterlockedMax(MaxSpeedOut[0], asuint(g_partial[0]));
}
