struct ConstantBuffer {
    WVP: mat4x4<f32>,
    World: mat4x4<f32>,
    WorldView: mat4x4<f32>,
    LightPos: vec4<f32>,
    LightColor: vec4<f32>,
    CameraPosition: vec4<f32>,
    CameraInfo: vec4<f32>,
    Ambient: vec4<f32>,
    DiffuseColor: vec4<f32>,
    SpecularColor: vec4<f32>,
    PBRParams: vec4<f32>,
    Intensities: vec4<f32>,
    ParallaxSettings: vec4<f32>,
    ParallaxShadowSettings: vec4<f32>,
    Light0Direction: vec4<f32>,
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;
@group(0) @binding(0) var depthTex: texture_2d<f32>;
@group(0) @binding(1) var depthTex2: texture_2d<f32>;
@group(0) @binding(32) var depthSampler: sampler;

@fragment fn FS(@builtin(position) hposition: vec4<f32>) -> @location(0) vec4<f32> {
    let screenUV = hposition.xy / constants.CameraInfo.zw;
    let sceneDepth = max(textureSample(depthTex, depthSampler, screenUV).r, textureSample(depthTex2, depthSampler, screenUV).r);
    if (sceneDepth > 0.0001 && hposition.z < sceneDepth * 0.995) { discard; }
    return constants.DiffuseColor;
}