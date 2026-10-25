// T850 pool fluid: derives a renderable 2D surface from the 3D velocity
// volume. One output texel per water column: height, foam, slope X/Z.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Depth;
    uint Iteration;
    float DeltaTime;
    float CellSize;
    float Viscosity;
    float Gravity;
    float WindX;
    float WindY;
    float WindZ;
    float Damping;
    float SurfaceLevel;
    float TimeValue;
    float PressureScale;
    float Padding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> VelocityIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float4> SurfaceIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceOut : register(u0);

float SurfaceHeight(int x, int z)
{
    if (x < 0 || z < 0 || x >= int(Width) || z >= int(Depth))
        return 0.0f;
    return SurfaceIn.Load(int3(x, z, 0)).x;
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.z >= Depth)
        return;

    const int surfaceY = clamp(int(SurfaceLevel * float(Height)), 1, int(Height) - 2);
    // Average two rows around the surface: the top row is a solid-boundary cell
    // whose velocity is clamped to zero, so sampling only there reads nothing.
    const float3 surfaceVelocity =
      (VelocityIn.Load(int4(int3(id.x, surfaceY, id.z), 0)).xyz +
       VelocityIn.Load(int4(int3(id.x, surfaceY - 1, id.z), 0)).xyz) * 0.5f;
    const float vertical = surfaceVelocity.y;
    const float below = VelocityIn.Load(int4(int3(id.x, max(surfaceY - 2, 0), id.z), 0)).y;

    // Wave equation driven by the 3D vertical velocity, damped and coupled to
    // the neighbouring columns so disturbances spread across the pool.
    const float center = SurfaceIn.Load(int3(id.x, id.z, 0)).x;
    const float laplacian =
        SurfaceHeight(int(id.x) + 1, int(id.z)) + SurfaceHeight(int(id.x) - 1, int(id.z)) +
        SurfaceHeight(int(id.x), int(id.z) + 1) + SurfaceHeight(int(id.x), int(id.z) - 1) -
        4.0f * center;

    // Travelling wind waves at a fixed base amplitude, modulated by the local
    // flow energy. A purely energy-scaled amplitude stays at exactly zero while
    // the field is calm, so the surface never starts to move.
    const float energy = saturate(length(surfaceVelocity) * 4.0f);
    const float baseAmplitude = 0.07f;
    const float wave =
        sin(float(id.x) * 0.60f + TimeValue * 2.2f) *
        cos(float(id.z) * 0.85f + TimeValue * 1.6f);
    const float wave2 = sin(float(id.x) * 0.16f + float(id.z) * 0.22f + TimeValue * 0.9f);
    const float target = (wave + wave2 * 0.9f) * baseAmplitude * (0.35f + energy);

    // Relax toward the target instead of integrating an increment, so the wave
    // cannot drift away from the pool's water level.
    float height = lerp(center, target, saturate(DeltaTime * 6.0f)) +
                   (vertical + below * 0.5f) * DeltaTime * 0.35f + laplacian * 0.12f;
    height *= saturate(1.0f - 0.6f * DeltaTime);
    height = clamp(height, -0.45f, 0.45f);

    const float slopeX = (SurfaceHeight(int(id.x) + 1, int(id.z)) - SurfaceHeight(int(id.x) - 1, int(id.z))) * 0.5f;
    const float slopeZ = (SurfaceHeight(int(id.x), int(id.z) + 1) - SurfaceHeight(int(id.x), int(id.z) - 1)) * 0.5f;
    const float foam = saturate(abs(vertical) * 2.0f + length(surfaceVelocity) * 1.5f + abs(height) * 3.0f);
    SurfaceOut[id.xy] = float4(height, foam, slopeX, slopeZ);
}
