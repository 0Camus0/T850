struct ConstantBuffer {
    WVP: mat4x4<f32>,
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;

#ifdef USE_SKINNING_QT
struct BoneBuffer {
    BoneQuats: array<vec4<f32>, 256>,
    BoneTrans: array<vec4<f32>, 256>,
}
@group(0) @binding(65) var<uniform> bones: BoneBuffer;
#elif defined(USE_SKINNING)
struct BoneBuffer {
    BoneMatrices: array<mat4x4<f32>, 256>,
}
@group(0) @binding(65) var<uniform> bones: BoneBuffer;
#endif

struct VertexInput {
    @location(0) position: vec4<f32>,
#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
    @location(1) joints: vec4<f32>,
    @location(2) weights: vec4<f32>,
#endif
}

@vertex fn VS(input: VertexInput) -> @builtin(position) vec4<f32> {
    var position = input.position;
#ifdef USE_SKINNING_QT
    let indices = vec4<i32>(input.joints);
    let quaternion = normalize(bones.BoneQuats[indices.x] * input.weights.x
        + bones.BoneQuats[indices.y] * input.weights.y
        + bones.BoneQuats[indices.z] * input.weights.z
        + bones.BoneQuats[indices.w] * input.weights.w);
    let translation = bones.BoneTrans[indices.x].xyz * input.weights.x
        + bones.BoneTrans[indices.y].xyz * input.weights.y
        + bones.BoneTrans[indices.z].xyz * input.weights.z
        + bones.BoneTrans[indices.w].xyz * input.weights.w;
    let point = position.xyz;
    let rotated = point + 2.0 * cross(quaternion.xyz, cross(quaternion.xyz, point) + quaternion.w * point);
    position = vec4<f32>(rotated + translation, 1.0);
#elif defined(USE_SKINNING)
    let indices = vec4<i32>(input.joints);
    let skinMatrix = bones.BoneMatrices[indices.x] * input.weights.x
        + bones.BoneMatrices[indices.y] * input.weights.y
        + bones.BoneMatrices[indices.z] * input.weights.z
        + bones.BoneMatrices[indices.w] * input.weights.w;
    position = skinMatrix * position;
#endif
    return constants.WVP * position;
}