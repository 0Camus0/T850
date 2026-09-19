#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform TorchParticleConstants {
    mat4 ViewProjection;
    vec4 EmitterAndTime;
    vec4 AdditionalEmitterXZ;
    vec4 OutputSizeCountEnabled;
    vec4 Motion;
    vec4 ParticleColor0;
    vec4 ParticleColor1;
    vec4 ParticleColor2;
    vec4 ShapeTuning;
    vec4 WobbleTuning;
    vec4 FadeTuning;
    vec4 IntensityTuning;
};

layout(rgba16f, binding = 1) writeonly uniform image2D OutputTexture;
layout(binding = 2) uniform sampler2D SceneDepth;

shared vec4 ProjectedParticles[64];
shared float ParticleAges[64];
shared uint TileMasks[2];

float Hash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return float(value & 0x00ffffffu) / 16777215.0;
}

vec3 ParticleColor(uint index)
{
    uint paletteIndex = index % 3u;
    if (paletteIndex == 0u) return ParticleColor0.rgb;
    if (paletteIndex == 1u) return ParticleColor1.rgb;
    return ParticleColor2.rgb;
}

vec3 EmitterPosition(uint emitterIndex)
{
    if (emitterIndex == 0u) return EmitterAndTime.xyz;
    if (emitterIndex == 1u)
        return vec3(AdditionalEmitterXZ.x, EmitterAndTime.y, AdditionalEmitterXZ.y);
    return vec3(AdditionalEmitterXZ.z, EmitterAndTime.y, AdditionalEmitterXZ.w);
}

void main()
{
    uvec2 dispatchId = gl_GlobalInvocationID.xy;
    uvec2 outputSize = uvec2(OutputSizeCountEnabled.xy);
    bool validPixel = all(lessThan(dispatchId, outputSize));
    uint lane = gl_LocalInvocationIndex;

    uint particleCount = uint(OutputSizeCountEnabled.z);
    if (OutputSizeCountEnabled.w < 0.5 || particleCount == 0u) {
        if (validPixel) imageStore(OutputTexture, ivec2(dispatchId), vec4(0.0));
        return;
    }

    float lifetime = max(Motion.x, 0.001);
    float time = EmitterAndTime.w;
    vec2 uv = (vec2(dispatchId) + vec2(0.5)) / vec2(outputSize);
    float aspect = float(outputSize.x) / max(float(outputSize.y), 1.0);
    vec2 tileMin = (vec2(gl_WorkGroupID.xy * 8u) + 0.5) / vec2(outputSize);
    vec2 tileMax = (vec2(min(gl_WorkGroupID.xy * 8u + 8u, outputSize)) - 0.5) / vec2(outputSize);
    float sceneDepth = 0.0;
    if (validPixel) sceneDepth = texelFetch(SceneDepth, ivec2(dispatchId), 0).r;
    vec3 accumulated = vec3(0.0);
    float accumulatedAlpha = 0.0;

    uint emitterCount = min(uint(OutputSizeCountEnabled.w), 3u);
        for (uint batch = 0u; batch < particleCount * emitterCount; batch += 64u) {
            if (lane < 2u) TileMasks[lane] = 0u;
            barrier();
            uint seedIndex = batch + lane;
            if (seedIndex < particleCount * emitterCount) {
                uint emitterIndex = seedIndex / particleCount;
        float phase = Hash(seedIndex * 9781u + 17u);
        float age = fract(time / lifetime + phase);
        float angle = Hash(seedIndex * 6271u + 43u) * 6.28318530718;
        float radialSeed = mix(ShapeTuning.x, 1.0, Hash(seedIndex * 3253u + 91u));
        float radialDistance = Motion.z * radialSeed * (ShapeTuning.y + age * ShapeTuning.z);
        float wobble = sin(time * (WobbleTuning.x + Hash(seedIndex * 1877u + 7u) *
            WobbleTuning.y) + angle) * Motion.z * ShapeTuning.w;

        vec3 position = EmitterPosition(emitterIndex);
        position.x += cos(angle) * radialDistance + wobble;
        position.z += sin(angle) * radialDistance - wobble * WobbleTuning.z;
        position.y += age * Motion.y;

        vec4 clip = ViewProjection * vec4(position, 1.0);
        if (clip.w > 0.0001) {
            float particleDepth = clip.z / clip.w;
            vec2 ndc = clip.xy / clip.w;
            vec2 centerUv = vec2(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5);
            float radius = Motion.w * mix(FadeTuning.x, FadeTuning.y, age) /
                max(clip.w, WobbleTuning.w);
            vec2 extent = vec2(radius / aspect, radius);
            if (particleDepth >= 0.0 && particleDepth <= 1.0 && radius > 0.0 &&
                all(greaterThanEqual(centerUv + extent, tileMin)) && all(lessThanEqual(centerUv - extent, tileMax))) {
                ProjectedParticles[lane] = vec4(centerUv, radius, particleDepth);
                ParticleAges[lane] = age;
                atomicOr(TileMasks[lane / 32u], 1u << (lane % 32u));
            }
        }
      }
      barrier();
      if (validPixel) {
        for (uint maskWord = 0u; maskWord < 2u; ++maskWord) {
            uint mask = TileMasks[maskWord];
            while (mask != 0u) {
                uint slot = maskWord * 32u + uint(findLSB(mask));
                mask &= mask - 1u;
                vec4 particle = ProjectedParticles[slot];
                if (particle.w < sceneDepth) continue;
                vec2 delta = uv - particle.xy;
                delta.x *= aspect;
                float radius = particle.z;
                float distanceToEdge = max(abs(delta.x), abs(delta.y));
                float pixelWidth = 1.0 / max(float(outputSize.y), 1.0);
                float edgeSoftness = min(pixelWidth, radius * FadeTuning.z);
                float shape = 1.0 - smoothstep(max(radius - edgeSoftness, 0.0), radius, distanceToEdge);
                float age = ParticleAges[slot];
                float fadeIn = smoothstep(0.0, FadeTuning.w, age);
                float fadeOut = 1.0 - smoothstep(IntensityTuning.x, 1.0, age);
                float intensity = shape * fadeIn * fadeOut;
                accumulated += ParticleColor((batch + slot) % particleCount) * intensity * IntensityTuning.y;
                accumulatedAlpha = max(accumulatedAlpha, intensity);
            }
        }
      }
      barrier();
    }

    if (validPixel) imageStore(OutputTexture, ivec2(dispatchId), vec4(accumulated, clamp(accumulatedAlpha, 0.0, 1.0)));
}