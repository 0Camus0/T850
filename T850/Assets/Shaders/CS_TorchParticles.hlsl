#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer TorchParticleConstants : register(b0)
{
    float4x4 ViewProjection;
    float4 EmitterAndTime;
    float4 AdditionalEmitterXZ;
    float4 OutputSizeCountEnabled;
    float4 Motion;
    float4 ParticleColor0;
    float4 ParticleColor1;
    float4 ParticleColor2;
    float4 ShapeTuning;
    float4 WobbleTuning;
    float4 FadeTuning;
    float4 IntensityTuning;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> OutputTexture : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float> SceneDepth : register(t0);

groupshared float4 ProjectedParticles[64];
groupshared float ParticleAges[64];
groupshared uint TileMasks[2];

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
        return ParticleColor0.rgb;
    if (paletteIndex == 1u)
        return ParticleColor1.rgb;
    return ParticleColor2.rgb;
}

float3 EmitterPosition(uint emitterIndex)
{
    if (emitterIndex == 0u)
        return EmitterAndTime.xyz;
    if (emitterIndex == 1u)
        return float3(AdditionalEmitterXZ.xy, EmitterAndTime.y).xzy;
    return float3(AdditionalEmitterXZ.zw, EmitterAndTime.y).xzy;
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID, uint lane : SV_GroupIndex, uint3 tile : SV_GroupID)
{
    const uint2 outputSize = uint2(OutputSizeCountEnabled.xy);
    const bool validPixel = all(dispatchId.xy < outputSize);

    const uint particleCount = (uint)OutputSizeCountEnabled.z;
    if (OutputSizeCountEnabled.w < 0.5f || particleCount == 0u) {
        if (validPixel) OutputTexture[dispatchId.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float lifetime = max(Motion.x, 0.001f);
    const float riseHeight = Motion.y;
    const float spread = Motion.z;
    const float baseSize = Motion.w;
    const float time = EmitterAndTime.w;
    const float2 uv = (float2(dispatchId.xy) + 0.5f) / float2(outputSize);
    const float aspect = (float)outputSize.x / max((float)outputSize.y, 1.0f);
    const float2 tileMin = (float2(tile.xy * 8u) + 0.5f) / float2(outputSize);
    const float2 tileMax = (float2(min(tile.xy * 8u + 8u, outputSize)) - 0.5f) / float2(outputSize);
    float sceneDepth = 0.0f;
    if (validPixel) sceneDepth = SceneDepth.Load(int3(dispatchId.xy, 0));

    float3 accumulated = 0.0f.xxx;
    float accumulatedAlpha = 0.0f;

    const uint emitterCount = min((uint)OutputSizeCountEnabled.w, 3u);
    [loop]
        for (uint batch = 0u; batch < particleCount * emitterCount; batch += 64u) {
            if (lane < 2u) TileMasks[lane] = 0u;
            GroupMemoryBarrierWithGroupSync();
            const uint seedIndex = batch + lane;
            if (seedIndex < particleCount * emitterCount) {
                const uint emitterIndex = seedIndex / particleCount;
        const float phase = Hash(seedIndex * 9781u + 17u);
        const float age = frac(time / lifetime + phase);
        const float angle = Hash(seedIndex * 6271u + 43u) * 6.28318530718f;
        const float radialSeed = lerp(ShapeTuning.x, 1.0f, Hash(seedIndex * 3253u + 91u));
        const float radialDistance = spread * radialSeed *
            (ShapeTuning.y + age * ShapeTuning.z);
        const float wobble = sin(time * (WobbleTuning.x +
            Hash(seedIndex * 1877u + 7u) * WobbleTuning.y) + angle) * spread * ShapeTuning.w;

        float3 position = EmitterPosition(emitterIndex);
        position.x += cos(angle) * radialDistance + wobble;
        position.z += sin(angle) * radialDistance - wobble * WobbleTuning.z;
        position.y += age * riseHeight;

        const float4 clip = mul(ViewProjection, float4(position, 1.0f));
        if (clip.w > 0.0001f) {
            const float particleDepth = clip.z / clip.w;
            const float2 ndc = clip.xy / clip.w;
            const float2 centerUv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
            const float radius = baseSize * lerp(FadeTuning.x, FadeTuning.y, age) /
                max(clip.w, WobbleTuning.w);
            const float2 extent = float2(radius / aspect, radius);
            if (particleDepth >= 0.0f && particleDepth <= 1.0f && radius > 0.0f &&
                all(centerUv + extent >= tileMin) && all(centerUv - extent <= tileMax)) {
                ProjectedParticles[lane] = float4(centerUv, radius, particleDepth);
                ParticleAges[lane] = age;
                InterlockedOr(TileMasks[lane / 32u], 1u << (lane % 32u));
            }
        }
      }
      GroupMemoryBarrierWithGroupSync();
      if (validPixel) {
        for (uint maskWord = 0u; maskWord < 2u; ++maskWord) {
            uint mask = TileMasks[maskWord];
            [loop]
            while (mask != 0u) {
                const uint slot = maskWord * 32u + firstbitlow(mask);
                mask &= mask - 1u;
                const float4 particle = ProjectedParticles[slot];
                if (particle.w < sceneDepth) continue;
                float2 delta = uv - particle.xy;
                delta.x *= aspect;
                const float radius = particle.z;
                const float distanceToEdge = max(abs(delta.x), abs(delta.y));
                const float pixelWidth = 1.0f / max((float)outputSize.y, 1.0f);
                const float edgeSoftness = min(pixelWidth, radius * FadeTuning.z);
                const float shape = 1.0f - smoothstep(max(radius - edgeSoftness, 0.0f), radius, distanceToEdge);
                const float age = ParticleAges[slot];
                const float fadeIn = smoothstep(0.0f, FadeTuning.w, age);
                const float fadeOut = 1.0f - smoothstep(IntensityTuning.x, 1.0f, age);
                const float intensity = shape * fadeIn * fadeOut;
                accumulated += ParticleColor((batch + slot) % particleCount) * intensity * IntensityTuning.y;
                accumulatedAlpha = max(accumulatedAlpha, intensity);
            }
        }
      }
      GroupMemoryBarrierWithGroupSync();
    }

    if (validPixel) OutputTexture[dispatchId.xy] = float4(accumulated, saturate(accumulatedAlpha));
}
