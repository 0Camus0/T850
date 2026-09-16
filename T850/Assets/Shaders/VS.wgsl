struct ConstantBuffer {
    WVP: mat4x4<f32>,
    World: mat4x4<f32>,
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;

struct VertexInput {
    @location(0) position: vec4<f32>,
    @location(1) normal: vec4<f32>,
    @location(2) texture0: vec2<f32>,
}
struct VertexOutput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) hnormal: vec4<f32>,
    @location(1) texture0: vec2<f32>,
}
@vertex fn VS(input: VertexInput) -> VertexOutput {
    return VertexOutput(constants.WVP * input.position, normalize(constants.World * input.normal), input.texture0);
}