// T850 pool fluid, pass 2 of 2: spread the displaced volume over the free surface.
//
// The obstacle opened a cavity in pass 1 and recorded, per column, how much water
// it removed. This pass puts that water back, but into OTHER columns, which is
// what turns "the body took up space" into a visible rise of the surface around
// it. Putting it back on top of the same column would keep the pool perfectly flat
// (that was the previous behaviour, and it was invisible).
//
// One thread per COLUMN. The distribution weight is the column's free capacity
// above its current surface, so every column rises by the same amount of
// elevation regardless of how much room it happened to have. That is what a
// communicating body of water actually does.
//
// The total to spread is summed in CPU-side from the same Removed buffer that
// pass 1 wrote, so the two passes cannot disagree about the volume.

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

// Totals from CS_FluidSphereSum3D: (volume displaced, total surface free
// capacity). Read straight from the GPU so no CPU round trip is needed.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<float2> Totals : register(t9);

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

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.z >= Depth)
        return;

    const int x = int(id.x);
    const int z = int(id.z);
    const int rows = int(min(Height, 64u));

    // Copy the column through, then add water on top of its current surface.
    for (int j = rows - 1; j >= 0; --j) {
        const int3 c = int3(x, j, z);
        AlphaOut[c] = float4(IsSolid(c) ? 0.0f : saturate(AlphaIn.Load(int4(c, 0)).x), 0.0f, 0.0f, 0.0f);
    }

    float2 totals = Totals[0];
    if (totals.x <= 0.0f || totals.y <= 0.0001f)
        return;

    // Find the topmost wet cell: that is this column's current surface.
    int top = -1;
    for (int j = rows - 1; j >= 0; --j) {
        const int3 c = int3(x, j, z);
        if (IsSolid(c))
            continue;
        if (saturate(AlphaIn.Load(int4(c, 0)).x) > 0.001f) {
            top = j;
            break;
        }
    }
    if (top < 0)
        return;   // wall column with no water: it cannot rise.

    const float surfaceAlpha = saturate(AlphaIn.Load(int4(int3(x, top, z), 0)).x);
    const float freeCapacity = 1.0f - surfaceAlpha;
    if (freeCapacity <= 0.0f)
        return;

    // Same rise in elevation for every column: the share of the displaced volume
    // is proportional to the room this column has above its surface.
    float remaining = totals.x * freeCapacity / totals.y;

    // Fill the fractional top cell first, then whole cells above it.
    {
        const float room = 1.0f - surfaceAlpha;
        const float added = min(room, remaining);
        AlphaOut[int3(x, top, z)] = float4(saturate(surfaceAlpha + added), 0.0f, 0.0f, 0.0f);
        remaining -= added;
    }
    for (int j = top + 1; j < rows && remaining > 0.0f; ++j) {
        const int3 c = int3(x, j, z);
        if (IsSolid(c))
            break;
        const float added = min(1.0f, remaining);
        AlphaOut[c] = float4(added, 0.0f, 0.0f, 0.0f);
        remaining -= added;
    }
}
