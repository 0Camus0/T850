// T850 PBF: per-texel fields for the screen-space water shade.
//
// Mode 0, one thread per wetness texel: the wet deck dries, wetness *= exp(-dt / drying time).
//
// Mode 1, one thread per surface column: caustic focusing of the SIMULATED surface. Sunlight
// refracted by a surface h(x, z) lands displaced by c d grad(h) at depth d (c = 1 - 1/1.33), so
// its intensity at the floor is 1 / (1 + c d lap(h)): a crest (lap < 0) focuses, a trough
// spreads. The column heights from CS_PbfSurface are per-column maxima of particle heights,
// noisy at the particle scale, so they are smoothed over about half a metre first. The field
// modulates the procedural caustic web in CS_ParticleShade, so the bright bands follow the
// sloshing waves.

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

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<int> ColumnHeights : register(t0);   // per column: max height above floor (float bits), wet weight

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<uint> Wetness : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<float> CausticField : register(u1);

// Water depth above the floor at a column, smoothed over a 5x5 neighbourhood of wet columns.
// Returns -1 where no column around is wet.
float SmoothedDepth(int tx, int tz)
{
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll]
    for (int oz = -2; oz <= 2; ++oz)
    [unroll]
    for (int ox = -2; ox <= 2; ++ox) {
        const int sx = tx + ox;
        const int sz = tz + oz;
        if (sx < 0 || sz < 0 || sx >= (int)SurfaceWidth || sz >= (int)SurfaceDepth) continue;
        const uint slot = (uint)sx + SurfaceWidth * (uint)sz;
        if (ColumnHeights[slot * 2u + 1u] <= 0) continue;
        const float w = exp(-0.35f * float(ox * ox + oz * oz));
        sum += asfloat(ColumnHeights[slot * 2u + 0u]) * w;
        weight += w;
    }
    return weight > 0.0f ? sum / weight : -1.0f;
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (Mode == 0u) {
        if (i >= WetWidth * WetHeight) return;
        const uint value = Wetness[i];
        if (value != 0u) Wetness[i] = uint(float(value) * WetDecay);
        return;
    }

    if (i >= SurfaceWidth * SurfaceDepth) return;
    const int tx = int(i % SurfaceWidth);
    const int tz = int(i / SurfaceWidth);
    const float cellX = (BasinMaxX - BasinMinX) / max((float)SurfaceWidth - 1.0f, 1.0f);
    const float cellZ = (BasinMaxZ - BasinMinZ) / max((float)SurfaceDepth - 1.0f, 1.0f);
    const float centre = SmoothedDepth(tx, tz);
    if (centre <= 0.0f) {
        CausticField[i] = 1.0f;
        return;
    }
    // Second differences two columns apart, on the smoothed field; a dry neighbour mirrors.
    const float xm = SmoothedDepth(tx - 2, tz);
    const float xp = SmoothedDepth(tx + 2, tz);
    const float zm = SmoothedDepth(tx, tz - 2);
    const float zp = SmoothedDepth(tx, tz + 2);
    const float hxm = xm > 0.0f ? xm : centre;
    const float hxp = xp > 0.0f ? xp : centre;
    const float hzm = zm > 0.0f ? zm : centre;
    const float hzp = zp > 0.0f ? zp : centre;
    const float laplacian = (hxp + hxm - 2.0f * centre) / (4.0f * cellX * cellX) +
                            (hzp + hzm - 2.0f * centre) / (4.0f * cellZ * cellZ);
    const float focus = 1.0f / max(1.0f + CausticGain * centre * laplacian, 0.35f);
    CausticField[i] = clamp(focus, 0.35f, 2.5f);
}
