// T850 PBF: secondary (diffuse) particles: spray, foam and air bubbles.
//
// Ihmsen et al. 2012. Particles emitted by CS_PbfWhitewater live in a ring buffer and are
// classified every update by the number of fluid particles around them:
//
//   spray   (< 6 fluid neighbours)   ballistic: gravity and a little air drag
//   foam    (6..20)                  rides the fluid: velocity = SPH average of the neighbours
//   bubble  (> 20)                   buoyancy plus drag toward the fluid velocity; it rises,
//                                    reaches the surface, loses neighbours and becomes foam
//
// Spray that lands on the deck or the coping wets it and dies; anything that leaves the pool
// region dies. The class is stored in velocity.w for the renderer (0 spray, 1 foam, 2 bubble).
//
// One thread per pool slot; free slots have life <= 0.

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
StructuredBuffer<float4> Positions : register(t0);    // fluid: xyz, w = alive

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<float4> Velocities : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> GridBuffer : register(t2);

// Two float4 per secondary particle: [0] position, life (<= 0 = free slot), [1] velocity, class.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<float4> Diffuse : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
RWStructuredBuffer<uint> Wetness : register(u1);      // 0..65535 per texel

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
    const uint k = id.x;
    if (k >= DiffuseCapacity) return;
    const float4 state = Diffuse[k * 2u + 0u];
    if (state.w <= 0.0f) return;
    float3 x = state.xyz;
    float3 v = Diffuse[k * 2u + 1u].xyz;
    float life = state.w;
    const float dt = DeltaTime;

    // Fluid neighbours: count and kernel-weighted mean velocity.
    const uint cells = GridX * GridY * GridZ;
    const float h = max(CellSize, 1.0e-4f);
    const float invH = 1.0f / h;
    const int cx = (int)floor((x.x - GridOriginX) * invH);
    const int cy = (int)floor((x.y - GridOriginY) * invH);
    const int cz = (int)floor((x.z - GridOriginZ) * invH);
    float neighbours = 0.0f;
    float weightSum = 0.0f;
    float3 fluidVelocity = float3(0.0f, 0.0f, 0.0f);
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
        for (uint n = 0u; n < count; ++n) {
            const uint j = GridBuffer[cells * PBFCellCapacity + cell * PBFCellCapacity + n];
            if (j >= ParticleCount) continue;
            const float4 pj = Positions[j];
            if (pj.w < 0.5f) continue;
            const float3 d = x - pj.xyz;
            const float r2 = dot(d, d);
            if (r2 >= h * h) continue;
            const float w = 1.0f - sqrt(r2) * invH;
            weightSum += w;
            fluidVelocity += Velocities[j].xyz * w;
            neighbours += 1.0f;
        }
    }
    fluidVelocity /= max(weightSum, 1.0e-6f);

    const float3 gravity = float3(0.0f, -Gravity, 0.0f);
    float kind;
    if (neighbours < 6.0f) {
        kind = 0.0f;                                   // spray
        v += gravity * dt;
        v *= max(1.0f - SprayDrag * dt, 0.0f);
        life -= dt;
    } else if (neighbours <= 20.0f) {
        kind = 1.0f;                                   // foam
        v = fluidVelocity;
        life -= dt;
    } else {
        kind = 2.0f;                                   // bubble
        v += -BubbleBuoyancy * gravity * dt + BubbleDrag * (fluidVelocity - v);
        life -= dt;
    }
    x += v * dt;

    // Collisions: inside the basin footprint the walls and the floor hold everything below
    // the coping; outside it the deck catches spray, which wets it.
    const bool inside = x.x > BasinMinX && x.x < BasinMaxX && x.z > BasinMinZ && x.z < BasinMaxZ;
    if (inside) {
        if (x.y < BasinMinY + 0.01f) {
            x.y = BasinMinY + 0.01f;
            v.y = max(v.y, 0.0f);
        }
    } else if (x.y < max(DeckY, BasinMaxY) + 0.01f) {
        StampWetness(x.xz, 0.35f + 0.65f * saturate(life));
        life = 0.0f;
    }
    const float3 centre = 0.5f * float3(BasinMinX + BasinMaxX, BasinMinY + BasinMaxY, BasinMinZ + BasinMaxZ);
    // Bit-pattern finiteness: `isfinite` lowers to OpIsNan, which Tint's SPIR-V reader aborts
    // on (WebGPU). See CS_FluidDiagnostics3D.hlsl.
    const uint3 xBits = asuint(x);
    const bool xFinite = all((xBits & 0x7F800000u) != 0x7F800000u);
    if (x.y < BasinMinY - 0.5f || any(abs(x - centre) > 25.0f) || !xFinite) life = 0.0f;

    Diffuse[k * 2u + 0u] = float4(x, life);
    Diffuse[k * 2u + 1u] = float4(v, kind);
}
