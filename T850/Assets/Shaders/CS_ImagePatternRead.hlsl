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
Texture2D<float4> InputTexture : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<uint> Output : register(u0);

uint PackBytes(uint4 value)
{
    return value.x | (value.y << 8u) | (value.z << 16u) | (value.w << 24u);
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= Width || dispatchId.y >= Height)
        return;
    const uint index = dispatchId.y * Width + dispatchId.x;
    const uint4 bytes = uint4(round(saturate(InputTexture.Load(int3(dispatchId.xy, 0))) * 255.0f));
    Output[index] = PackBytes(bytes);
}
