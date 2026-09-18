struct BlurConstants {
    outputSize: vec2<u32>,
    kernelSize: u32,
    direction: u32,
    radiusAndPadding: vec4<f32>,
    weights: array<vec4<f32>, 6>,
}

@group(0) @binding(0) var<uniform> constants: BlurConstants;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var inputSampler: sampler;
@group(0) @binding(3) var outputTexture: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    if (any(dispatchId.xy >= constants.outputSize)) { return; }
    let inputSize = max(textureDimensions(inputTexture), vec2<u32>(1u));
    let uv = (vec2<f32>(dispatchId.xy) + 0.5) / vec2<f32>(constants.outputSize);
    let texelStep = constants.radiusAndPadding.x / vec2<f32>(inputSize);
    let direction = select(vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), constants.direction == 0u);
    let origin = -((f32(constants.kernelSize) - 1.0) * 0.5);
    var sum = vec3<f32>(0.0);
    for (var index = 0u; index < constants.kernelSize; index++) {
        let offset = origin + f32(index);
        sum += constants.weights[index >> 2u][index & 3u] * textureSampleLevel(
            inputTexture, inputSampler, uv + direction * offset * texelStep, 0.0).rgb;
    }
    textureStore(outputTexture, dispatchId.xy, vec4<f32>(sum, 1.0));
}