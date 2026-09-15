#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
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
#ifdef USE_SKINNING_QT
    BoneQuats: array<vec4<f32>, 256>,
    BoneTrans: array<vec4<f32>, 256>,
#else
    BoneMatrices: array<mat4x4<f32>, 256>,
#endif
}
@group(0) @binding(64) var<uniform> constants: ConstantBuffer;
#else
struct MeshInstanceCB {
    WVP: mat4x4<f32>,
    World: mat4x4<f32>,
    WorldView: mat4x4<f32>,
}
@group(0) @binding(65) var<uniform> constants: MeshInstanceCB;
#endif

#ifdef USE_SKINNING_TEXTURE
@group(0) @binding(24) var BoneTexture: texture_2d<f32>;
fn getBoneMatrix(index: i32) -> mat4x4<f32> {
    let textureWidth = i32(textureDimensions(BoneTexture).x);
    let pixelIndex = index * 4;
    let row0 = textureLoad(BoneTexture, vec2<i32>(pixelIndex % textureWidth, pixelIndex / textureWidth), 0);
    let row1 = textureLoad(BoneTexture, vec2<i32>((pixelIndex + 1) % textureWidth, (pixelIndex + 1) / textureWidth), 0);
    let row2 = textureLoad(BoneTexture, vec2<i32>((pixelIndex + 2) % textureWidth, (pixelIndex + 2) / textureWidth), 0);
    let row3 = textureLoad(BoneTexture, vec2<i32>((pixelIndex + 3) % textureWidth, (pixelIndex + 3) / textureWidth), 0);
    return transpose(mat4x4<f32>(row0, row1, row2, row3));
}
#endif

#ifdef USE_NORMALS
#define NORMAL_COUNT 1
#else
#define NORMAL_COUNT 0
#endif
#ifdef USE_TANGENTS
#define TANGENT_COUNT 1
#else
#define TANGENT_COUNT 0
#endif
#ifdef USE_BINORMALS
#define BINORMAL_COUNT 1
#else
#define BINORMAL_COUNT 0
#endif
#ifdef USE_TEXCOORD0
#define UV0_COUNT 1
#else
#define UV0_COUNT 0
#endif
#ifdef USE_TEXCOORD1
#define UV1_COUNT 1
#else
#define UV1_COUNT 0
#endif
#ifdef USE_TEXCOORD2
#define UV2_COUNT 1
#else
#define UV2_COUNT 0
#endif
#ifdef USE_TEXCOORD3
#define UV3_COUNT 1
#else
#define UV3_COUNT 0
#endif
#define UV_START (NORMAL_COUNT + TANGENT_COUNT + BINORMAL_COUNT)
#define POSITION_SLOT (UV_START + UV0_COUNT + UV1_COUNT + UV2_COUNT + UV3_COUNT)

struct VertexInput {
    @location(0) position: vec4<f32>,
#ifdef USE_NORMALS
    @location(1) normal: vec4<f32>,
#endif
#ifdef USE_TANGENTS
    @location(1 + NORMAL_COUNT) tangent: vec4<f32>,
#endif
#ifdef USE_BINORMALS
    @location(1 + NORMAL_COUNT + TANGENT_COUNT) binormal: vec4<f32>,
#endif
#ifdef USE_TEXCOORD0
    @location(1 + UV_START) texture0: vec2<f32>,
#endif
#ifdef USE_TEXCOORD1
    @location(1 + UV_START + UV0_COUNT) texture1: vec2<f32>,
#endif
#ifdef USE_TEXCOORD2
    @location(1 + UV_START + UV0_COUNT + UV1_COUNT) texture2: vec2<f32>,
#endif
#ifdef USE_TEXCOORD3
    @location(1 + UV_START + UV0_COUNT + UV1_COUNT + UV2_COUNT) texture3: vec2<f32>,
#endif
#if defined(USE_SKINNING) || defined(USE_SKINNING_QT) || defined(USE_SKINNING_TEXTURE)
    @location(1 + POSITION_SLOT) joints: vec4<f32>,
    @location(2 + POSITION_SLOT) weights: vec4<f32>,
#endif
}
struct VertexOutput {
    @builtin(position) hposition: vec4<f32>,
#ifdef USE_NORMALS
    @location(0) hnormal: vec4<f32>,
#endif
#ifdef USE_TANGENTS
    @location(NORMAL_COUNT) htangent: vec4<f32>,
#endif
#ifdef USE_BINORMALS
    @location(NORMAL_COUNT + TANGENT_COUNT) hbinormal: vec4<f32>,
#endif
#ifdef USE_TEXCOORD0
    @location(UV_START) texture0: vec2<f32>,
#endif
#ifdef USE_TEXCOORD1
    @location(UV_START + UV0_COUNT) texture1: vec2<f32>,
#endif
#ifdef USE_TEXCOORD2
    @location(UV_START + UV0_COUNT + UV1_COUNT) texture2: vec2<f32>,
#endif
#ifdef USE_TEXCOORD3
    @location(UV_START + UV0_COUNT + UV1_COUNT + UV2_COUNT) texture3: vec2<f32>,
#endif
    @location(POSITION_SLOT) Pos: vec4<f32>,
    @location(POSITION_SLOT + 1) WorldPos: vec4<f32>,
}

fn upperMatrix(matrix: mat4x4<f32>) -> mat3x3<f32> {
    return mat3x3<f32>(matrix[0].xyz, matrix[1].xyz, matrix[2].xyz);
}
fn rotateQuaternion(quaternion: vec4<f32>, value: vec3<f32>) -> vec3<f32> {
    return value + 2.0 * cross(quaternion.xyz, cross(quaternion.xyz, value) + quaternion.w * value);
}

@vertex fn VS(vertex: VertexInput) -> VertexOutput {
    var input = vertex;
    var result: VertexOutput;
#if defined(USE_SKINNING_TEXTURE) || defined(USE_SKINNING_QT) || defined(USE_SKINNING)
    let indices = vec4<i32>(input.joints);
#ifdef USE_SKINNING_TEXTURE
    let skinMatrix = getBoneMatrix(indices.x) * input.weights.x + getBoneMatrix(indices.y) * input.weights.y
        + getBoneMatrix(indices.z) * input.weights.z + getBoneMatrix(indices.w) * input.weights.w;
    input.position = input.position * skinMatrix;
#ifdef USE_NORMALS
    input.normal = vec4<f32>(input.normal.xyz * upperMatrix(skinMatrix), input.normal.w);
#endif
#ifdef USE_TANGENTS
    input.tangent = vec4<f32>(input.tangent.xyz * upperMatrix(skinMatrix), input.tangent.w);
#endif
#ifdef USE_BINORMALS
    input.binormal = vec4<f32>(input.binormal.xyz * upperMatrix(skinMatrix), input.binormal.w);
#endif
#elif defined(USE_SKINNING_QT)
    let quaternion = normalize(constants.BoneQuats[indices.x] * input.weights.x + constants.BoneQuats[indices.y] * input.weights.y
        + constants.BoneQuats[indices.z] * input.weights.z + constants.BoneQuats[indices.w] * input.weights.w);
    let translation = constants.BoneTrans[indices.x].xyz * input.weights.x + constants.BoneTrans[indices.y].xyz * input.weights.y
        + constants.BoneTrans[indices.z].xyz * input.weights.z + constants.BoneTrans[indices.w].xyz * input.weights.w;
    input.position = vec4<f32>(rotateQuaternion(quaternion, input.position.xyz) + translation, 1.0);
#ifdef USE_NORMALS
    input.normal = vec4<f32>(rotateQuaternion(quaternion, input.normal.xyz), input.normal.w);
#endif
#ifdef USE_TANGENTS
    input.tangent = vec4<f32>(rotateQuaternion(quaternion, input.tangent.xyz), input.tangent.w);
#endif
#ifdef USE_BINORMALS
    input.binormal = vec4<f32>(rotateQuaternion(quaternion, input.binormal.xyz), input.binormal.w);
#endif
#else
    let skinMatrix = constants.BoneMatrices[indices.x] * input.weights.x + constants.BoneMatrices[indices.y] * input.weights.y
        + constants.BoneMatrices[indices.z] * input.weights.z + constants.BoneMatrices[indices.w] * input.weights.w;
    input.position = skinMatrix * input.position;
#ifdef USE_NORMALS
    input.normal = vec4<f32>(upperMatrix(skinMatrix) * input.normal.xyz, input.normal.w);
#endif
#ifdef USE_TANGENTS
    input.tangent = vec4<f32>(upperMatrix(skinMatrix) * input.tangent.xyz, input.tangent.w);
#endif
#ifdef USE_BINORMALS
    input.binormal = vec4<f32>(upperMatrix(skinMatrix) * input.binormal.xyz, input.binormal.w);
#endif
#endif
#endif
#ifdef USE_TEXCOORD0
    result.texture0 = input.texture0;
#endif
#ifdef USE_TEXCOORD1
    result.texture1 = input.texture1;
#endif
#ifdef USE_TEXCOORD2
    result.texture2 = input.texture2;
#endif
#ifdef USE_TEXCOORD3
    result.texture3 = input.texture3;
#endif
    result.hposition = constants.WVP * input.position;
    result.Pos = result.hposition;
#ifndef SHADOW_MAP_PASS
    let rotation = upperMatrix(constants.World);
#ifdef USE_NORMALS
    result.hnormal = vec4<f32>(normalize(rotation * input.normal.xyz), 1.0);
#endif
#ifdef USE_TANGENTS
    result.htangent = vec4<f32>(normalize(rotation * input.tangent.xyz), 1.0);
#endif
#ifdef USE_BINORMALS
    result.hbinormal = vec4<f32>(normalize(rotation * input.binormal.xyz), 1.0);
#endif
    result.WorldPos = constants.World * input.position;
#endif
    return result;
}