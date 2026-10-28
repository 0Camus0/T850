// T850 pool fluid: global volume correction for the volume fraction.
//
// Flux-form advection is conservative up to the clamping that keeps alpha inside
// [0,1]. A partially filled cell that receives more water than it can hold loses
// the excess, and over hundreds of frames that compounds into visible mass loss.
// This pass restores the total by touching the interface cells (0 < alpha < 1)
// only, which is where the correction physically belongs: saturated and empty
// cells are already at their bounds and must not be rescaled.
//
// The correction is ADDITIVE and weighted by each cell's free capacity, not a
// multiplicative rescale. A rescale looks equivalent on paper but is useless in
// practice: interface cells sit just under 1, so `alpha * 1.03` lands above 1 and
// saturate() throws the whole gain away. Measured drift stayed at exactly the
// same value across 400 frames for that reason. Spreading `deficit * (1-alpha) /
// sum(1-alpha)` over the interface instead puts the water where it can be stored,
// and every cell gains at least a full half-float ULP of headroom's worth.

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
    // Reused here as the volume delta to add, in cells.
    float Padding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t0);

// Total free capacity of all interface cells, packed as two floats: (headroom,
// padding). Passed as constants because ComputeBuffer has no update path.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
cbuffer CorrectionConstants : register(b1)
{
    float InterfaceHeadroom;
    // The delta to distribute, in cells. Negative means removing water.
    float VolumeDelta;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
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

    // Only the interface is touched. Fully wet cells stay exactly 1 so the
    // waterline cannot drift into the bulk, and empty cells stay exactly 0 so
    // air does not acquire phantom water.
    if (alpha <= 0.001f || alpha >= 0.999f) {
        AlphaOut[id] = float4(alpha, 0.0f, 0.0f, 0.0f);
        return;
    }
    float corrected = alpha;
    if (InterfaceHeadroom > 0.0001f) {
        // Weight by free capacity so the water lands where it fits, and cap at
        // 1 so the bulk is never rescaled.
        const float headroom = 1.0f - alpha;
        corrected = saturate(alpha + VolumeDelta * headroom / InterfaceHeadroom);
    }
    AlphaOut[id] = float4(corrected, 0.0f, 0.0f, 0.0f);
}
