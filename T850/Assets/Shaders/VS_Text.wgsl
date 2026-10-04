struct VertexInput {
    @location(0) position: vec4<f32>,
    @location(1) texture0: vec2<f32>,
}

struct VertexOutput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) texture0: vec2<f32>,
}

@vertex fn VS(input: VertexInput) -> VertexOutput {
    return VertexOutput(input.position, input.texture0);
}