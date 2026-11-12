// T850 Position Based Fluids: velocity update and XSPH viscosity (Algorithm 1 lines 20-23).
//
//   v_i <- (x*_i - x_i) / dt                            (line 21)
//   v_i <- v_i + c * sum_j (v_j - v_i) * W(p_i - p_j)   (eq 17)
//   x_i <- x*_i                                          (line 23)
//
// Vorticity confinement (eqs 15-16) is a separate optional kernel; the XSPH pass here
// always runs, because PBD's position projection loses energy and the paper reports
// viscosity as the term that keeps motion coherent.

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
    float WallFriction;   // no-slip boundary viscosity multiplier
};

#define PBFCellCapacity 64u

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
RWStructuredBuffer<float4> Positions : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<float4> Predicted : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
// READ-ONLY. XSPH reads neighbours' velocities, so writing this buffer in the same
// dispatch raced with those reads (the same defect the solve had). Read here, write
// VelocitiesNext, and let the host swap.
StructuredBuffer<float4> Velocities : register(t3);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<float4> VelocitiesNext : register(u4);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<uint> GridBuffer : register(t1);

// Solid intrusions (pool steps): min xyz, max xyz per box. The basin itself is analytic.
struct SolidBox { float4 lo; float4 hi; };

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
StructuredBuffer<SolidBox> SolidBoxes : register(t2);

// The XSPH sum reads the previous field (Velocities, read-only) and writes VelocitiesNext.
// The host copies VelocitiesNext back into the published Velocities buffer afterwards, so
// the address the renderer holds never changes.
//
// Neighbour positions come from `Predicted`, which this kernel does not modify (it only
// copies Predicted into Positions afterwards), so there is no intra-dispatch hazard.
float Poly6(float r2)
{
    const float h = SmoothingRadius;
    if (r2 >= h * h) return 0.0f;
    const float d = h * h - r2;
    return (315.0f / (64.0f * 3.14159265f * pow(h, 9.0f))) * d * d * d;
}

// Missing fraction of a Poly6 neighbourhood at distance d from a solid face. This is the
// same D(d) the density pass adds to the density sum (see CS_PbfDensity.hlsl), so the
// boundary carries the same weight in the velocity average as it does in the constraint.
float BoundaryDensity(float d)
{
    const float u = clamp(d / SmoothingRadius, 0.0f, 1.0f);
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

float BoundaryBasinFraction(float3 p)
{
    float f = 0.0f;
    if (p.x > BasinMinX && p.x < BasinMaxX && p.z > BasinMinZ && p.z < BasinMaxZ)
        f += BoundaryDensity(p.y - BasinMinY);
    if (p.y < WallTopY && p.z > BasinMinZ && p.z < BasinMaxZ) {
        f += BoundaryDensity(p.x - BasinMinX);
        f += BoundaryDensity(BasinMaxX - p.x);
    }
    if (p.y < WallTopY && p.x > BasinMinX && p.x < BasinMaxX) {
        f += BoundaryDensity(p.z - BasinMinZ);
        f += BoundaryDensity(BasinMaxZ - p.z);
    }
    return f;
}

float BoundaryBoxFraction(float3 p, float3 lo, float3 hi)
{
    return BoundaryDensity(length(p - clamp(p, lo, hi)));
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;
    const uint cells = GridX * GridY * GridZ;
    const float inv = 1.0f / SmoothingRadius;   // cell = h (see CS_PbfBuildGrid)
    const float invDt = 1.0f / max(DeltaTime, 1e-6f);

    // Dead particles: keep them parked, zero velocity, and do not commit any motion.
    if (Predicted[i].w < 0.5f) {
        VelocitiesNext[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        Positions[i] = float4(Predicted[i].xyz, 0.0f);
        return;
    }

    const float3 xi = Positions[i].xyz;
    const float3 xp = Predicted[i].xyz;

    float3 vi;
    const float3 vRaw = (xp - xi) * invDt;
    const float speed = length(vRaw);
    vi = speed > MaxVelocity ? vRaw * (MaxVelocity / speed) : vRaw;

    // XSPH: weighted average towards neighbours' velocities.
    const int cx = clamp((int)floor((xp.x - GridOriginX) * inv), 0, (int)GridX - 1);
    const int cy = clamp((int)floor((xp.y - GridOriginY) * inv), 0, (int)GridY - 1);
    const int cz = clamp((int)floor((xp.z - GridOriginZ) * inv), 0, (int)GridZ - 1);
    const float h2 = SmoothingRadius * SmoothingRadius;
    // XSPH as a NORMALISED weighted average (Schechter & Bridson). The paper writes
    // c * sum (vj - vi) W; that is only well scaled because its W is normalised by the
    // neighbour count. With this solver's raw Poly6 magnitudes the unnormalised sum is
    // O(1e3) and instantly destabilises the field, so divide by the kernel sum.
    float3 accum = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;

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
            const float3 d = xp - pj;
            const float r2 = dot(d, d);
            if (r2 >= h2) continue;
            const float3 vj = Velocities[j].xyz;
            const float w = Poly6(r2);
            accum += (vj - vi) * w;
            weightSum += w;
        }
    }
    if (weightSum > 1e-12f)
        vi += ViscosityC * (accum / weightSum);

    // No-slip boundary viscosity. The solid is a neighbour with zero velocity and the
    // missing weight D(d), so the same XSPH coefficient damps the boundary layer:
    //   v_i <- v_i + c * D(d) * (0 - v_i)
    // Without it the analytic boundary is free-slip: the wall shell is held out of the
    // wall but is free to slide down it, and that drove a persistent circulation cell
    // (measured: ~0.3 m/s downward in the one-particle wall layer, ~0.13 m/s at the
    // surface) long after the pool should have settled. The bulk is unaffected because
    // D(d) is zero beyond one smoothing radius.
    float boundary = BoundaryBasinFraction(xp);
    [loop]
    for (uint b = 0u; b < BoxCount; ++b)
        boundary += BoundaryBoxFraction(xp, SolidBoxes[b].lo.xyz, SolidBoxes[b].hi.xyz);
    if (boundary > 0.0f)
        vi *= max(0.0f, 1.0f - WallFriction * ViscosityC * boundary);

    // Inelastic contact. A hard clamp resolves penetration by MOVING the particle, and
    // v = (x* - x)/dt turns that displacement into a kick. At the floor and walls that
    // produced a permanent ~1.5 m/s "boiling" in the corners long after the pool should
    // have settled. Remove the normal component at the solid face; tangential motion
    // (and the splash leaving the surface) is untouched.
    const float cm = ParticleRadius * 0.5f;
    if (xp.y <= BasinMinY + cm + 1e-4f)
        vi.y = 0.0f;
    if (xp.y < WallTopY) {
        if (xp.x <= BasinMinX + cm + 1e-4f) vi.x = 0.0f;
        else if (xp.x >= BasinMaxX - cm - 1e-4f) vi.x = 0.0f;
        if (xp.z <= BasinMinZ + cm + 1e-4f) vi.z = 0.0f;
        else if (xp.z >= BasinMaxZ - cm - 1e-4f) vi.z = 0.0f;
    }

    // Re-clamp: viscosity mixing must not push the field past the solver's speed cap.
    const float vs = length(vi);
    if (vs > MaxVelocity) vi *= MaxVelocity / vs;

    VelocitiesNext[i] = float4(vi, 0.0f);
    Positions[i] = float4(xp, 1.0f);   // alive
}
