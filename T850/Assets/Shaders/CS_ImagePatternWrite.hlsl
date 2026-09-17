#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ImagePatternConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Seed;
    uint Padding;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba8]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

uint4 PatternBytes(uint2 pixel)
{
    const uint linearIndex = pixel.y * Width + pixel.x;
    return uint4(
        (linearIndex * 17u + Seed) & 255u,
        (pixel.x * 29u + pixel.y * 11u + Seed * 3u) & 255u,
        (pixel.x * 7u + pixel.y * 31u + Seed * 5u) & 255u,
        255u);
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= Width || dispatchId.y >= Height)
        return;
    OutputTexture[dispatchId.xy] = float4(PatternBytes(dispatchId.xy)) / 255.0f;
}
