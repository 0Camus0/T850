struct TorchParticleConstants {
    viewProjection: mat4x4<f32>,
    emitterAndTime: vec4<f32>,
    outputSizeCountEnabled: vec4<f32>,
    motion: vec4<f32>,
    particleColor0: vec4<f32>,
    particleColor1: vec4<f32>,
    particleColor2: vec4<f32>,
    shapeTuning: vec4<f32>,
    wobbleTuning: vec4<f32>,
    fadeTuning: vec4<f32>,
    intensityTuning: vec4<f32>,
}

@group(0) @binding(0) var<uniform> constants: TorchParticleConstants;
@group(0) @binding(1) var outputTexture: texture_storage_2d<rgba16float, write>;
@group(0) @binding(2) var sceneDepth: texture_2d<f32>;

fn Hash(seed: u32) -> f32 {
    var value = seed;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return f32(value & 0x00ffffffu) / 16777215.0;
}

fn ParticleColor(index: u32) -> vec3<f32> {
    let paletteIndex = index % 3u;
    if (paletteIndex == 0u) { return constants.particleColor0.rgb; }
    if (paletteIndex == 1u) { return constants.particleColor1.rgb; }
    return constants.particleColor2.rgb;
}

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    let outputSize = vec2<u32>(constants.outputSizeCountEnabled.xy);
    if (any(dispatchId.xy >= outputSize)) { return; }
    let particleCount = u32(constants.outputSizeCountEnabled.z);
    if (constants.outputSizeCountEnabled.w < 0.5 || particleCount == 0u) {
        textureStore(outputTexture, dispatchId.xy, vec4<f32>(0.0));
        return;
    }
    let lifetime = max(constants.motion.x, 0.001);
    let spread = constants.motion.z;
    let time = constants.emitterAndTime.w;
    let uv = (vec2<f32>(dispatchId.xy) + 0.5) / vec2<f32>(outputSize);
    let aspect = f32(outputSize.x) / max(f32(outputSize.y), 1.0);
    let sampledDepth = textureLoad(sceneDepth, dispatchId.xy, 0).r;
    var accumulated = vec3<f32>(0.0);
    var accumulatedAlpha = 0.0;
    for (var index = 0u; index < particleCount; index++) {
        let phase = Hash(index * 9781u + 17u);
        let age = fract(time / lifetime + phase);
        let angle = Hash(index * 6271u + 43u) * 6.28318530718;
        let radialSeed = mix(constants.shapeTuning.x, 1.0, Hash(index * 3253u + 91u));
        let radialDistance = spread * radialSeed * (constants.shapeTuning.y + age * constants.shapeTuning.z);
        let wobble = sin(time * (constants.wobbleTuning.x +
            Hash(index * 1877u + 7u) * constants.wobbleTuning.y) + angle) * spread * constants.shapeTuning.w;
        var position = constants.emitterAndTime.xyz;
        position.x += cos(angle) * radialDistance + wobble;
        position.z += sin(angle) * radialDistance - wobble * constants.wobbleTuning.z;
        position.y += age * constants.motion.y;
        let clip = constants.viewProjection * vec4<f32>(position, 1.0);
        if (clip.w <= 0.0001) { continue; }
        let particleDepth = clip.z / clip.w;
        if (particleDepth < 0.0 || particleDepth > 1.0 || particleDepth < sampledDepth) { continue; }
        let ndc = clip.xy / clip.w;
        let centerUv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        var delta = uv - centerUv;
        delta.x *= aspect;
        let radius = constants.motion.w * mix(constants.fadeTuning.x, constants.fadeTuning.y, age) /
            max(clip.w, constants.wobbleTuning.w);
        let distanceToEdge = max(abs(delta.x), abs(delta.y));
        let pixelWidth = 1.0 / max(f32(outputSize.y), 1.0);
        let edgeSoftness = min(pixelWidth, radius * constants.fadeTuning.z);
        let shape = 1.0 - smoothstep(max(radius - edgeSoftness, 0.0), radius, distanceToEdge);
        let fadeIn = smoothstep(0.0, constants.fadeTuning.w, age);
        let fadeOut = 1.0 - smoothstep(constants.intensityTuning.x, 1.0, age);
        let intensity = shape * fadeIn * fadeOut;
        accumulated += ParticleColor(index) * intensity * constants.intensityTuning.y;
        accumulatedAlpha = max(accumulatedAlpha, intensity);
    }
    textureStore(outputTexture, dispatchId.xy, vec4<f32>(accumulated, clamp(accumulatedAlpha, 0.0, 1.0)));
}