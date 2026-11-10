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
    float WallTopY;   // solid walls end here (the pool rim); open above
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

// Solid intrusions (pool steps): min xyz, max xyz per box. The basin itself is analytic.
struct SolidBox { float4 lo; float4 hi; };

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<SolidBox> SolidBoxes : register(t2);


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

// Coincident particles: at r = 0 the Spiky gradient is undefined. Two particles can reach the
// same position (a deep compression step, or both clamped onto one wall corner); returning
// zero there let the pair stay coincident for the rest of the run. Give the singularity a
// deterministic direction from the pair id and evaluate at a small radius.
float3 PairOffset(float3 d, uint i, uint j)
{
    if (dot(d, d) > 1e-8f) return d;
    uint h = (i * 73856093u) ^ (j * 19349663u);
    h ^= h >> 13;
    h *= 16777619u;
    h ^= h >> 15;
    const float3 raw = float3((float)(h & 255u) - 127.5f,
                              (float)((h >> 8) & 255u) - 127.5f,
                              (float)((h >> 16) & 255u) - 127.5f);
    return raw * (1.0e-4f / max(length(raw), 1e-3f));
}

float3 SpikyGrad(float3 d)
{
    const float r = length(d);
    const float h = SmoothingRadius;
    if (r <= 1e-6f || r >= h) return float3(0.0f, 0.0f, 0.0f);
    const float s = -45.0f / (3.14159265f * pow(h, 6.0f)) * (h - r) * (h - r) / r;
    return d * (s / KernelScaleValue());
}


// ---------------------------------------------------------------------------
// Analytic solid boundary (Akinci et al. 2012, "Versatile rigid-fluid coupling", in its
// continuous form). The container is a set of analytic surfaces, so the missing half-space
// of the Poly6 kernel can be integrated in closed form instead of sampled with boundary
// particles:
//
//   D(d)   = 0.5 * P(d/h) / P(0)          missing fraction of the density sum
//   D'(d)  = -(315 / (256 h)) (1-(d/h)^2)^4
//   P(u)   = (1-u) - 4/3(1-u^3) + 6/5(1-u^5) - 4/7(1-u^7) + 1/9(1-u^9)
//
// with P(0) = 128/315 (so D(0) = 0.5: a particle on the face is missing half its
// neighbourhood) and D(d >= h) = 0.
//
// WHY. A hard position clamp as the only wall treatment leaves the fluid packed ~25% denser
// than the rest lattice next to a wall, but the density sum only sees fluid and still reads
// ~1.0. The constraint therefore believes the wall layer is at rest (lambda ~ 0) and the
// interior's pressure is free to squeeze that layer along the wall -- the clamp cancels the
// horizontal part of every correction, so the squeeze comes out vertically. That is the
// "water glued to the pool wall" artefact: a one-particle-thick sheet standing 0.5-0.7 m
// above the real surface and never draining.
//
// Feeding the missing wall mass into BOTH the density (so lambda sees the real packing) and
// the gradient (so the wall itself carries pressure, lambda_b = lambda_i) makes the wall a
// participant in the constraint instead of a mirror. The layer then relaxes off the wall.
//
// D' also contributes (D')^2 to the CFM denominator, the same way a fluid neighbour
// contributes |grad W|^2, so lambda stays conditioned.
// ---------------------------------------------------------------------------
float BoundaryDensity(float u)
{
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float u5 = u3 * u2;
    const float u7 = u5 * u2;
    const float u9 = u7 * u2;
    const float p = (1.0f - u)
                  - (4.0f / 3.0f) * (1.0f - u3)
                  + (6.0f / 5.0f) * (1.0f - u5)
                  - (4.0f / 7.0f) * (1.0f - u7)
                  + (1.0f / 9.0f) * (1.0f - u9);
    return 0.5f * p * (315.0f / 128.0f);
}

// dD/dd. Negative: the deficit shrinks as the particle moves away from the solid.
float BoundarySlope(float u)
{
    const float w = 1.0f - u * u;
    const float w2 = w * w;
    return -(315.0f / (256.0f * SmoothingRadius)) * w2 * w2;
}

// n points from the solid into the fluid, d is the distance from the solid face.
void BoundaryAccumulate(float d, float3 n,
                        inout float density, inout float3 gradSum, inout float gradSqSum)
{
    if (d <= 0.0f || d >= SmoothingRadius) return;
    const float u = d / SmoothingRadius;
    const float slope = BoundarySlope(u);   // < 0, so slope*n points toward the solid
    density += BoundaryDensity(u);
    gradSum += slope * n;                   // same convention as SpikyGrad (i -> neighbour)
    gradSqSum += slope * slope;
}

// Pool basin: floor everywhere inside the footprint; four walls up to the rim (open above).
void BoundaryAccumulateBasin(float3 p,
                             inout float density, inout float3 gradSum, inout float gradSqSum)
{
    if (p.x > BasinMinX && p.x < BasinMaxX && p.z > BasinMinZ && p.z < BasinMaxZ) {
        BoundaryAccumulate(p.y - BasinMinY, float3(0.0f, 1.0f, 0.0f), density, gradSum, gradSqSum);
    }
    if (p.y < WallTopY && p.z > BasinMinZ && p.z < BasinMaxZ) {
        BoundaryAccumulate(p.x - BasinMinX, float3( 1.0f, 0.0f, 0.0f), density, gradSum, gradSqSum);
        BoundaryAccumulate(BasinMaxX - p.x, float3(-1.0f, 0.0f, 0.0f), density, gradSum, gradSqSum);
    }
    if (p.y < WallTopY && p.x > BasinMinX && p.x < BasinMaxX) {
        BoundaryAccumulate(p.z - BasinMinZ, float3(0.0f, 0.0f,  1.0f), density, gradSum, gradSqSum);
        BoundaryAccumulate(BasinMaxZ - p.z, float3(0.0f, 0.0f, -1.0f), density, gradSum, gradSqSum);
    }
}

// Step intrusions: nearest point on the AABB, so edges and corners get the right direction.
void BoundaryAccumulateBox(float3 p, float3 lo, float3 hi,
                           inout float density, inout float3 gradSum, inout float gradSqSum)
{
    const float3 dv = p - clamp(p, lo, hi);
    const float d = length(dv);
    if (d <= 1e-5f || d >= SmoothingRadius) return;
    BoundaryAccumulate(d, dv / d, density, gradSum, gradSqSum);
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
            const float3 d = PairOffset(pi - pj, i, j);
            const float r2 = dot(d, d);
            if (r2 >= h2) continue;
            density += Poly6(r2);
            const float3 g = SpikyGrad(d);   // zero for j == i (r = 0)
            gradSum += g;
            gradSqSum += dot(g, g);
            ++neighbours;
        }
    }

    // Solid boundaries are part of the density estimate, not a mirror bolted onto it: the
    // missing half-space contributes to the density, to the gradient sum and to the CFM
    // denominator (see the boundary block above).
    BoundaryAccumulateBasin(pi, density, gradSum, gradSqSum);
    [loop]
    for (uint b = 0u; b < BoxCount; ++b)
        BoundaryAccumulateBox(pi, SolidBoxes[b].lo.xyz, SolidBoxes[b].hi.xyz,
                              density, gradSum, gradSqSum);

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
