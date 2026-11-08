// T850 Position Based Fluids: density constraint and lambda (Algorithm 1 lines 9-11).
//
//   C_i = rho_i / rho0 - 1                          (eq 1)
//   rho_i = sum_j W(p_i - p_j, h)                   (eq 2, unit mass dropped)
//   lambda_i = -C_i / (sum_k |grad_k C_i|^2 + eps)  (eq 11, CFM)
//
// sum_k |grad_k C_i|^2 has the two cases of eq 8: |sum_j grad W|^2 for k = i and
// |grad W|^2 for every neighbour j. Both are accumulated here over the fixed set of
// neighbours from the predicted-position grid; the solve pass re-reads positions.

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
StructuredBuffer<uint> GridBuffer : register(t1);

// x = lambda, y = density, z = |sum_j grad W|^2, w = neighbour count
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<float4> Lambda : register(u0);


// ---------------------------------------------------------------------------
// Kernel NORMALISATION.
//
// The paper (eq 12/14) divides the pressure impulse by rho0 and works in units where a
// full neighbourhood integrates to rho0 = 1. The raw Muller Poly6/Spiky constants are in
// SI-scale units: for this solver's lattice (h = 0.52 m, spacing 0.13 m) a full
// neighbourhood sums to rho ~= 455 and |gradW| ~= 850 /m^4. Feeding those into
// dp = (1/rho0) sum (lambda_i + lambda_j + s) gradW makes every correction ~2e4 too
// SMALL (a 5 % density error asked for 6.5 mm and produced 3e-7 m), so gravity wins and
// the position projection does nothing.
//
// Fix: scale both kernels by the MASS normalisation  m / rho0 = s^3 / 1, i.e. divide by
// (rho_lattice * s^3), which the host passes as RestScale. For a lattice this factor is
// 1 by construction, so the density becomes dimensionless (lattice -> 1) while the
// gradient stays a true spatial derivative of the scaled kernel. A 5 % density error then
// yields a few millimetres of position correction per iteration instead of 3e-7 m.
// ---------------------------------------------------------------------------
float KernelScaleValue()
{
    // rho_lattice * spacing^3, passed from the host (PbfFluid::LatticeRestDensity * r^3).
    return max(RestScale, 1e-6f);
}

float Poly6(float r2)
{
    const float h = SmoothingRadius;
    if (r2 >= h * h) return 0.0f;
    const float d = h * h - r2;
    return (315.0f / (64.0f * 3.14159265f * pow(h, 9.0f))) * d * d * d / KernelScaleValue();
}

float3 SpikyGrad(float3 d)
{
    const float r = length(d);
    const float h = SmoothingRadius;
    if (r <= 1e-6f || r >= h) return float3(0.0f, 0.0f, 0.0f);
    const float s = -45.0f / (3.14159265f * pow(h, 6.0f)) * (h - r) * (h - r) / r;
    return d * (s / KernelScaleValue());
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;
    const uint cells = GridX * GridY * GridZ;
    const float inv = 1.0f / SmoothingRadius;   // cell = h (see CS_PbfBuildGrid)
    // Dead particles have no neighbours and no constraint. Writing zeros keeps every
    // downstream read (solve, XSPH, diagnostics) defined.
    if (Predicted[i].w < 0.5f) {
        Lambda[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float3 pi = Predicted[i].xyz;

    const int cx = clamp((int)floor((pi.x - GridOriginX) * inv), 0, (int)GridX - 1);
    const int cy = clamp((int)floor((pi.y - GridOriginY) * inv), 0, (int)GridY - 1);
    const int cz = clamp((int)floor((pi.z - GridOriginZ) * inv), 0, (int)GridZ - 1);

    float density = 0.0f;
    float3 gradSum = float3(0.0f, 0.0f, 0.0f);
    float gradSqSum = 0.0f;
    uint neighbours = 0u;
    const float h2 = SmoothingRadius * SmoothingRadius;

    // No wall mirroring. It added density mass without a matching gradient term, so lambda
    // was inconsistent, and it mirrored across faces the particle was not actually near
    // (above the rim, outside the footprint, and the open top). Boundary under-density is
    // handled the published way: a unilateral constraint, and (if walls still stack) Akinci
    // boundary particles, which contribute to both density and gradient.
    [loop]
    for (int dz = -1; dz <= 1; ++dz)
    [loop]
    for (int dy = -1; dy <= 1; ++dy)
    [loop]
    for (int dx = -1; dx <= 1; ++dx) {
        const int gx = cx + dx, gy = cy + dy, gz = cz + dz;
        if (gx < 0 || gy < 0 || gz < 0 || gx >= (int)GridX || gy >= (int)GridY || gz >= (int)GridZ)
            continue;
        const uint cell = (uint)gx + GridX * ((uint)gy + GridY * (uint)gz);
        const uint count = min(GridBuffer[cell], PBFCellCapacity);
        [loop]
        for (uint k = 0u; k < count; ++k) {
            const uint j = GridBuffer[cells * PBFCellCapacity + cell * PBFCellCapacity + k];
            if (j >= ParticleCount) continue;
            const float3 pj = Predicted[j].xyz;
            const float3 d = pi - pj;
            const float r2 = dot(d, d);
            if (r2 >= h2) continue;
            density += Poly6(r2);
            const float3 g = SpikyGrad(d);   // zero for j == i (r = 0)
            gradSum += g;
            gradSqSum += dot(g, g);
            ++neighbours;
        }
    }

    // eq 11 as published:
    //   lambda_i = -C_i / (|sum_j gradW|^2 + sum_j |gradW|^2 + eps)
    // The self term is the k = i case, |sum gradW|^2, and the neighbour terms are the
    // per-neighbour |g|^2. eps is a real relaxation, not a numerical crumb: with the
    // dimensionless kernels denom is O(10), so 1e-6 left lambda effectively unregularised.
    const float denomRef = RestDenomReference;
    const float denom = dot(gradSum, gradSum) + gradSqSum + StiffnessEps * denomRef;
    // Unilateral constraint: pressure only resists compression. Clamping at zero is what
    // the paper (§4) and most real-time PBF implementations use; a two-sided constraint
    // makes an under-dense free surface/surface layer pull itself inward.
    const float constraint = max(density / RestDensity - 1.0f, 0.0f);
    const float lambda = denom > 0.0f ? -constraint / denom : 0.0f;

    Lambda[i] = float4(lambda, density, dot(gradSum, gradSum), (float)neighbours);
}
