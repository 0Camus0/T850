struct TorchParticleConstants {
    viewProjection: mat4x4<f32>,
    emitterAndTime: vec4<f32>,
    additionalEmitterXZ: vec4<f32>,
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

var<workgroup> projectedParticles: array<vec4<f32>, 64>;
var<workgroup> particleAges: array<f32, 64>;
var<workgroup> tileMasks: array<atomic<u32>, 2>;

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

fn EmitterPosition(emitterIndex: u32) -> vec3<f32> {
    if (emitterIndex == 0u) { return constants.emitterAndTime.xyz; }
    if (emitterIndex == 1u) {
        return vec3<f32>(constants.additionalEmitterXZ.x,
                         constants.emitterAndTime.y,
                         constants.additionalEmitterXZ.y);
    }
    return vec3<f32>(constants.additionalEmitterXZ.z,
                     constants.emitterAndTime.y,
                     constants.additionalEmitterXZ.w);
}

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>,
      @builtin(local_invocation_index) lane: u32,
      @builtin(workgroup_id) tile: vec3<u32>) {
    let outputSize = vec2<u32>(constants.outputSizeCountEnabled.xy);
    let validPixel = all(dispatchId.xy < outputSize);
    let particleCount = u32(constants.outputSizeCountEnabled.z);
    if (constants.outputSizeCountEnabled.w < 0.5 || particleCount == 0u) {
        if (validPixel) { textureStore(outputTexture, dispatchId.xy, vec4<f32>(0.0)); }
        return;
    }
    let lifetime = max(constants.motion.x, 0.001);
    let spread = constants.motion.z;
    let time = constants.emitterAndTime.w;
    let uv = (vec2<f32>(dispatchId.xy) + 0.5) / vec2<f32>(outputSize);
    let aspect = f32(outputSize.x) / max(f32(outputSize.y), 1.0);
    let tileMin = (vec2<f32>(tile.xy * 8u) + 0.5) / vec2<f32>(outputSize);
    let tileMax = (vec2<f32>(min(tile.xy * 8u + 8u, outputSize)) - 0.5) / vec2<f32>(outputSize);
    var sampledDepth = 0.0;
    if (validPixel) { sampledDepth = textureLoad(sceneDepth, dispatchId.xy, 0).r; }
    var accumulated = vec3<f32>(0.0);
    var accumulatedAlpha = 0.0;
    let emitterCount = min(u32(constants.outputSizeCountEnabled.w), 3u);
    for (var batch = 0u; batch < particleCount * emitterCount; batch += 64u) {
        if (lane < 2u) { atomicStore(&tileMasks[lane], 0u); }
        workgroupBarrier();
        let seedIndex = batch + lane;
        if (seedIndex < particleCount * emitterCount) {
            let emitterIndex = seedIndex / particleCount;
            let phase = Hash(seedIndex * 9781u + 17u);
            let age = fract(time / lifetime + phase);
            let angle = Hash(seedIndex * 6271u + 43u) * 6.28318530718;
            let radialSeed = mix(constants.shapeTuning.x, 1.0, Hash(seedIndex * 3253u + 91u));
            let radialDistance = spread * radialSeed * (constants.shapeTuning.y + age * constants.shapeTuning.z);
            let wobble = sin(time * (constants.wobbleTuning.x +
                Hash(seedIndex * 1877u + 7u) * constants.wobbleTuning.y) + angle) * spread * constants.shapeTuning.w;
            var position = EmitterPosition(emitterIndex);
            position.x += cos(angle) * radialDistance + wobble;
            position.z += sin(angle) * radialDistance - wobble * constants.wobbleTuning.z;
            position.y += age * constants.motion.y;
            let clip = constants.viewProjection * vec4<f32>(position, 1.0);
            if (clip.w > 0.0001) {
                let particleDepth = clip.z / clip.w;
                let ndc = clip.xy / clip.w;
                let centerUv = vec2<f32>(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
                let radius = constants.motion.w * mix(constants.fadeTuning.x, constants.fadeTuning.y, age) /
                    max(clip.w, constants.wobbleTuning.w);
                let extent = vec2<f32>(radius / aspect, radius);
                if (particleDepth >= 0.0 && particleDepth <= 1.0 && radius > 0.0 &&
                    all(centerUv + extent >= tileMin) && all(centerUv - extent <= tileMax)) {
                    projectedParticles[lane] = vec4<f32>(centerUv, radius, particleDepth);
                    particleAges[lane] = age;
                    atomicOr(&tileMasks[lane / 32u], 1u << (lane % 32u));
                }
            }
        }
        workgroupBarrier();
        if (validPixel) {
            for (var half = 0u; half < 2u; half++) {
                var mask = atomicLoad(&tileMasks[half]);
                while (mask != 0u) {
                    let slot = half * 32u + firstTrailingBit(mask);
                    mask &= mask - 1u;
                    let particle = projectedParticles[slot];
                    if (particle.w < sampledDepth) { continue; }
                    var delta = uv - particle.xy;
                    delta.x *= aspect;
                    let radius = particle.z;
                    let distanceToEdge = max(abs(delta.x), abs(delta.y));
                    let pixelWidth = 1.0 / max(f32(outputSize.y), 1.0);
                    let edgeSoftness = min(pixelWidth, radius * constants.fadeTuning.z);
                    let shape = 1.0 - smoothstep(max(radius - edgeSoftness, 0.0), radius, distanceToEdge);
                    let age = particleAges[slot];
                    let fadeIn = smoothstep(0.0, constants.fadeTuning.w, age);
                    let fadeOut = 1.0 - smoothstep(constants.intensityTuning.x, 1.0, age);
                    let intensity = shape * fadeIn * fadeOut;
                    accumulated += ParticleColor((batch + slot) % particleCount) * intensity * constants.intensityTuning.y;
                    accumulatedAlpha = max(accumulatedAlpha, intensity);
                }
            }
        }
        workgroupBarrier();
    }
    if (validPixel) {
        textureStore(outputTexture, dispatchId.xy, vec4<f32>(accumulated, clamp(accumulatedAlpha, 0.0, 1.0)));
    }
}