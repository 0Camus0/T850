// T850 Position Based Fluids: clear the neighbour grid.
//
// Grid layout (one StructuredBuffer<uint>, `GridBuffer`), used by every PBF kernel:
//   [0            .. cells*CAP-1]        cell counts
//   [cells*CAP    .. cells*CAP*CAP-1]    bucket indices (CAP per cell)
// where cells = GridX*GridY*GridZ and CAP = PBFCellCapacity.
//
// The engine has no sort / prefix-sum / atomic-append, and no indirect dispatch, so
// the grid is a fixed-capacity uniform hash: each cell owns CAP index slots and one
// count word. Overflow in a cell drops the excess from that cell's neighbour list;
// with CAP=64 and h=0.4 m a 0.09 m lattice puts ~5 particles per cell, so the cap is
// an order of magnitude above the expected occupancy.

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

#define PBFCellCapacity 64u

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
RWStructuredBuffer<uint> GridBuffer : register(u0);

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint cells = GridX * GridY * GridZ;
    const uint wordCount = cells * PBFCellCapacity * 2u;
    // One thread per 4 words; the host dispatches ceil(wordCount / (64*4)) groups.
    for (uint k = 0u; k < 4u; ++k) {
        const uint w = id.x * 4u + k;
        if (w < wordCount)
            GridBuffer[w] = 0u;
    }
}
