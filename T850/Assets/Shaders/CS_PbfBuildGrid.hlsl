// T850 Position Based Fluids: scatter predicted positions into the neighbour grid.
//
// Grid layout (StructuredBuffer<uint>, `GridBuffer`) shared by every PBF kernel:
//   counts : [0 .. cells*CAP-1]                 one uint per cell
//   buckets: [cells*CAP .. cells*CAP*CAP-1]     CAP particle indices per cell
// cells = GridX*GridY*GridZ, CAP = 64.
//
// No sort, no prefix-sum and no atomic-append exist in this engine, so the grid is a
// fixed-capacity uniform hash. A cell that overflows drops the excess indices from
// its neighbour list; at the shipped density (~5 particles per cell) that never fires.

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
StructuredBuffer<float4> Predicted : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<uint> GridBuffer : register(u0);

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;
    const uint cells = GridX * GridY * GridZ;
    // Cell = h. With h = 2 * spacing a cell holds ~8 particles at rest, so the 64-slot
    // bucket still has 8x compression headroom, and the neighbourhood is 27 cells instead
    // of 125. (h/2 was a workaround for a 4x-spacing kernel; that made every lookup scan
    // 125 cells and is no longer needed.)
    const float inv = 1.0f / SmoothingRadius;
    // Dead particles (retired by the cull) are parked far below the floor. Inserting them
    // would put ~240 parked indices into one clamped cell and pollute every neighbourhood.
    if (Predicted[i].w < 0.5f)
        return;
    const float3 p = Predicted[i].xyz;
    const int cx = clamp((int)floor((p.x - GridOriginX) * inv), 0, (int)GridX - 1);
    const int cy = clamp((int)floor((p.y - GridOriginY) * inv), 0, (int)GridY - 1);
    const int cz = clamp((int)floor((p.z - GridOriginZ) * inv), 0, (int)GridZ - 1);
    const uint cell = (uint)cx + GridX * ((uint)cy + GridY * (uint)cz);
    // InterlockedAdd returns void in D3D HLSL (and in vkd3d-proton): the original value comes
    // out through an in/out parameter. Writing (uint)InterlockedAdd(...) compiles on glslang
    // for Vulkan but fails on every D3D compiler.
    uint slot = 0u;
    InterlockedAdd(GridBuffer[cell], 1u, slot);
    if (slot < PBFCellCapacity)
        GridBuffer[cells * PBFCellCapacity + cell * PBFCellCapacity + slot] = i;
}
