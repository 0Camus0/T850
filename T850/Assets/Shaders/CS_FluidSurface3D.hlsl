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
        return WaterPlaneY;
    float water = 0.0f;
    for (uint j = 0; j < Height; ++j)
        water += saturate(AlphaIn.Load(int4(int3(x, int(j), z), 0)).x);
    if (water <= 0.001f)
        return WaterPlaneY;
    return BasinMinY + water * CellSize;
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

    const int cx = int(id.x);
    const int cz = int(id.z);

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
    const float foam = SurfaceFoam(cx, cz, surfaceRow);

    SurfaceOut[id.xy] = float4(height, foam, slopeX, slopeZ);
}
