// T850 pool fluid: the simulated water level around one body, for its buoyancy (W-06).
//
// The body integrator used to float on the *authored* water plane, so it could not ride a
// wave and its energy loss had to be faked with a heave damping ratio. This kernel averages
// the solver's own surface over an annulus just outside the body's footprint — outside, so
// the dip the body presses into the water does not drag its own reference level down with
// it — and writes the result to a one-float buffer the host reads asynchronously.
//
// Since W-19 the surface texture's covered texels already hold the inpainted free-surface
// level, so sampling the annulus is equivalent to re-deriving the level from alpha columns
// (which stop at the body's underside) but far cheaper.
//
// One dispatch per query per frame; the whole grid is covered so the annulus is sampled from
// every texel that overlaps it, and the sum is a fixed-point InterlockedAdd pair.

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
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
};

// Body position and radius in world units, packed like StampConstants so the host code and
// the kernel agree on one layout.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
cbuffer BodyLevelConstants : register(b1)
{
    float BasinMinX;
    float BasinMinZ;
    float BodyX;
    float BodyZ;
    float BodyRadius;
    float StepX;
    float StepZ;
    float LevelPad;
};

// (height, foam, slopeX, slopeZ) per column.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float4> SurfaceIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<uint2> Result : register(u0);

groupshared uint2 g_partial[64];

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    // Fixed-point: height in 1/1024 m and the sample count, so the sum stays exact.
    // Dispatched as (width, depth, 1): the grid row lives in id.y.
    uint2 value = uint2(0u, 0u);
    if (id.x < Width && id.y < Depth) {
        const float wx = BasinMinX + ((float)id.x + 0.5f) * StepX;
        const float wz = BasinMinZ + ((float)id.y + 0.5f) * StepZ;
        const float dx = wx - BodyX;
        const float dz = wz - BodyZ;
        const float distance = sqrt(dx * dx + dz * dz);
        // Just outside the footprint, out to one body radius beyond it: the water the body
        // actually displaces without the crater it sits in.
        const float inner = BodyRadius * 1.05f;
        const float outer = BodyRadius * 2.0f + StepX;
        const uint solid = WallMask[(id.x) + Width * (id.y)];
        if (solid == 0u && distance >= inner && distance <= outer) {
            const float level = SurfaceIn.Load(int3(id.x, id.y, 0)).x;
            value.x = (uint)clamp(level * 1024.0f, -60000.0f, 60000.0f);
            value.y = 1u;
        }
    }
    g_partial[index] = value;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (index < stride)
            g_partial[index] += g_partial[index + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u) {
        // One slot per group, no atomics: the host sums the groups. A shared accumulator
        // would have to be zeroed before every query, and there is no buffer-write API.
        const uint groupLinear = (id.x / 8u) + ((Width + 7u) / 8u) * (id.y / 8u);
        Result[groupLinear] = g_partial[0];
    }
}
