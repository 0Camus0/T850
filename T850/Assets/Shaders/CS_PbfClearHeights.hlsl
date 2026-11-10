// T850 Position Based Fluids: zero the packed column-height buffer before the splat.
// Separate from CS_PbfClearGrid because that kernel sizes its work from the neighbour
// grid, while this one must cover exactly SurfaceWidth * SurfaceDepth words.

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
    float WallTopY;   // solid walls end here (the pool rim); open above
    // Surface wind stress (m/s), applied to the free-surface layer in CS_PbfIntegrate.
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
// int to match CS_PbfSurface: D3D interlocked operations require signed operands.
RWStructuredBuffer<int> ColumnHeights : register(u0);

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    // Two words per texel: weighted height sum and weight sum.
    const uint texels = SurfaceWidth * SurfaceDepth * 2u;
    // Four words per thread.
    for (uint k = 0u; k < 4u; ++k) {
        const uint w = id.x * 4u + k;
        if (w < texels)
            ColumnHeights[w] = 0u;
    }
}
