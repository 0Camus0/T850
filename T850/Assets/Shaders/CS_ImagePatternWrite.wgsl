struct ImagePatternConstants {
    width: u32,
    height: u32,
    seed: u32,
    padding: u32,
}

@group(0) @binding(0) var<uniform> constants: ImagePatternConstants;
@group(0) @binding(1) var outputTexture: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    if (dispatchId.x >= constants.width || dispatchId.y >= constants.height) { return; }
    let pixel = dispatchId.xy;
    let linearIndex = pixel.y * constants.width + pixel.x;
    let bytes = vec4<u32>(
        (linearIndex * 17u + constants.seed) & 255u,
        (pixel.x * 29u + pixel.y * 11u + constants.seed * 3u) & 255u,
        (pixel.x * 7u + pixel.y * 31u + constants.seed * 5u) & 255u,
        255u);
    textureStore(outputTexture, pixel, vec4<f32>(bytes) / 255.0);
}