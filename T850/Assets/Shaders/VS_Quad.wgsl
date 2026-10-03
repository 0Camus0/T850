struct QuadFrameCB {
    WVP: mat4x4<f32>,
    World: mat4x4<f32>,
    WorldView: mat4x4<f32>,
    WVPInverse: mat4x4<f32>,
    WVPLight: mat4x4<f32>,
    Projection: mat4x4<f32>,
    CameraPosition: vec4<f32>,
    CameraInfo: vec4<f32>,
    LightCameraPosition: vec4<f32>,
    LightCameraInfo: vec4<f32>,
}
@group(0) @binding(64) var<uniform> frame: QuadFrameCB;

struct VertexInput {
    @location(0) position: vec4<f32>,
    @location(1) texture0: vec2<f32>,
}
struct VertexOutput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) texture0: vec2<f32>,
    @location(1) Pos: vec4<f32>,
    @location(2) PosCorner: vec4<f32>,
    @location(3) ClipPos: vec2<f32>,
}
@vertex fn VS(input: VertexInput) -> VertexOutput {
    var result: VertexOutput;
    result.hposition = frame.WVP * input.position;
    result.texture0 = input.texture0;
    result.Pos = result.hposition;
    result.ClipPos = input.position.xy;
    let corner = frame.WVPInverse * vec4<f32>(input.position.xy, 0.0, 1.0);
    result.PosCorner = vec4<f32>(corner.xyz / corner.w, corner.w) - frame.CameraPosition;
    return result;
}