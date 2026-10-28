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
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceOut : register(u0);

// Absolute world Y of the bottom of the solver domain. Must be the real basin
// floor, not the authored water plane, or every height is offset by the depth.
#define T8_BASIN_MIN_Y (-1.60f)
// Authored water plane. R is written as a DELTA from this plane because the water
// mesh already sits at it and the vertex shader adds R to its Y. Writing an
// absolute elevation here would drop the surface by the full depth.
#define T8_WATER_PLANE_Y (-0.04f)

// Column integral of alpha, expressed as a world-space elevation.
// Integrating rather than taking the topmost alpha > 0.5 cell handles a
// partially filled interface cell continuously, avoiding the one-cell
// quantisation jitter that thresholding produces as the surface moves.
//
// A column with no water at all is a wall column (the domain shell is solid, so
// alpha there is exactly 0). Reporting the basin floor for those would drag the
// water mesh's edge vertices down the full depth, so they report the water plane
// instead and contribute a zero delta.
float ColumnElevation(int x, int z)
{
    if (x < 0 || z < 0 || x >= int(Width) || z >= int(Depth))
        return T8_WATER_PLANE_Y;
    float water = 0.0f;
    for (uint j = 0; j < Height; ++j)
        water += saturate(AlphaIn.Load(int4(int3(x, int(j), z), 0)).x);
    if (water <= 0.001f)
        return T8_WATER_PLANE_Y;
    return T8_BASIN_MIN_Y + water * CellSize;
}

// Foam from velocity divergence in the surface band. Divergence is the cheap,
// physically meaningful whitecap signal: stretching surface flow has positive
// divergence, slack water has none. An alpha threshold would give a binary mask.
float SurfaceFoam(int x, int z, int surfaceRow)
{
    float divergence = 0.0f;
    const float scale = 0.5f / CellSize;
    for (int row = surfaceRow - 2; row <= surfaceRow; ++row) {
        const int3 c = int3(x, clamp(row, 0, int(Height) - 1), z);
        const float3 vxp = VelocityIn.Load(int4(c + int3(1, 0, 0), 0)).xyz;
        const float3 vxm = VelocityIn.Load(int4(c - int3(1, 0, 0), 0)).xyz;
        const float3 vyp = VelocityIn.Load(int4(c + int3(0, 1, 0), 0)).xyz;
        const float3 vym = VelocityIn.Load(int4(c - int3(0, 1, 0), 0)).xyz;
        const float3 vzp = VelocityIn.Load(int4(c + int3(0, 0, 1), 0)).xyz;
        const float3 vzm = VelocityIn.Load(int4(c - int3(0, 0, 1), 0)).xyz;
        divergence += ((vxp.x - vxm.x) + (vyp.y - vym.y) + (vzp.z - vzm.z)) * scale;
    }
    return saturate(divergence * 0.35f);
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.z >= Depth)
        return;

    const float elevation = ColumnElevation(int(id.x), int(id.z));
    // R is a delta from the authored water plane, matching the shader contract:
    // the mesh already sits at that plane and adds R to its Y.
    const float height = elevation - T8_WATER_PLANE_Y;

    // Slopes are per-texel height DIFFERENCES over the 0.09 m cell step, matching
    // the shader's slope convention (central difference, no divide by spacing).
    // Dividing here would make the fragment shader's slope scale wrong.
    const float slopeX = (ColumnElevation(int(id.x) + 1, int(id.z)) -
                          ColumnElevation(int(id.x) - 1, int(id.z))) * 0.5f;
    const float slopeZ = (ColumnElevation(int(id.x), int(id.z) + 1) -
                          ColumnElevation(int(id.x), int(id.z) - 1)) * 0.5f;

    // Row holding the waterline, so foam samples the interface band.
    const int surfaceRow = clamp(int((elevation - T8_BASIN_MIN_Y) / CellSize),
                                 1, int(Height) - 1);
    const float foam = SurfaceFoam(int(id.x), int(id.z), surfaceRow);

    SurfaceOut[id.xy] = float4(height, foam, slopeX, slopeZ);
}
