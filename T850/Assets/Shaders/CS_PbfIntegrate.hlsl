// T850 Position Based Fluids (Macklin & Muller 2013): step 1+2 of Algorithm 1.
//
//   vi <- vi + dt * f_ext(xi)
//   x*i <- xi + dt * vi
//
// Every PBF kernel shares this cbuffer layout so one BuildConstants() feeds them all.
// Grid is the NEIGHBOUR grid (cell = h), not a fluid grid: PBF is Lagrangian.

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

// Position (xyz) + flags (w) per particle.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
RWStructuredBuffer<float4> Positions : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float4> Predicted : register(u1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<float4> Velocities : register(u2);

// Solid intrusions (pool steps): min xyz, max xyz per box, float4 pairs.
struct SolidBox { float4 lo; float4 hi; };

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<SolidBox> SolidBoxes : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
// x = lambda, y = density. Read only for the free-surface test that gates wind.
StructuredBuffer<float4> Lambda : register(t1);

float3 LoadPosition(uint i) { return Positions[i].xyz; }
// w is the alive flag: 1 while the particle is simulated, 0 once the cull retires it.
void StorePredicted(uint i, float3 p, float alive) {
    Predicted[i] = float4(p, alive);
}

// Push a point out of every solid box and back inside the basin shell.
// The basin walls are the pool's inner faces, so a particle may never leave it.
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
        // Solid up to the rim (the authored freeboard). Stopping at the water plane left a
        // hole through which a splash crossed the wall and was snapped onto the deck.
        if (p.y < WallTopY) {
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
    // Retired particles are parked at y = -1000. They must not receive gravity, wind or a
    // collision clamp: outside the footprint the deck clamp used to lift them to y = 0.185,
    // where they shared one grid cell and were launched at the velocity cap every substep.
    if (Positions[i].w < 0.5f) {
        Velocities[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        StorePredicted(i, LoadPosition(i), 0.0f);
        return;
    }
    float3 v = Velocities[i].xyz;
    v.y -= Gravity * DeltaTime;

    // Wind: a tangential drag on the free-surface layer only. Height above the resting
    // plane alone is not a surface test -- on this scene it matched the whole airborne
    // sphere. The previous density pass (Lambda.y) is the honest proxy: interior particles
    // sit at ~RestDensity, the free surface below ~0.9, and particles that have never been
    // measured read 0 and are skipped.
    {
        const float3 p0 = Positions[i].xyz;
        const float windSpeed = length(float2(WindX, WindZ));
        const float density = Lambda[i].y;
        const bool freeSurface = density > 1e-4f && density < 0.9f * RestDensity;
        if (windSpeed > 0.0f && freeSurface && p0.y > WaterPlaneY - ParticleRadius) {
            const float3 windDir = float3(WindX / windSpeed, 0.0f, WindZ / windSpeed);
            // Air drag is ~1e-3 of water; multiply so the effect is visible at this scale.
            const float drag = WindStrength * windSpeed * 1.0e-3f;
            const float3 rel = windDir * windSpeed - v;
            v += float3(rel.x, 0.0f, rel.z) * min(drag * DeltaTime, 0.5f);
        }
    }
    const float speed = length(v);
    if (speed > MaxVelocity)
        v *= MaxVelocity / speed;
    Velocities[i] = float4(v, 0.0f);

    const float3 p = LoadPosition(i) + v * DeltaTime;
    StorePredicted(i, Collide(p, LoadPosition(i)), 1.0f);
}
