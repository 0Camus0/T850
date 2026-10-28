// T850 pool fluid: initialise the free-surface volume fraction.
// One float per voxel in .x: 1 = water, 0 = air. The row straddling the initial
// waterline holds the fractional value so the surface starts exactly at the
// requested fill level rather than snapping to a cell boundary.

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
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
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

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);
    // Solid voxels hold no water at all.
    if (IsSolid(cell)) {
        AlphaOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // SurfaceLevel is the fractional fill of the domain height. The cell centre
    // sits at (j + 0.5) / Height, so the fill line falls inside whichever cell
    // satisfies fillHeight >= j and fillHeight < j + 1.
    const float fillHeight = SurfaceLevel * float(Height);
    const float cellBottom = float(id.y);
    const float cellTop = cellBottom + 1.0f;

    float alpha;
    if (fillHeight >= cellTop)
        alpha = 1.0f;
    else if (fillHeight <= cellBottom)
        alpha = 0.0f;
    else
        alpha = fillHeight - cellBottom;   // partial cell at the waterline

    AlphaOut[id] = float4(alpha, 0.0f, 0.0f, 0.0f);
}
