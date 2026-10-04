#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer PostProcessConstants : register(b0)
{
    float4 OutputSizeAndParams0;
    float4 Params1;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> HdrTexture : register(t0);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float> AdaptedLuminanceTexture : register(t1);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
SamplerState HdrSampler : register(s0);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
SamplerState LuminanceSampler : register(s1);
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba8]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    const uint2 outputSize = uint2(OutputSizeAndParams0.xy);
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
        return;

    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(outputSize);
    float3 color = HdrTexture.SampleLevel(HdrSampler, uv, 0.0f).rgb;
    float avgLuminance = exp(AdaptedLuminanceTexture.SampleLevel(
        LuminanceSampler, float2(0.5f, 0.5f), 0.0f));

    avgLuminance = max(avgLuminance, 0.001f);
    const float keyValue = 1.03f - (2.0f / (2.0f + log10(avgLuminance + 1.0f)));
    const float linearExposure = keyValue / avgLuminance;
    const float threshold = OutputSizeAndParams0.z;
    const float exposureComp = OutputSizeAndParams0.w;
    color = exp2(log2(max(linearExposure, 0.0001f)) + exposureComp - threshold) * color;

    const float pixelLuminance = max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
    const float whiteLevel = max(Params1.x, 0.001f);
    const float toneMappedLuminance = pixelLuminance *
        (1.0f + pixelLuminance / (whiteLevel * whiteLevel)) / (1.0f + pixelLuminance);
    const float3 toneMapped = toneMappedLuminance * (color / pixelLuminance);
    OutputTexture[dispatchId.xy] = float4(toneMapped, 1.0f);
}
