// T850 pool fluid: the simulated water level and current around every body, for their
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

// Domain origin, authored water plane, column step and the number of bodies. Same order as
// the host's LevelConstants, so one memcpy fills both.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
cbuffer BodyLevelConstants : register(b1)
{
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float WaterPlaneY;
    float StepX;
    float StepY;
    float StepZ;
    uint BodyCount;
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

// One (x, y, z, radius) per body; radius 0 marks a disabled body.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<float4> Bodies : register(t9);

// One (mean level offset from the plane, current x, current z, sample count) per body.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
RWStructuredBuffer<float4> Result : register(u0);

// One thread per body walks the columns of its own annulus: every body is answered by one
// dispatch and one readback, however many balls are spawned.
[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= BodyCount)
        return;
    const float4 body = Bodies[id.x];
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (body.w > 0.0f) {
        // Just outside the footprint, out to one body radius beyond it: the water the body
        // actually displaces without the crater it sits in.
        const float inner = body.w * 1.05f;
        const float outer = body.w * 2.0f + StepX;
        const int x0 = max((int)floor((body.x - outer - BasinMinX) / StepX), 0);
        const int x1 = min((int)ceil((body.x + outer - BasinMinX) / StepX), (int)Width - 1);
        const int z0 = max((int)floor((body.z - outer - BasinMinZ) / StepZ), 0);
        const int z1 = min((int)ceil((body.z + outer - BasinMinZ) / StepZ), (int)Depth - 1);
        const int bodyRow = clamp((int)((body.y - BasinMinY) / StepY), 0, (int)Height - 1);
        float sumLevel = 0.0f, sumU = 0.0f, sumW = 0.0f, count = 0.0f;
        [loop]
        for (int z = z0; z <= z1; ++z) {
            [loop]
            for (int x = x0; x <= x1; ++x) {
                const float dx = BasinMinX + ((float)x + 0.5f) * StepX - body.x;
                const float dz = BasinMinZ + ((float)z + 0.5f) * StepZ - body.z;
                const float distance = sqrt(dx * dx + dz * dz);
                if (distance < inner || distance > outer)
                    continue;
                const float level = SurfaceIn.Load(int3(x, z, 0)).x;
                const int topRow =
                    clamp((int)((WaterPlaneY + level - BasinMinY) / StepY), 0, (int)Height - 1);
                // Walls and other bodies at the waterline are not open water.
                if (WallMask[(uint)x + Width * ((uint)topRow + Height * (uint)z)] != 0u)
                    continue;
                // The current that matters for drag is the one at the body's own depth, not
                // the orbital velocity at the wave crest: the column's topmost wet cell, or
                // the row nearest the body's centre if that is deeper.
                const float4 v = VelocityIn.Load(int4(x, min(topRow, bodyRow), z, 0));
                sumLevel += level;
                sumU += v.x;
                sumW += v.z;
                count += 1.0f;
            }
        }
        if (count > 0.0f)
            result = float4(sumLevel / count, sumU / count, sumW / count, count);
    }
    Result[id.x] = result;
}
