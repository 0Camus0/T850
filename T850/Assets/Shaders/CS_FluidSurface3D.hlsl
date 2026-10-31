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
//
// A floating body is a solid run that sits on an open cell; steps stand on the floor
// and never match. The test reads only the mask, so a column's classification cannot
// flip with alpha noise from one frame to the next.
struct Column
{
    float elevation;
    float bodyTop;      // world Y of the top of the floating body
    float waterAbove;   // alpha integral above the body, in cells
    bool hasBody;
    bool wet;
};

// Depth below the column-integral surface, in cells, past which air counts as volume.
// The far-field interface smears over ~2 cells (0.94 / 0.79 / 0.43 / 0.14 with R32F).
static const float TRAPPED_AIR_DEPTH_CELLS = 2.0f;

Column ReadColumn(int x, int z)
{
    Column col;
    col.elevation = WaterPlaneY;
    col.bodyTop = WaterPlaneY;
    col.waterAbove = 0.0f;
    col.hasBody = false;
    col.wet = false;
    if (x < 0 || z < 0 || x >= int(Width) || z >= int(Depth))
        return col;
    // Solid cells (floor, steps, a submerged body) count as full only when wet
    // water sits above them; otherwise the surface over a step sinks by its height.
    float water = 0.0f;
    float pendingSolid = 0.0f;
    bool belowOpen = false;
    bool inBody = false;
    for (uint j = 0; j < Height; ++j) {
        const int3 c = int3(x, int(j), z);
        if (IsSolid(c)) {
            if (belowOpen) {
                col.hasBody = true;
                col.waterAbove = 0.0f;
                inBody = true;
            }
            if (inBody)
                col.bodyTop = BasinMinY + float(j + 1u) * CellSize;
            pendingSolid += 1.0f;
            belowOpen = false;
            continue;
        }
        inBody = false;
        belowOpen = true;
        const float a = saturate(AlphaIn.Load(int4(c, 0)).x);
        water += a;
        col.waterAbove += a;
        if (a >= 0.5f) {
            water += pendingSolid;
            pendingSolid = 0.0f;
        }
    }
    if (!col.hasBody)
        col.waterAbove = 0.0f;
    col.wet = water > 0.001f;
    if (col.wet) {
        // Air well below the free surface lifts it instead of lowering it. The solver has
        // no bubble buoyancy, so air a plunging body leaves behind (W-07) stays where it
        // is; read as missing water it drew a 2-3 cm moat round a floating ball once
        // alpha became exact (W-02). The ramp starts below the interface smear and is
        // continuous in the level, so it cannot add quantisation jitter.
        float trapped = 0.0f;
        for (uint k = 0; k < Height; ++k) {
            const float depthCells = water - (float(k) + 0.5f);
            if (depthCells <= TRAPPED_AIR_DEPTH_CELLS)
                break;
            const int3 c = int3(x, int(k), z);
            if (IsSolid(c))
                continue;
            trapped += saturate(depthCells - TRAPPED_AIR_DEPTH_CELLS) *
                       (1.0f - saturate(AlphaIn.Load(int4(c, 0)).x));
        }
        col.elevation = BasinMinY + (water + trapped) * CellSize;
    }
    return col;
}

// Longest walk from a body column to open water. A 0.25 m ball spans ~3 cells of
// radius; 8 cells also covers a ball twice that size.
static const int INPAINT_REACH = 8;

// Under a floating body the column integral stops at the body's underside, which drew
// a funnel around the ball. Body columns instead take the mean level of the nearest
// body-free columns in 8 directions, blended continuously toward the water lying on
// top of the body as the body submerges.
float ColumnElevation(int x, int z)
{
    const Column own = ReadColumn(x, z);
    if (!own.hasBody)
        return own.elevation;

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
            const Column neighbour = ReadColumn(x + directions[d].x * s, z + directions[d].y * s);
            if (neighbour.hasBody)
                continue;
            if (neighbour.wet) {
                sum += neighbour.elevation;
                count += 1.0f;
            }
            break;
        }
    }
    const float ring = count > 0.0f ? sum / count : own.elevation;
    const float overBody = own.bodyTop + own.waterAbove * CellSize;
    return lerp(ring, overBody, saturate(own.waterAbove));
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

// Column elevations for an 8x8 group plus a one-column border, so each column is
// integrated once per group instead of once per tent tap (9x).
groupshared float s_elevation[10][10];

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    const uint flat = gtid.x + 8u * gtid.y;
    for (uint slot = flat; slot < 100u; slot += 64u) {
        const int tx = int(slot % 10u);
        const int tz = int(slot / 10u);
        s_elevation[tz][tx] = ColumnElevation(int(gid.x) * 8 + tx - 1, int(gid.y) * 8 + tz - 1);
    }
    GroupMemoryBarrierWithGroupSync();

    // Dispatched as (width, depth, 1): the grid row lives in id.y.
    if (id.x >= Width || id.y >= Depth)
        return;

    const int cx = int(id.x);
    const int cz = int(id.y);

    // Raw column elevations around this texel, read from the group's tile.
    float e[3][3];
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            e[dz + 1][dx + 1] = s_elevation[int(gtid.y) + 1 + dz][int(gtid.x) + 1 + dx];
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
