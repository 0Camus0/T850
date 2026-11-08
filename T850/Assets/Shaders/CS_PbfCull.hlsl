// T850 Position Based Fluids: retire particles that left the water.
//
// The pool is an OPEN container, so a splash throws particles onto the deck or over the wall.
// Those must not keep participating in the simulation (they would form a puddle that the
// density constraint tries to pull back in), and they must not be drawn.
//
// This is deliberately NOT a compaction. Compaction needs a scratch buffer, a readback to
// learn the surviving count and a handle swap; every one of those invalidated a pointer held
// elsewhere (the particle renderer, a descriptor set recorded earlier in the frame), and that
// is exactly what crashed. Retired particles are instead PARKED: written far below the floor
// with zero velocity. They keep their slot, so buffers keep their size and address for the
// solver's whole lifetime, and they are out of every density neighbourhood and out of frame.
//
// Parking is idempotent and order-independent, so no atomics or barriers are needed.

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
    float RestScale;
    float RestDenomReference;
    float MinNeighbours;
    float Pad6;
    float WindX;
    float WindZ;
    float WindStrength;
    float Pad7;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
RWStructuredBuffer<float4> Positions : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float4> Velocities : register(u1);

// How far below the floor a retired particle is parked. Well outside the neighbour grid and
// the kernel radius, so it cannot influence anything that is still simulated.
#define PARKED_Y (-1000.0f)

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;

    const float3 p = Positions[i].xyz;

    // Already parked: keep the alive flag clear and do nothing else (idempotent).
    if (p.y <= PARKED_Y + 1.0f) {
        Positions[i].w = 0.0f;
        return;
    }

    const float margin = ParticleRadius;
    if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z)) {
        Positions[i] = float4(0.0f, PARKED_Y, 0.0f, 0.0f);   // dead: skipped by every later kernel
        Velocities[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const bool belowFloor = p.y < BasinMinY - 2.0f * margin;
    const float slack = 0.6f;
    const bool outsideXZ = p.x < BasinMinX - slack || p.x > BasinMaxX + slack ||
                           p.z < BasinMinZ - slack || p.z > BasinMaxZ + slack;
    if (!belowFloor && !outsideXZ)
        return;

    Positions[i] = float4(0.0f, PARKED_Y, 0.0f, 0.0f);   // dead: skipped by every later kernel
    Velocities[i] = float4(0.0f, 0.0f, 0.0f, 0.0f);
}
