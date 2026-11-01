// T850 pool fluid: the simulated water level and current around one body, for its
// buoyancy and drag (W-06).
//
// The body integrator used to float on the *authored* water plane, so it could not ride a
// wave and its energy loss had to be faked with a heave damping ratio. This kernel averages
// the solver's own surface over an annulus just outside the body's footprint — outside, so
// the dip the body presses into the water does not drag its own reference level down with
// it — and writes the result to a small buffer the host reads asynchronously.
//
// Since W-19 the surface texture's covered texels already hold the inpainted free-surface
// level, so sampling the annulus is equivalent to re-deriving the level from alpha columns
// (which stop at the body's underside) but far cheaper.
//
// W-06 step 3 rides along in the same pass: the horizontal liquid velocity at the waterline
// of the same annulus cells, so the host can apply drag to (v - flow) instead of to v.
// Without it the drag term is an absolute brake and a current cannot carry the body.
//
// One dispatch per query per frame; the whole grid is covered so the annulus is sampled from
// every texel that overlaps it, and the sum is a fixed-point reduction per thread group.

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

// Domain origin, authored water plane, body position and radius in world units, plus the
// column step. Same order as the host's LevelConstants, so one memcpy fills both.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
cbuffer BodyLevelConstants : register(b1)
{
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float WaterPlaneY;
    float BodyX;
    float BodyY;
    float BodyZ;
    float BodyRadius;
    float StepX;
    float StepY;
    float StepZ;
    float LevelPad;
};

// (height, foam, slopeX, slopeZ) per column.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float4> SurfaceIn : register(t0);

// Staggered velocity volume: texel c holds the cell's +X/+Y/+Z face velocities.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
Texture3D<float4> VelocityIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
RWStructuredBuffer<uint4> Result : register(u0);

// Fixed point: height in 1/1024 m, velocity in 1/1024 m/s, and the sample count. Integer
// sums keep the reduction exact and the readback trivial.
#define LEVEL_SCALE 1024.0f

// Two's complement in a uint lane: a plain (uint) cast of a negative float is 0 (ftou) or
// undefined, which dropped every negative level offset and current. The host reads int32.
uint PackSigned(float value)
{
    return asuint((int)round(clamp(value * LEVEL_SCALE, -60000.0f, 60000.0f)));
}

groupshared uint4 g_partial[64];

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    // Dispatched as (width, depth, 1): the grid row lives in id.y.
    uint4 value = uint4(0u, 0u, 0u, 0u);
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
        if (distance >= inner && distance <= outer) {
            const float level = SurfaceIn.Load(int3(id.x, id.y, 0)).x;
            const int topRow =
                clamp((int)((WaterPlaneY + level - BasinMinY) / StepY), 0, (int)Height - 1);
            // Walls and other bodies at the waterline are not open water.
            const uint solid = WallMask[id.x + Width * ((uint)topRow + Height * id.y)];
            if (solid == 0u) {
                value.x = PackSigned(level);
                value.y = 1u;
                // The current that matters for drag is the one at the body's own depth, not
                // the orbital velocity at the wave crest: sample the column's topmost wet
                // cell, then the row nearest the body's centre if that is deeper.
                const int bodyRow = clamp((int)((BodyY - BasinMinY) / StepY), 0, (int)Height - 1);
                const int row = min(topRow, bodyRow);
                const float4 v = VelocityIn.Load(int4(id.x, row, id.y, 0));
                value.z = PackSigned(v.x);
                value.w = PackSigned(v.z);
            }
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
