struct ConstantBuffer {
    WVP: mat4x4<f32>,
    LineColor: vec4<f32>,
    DepthParams: vec4<f32>,
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;

@vertex fn VS(@location(0) position: vec4<f32>) -> @builtin(position) vec4<f32> {
    return constants.WVP * position;
}