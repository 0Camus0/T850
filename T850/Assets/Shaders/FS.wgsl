@group(0) @binding(0) var TextureRGB: texture_2d<f32>;
@group(0) @binding(32) var SS: sampler;

struct FragmentInput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) hnormal: vec4<f32>,
    @location(1) texture0: vec2<f32>,
}
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    return textureSample(TextureRGB, SS, input.texture0);
}