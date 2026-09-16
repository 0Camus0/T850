struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
}

struct VertexOutput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) hnormal: vec4<f32>,
}

@vertex fn VS(input: VertexInput) -> VertexOutput {
    return VertexOutput(vec4<f32>(input.position, 1.0), vec4<f32>(input.normal, 1.0));
}