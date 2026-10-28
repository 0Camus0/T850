// T850 pool fluid: zero a volume.
//
// Used by the simulation reset. Restoring the initial state means clearing the
// momentum the previous drop injected, otherwise the next drop lands in water that
// is already moving and the results are not comparable. Storage textures here are
// write-only, so there is no in-place clear: this kernel is the write step.

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
RWTexture3D<float4> TargetOut : register(u0);

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    TargetOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
}
