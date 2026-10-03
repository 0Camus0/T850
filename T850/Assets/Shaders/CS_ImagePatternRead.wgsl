struct ImagePatternConstants {
    width: u32,
    height: u32,
    seed: u32,
    padding: u32,
}

@group(0) @binding(0) var<uniform> constants: ImagePatternConstants;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var<storage, read_write> output: array<u32>;

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) dispatchId: vec3<u32>) {
    if (dispatchId.x >= constants.width || dispatchId.y >= constants.height) { return; }
    let index = dispatchId.y * constants.width + dispatchId.x;
    let bytes = vec4<u32>(round(clamp(textureLoad(inputTexture, dispatchId.xy, 0),
        vec4<f32>(0.0), vec4<f32>(1.0)) * 255.0));
    output[index] = bytes.x | (bytes.y << 8u) | (bytes.z << 16u) | (bytes.w << 24u);
}