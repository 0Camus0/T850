// T850 pool fluid: global volume correction for the volume fraction.
//
// Flux-form advection is conservative up to the clamping that keeps alpha inside
// [0,1]. A partially filled cell that receives more water than it can hold loses
// the excess, and over hundreds of frames that compounds into visible mass loss.
// This pass restores the total by touching the interface cells only, which is
// where the correction physically belongs: saturated and empty cells are already
// at their bounds and must not be rescaled.
//
// The correction is ADDITIVE. A positive delta is weighted by each cell's free
// capacity so the water lands where it fits; a negative delta is weighted by the
// cell's own alpha so removal comes from cells that actually hold water. Cells
// below the air threshold are air for the pressure solve, so water deposited
// there would float forever and is never touched.

#define T8_LIQUID_THRESHOLD 0.5f

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
    // 1 when the rows above the domain are open air, 0 for a closed ceiling.
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t0);

// (InterfaceHeadroom, VolumeDelta, InterfaceAlpha) come from CS_FluidVolumeReduce3D's
// sums, read on the GPU, so the correction never reads the volume back to the CPU.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
cbuffer CorrectionConstants : register(b1)
{
    // Target total alpha in cells: seed volume minus what the bodies displace.
    float ReferenceVolume;
    // Sum slot written by this round's reduction; the other slot is cleared here.
    uint SumSlot;
    float CorrectionPad0;
    float CorrectionPad1;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> AlphaOut : register(u0);

// Fixed-point sums from CS_FluidVolumeReduce3D (whole cells, 1/65536 fractions).
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<uint4> Sums : register(u1);

float Unsplit(uint whole, uint frac)
{
    return (float)whole + (float)frac * (1.0f / 65536.0f);
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint entry = (SumSlot & 1u) * 2u;
    const uint4 sumsA = Sums[entry];
    const uint4 sumsB = Sums[entry + 1u];
    // The next round accumulates into the other slot, so it must start from zero.
    if (all(id == uint3(0u, 0u, 0u))) {
        const uint other = ((SumSlot + 1u) & 1u) * 2u;
        Sums[other] = uint4(0u, 0u, 0u, 0u);
        Sums[other + 1u] = uint4(0u, 0u, 0u, 0u);
    }
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    const float alpha = saturate(AlphaIn.Load(int4(int3(id), 0)).x);

    const float total = Unsplit(sumsA.x, sumsA.y);
    const float InterfaceHeadroom = Unsplit(sumsA.z, sumsA.w);
    const float InterfaceAlpha = Unsplit(sumsB.x, sumsB.y);
    // Within noise, or no interface room to put water / water to take: leave it alone.
    float VolumeDelta = ReferenceVolume - total;
    if (abs(VolumeDelta) <= 0.5f ||
        (VolumeDelta > 0.0f && InterfaceHeadroom <= 1.0f) ||
        (VolumeDelta < 0.0f && InterfaceAlpha <= 1.0f))
        VolumeDelta = 0.0f;

    // Only liquid cells with room are touched. Lost water comes from clipped
    // bulk cells, and depositing into air-classified cells would float it.
    if (alpha < T8_LIQUID_THRESHOLD || alpha >= 0.999f) {
        AlphaOut[id] = float4(alpha, 0.0f, 0.0f, 0.0f);
        return;
    }
    float corrected = alpha;
    if (VolumeDelta > 0.0f && InterfaceHeadroom > 0.0001f) {
        // Weight by free capacity so the water lands where it fits, and cap at
        // 1 so the bulk is never rescaled.
        const float headroom = 1.0f - alpha;
        corrected = saturate(alpha + VolumeDelta * headroom / InterfaceHeadroom);
    } else if (VolumeDelta < 0.0f && InterfaceAlpha > 0.0001f) {
        // Weight by alpha so removal comes from cells that actually hold water.
        corrected = saturate(alpha + VolumeDelta * alpha / InterfaceAlpha);
    }
    AlphaOut[id] = float4(corrected, 0.0f, 0.0f, 0.0f);
}
