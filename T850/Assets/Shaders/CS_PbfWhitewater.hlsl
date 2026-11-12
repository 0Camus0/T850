// T850 PBF: whitewater sources for the screen-space renderer.
//
// Ihmsen, Akinci, Akinci and Teschner, "Unified spray, foam and air bubbles for particle-based
// fluids", The Visual Computer 28 (2012). Per fluid particle, three potentials, each clamped
// to [min, max] and mapped to 0..1:
//
//   trapped air  sum_j |v_ij| (1 - v_ij.x_ij) W(x_ij)   neighbours colliding (impact, plunge)
//   wave crest   a surface particle moving along its outward normal (crests, lips, sheets)
//   energy       0.5 |v|^2
//
// They drive three things:
//   1. Whitewater carried by the fluid (persistent, one float4 per particle, decaying):
//        x foam       surface whitewater
//        y aeration   entrained air under the surface (milky water after a plunge)
//        z agitation  smoothed speed, which the shade turns into ripple strength
//        w            trapped-air potential, for the debug view
//   2. Emission of secondary particles into the ring buffer `Diffuse` (CS_PbfDiffuse moves
//      and classifies them as spray, foam or bubbles).
//   3. Wetting: water flying low over the deck or the coping marks the wetness map where it
//      lands (the cull retires it a few centimetres lower).
//
// One thread per fluid particle; the neighbour grid must be built from `Positions`.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer PbfWhitewaterConstants : register(b0)
{
    uint  ParticleCount;
    uint  DiffuseCapacity;
    uint  GridX;
    uint  GridY;

    uint  GridZ;
    uint  FrameSeed;
    uint  SurfaceWidth;
    uint  SurfaceDepth;

    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float CellSize;           // neighbour radius h (the grid cell)

    float DeltaTime;          // seconds simulated since the previous whitewater update
    float Spacing;            // particle spacing
    float Gravity;            // m/s^2, positive
    float SurfaceNeighbours;  // at or above: an interior (submerged) particle

    float BasinMinX;
    float BasinMinY;          // floor
    float BasinMinZ;
    float BasinMaxX;

    float BasinMaxY;          // coping top
    float BasinMaxZ;
    float WaterPlaneY;
    float DeckY;

    float TrappedAirMin;
    float TrappedAirMax;
    float CrestMin;
    float CrestMax;

    float EnergyMin;
    float EnergyMax;
    float FoamGain;           // foam per second at full potential
    float FoamDecay;          // exp(-dt / foam lifetime)

    float AerationGain;
    float AerationDecay;
    float SprayRate;          // secondary particles per second per fluid particle (trapped air)
    float CrestRate;          // secondary particles per second per fluid particle (crests)

    float DiffuseLifetime;
    float BubbleBuoyancy;
    float BubbleDrag;
    float SprayDrag;

    float WetOriginX;
    float WetOriginZ;
    float WetTexelSize;
    float WetDecay;           // exp(-dt / drying time)

    uint  WetWidth;
    uint  WetHeight;
    uint  Mode;               // CS_PbfFields only
    float CausticGain;        // CS_PbfFields only
};

#define PBFCellCapacity 64u

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Positions : register(t0);    // xyz, w = alive

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<float4> Velocities : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> GridBuffer : register(t2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<float4> Whitewater : register(u0);

// Two float4 per secondary particle: [0] position, life (<= 0 = free slot), [1] velocity, class.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
RWStructuredBuffer<float4> Diffuse : register(u1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
RWStructuredBuffer<uint> Counter : register(u2);      // [0] ring cursor

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(7, 0)]]
#endif
RWStructuredBuffer<uint> Wetness : register(u3);      // 0..65535 per texel

uint Pcg(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random01(inout uint seed)
{
    seed = Pcg(seed);
    return float(seed >> 8u) * (1.0f / 16777216.0f);
}

float ClampMap(float value, float lo, float hi)
{
    return saturate((value - lo) / max(hi - lo, 1.0e-6f));
}

// Marks the wetness map around a landing point (3x3 texels, soft edge).
void StampWetness(float2 xz, float amount)
{
    if (WetWidth == 0u || WetHeight == 0u) return;
    const float2 t = (xz - float2(WetOriginX, WetOriginZ)) / max(WetTexelSize, 1.0e-4f);
    const int2 c = int2(floor(t));
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    [unroll]
    for (int dx = -1; dx <= 1; ++dx) {
        const int2 texel = c + int2(dx, dy);
        if (texel.x < 0 || texel.y < 0 || texel.x >= int(WetWidth) || texel.y >= int(WetHeight)) continue;
        const float f = saturate(1.0f - length(t - (float2(texel) + 0.5f)) / 1.6f);
        const uint value = uint(saturate(amount * f) * 65535.0f);
        if (value > 0u) InterlockedMax(Wetness[uint(texel.x) + uint(texel.y) * WetWidth], value);
    }
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount) return;
    const float4 self = Positions[i];
    if (self.w < 0.5f) {
        Whitewater[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float3 xi = self.xyz;
    const float3 vi = Velocities[i].xyz;

    const uint cells = GridX * GridY * GridZ;
    const float h = max(CellSize, 1.0e-4f);
    const float invH = 1.0f / h;
    const int cx = clamp((int)floor((xi.x - GridOriginX) * invH), 0, (int)GridX - 1);
    const int cy = clamp((int)floor((xi.y - GridOriginY) * invH), 0, (int)GridY - 1);
    const int cz = clamp((int)floor((xi.z - GridOriginZ) * invH), 0, (int)GridZ - 1);

    float trapped = 0.0f;
    float3 gradient = float3(0.0f, 0.0f, 0.0f);
    float neighbours = 0.0f;
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
            if (j == i || j >= ParticleCount) continue;
            const float4 pj = Positions[j];
            if (pj.w < 0.5f) continue;
            const float3 d = xi - pj.xyz;
            const float r2 = dot(d, d);
            if (r2 >= h * h || r2 < 1.0e-10f) continue;
            const float r = sqrt(r2);
            const float w = 1.0f - r * invH;
            const float3 vij = vi - Velocities[j].xyz;
            const float vlen = length(vij);
            // (1 - v_ij . x_ij) is 2 for particles running into each other, 0 when parting.
            if (vlen > 1.0e-4f) trapped += vlen * (1.0f - dot(vij / vlen, d / r)) * w;
            gradient += d / r * w;   // points away from the neighbours: outward at a surface
            neighbours += 1.0f;
        }
    }

    const float speed = length(vi);
    const float surfaceness = 1.0f - saturate(neighbours / max(SurfaceNeighbours, 1.0f));
    const float gradientLength = length(gradient);
    const float3 outwardNormal = gradientLength > 1.0e-4f ? gradient / gradientLength : float3(0.0f, 1.0f, 0.0f);
    const float outward = speed > 1.0e-3f ? dot(vi / speed, outwardNormal) : 0.0f;
    const float crest = surfaceness * saturate((outward - 0.4f) / 0.6f) * speed;
    const float energy = 0.5f * speed * speed;
    const float trappedPotential = ClampMap(trapped, TrappedAirMin, TrappedAirMax);
    const float crestPotential = ClampMap(crest, CrestMin, CrestMax);
    const float energyPotential = ClampMap(energy, EnergyMin, EnergyMax);
    const float source = (trappedPotential + crestPotential) * energyPotential;

    // Whitewater carried by the fluid: foam at the surface, entrained air below it. Foam that
    // is pulled under becomes air; air that reaches the surface becomes foam again.
    const float4 previous = Whitewater[i];
    float foam = previous.x * FoamDecay;
    float aeration = previous.y * AerationDecay;
    const bool submerged = neighbours >= SurfaceNeighbours;
    if (submerged) {
        aeration += source * AerationGain * DeltaTime + foam * 0.5f;
        foam *= 0.5f;
    } else {
        foam += source * FoamGain * DeltaTime + aeration * (1.0f - AerationDecay);
    }
    const float targetAgitation = saturate((speed - 0.05f) / 0.8f);
    const float agitation = lerp(previous.z, targetAgitation, 1.0f - exp(-DeltaTime / 0.25f));
    Whitewater[i] = float4(min(foam, 2.0f), min(aeration, 2.0f), agitation, trappedPotential);

    // Secondary particles, placed in a cylinder around the velocity (Ihmsen et al. 2012).
    if (DiffuseCapacity > 0u && source > 0.0f && DeltaTime > 0.0f) {
        uint seed = i * 9781u + FrameSeed * 6271u + 1u;
        const float expected = energyPotential * (SprayRate * trappedPotential + CrestRate * crestPotential) * DeltaTime;
        uint emit = uint(expected);
        if (Random01(seed) < frac(expected)) emit += 1u;
        emit = min(emit, 4u);
        if (emit > 0u) {
            uint base = 0u;
            InterlockedAdd(Counter[0], emit, base);
            const float3 axis = speed > 1.0e-3f ? vi / speed : float3(0.0f, 1.0f, 0.0f);
            const float3 helper = abs(axis.y) < 0.9f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
            const float3 e1 = normalize(cross(axis, helper));
            const float3 e2 = cross(axis, e1);
            [loop]
            for (uint k = 0u; k < emit; ++k) {
                const float radius = 0.5f * Spacing * sqrt(Random01(seed));
                const float angle = 6.2831853f * Random01(seed);
                const float along = Random01(seed);
                const float3 offset = radius * (cos(angle) * e1 + sin(angle) * e2);
                const float3 position = xi + offset + vi * DeltaTime * along;
                const float3 velocity = vi + offset * 2.0f;
                const float life = DiffuseLifetime * (0.5f + Random01(seed));
                const uint slot = (base + k) % DiffuseCapacity;
                Diffuse[slot * 2u + 0u] = float4(position, life);
                Diffuse[slot * 2u + 1u] = float4(velocity, 0.0f);
            }
        }
    }

    // Water about to land on the deck or the coping, or splashing over the coping lip.
    const bool outside = xi.x < BasinMinX || xi.x > BasinMaxX || xi.z < BasinMinZ || xi.z > BasinMaxZ;
    const float edge = min(min(xi.x - BasinMinX, BasinMaxX - xi.x), min(xi.z - BasinMinZ, BasinMaxZ - xi.z));
    if ((outside && xi.y < DeckY + 0.6f && vi.y < 0.5f) || (!outside && edge < 0.15f && xi.y > BasinMaxY))
        StampWetness(xi.xz, 1.0f);
}
