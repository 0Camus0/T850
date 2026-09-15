struct BlurConstants {
    width: u32,
    height: u32,
    directionX: i32,
    directionY: i32,
}

@group(0) @binding(0) var<uniform> constants: BlurConstants;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var outputTexture: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8, 1)
fn CS(@builtin(global_invocation_id) threadId: vec3<u32>) {
    if (threadId.x >= constants.width || threadId.y >= constants.height) { return; }
    let pixel = vec2<i32>(threadId.xy);
    let direction = vec2<i32>(constants.directionX, constants.directionY);
    let maximum = vec2<i32>(i32(constants.width), i32(constants.height)) - vec2<i32>(1);
    var result = textureLoad(inputTexture, clamp(pixel - 2 * direction, vec2<i32>(0), maximum), 0);
    result += 4.0 * textureLoad(inputTexture, clamp(pixel - direction, vec2<i32>(0), maximum), 0);
    result += 6.0 * textureLoad(inputTexture, pixel, 0);
    result += 4.0 * textureLoad(inputTexture, clamp(pixel + direction, vec2<i32>(0), maximum), 0);
    result += textureLoad(inputTexture, clamp(pixel + 2 * direction, vec2<i32>(0), maximum), 0);
    textureStore(outputTexture, pixel, result / 16.0);
}