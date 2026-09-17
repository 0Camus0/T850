#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform TorchParticleConstants {
    mat4 ViewProjection;
    vec4 EmitterAndTime;
    vec4 OutputSizeCountEnabled;
    vec4 Motion;
};

layout(rgba16f, binding = 1) writeonly uniform image2D OutputTexture;

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
    if (paletteIndex == 0u) return vec3(1.0, 0.04, 0.01);
    if (paletteIndex == 1u) return vec3(1.0, 0.32, 0.015);
    return vec3(1.0, 0.88, 0.08);
}

void main()
{
    uvec2 dispatchId = gl_GlobalInvocationID.xy;
    uvec2 outputSize = uvec2(OutputSizeCountEnabled.xy);
    if (any(greaterThanEqual(dispatchId, outputSize)))
        return;

    uint particleCount = uint(OutputSizeCountEnabled.z);
    if (OutputSizeCountEnabled.w < 0.5 || particleCount == 0u) {
        imageStore(OutputTexture, ivec2(dispatchId), vec4(0.0));
        return;
    }

    float lifetime = max(Motion.x, 0.001);
    float time = EmitterAndTime.w;
    vec2 uv = (vec2(dispatchId) + vec2(0.5)) / vec2(outputSize);
    float aspect = float(outputSize.x) / max(float(outputSize.y), 1.0);
    vec3 accumulated = vec3(0.0);
    float accumulatedAlpha = 0.0;

    for (uint index = 0u; index < particleCount; ++index) {
        float phase = Hash(index * 9781u + 17u);
        float age = fract(time / lifetime + phase);
        float angle = Hash(index * 6271u + 43u) * 6.28318530718;
        float radialSeed = 0.35 + Hash(index * 3253u + 91u) * 0.65;
        float radialDistance = Motion.z * radialSeed * (0.25 + age * 0.75);
        float wobble = sin(time * (2.0 + Hash(index * 1877u + 7u) * 2.0) + angle) * Motion.z * 0.18;

        vec3 position = EmitterAndTime.xyz;
        position.x += cos(angle) * radialDistance + wobble;
        position.z += sin(angle) * radialDistance - wobble * 0.5;
        position.y += age * Motion.y;

        vec4 clip = ViewProjection * vec4(position, 1.0);
        if (clip.w <= 0.0001) continue;
        vec2 ndc = clip.xy / clip.w;
        vec2 centerUv = vec2(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5);
        vec2 delta = uv - centerUv;
        delta.x *= aspect;

        float radius = Motion.w * mix(1.0, 0.45, age) / max(clip.w, 0.5);
        float distanceToEdge = max(abs(delta.x), abs(delta.y));
        float pixelWidth = 1.0 / max(float(outputSize.y), 1.0);
        float edgeSoftness = min(pixelWidth, radius * 0.25);
        float shape = 1.0 - smoothstep(max(radius - edgeSoftness, 0.0), radius, distanceToEdge);
        float fadeIn = smoothstep(0.0, 0.08, age);
        float fadeOut = 1.0 - smoothstep(0.55, 1.0, age);
        float intensity = shape * fadeIn * fadeOut;
        accumulated += ParticleColor(index) * intensity * 2.4;
        accumulatedAlpha = max(accumulatedAlpha, intensity);
    }

    imageStore(OutputTexture, ivec2(dispatchId), vec4(accumulated, clamp(accumulatedAlpha, 0.0, 1.0)));
}