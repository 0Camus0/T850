// T850 Position Based Fluids: one Jacobi position projection (Algorithm 1 lines 12-18).
//
//   s_corr = -k * (W(p_i - p_j, h) / W(dq, h))^n          (eq 13)
//   dp_i   = 1/rho0 * sum_j (lambda_i + lambda_j + s_corr) * grad W(p_i - p_j, h)   (eq 14)
//
// lambda is read from the density pass and held fixed for this iteration (Jacobi, not
// Gauss-Seidel). Positions are corrected in place on `Predicted`; the caller runs this
// kernel `solverIterations` times, re-running the density pass between iterations.

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
// READ-ONLY source. The previous version read and wrote `Predicted` in the same dispatch,
// so a thread could see a neighbour another thread had already moved. That race breaks the
// symmetry of a uniform lattice (corrections that must cancel to zero instead add up) and
// injects energy: a still pool reached ~9.6 m/s with gravity off. Jacobi projection must
// read one buffer and write another.
StructuredBuffer<float4> Predicted : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float4> PredictedNext : register(u1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> GridBuffer : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<float4> Lambda : register(t2);

struct SolidBox { float4 lo; float4 hi; };

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<SolidBox> SolidBoxes : register(t3);


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

// grad_{p_i} W(p_i - p_j, h). Since dW/dr < 0 this points from p_i TOWARD p_j (along -d).
//
// This is the paper's convention and it must stay negative. Combined with
//   lambda_i = -C_i / (|sum gradW|^2 + sum |gradW|^2 + eps),  C_i = rho_i/rho0 - 1,
// it gives: compressed (rho > rho0) -> C>0 -> lambda<0 -> dp = lambda * gradW(inward)
// = OUTWARD. Expanded (rho < rho0) -> lambda>0 -> dp is INWARD. Both correct.
//
// An earlier "fix" negated this to make a compressed cell push its neighbours apart; that
// inverted the whole constraint and made under-dense fluid explode outward instead of
// pulling back. Measured then: a still lattice at rho 0.88 reached 9 m/s in three frames
// with gravity switched off.
float3 SpikyGrad(float3 d)
{
    const float r = length(d);
    const float h = SmoothingRadius;
    if (r <= 1e-6f || r >= h) return float3(0.0f, 0.0f, 0.0f);
    const float s = -45.0f / (3.14159265f * pow(h, 6.0f)) * (h - r) * (h - r) / r;
    return d * (s / KernelScaleValue());
}

float3 Collide(float3 p, float3 prev)
{
    // ---------------------------------------------------------------------------
    // The pool is a CONTAINER, not a closed box: solid floor, four inner walls and the
    // step intrusions, with an OPEN top. The previous version clamped Y to BasinMaxY,
    // which is what made the water bounce off an invisible ceiling.
    //
    // A particle above the rim simply falls in; a particle outside the footprint falls
    // past the coping onto the deck and is removed by the host cull.
    // ---------------------------------------------------------------------------
    const float m = ParticleRadius * 0.5f;
    const bool insideFootprint = p.x > BasinMinX - m && p.x < BasinMaxX + m &&
                                 p.z > BasinMinZ - m && p.z < BasinMaxZ + m;

    if (insideFootprint) {
        // Inside the pool: floor and walls hold the water. Walls only act BELOW the rim,
        // so a splash that rises above the coping can leave.
        p.y = max(p.y, BasinMinY + m);
        if (p.y < WaterPlaneY) {
            p.x = clamp(p.x, BasinMinX + m, BasinMaxX - m);
            p.z = clamp(p.z, BasinMinZ + m, BasinMaxZ - m);
        }
    } else {
        // Outside the footprint: the deck is solid at its surface.
        const float deckY = WaterPlaneY + 0.16f;
        p.y = max(p.y, deckY + m);
    }

    // Pool steps: push out through the face the particle came from, and never move it more
    // than one spacing in a single call.
    //
    // The previous shortest-axis push could teleport a particle from the far side of a
    // 1-2 m step box to its nearest face in one substep. UpdateVelocity divides that
    // displacement by dt, so a deep penetration became a 12 m/s launch -- the fluid fired
    // itself out of the pool on impact. Using the previous position picks the face it
    // actually crossed, and the spacing clamp bounds the per-step velocity injection.
    [loop]
    for (uint b = 0u; b < BoxCount; ++b) {
        const float3 lo = SolidBoxes[b].lo.xyz;
        const float3 hi = SolidBoxes[b].hi.xyz;
        if (p.x <= lo.x || p.x >= hi.x || p.y <= lo.y || p.y >= hi.y || p.z <= lo.z || p.z >= hi.z)
            continue;
        int axis = -1;
        float push = 0.0f;
        float sign = 0.0f;
        // Exit the way the particle entered if the previous position was outside the box.
        if (prev.x <= lo.x)      { axis = 0; sign = -1.0f; push = p.x - lo.x; }
        else if (prev.x >= hi.x) { axis = 0; sign =  1.0f; push = hi.x - p.x; }
        else if (prev.y <= lo.y) { axis = 1; sign = -1.0f; push = p.y - lo.y; }
        else if (prev.y >= hi.y) { axis = 1; sign =  1.0f; push = hi.y - p.y; }
        else if (prev.z <= lo.z) { axis = 2; sign = -1.0f; push = p.z - lo.z; }
        else if (prev.z >= hi.z) { axis = 2; sign =  1.0f; push = hi.z - p.z; }
        if (axis < 0) {
            // Fully inside (started there): fall back to the nearest face.
            const float dxLo = p.x - lo.x, dxHi = hi.x - p.x;
            const float dyLo = p.y - lo.y, dyHi = hi.y - p.y;
            const float dzLo = p.z - lo.z, dzHi = hi.z - p.z;
            float best = dxLo; axis = 0; sign = -1.0f;
            if (dxHi < best) { best = dxHi; axis = 0; sign = 1.0f; }
            if (dyLo < best) { best = dyLo; axis = 1; sign = -1.0f; }
            if (dyHi < best) { best = dyHi; axis = 1; sign = 1.0f; }
            if (dzLo < best) { best = dzLo; axis = 2; sign = -1.0f; }
            if (dzHi < best) { best = dzHi; axis = 2; sign = 1.0f; }
            push = best;
        }
        // 0.1x spacing: UpdateVelocity divides this by dt, so one spacing per call was a
        // 15.6 m/s launch at dt = 1/120. The push repeats every substep until the particle
        // is clear, which costs a few frames but keeps the injected speed below 1.6 m/s.
        push = min(push + m, ParticleRadius * 0.05f);
        if (axis == 0) p.x += sign * push;
        else if (axis == 1) p.y += sign * push;
        else p.z += sign * push;
    }
    return p;
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;
    const uint cells = GridX * GridY * GridZ;
    const float inv = 1.0f / SmoothingRadius;   // cell = h (see CS_PbfBuildGrid)
    // Dead particles are parked and must not be moved, but PredictedNext is read by the
    // copy-back and by the next density pass, so pass the value through with w = 0.
    if (Predicted[i].w < 0.5f) {
        PredictedNext[i] = float4(Predicted[i].xyz, 0.0f);
        return;
    }
    const float3 pi = Predicted[i].xyz;
    const float lambdaI = Lambda[i].x;

    // Reference separation for s_corr: |dq| = 0.3 h, the middle of the paper's 0.1-0.3 h.
    const float dq = 0.3f * SmoothingRadius;
    const float wDq = Poly6(dq * dq);
    const float invWDq = wDq > 1e-12f ? 1.0f / wDq : 0.0f;

    const int cx = clamp((int)floor((pi.x - GridOriginX) * inv), 0, (int)GridX - 1);
    const int cy = clamp((int)floor((pi.y - GridOriginY) * inv), 0, (int)GridY - 1);
    const int cz = clamp((int)floor((pi.z - GridOriginZ) * inv), 0, (int)GridZ - 1);

    float3 dp = float3(0.0f, 0.0f, 0.0f);
    const float h2 = SmoothingRadius * SmoothingRadius;

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
            if (j >= ParticleCount || j == i) continue;
            const float3 pj = Predicted[j].xyz;
            const float3 d = pi - pj;
            const float r2 = dot(d, d);
            if (r2 >= h2) continue;
            const float3 g = SpikyGrad(d);
            // Artificial pressure (eq 13) counteracts TENSILE CLUSTERING, so it is applied
            // only to pairs meaningfully closer than the rest spacing. Evaluating it on the
            // rest lattice (where the kernel ratio is still ~0.56) leaves an unbalanced
            // repulsion at every free surface and inflates the body. The 0.9 factor avoids
            // the float-equality trap where a rest pair reads 1e-8 below the spacing.
            //
            // NOTE: with the compression-only constraint below, this term has no balancing
            // surface attraction, so the shipped scene runs ArtPressureK = 0. The code is
            // kept for a two-sided solver.
            const float w = Poly6(r2);
            const float ratio = w * invWDq;
            const float scorr = r2 < 0.81f * ParticleRadius * ParticleRadius
                ? -ArtPressureK * pow(ratio, ArtPressureN) : 0.0f;
            const float lambdaJ = Lambda[j].x;
            dp += (lambdaI + lambdaJ + scorr) * g;
        }
    }

    // eq 14 exactly: dp = (1/rho0) * sum_j (lambda_i + lambda_j + s_corr) * gradW.
    //
    // There is NO 1/neighbour-count factor. That factor was compensating for a kernel
    // radius of 4x spacing (~250 neighbours) and made the projection ~250x too weak, so
    // the fluid compressed into a single layer on the floor. With h = 2x spacing (~33
    // neighbours) and the published equation the projection has the right magnitude.
    dp *= (1.0f / RestDensity);
    // Hard cap on one correction, sized to 0.1x the spacing (not the kernel radius).
    // UpdateVelocity converts this correction into velocity (v = dp/dt), so the old 0.5x
    // cap was a 7.8 m/s kick for any compressed particle at dt = 1/120: the solver fired
    // particles at the 12 m/s cap while rhoMax stayed at 1.1. 0.013 m keeps the injected
    // speed below 1.6 m/s and still lets compression relax over the frame's iterations.
    const float maxStep = ParticleRadius * 0.1f;
    const float l = length(dp);
    if (l > maxStep)
        dp *= maxStep / l;
    // Collision response at the end of every projection (Algorithm 1 line 14).
    const float3 safe = Collide(pi + dp, pi);
    PredictedNext[i] = float4(safe, 1.0f);
}
