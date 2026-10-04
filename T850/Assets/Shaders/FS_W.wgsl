struct ConstantBuffer {
    WVP: mat4x4<f32>,
    LineColor: vec4<f32>,
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;

@fragment fn FS() -> @location(0) vec4<f32> {
    return constants.LineColor;
}