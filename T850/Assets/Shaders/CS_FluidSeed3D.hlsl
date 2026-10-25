// T850 pool fluid: deterministic seed for the velocity volume.
// Writes a known, spatially varying field so every later stage has defined
// input instead of uninitialised memory.

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
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> VelocityOut : register(u0);

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
#ifdef SEED_DIVERGENT
    // Zero-mean divergence: vx = sin(2*pi*x/W) has dvx/dx averaging to zero over
    // a period. A field with net divergence has no Poisson solution under wall
    // Neumann conditions, so pressure would grow without bound instead of
    // projecting.
    const float phase = (float(id.x) + 0.5f) / float(Width) * 6.2831853f;
    VelocityOut[id] = float4(sin(phase) * 0.5f, 0.0f, 0.0f, 0.0f);
#else
    // 1.0 m/s along +X everywhere: a known field the diagnostics can assert.
    VelocityOut[id] = float4(1.0f, 0.0f, 0.0f, 0.0f);
#endif
}
