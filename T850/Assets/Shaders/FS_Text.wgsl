struct ConstantBuffer {
    color: vec4<f32>,
}

@group(0) @binding(64) var<uniform> constants: ConstantBuffer;
@group(0) @binding(0) var tex0: texture_2d<f32>;
@group(0) @binding(32) var SS: sampler;

struct FragmentInput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) texture0: vec2<f32>,
}

@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let alpha = textureSample(tex0, SS, input.texture0).r;
    return vec4<f32>(constants.color.xyz, alpha);
}