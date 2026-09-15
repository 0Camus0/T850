struct ConstantBuffer {
    WVP: mat4x4<f32>,
    LineColor: vec4<f32>,
    DepthParams: vec4<f32>,
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;
@group(0) @binding(0) var depthTex: texture_2d<f32>;
@group(0) @binding(1) var depthTex2: texture_2d<f32>;
@group(0) @binding(32) var SS: sampler;

@fragment fn FS(@builtin(position) hposition: vec4<f32>) -> @location(0) vec4<f32> {
    let screenUV = clamp(hposition.xy * constants.DepthParams.xy, vec2<f32>(0.0), vec2<f32>(1.0));
    let sceneDepth = max(textureSample(depthTex, SS, screenUV).r, textureSample(depthTex2, SS, screenUV).r);
    let wireDepth = hposition.z;
    if (wireDepth < sceneDepth * (1.0 - constants.DepthParams.w) && sceneDepth > 0.0) { discard; }
    return constants.LineColor;
}