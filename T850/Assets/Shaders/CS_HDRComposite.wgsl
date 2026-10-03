struct PostProcessConstants {
    outputSizeAndParams0: vec4<f32>,
    params1: vec4<f32>,
}

@group(0) @binding(0) var<uniform> constants: PostProcessConstants;
@group(0) @binding(1) var hdrTexture: texture_2d<f32>;
@group(0) @binding(2) var bloomTexture: texture_2d<f32>;
@group(0) @binding(3) var adaptedLuminanceTexture: texture_2d<f32>;
@group(0) @binding(4) var hdrSampler: sampler;
@group(0) @binding(5) var bloomSampler: sampler;
@group(0) @binding(6) var luminanceSampler: sampler;
@group(0) @binding(7) var outputTexture: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    let outputSize = vec2<u32>(constants.outputSizeAndParams0.xy);
    if (any(dispatchId.xy >= outputSize)) { return; }
    let uv = (vec2<f32>(dispatchId.xy) + 0.5) / vec2<f32>(outputSize);
    let hdrColor = textureSampleLevel(hdrTexture, hdrSampler, uv, 0.0).rgb;
    let avgLuminance = max(exp(textureSampleLevel(
        adaptedLuminanceTexture, luminanceSampler, vec2<f32>(0.5), 0.0).r), 0.001);
    let keyValue = 1.03 - (2.0 / (2.0 + log(avgLuminance + 1.0) / log(10.0)));
    let linearExposure = keyValue / avgLuminance;
    let bloomFactor = constants.outputSizeAndParams0.z;
    let exposureComp = constants.outputSizeAndParams0.w;
    let color = exp2(log2(max(linearExposure, 0.0001)) + exposureComp) * hdrColor;
    let pixelLuminance = max(dot(color, vec3<f32>(0.299, 0.587, 0.114)), 0.0001);
    let whiteLevel = max(constants.params1.x, 0.001);
    let toneMappedLuminance = pixelLuminance *
        (1.0 + pixelLuminance / (whiteLevel * whiteLevel)) / (1.0 + pixelLuminance);
    let toneMapped = toneMappedLuminance * (color / pixelLuminance);
    textureStore(outputTexture, dispatchId.xy, vec4<f32>(
        toneMapped + bloomFactor * textureSampleLevel(bloomTexture, bloomSampler, uv, 0.0).rgb, 1.0));
}