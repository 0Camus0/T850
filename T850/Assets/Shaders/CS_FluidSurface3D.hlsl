// T850 pool fluid: derives the renderable 2D water surface from the simulated
// volume fraction. One output texel per water column:
//   R = surface elevation in metres (world Y)
//   G = foam
//   B = slope dH/dx (dimensionless gradient)
//   A = slope dH/dz
//
// This replaces the old heuristic that relaxed a height toward a procedural sine
// target from surface-layer velocity. The height is now the column integral of
// alpha, so the visible waterline is exactly consistent with the conserved mass:
// whatever the solver conserves, the surface shows.

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
Texture3D<float4> AlphaIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceOut : register(u0);

// Absolute world Y of the bottom of the solver domain, and the authored water
// plane. Both arrive as constants from the pool layout so nothing about the pool
// placement is baked into this kernel. R is written as a DELTA from the water
// plane because the water mesh already sits at it and the vertex shader adds R to
// its Y; writing an absolute elevation here would drop the surface by the depth.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
cbuffer GeometryConstants : register(b1)
{
    float BasinMinY;
    float WaterPlaneY;
    float WorldOriginX;
    float WorldOriginZ;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

bool IsSolid(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z))] != 0u;
}

// Column integral of alpha, expressed as a world-space elevation.
// Integrating rather than taking the topmost alpha > 0.5 cell handles a
// partially filled interface cell continuously, avoiding the one-cell
// quantisation jitter that thresholding produces as the surface moves.
//
// A column with no water at all is a wall column (the domain shell is solid, so
// alpha there is exactly 0). Reporting the basin floor for those would drag the
// water mesh's edge vertices down the full depth, so they report the water plane
// instead and contribute a zero delta.
static const uint COLUMN_DRY = 0u;
static const uint COLUMN_FREE = 1u;
// A floating body rests on this column: the integral stops at the body's underside,
// not at the free surface around it.
static const uint COLUMN_COVERED = 2u;

float RawColumnElevation(int x, int z, out uint state)
{
    state = COLUMN_DRY;
    if (x < 0 || z < 0 || x >= int(Width) || z >= int(Depth))
        return WaterPlaneY;
    // Solid cells (floor, steps, a submerged body) count as full only when wet
    // water sits above them; otherwise the surface over a step sinks by its height.
    float water = 0.0f;
    float pendingSolid = 0.0f;
    bool onWater = false;
    bool covered = false;
    for (uint j = 0; j < Height; ++j) {
        const int3 c = int3(x, int(j), z);
        if (IsSolid(c)) {
            pendingSolid += 1.0f;
            // A solid run that starts on wet water is a body floating on it; a run
            // that starts on air (a falling body) leaves the surface below intact.
            covered = covered || onWater;
            continue;
        }
        const float a = saturate(AlphaIn.Load(int4(c, 0)).x);
        water += a;
        onWater = a >= 0.5f;
        if (onWater) {
            water += pendingSolid;
            pendingSolid = 0.0f;
            covered = false;
        }
    }
    if (water <= 0.001f)
        return WaterPlaneY;
    state = covered ? COLUMN_COVERED : COLUMN_FREE;
    return BasinMinY + water * CellSize;
}

// Longest walk from a covered column to open water. A 0.25 m ball spans ~3 cells of
// radius; 8 cells also covers a ball twice that size.
static const int INPAINT_REACH = 8;

// Covered columns take the mean free-surface level found by walking outward in 8
// directions. Without this the mesh dives to the body's underside, and the 3x3 tent
// spreads that into a visible funnel around a floating ball.
float ColumnElevation(int x, int z)
{
    uint state;
    const float own = RawColumnElevation(x, z, state);
    if (state != COLUMN_COVERED)
        return own;

    const int2 directions[8] = {
        int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1),
        int2(1, 1), int2(-1, 1), int2(1, -1), int2(-1, -1)
    };
    float sum = 0.0f;
    float count = 0.0f;
    [loop]
    for (int d = 0; d < 8; ++d) {
        [loop]
        for (int s = 1; s <= INPAINT_REACH; ++s) {
            uint neighbourState;
            const float e = RawColumnElevation(x + directions[d].x * s, z + directions[d].y * s, neighbourState);
            if (neighbourState == COLUMN_COVERED)
                continue;
            if (neighbourState == COLUMN_FREE) {
                sum += e;
                count += 1.0f;
            }
            break;
        }
    }
    return count > 0.0f ? sum / count : own;
}

// Foam from velocity divergence in the surface band. Divergence is the cheap,
// physically meaningful whitecap signal: stretching surface flow has positive
// divergence, slack water has none. An alpha threshold would give a binary mask.
float SurfaceFoam(int x, int z, int surfaceRow)
{
    float divergence = 0.0f;
    const int3 hi = int3(int(Width) - 1, int(Height) - 1, int(Depth) - 1);
    x = clamp(x, 0, hi.x);
    z = clamp(z, 0, hi.z);
    for (int row = surfaceRow - 2; row <= surfaceRow; ++row) {
        const int3 c = int3(x, clamp(row, 0, hi.y), z);
        // Staggered grid: texel c holds the cell's +X/+Y/+Z faces; the -face is
        // the previous texel, or a wall (0) at index -1.
        const float3 plus = VelocityIn.Load(int4(c, 0)).xyz;
        float uMinus = 0.0f, vMinus = 0.0f, wMinus = 0.0f;
        if (c.x > 0) uMinus = VelocityIn.Load(int4(c - int3(1, 0, 0), 0)).x;
        if (c.y > 0) vMinus = VelocityIn.Load(int4(c - int3(0, 1, 0), 0)).y;
        if (c.z > 0) wMinus = VelocityIn.Load(int4(c - int3(0, 0, 1), 0)).z;
        divergence += ((plus.x - uMinus) + (plus.y - vMinus) + (plus.z - wMinus)) / CellSize;
    }
    return saturate(divergence * 0.35f);
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    // Dispatched as (width, depth, 1): the grid row lives in id.y.
    if (id.x >= Width || id.y >= Depth)
        return;

    const int cx = int(id.x);
    const int cz = int(id.y);

    // Raw column elevations around this texel. These 9 sums replace 9 separate
    // ColumnElevation calls below, so the smoothing costs one pass of loads.
    float e[3][3];
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            e[dz + 1][dx + 1] = ColumnElevation(cx + dx, cz + dz);
        }
    }

    // A column sum changes in whole-cell steps as the interface cell fills, so the
    // raw elevation is staircase-quantised on 0.09 m cells and reads as blocks at
    // the water edge. A separable 3x3 tent (weights 1,2,1 / 16) turns that into a
    // smooth ramp. It is applied here rather than on the texture because storage
    // textures are not filterable: there is no hardware bilinear to lean on, so the
    // values themselves have to be smooth.
    const float elevation = (1.0f / 16.0f) *
        (e[0][0] + 2.0f * e[0][1] + e[0][2] +
         2.0f * e[1][0] + 4.0f * e[1][1] + 2.0f * e[1][2] +
         e[2][0] + 2.0f * e[2][1] + e[2][2]);

    // R is a delta from the authored water plane, matching the shader contract:
    // the mesh already sits at that plane and adds R to its Y.
    const float height = elevation - WaterPlaneY;

    // Slopes are per-texel height DIFFERENCES over the 0.09 m cell step, matching
    // the shader's slope convention (central difference, no divide by spacing).
    // Dividing here would make the fragment shader's slope scale wrong. They come
    // from the SAME tent weights, so the normal cannot disagree with the surface.
    const float slopeX = (1.0f / 8.0f) * 0.5f *
        ((e[0][2] + 2.0f * e[1][2] + e[2][2]) - (e[0][0] + 2.0f * e[1][0] + e[2][0]));
    const float slopeZ = (1.0f / 8.0f) * 0.5f *
        ((e[2][0] + 2.0f * e[2][1] + e[2][2]) - (e[0][0] + 2.0f * e[0][1] + e[0][2]));

    // Row holding the waterline, so foam samples the interface band.
    const int surfaceRow = clamp(int((elevation - BasinMinY) / CellSize),
                                 1, int(Height) - 1);
    // Foam comes from velocity divergence, which is a second derivative and is
    // therefore very spiky at single cells. Averaging three columns (sharing the
    // same surface row, so no extra row fetches) removes the single-cell pops that
    // read as flashing whitecaps without washing the foam shape out.
    const float foam = (SurfaceFoam(cx - 1, cz, surfaceRow) +
                        SurfaceFoam(cx, cz, surfaceRow) +
                        SurfaceFoam(cx + 1, cz, surfaceRow)) * (1.0f / 3.0f);

    SurfaceOut[id.xy] = float4(height, foam, slopeX, slopeZ);
}
