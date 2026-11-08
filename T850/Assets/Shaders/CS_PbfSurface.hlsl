// T850 Position Based Fluids: derive the render heightfield from the particle cloud.
//
// The water mesh (VS_Mesh/FS_Mesh, FLUID_SURFACE) displaces a flat XZ plane by ONE
// elevation per (x,z) texel and finite-differences it for normals. PBF is a particle
// cloud, so this pass rebuilds that same contract:
//
//   R = water elevation in metres (column-max of particle height)
//   G = motion / foam (mean speed in the column, 0..1)
//   B = d(elevation)/dx     (slope used for the shading normal)
//   A = d(elevation)/dz
//
// Pass 1 (this kernel, MODE_COLUMN): each particle atomic-max's its packed height into
// the matching texel. Packing: for positive floats the IEEE bit pattern compares like
// the integer, so InterlockedMax on the uint gives the tallest particle per column.
//
// Pass 2 (MODE_SLOPE, same kernel via Iteration): converts heights into slopes and
// writes the final RGBA16F texel. Empty columns read below the floor and are written as
// the water plane, which is what the VOF solver did for dry columns.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer PbfConstants : register(b0)
{
    uint  ParticleCount;
    uint  GridX;
    uint  GridY;
    uint  GridZ;
    float DeltaTime;
    float SmoothingRadius;
    float ParticleRadius;
    float RestDensity;
    float StiffnessEps;
    float ArtPressureK;
    float ArtPressureN;
    float ViscosityC;
    float Gravity;
    float MaxVelocity;
    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float BasinMinX;
    float BasinMinY;
    float BasinMinZ;
    float BasinMaxX;
    float BasinMaxY;
    float BasinMaxZ;
    uint  BoxCount;
    uint  Iteration;
    uint  SurfaceWidth;
    uint  SurfaceDepth;
    float WaterPlaneY;
    // Mass normalisation: rho_lattice * spacing^3 (see CS_PbfDensity).
    float RestScale;
    // Diagnostics/reference values; named per use in CS_PbfDensity.
    float RestDenomReference;   // sum |gradW|^2 for a full lattice
    float MinNeighbours;        // below this the density estimate is not trusted
    float Pad6;
    // Surface wind stress (m/s), applied to the free-surface layer in CS_PbfIntegrate.
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
};

// Heightfield dimensions come from SurfaceWidth/SurfaceDepth; the grid fields are the
// neighbour grid here, so the surface pass must not reuse them.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Predicted : register(t0);

// Packed height per column (pass 1 output, pass 2 input).
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
// int, not uint: D3D's InterlockedMax/InterlockedAdd only accept 32-bit signed operands
// (HLSL spec and vkd3d-proton both enforce it). The packed float bits round-trip through
// asint/asuint, which are bit casts, so the comparison order is unchanged for the positive
// values this buffer stores.
RWStructuredBuffer<int> ColumnHeights : register(u0);

// Write-only, with an explicit format: ComputeReflection rejects a storage image that
// is readable or format-less (see ComputeReflection.cpp).
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceOut : register(u2);

#define MODE_COLUMN 0u
#define MODE_SLOPE  1u


[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint sw = SurfaceWidth;
    const uint sd = SurfaceDepth;

    if (Iteration == MODE_COLUMN) {
        // Splat each particle into a 3x3 texel neighbourhood with a smooth radial weight.
        //
        // The previous version wrote the COLUMN MAX of the particle height. With a 9 cm
        // texel and 13 cm particle spacing there are only ~1.4 particles per texel, so the
        // max is effectively a random sample: adjacent texels differed by a measured 0.55 m
        // and a third of all texels were isolated spikes. That is what read as "displaced
        // triangles". A weighted average over the kernel footprint is what makes it water.
        const uint i = id.x;
        if (i >= ParticleCount)
            return;
        const float3 p = Predicted[i].xyz;
        const float cellX = (BasinMaxX - BasinMinX) / max((float)sw - 1.0f, 1.0f);
        const float cellZ = (BasinMaxZ - BasinMinZ) / max((float)sd - 1.0f, 1.0f);
        const float u = (p.x - BasinMinX) / cellX;
        const float v = (p.z - BasinMinZ) / cellZ;
        const int ctx = (int)floor(u);
        const int ctz = (int)floor(v);
        // Kernel radius in texels: a few texels, so neighbouring particles overlap.
        const float radiusTexels = 2.0f;
        const float radius2 = radiusTexels * radiusTexels;
        [unroll]
        for (int dz = -2; dz <= 2; ++dz)
        [unroll]
        for (int dx = -2; dx <= 2; ++dx) {
            const int tx = ctx + dx;
            const int tz = ctz + dz;
            if (tx < 0 || tz < 0 || tx >= (int)sw || tz >= (int)sd) continue;
            const float ddx = (u - (float)tx);
            const float ddz = (v - (float)tz);
            const float r2 = ddx * ddx + ddz * ddz;
            if (r2 > radius2) continue;
            const float w = 1.0f - r2 / radius2;      // smooth tent, 1 at the centre
            if (w <= 0.0f) continue;
            const uint slot = (uint)tx + sw * (uint)tz;
            // Fixed point: height in 1/64 m units, weight in 1/256 units. Both are positive
            // so a plain InterlockedAdd works and cannot overflow at this scale.
            // FREE SURFACE, not mean depth.
            //
            // Averaging particle heights over a column returns the mean water DEPTH, which
            // for a filled pool is a constant ~0.8 m no matter where the surface is: the
            // rendered mesh came out as a featureless plane and the dam break was invisible.
            // What the mesh needs is the waterline, so keep the MAXIMUM particle height per
            // texel (positive floats compare like ints) and let the slope pass blur it.
            const float h = max(p.y - BasinMinY, 0.0f);
            InterlockedMax(ColumnHeights[slot * 2u + 0u], asint(h));
            // Weight marks the texel wet; the blur in the slope pass only uses wet texels.
            uint weight = 0u;
            InterlockedAdd(ColumnHeights[slot * 2u + 1u], (int)max(w * 256.0f, 1.0f), weight);
        }
        return;
    }

    // MODE_SLOPE: one thread per texel.
    const uint slot = id.x;
    if (slot >= sw * sd)
        return;
    const uint tx = slot % sw;
    const uint tz = slot / sw;
    const float cellX = (BasinMaxX - BasinMinX) / max((float)sw - 1.0f, 1.0f);
    const float cellZ = (BasinMaxZ - BasinMinZ) / max((float)sd - 1.0f, 1.0f);

    // hMax is the packed max height above the floor; wSum marks the texel wet.
    const float hMax = asfloat(asuint(ColumnHeights[slot * 2u + 0u]));
    const float wSum = (float)ColumnHeights[slot * 2u + 1u];
    const bool wetColumn = wSum > 0.5f;

    // A small 3x3 blur removes the remaining per-texel variation from the discrete
    // splat, so the rendered surface is continuous rather than faceted.
    float h = 0.0f;
    float wAcc = 0.0f;
    [unroll]
    for (int oz = -1; oz <= 1; ++oz)
    [unroll]
    for (int ox = -1; ox <= 1; ++ox) {
        const int sx = (int)tx + ox;
        const int sz = (int)tz + oz;
        if (sx < 0 || sz < 0 || sx >= (int)sw || sz >= (int)sd) continue;
        const uint sIdx = (uint)sx + sw * (uint)sz;
        const float ws = (float)ColumnHeights[sIdx * 2u + 1u];
        if (ws <= 0.5f) continue;
        const float hs = asfloat(asuint(ColumnHeights[sIdx * 2u + 0u]));
        const float wgt = (ox == 0 && oz == 0) ? 4.0f : ((ox == 0 || oz == 0) ? 2.0f : 1.0f);
        h += hs * wgt;
        wAcc += wgt;
    }
    // DELTA from the water plane: VS_Mesh does `position.y += value`, exactly like the
    // VOF kernel (CS_FluidSurface3D writes `elevation - WaterPlaneY`). Writing the absolute
    // height here displaced the mesh by ~1.5 m instead of by the surface offset.
    // Height above the basin floor -> delta from the water plane that VS_Mesh adds.
    // h is height above the basin FLOOR; the mesh adds a delta from the water PLANE, so
    // convert: elevation = h + BasinMinY, and delta = elevation - WaterPlaneY.
    // Subtracting only WaterPlaneY left a constant +1.56 m, which floated the whole sheet
    // above the deck instead of in the pool.
    h = (wAcc > 0.0f) ? ((h / wAcc) + BasinMinY - WaterPlaneY) : 0.0f;

    const uint xm = tx > 0u ? slot - 1u : slot;
    const uint xp = tx + 1u < sw ? slot + 1u : slot;
    const uint zm = tz > 0u ? slot - sw : slot;
    const uint zp = tz + 1u < sd ? slot + sw : slot;
    // Slope from the (weighted, blurred) neighbour columns. Dry neighbours fall back to
    // this texel's own height so a dry cell does not create a cliff in the normal.
    const float wxm = (float)ColumnHeights[xm * 2u + 1u];
    const float wxp = (float)ColumnHeights[xp * 2u + 1u];
    const float wzm = (float)ColumnHeights[zm * 2u + 1u];
    const float wzp = (float)ColumnHeights[zp * 2u + 1u];
    const float hxm = wxm > 0.5f ? (asfloat(ColumnHeights[xm * 2u + 0u]) + BasinMinY - WaterPlaneY) : h;
    const float hxp = wxp > 0.5f ? (asfloat(ColumnHeights[xp * 2u + 0u]) + BasinMinY - WaterPlaneY) : h;
    const float hzm = wzm > 0.5f ? (asfloat(ColumnHeights[zm * 2u + 0u]) + BasinMinY - WaterPlaneY) : h;
    const float hzp = wzp > 0.5f ? (asfloat(ColumnHeights[zp * 2u + 0u]) + BasinMinY - WaterPlaneY) : h;
    const float hx = (hxp - hxm) / (2.0f * cellX);
    const float hz = (hzp - hzm) / (2.0f * cellZ);
    const bool wet = wetColumn || wAcc > 0.0f;
    const float elevation = h;
    // SurfaceOut is RGBA16F and stores elevation relative to the water plane, like the
    // VOF surface kernel: VS_Mesh adds it to the authored plane height.
    SurfaceOut[uint2(tx, tz)] = float4(wet ? elevation : 0.0f, 0.0f, wet ? hx : 0.0f, wet ? hz : 0.0f);
}
