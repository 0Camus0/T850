struct FragmentInput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) hnormal: vec4<f32>,
}

@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.hnormal.rgb, 1.0);
}