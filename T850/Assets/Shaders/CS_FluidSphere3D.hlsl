// T850 pool fluid, pass 1 of 2: open the cavity an obstacle displaces.
//
// One thread per COLUMN. This pass only REMOVES water: it takes away the alpha
// the object overlaps, up to the column's free capacity, and records how much it
// took into Removed[x + Width * z]. It deliberately does not put the water back,
// which is what makes the obstacle visible: an obstacle occupies space, so the
// water surface has to dip where the body is and rise everywhere else.
//
// An earlier version re-added the same volume on top of the same column to keep
// the column sum constant. That conserved mass but was invisible: the column
// integral that defines the rendered surface did not change at all, so the pool
// stayed perfectly flat. The displaced volume has to move SIDEWAYS, into the rest
// of the surface, which is exactly what pass 2 does.
//
// Object parameters arrive as constants, not as a structured buffer: this
// engine's ComputeBuffer has no update path, so a buffer seeded once would be
// read stale forever and the obstacle would never move. Constants are re-uploaded
// on every dispatch, so they are always live.
//
// WorldOriginX/Z are the world coordinates of the CENTRE of grid cell 0, and
// BasinMinY is the world Y of the bottom face of row 0. Passing them in keeps the
// solver free of hard-coded pool placement.

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
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
cbuffer ObjectConstants : register(b1)
{
    float ObjectCentreX;
    float ObjectCentreY;
    float ObjectCentreZ;
    float ObjectRadius;

    float ObjectVelX;
    float ObjectVelY;
    float ObjectVelZ;
    float ObjectEnabled;

    float WorldOriginX;
    float WorldOriginZ;
    float BasinMinY;
    float ObjectPadding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> AlphaOut : register(u0);

// Per column: .x = volume removed by the body this frame (cells), .y = free
// capacity above this column's current surface. Pass 2 needs the global sums of
// both, and taking them from this same buffer guarantees the two passes cannot
// disagree about the volume.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
RWStructuredBuffer<float2> Removed : register(u1);

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool IsSolid(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

static float sColumn[64];

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.z >= Depth)
        return;

    const int x = int(id.x);
    const int z = int(id.z);
    const uint rows = min(Height, 64u);

    for (uint j = 0; j < rows; ++j) {
        const int3 c = int3(x, int(j), z);
        sColumn[j] = IsSolid(c) ? 0.0f : saturate(AlphaIn.Load(int4(c, 0)).x);
    }

    float removed = 0.0f;
    const float radius = ObjectRadius;
    // Free capacity above this column's CURRENT surface. Pass 2 divides the total
    // displaced volume by the sum of these, so every column rises by the same
    // elevation: the share of the water is proportional to the room it has.
    float surfaceFreeCapacity = 0.0f;
    for (int j = int(rows) - 1; j >= 0; --j) {
        const int3 c = int3(x, j, z);
        if (IsSolid(c))
            continue;
        if (saturate(AlphaIn.Load(int4(c, 0)).x) > 0.001f) {
            surfaceFreeCapacity = 1.0f - saturate(AlphaIn.Load(int4(c, 0)).x);
            break;
        }
    }

    if (ObjectEnabled >= 0.5f && radius > 0.0f) {
        // World position of this column's centre in X and Z. WorldOriginX/Z are
        // the centre of cell 0, so this is geometry-driven, not pool-specific.
        const float worldX = WorldOriginX + float(x) * CellSize;
        const float worldZ = WorldOriginZ + float(z) * CellSize;
        const float dx = worldX - ObjectCentreX;
        const float dz = worldZ - ObjectCentreZ;
        const float radial2 = dx * dx + dz * dz;

        if (radial2 < radius * radius) {
            const float halfSpan = sqrt(max(radius * radius - radial2, 0.0f));
            const float spanLo = ObjectCentreY - halfSpan;
            const float spanHi = ObjectCentreY + halfSpan;

            // Free capacity of this column. Removal is capped by it so a column
            // already full cannot lose water it would have nowhere to put back.
            float headroom = 0.0f;
            for (uint j = 0; j < rows; ++j) {
                if (IsSolid(int3(x, int(j), z)))
                    continue;
                headroom += 1.0f - sColumn[j];
            }

            for (uint j = 0; j < rows; ++j) {
                if (removed >= headroom)
                    break;
                const float cellLo = BasinMinY + float(j) * CellSize;
                const float cellHi = cellLo + CellSize;
                const float overlap = min(cellHi, spanHi) - max(cellLo, spanLo);
                if (overlap <= 0.0f)
                    continue;
                const float fraction = saturate(overlap / CellSize);
                const float taken = min(min(sColumn[j], fraction), headroom - removed);
                sColumn[j] -= taken;
                removed += taken;
            }
        }
    }

    Removed[uint(x) + Width * uint(z)] = float2(removed, surfaceFreeCapacity);
    // Exactly one write per cell, always reached, so no path can leave the
    // destination stale. Solid cells are written as zero too: after a ping-pong
    // they would otherwise keep the other buffer's stale alpha, which leaks into
    // the column integral and silently loses mass.
    for (uint j = 0; j < rows; ++j) {
        const int3 c = int3(x, int(j), z);
        AlphaOut[c] = float4(IsSolid(c) ? 0.0f : saturate(sColumn[j]), 0.0f, 0.0f, 0.0f);
    }
}
