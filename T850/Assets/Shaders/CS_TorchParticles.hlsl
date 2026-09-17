#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer TorchParticleConstants : register(b0)
{
    float4x4 ViewProjection;
    float4 EmitterAndTime;
    float4 OutputSizeCountEnabled;
    float4 Motion;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

float Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (float)(value & 0x00ffffffu) / 16777215.0f;
}

float3 ParticleColor(uint index)
{
    const uint paletteIndex = index % 3u;
    if (paletteIndex == 0u)
        return float3(1.0f, 0.04f, 0.01f);
    if (paletteIndex == 1u)
        return float3(1.0f, 0.32f, 0.015f);
    return float3(1.0f, 0.88f, 0.08f);
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    const uint2 outputSize = uint2(OutputSizeCountEnabled.xy);
    if (dispatchId.x >= outputSize.x || dispatchId.y >= outputSize.y)
        return;

    const uint particleCount = (uint)OutputSizeCountEnabled.z;
    if (OutputSizeCountEnabled.w < 0.5f || particleCount == 0u) {
        OutputTexture[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float lifetime = max(Motion.x, 0.001f);
    const float riseHeight = Motion.y;
    const float spread = Motion.z;
    const float baseSize = Motion.w;
    const float time = EmitterAndTime.w;
    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(outputSize);
    const float aspect = (float)outputSize.x / max((float)outputSize.y, 1.0f);

    float3 accumulated = 0.0f.xxx;
    float accumulatedAlpha = 0.0f;

    [loop]
    for (uint index = 0u; index < particleCount; ++index) {
        const float phase = Hash(index * 9781u + 17u);
        const float age = frac(time / lifetime + phase);
        const float angle = Hash(index * 6271u + 43u) * 6.28318530718f;
        const float radialSeed = 0.35f + Hash(index * 3253u + 91u) * 0.65f;
        const float radialDistance = spread * radialSeed * (0.25f + age * 0.75f);
        const float wobble = sin(time * (2.0f + Hash(index * 1877u + 7u) * 2.0f) + angle) * spread * 0.18f;

        float3 position = EmitterAndTime.xyz;
        position.x += cos(angle) * radialDistance + wobble;
        position.z += sin(angle) * radialDistance - wobble * 0.5f;
        position.y += age * riseHeight;

        const float4 clip = mul(ViewProjection, float4(position, 1.0f));
        if (clip.w <= 0.0001f)
            continue;

        const float2 ndc = clip.xy / clip.w;
        const float2 centerUv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        float2 delta = uv - centerUv;
        delta.x *= aspect;

        const float radius = baseSize * lerp(1.0f, 0.45f, age) / max(clip.w, 0.5f);
        const float distanceToEdge = max(abs(delta.x), abs(delta.y));
        const float pixelWidth = 1.0f / max((float)outputSize.y, 1.0f);
        const float edgeSoftness = min(pixelWidth, radius * 0.25f);
        const float shape = 1.0f - smoothstep(
            max(radius - edgeSoftness, 0.0f), radius, distanceToEdge);
        const float fadeIn = smoothstep(0.0f, 0.08f, age);
        const float fadeOut = 1.0f - smoothstep(0.55f, 1.0f, age);
        const float intensity = shape * fadeIn * fadeOut;

        accumulated += ParticleColor(index) * intensity * 2.4f;
        accumulatedAlpha = max(accumulatedAlpha, intensity);
    }

    OutputTexture[dispatchId.xy] = float4(accumulated, saturate(accumulatedAlpha));
}
