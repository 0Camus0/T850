// T850 pool fluid: global volume correction for the volume fraction.
//
// Flux-form advection is conservative up to the clamping that keeps alpha inside
// [0,1]. A partially filled cell that receives more water than it can hold loses
// the excess, and over hundreds of frames that compounds into visible mass loss.
// This pass restores the total by scaling the interface cells (0 < alpha < 1)
// only, which is where the correction physically belongs: saturated and empty
// cells are already at their bounds and must not be rescaled.

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
    // Reused here as the multiplicative correction applied to interface cells.
    float Padding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> AlphaOut : register(u0);

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    const float alpha = saturate(AlphaIn.Load(int4(int3(id), 0)).x);

    // Only the interface is rescaled. Fully wet cells stay exactly 1 so the
    // waterline cannot drift into the bulk, and empty cells stay exactly 0 so
    // air does not acquire phantom water.
    if (alpha <= 0.001f || alpha >= 0.999f) {
        AlphaOut[id] = float4(alpha, 0.0f, 0.0f, 0.0f);
        return;
    }
    AlphaOut[id] = float4(saturate(alpha * Padding0), 0.0f, 0.0f, 0.0f);
}
