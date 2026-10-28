// T850 pool fluid: applies wind, buoyancy and viscous diffusion to the
// velocity volume, with solid walls on the volume boundary.

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
    // Sustained surface forcing gain, from JSON.
    float ForcingAmplitude;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> VelocityIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> VelocityOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool IsSolid(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

float4 Fetch(int3 cell)
{
    if (IsSolid(cell))
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    return VelocityIn.Load(int4(cell, 0));
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);
    if (IsSolid(cell))
    {
        VelocityOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float4 center = VelocityIn.Load(int4(cell, 0));
    float3 velocity = center.xyz;

    // Viscous diffusion from the six neighbours.
    float4 laplacian = Fetch(cell + int3(1, 0, 0)) + Fetch(cell - int3(1, 0, 0)) +
                       Fetch(cell + int3(0, 1, 0)) + Fetch(cell - int3(0, 1, 0)) +
                       Fetch(cell + int3(0, 0, 1)) + Fetch(cell - int3(0, 0, 1)) -
                       6.0f * center;
    velocity += laplacian.xyz * Viscosity;

    // Wind drives the surface layer. The layer is defined by the surface level;
    // the volume fraction is not visible to this kernel, so alpha-weighted
    // viscosity and gravity live in the alpha-aware kernels instead.
    const float surfaceY = SurfaceLevel * float(Height);
    if (float(id.y) > surfaceY - 2.0f)
    {
        // Amplitude comes from JSON via FluidSettings; 1.0 is the tuned default.
        // Without this the pool decays to perfectly flat within seconds because
        // the seed is a one-shot impulse with no sustained energy input.
        const float gain = max(ForcingAmplitude, 0.0f) * DeltaTime;
        const float stirX = sin(TimeValue * 0.9f + float(id.x) * 0.31f + float(id.z) * 0.17f);
        const float stirZ = cos(TimeValue * 0.7f + float(id.z) * 0.29f + float(id.x) * 0.13f);
        velocity.x += (WindX + stirX * 0.45f) * gain;
        velocity.z += (WindZ + stirZ * 0.45f) * gain;
        velocity.y += (WindY + stirX * stirZ * 0.25f) * gain;
    }

    // Gravity acts on the water; the hydrostatic balance is the pressure solve's
    // job now that the free surface gives it a Dirichlet reference, so the old
    // hand-tuned counter-lift is gone.
    velocity.y -= Gravity * DeltaTime;

    velocity *= saturate(1.0f - Damping * DeltaTime);
    velocity = clamp(velocity, -12.0f, 12.0f);
    VelocityOut[id] = float4(velocity, center.w);
}
