// T850 pool fluid: sums the volume fraction on the GPU for the volume correction.
//
// The correction used to copy the whole alpha volume into a buffer and read it back
// to the CPU every few frames: a full GPU stall (10-15 ms hitches). This kernel sums
// the same three quantities into a tiny buffer that CS_FluidVolumeCorrect3D reads
// directly, so the correction never leaves the GPU.
//
// Sums[2*slot]   = (total whole, total frac, headroom whole, headroom frac)
// Sums[2*slot+1] = (interface alpha whole, interface alpha frac, 0, 0)
// Each quantity is split into whole cells and 1/65536 fractions so 32-bit integer
// atomics neither overflow on large grids nor lose the sub-cell precision.

#define T8_LIQUID_THRESHOLD 0.5f

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Depth;
    // The sum slot (0 or 1) this round accumulates into.
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
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<uint4> Sums : register(u0);

groupshared float3 g_partial[64];

void AddSplit(uint entry, uint component, float value)
{
    const float whole = floor(value);
    const uint wholeBits = (uint)whole;
    const uint fracBits = (uint)((value - whole) * 65536.0f);
    if (component == 0u) {
        InterlockedAdd(Sums[entry].x, wholeBits);
        InterlockedAdd(Sums[entry].y, fracBits);
    } else {
        InterlockedAdd(Sums[entry].z, wholeBits);
        InterlockedAdd(Sums[entry].w, fracBits);
    }
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    // Same band as the correction kernel: liquid cells with room, [threshold, 0.999).
    float3 value = float3(0.0f, 0.0f, 0.0f);
    if (id.x < Width && id.y < Height && id.z < Depth) {
        const float alpha = AlphaIn.Load(int4(int3(id), 0)).x;
        value.x = alpha;
        if (alpha >= T8_LIQUID_THRESHOLD && alpha < 0.999f)
            value.yz = float2(1.0f - alpha, alpha);
    }
    g_partial[index] = value;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (index < stride)
            g_partial[index] += g_partial[index + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (index == 0u) {
        const uint entry = (Iteration & 1u) * 2u;
        const float3 sum = max(g_partial[0], float3(0.0f, 0.0f, 0.0f));
        AddSplit(entry, 0u, sum.x);
        AddSplit(entry, 1u, sum.y);
        AddSplit(entry + 1u, 0u, sum.z);
    }
}
