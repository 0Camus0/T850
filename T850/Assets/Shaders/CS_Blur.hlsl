#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer BlurConstants : register(b0)
{
    uint2 OutputSize;
    uint KernelSize;
    uint Direction;
    float4 RadiusAndPadding;
    float4 Weights[6];
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> InputTexture : register(t0);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
SamplerState InputSampler : register(s0);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba8]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

float GetWeight(uint index)
{
    return Weights[index >> 2][index & 3];
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= OutputSize.x || dispatchId.y >= OutputSize.y)
        return;

    uint inputWidth;
    uint inputHeight;
    InputTexture.GetDimensions(inputWidth, inputHeight);
    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(OutputSize);
    const float2 texelStep = RadiusAndPadding.x / float2(max(inputWidth, 1u), max(inputHeight, 1u));
    const float2 direction = Direction == 0u ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
    const float origin = -((float(KernelSize) - 1.0f) * 0.5f);

    float3 sum = 0.0f.xxx;
    [loop]
    for (uint index = 0; index < KernelSize; ++index) {
        const float offset = origin + float(index);
        sum += GetWeight(index) * InputTexture.SampleLevel(
            InputSampler, uv + direction * offset * texelStep, 0.0f).rgb;
    }
    OutputTexture[dispatchId.xy] = float4(sum, 1.0f);
}
