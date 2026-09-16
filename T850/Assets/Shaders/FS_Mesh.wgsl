#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
#define FRAME material
struct MeshMaterialCB {
    WVP: mat4x4<f32>,
    World: mat4x4<f32>,
    WorldView: mat4x4<f32>,
    LightPos: vec4<f32>,
    LightColor: vec4<f32>,
    CameraPosition: vec4<f32>,
    CameraInfo: vec4<f32>,
#else
#define FRAME frame
struct MeshFrameCB {
    LightPos: vec4<f32>,
    LightColor: vec4<f32>,
    CameraPosition: vec4<f32>,
    CameraInfo: vec4<f32>,
    ParallaxSettings: vec4<f32>,
    ParallaxShadowSettings: vec4<f32>,
    Light0Direction: vec4<f32>,
    LightPositions: array<vec4<f32>, 128>,
    LightColors: array<vec4<f32>, 128>,
    LightRadius: array<vec4<f32>, 32>,
}
@group(0) @binding(64) var<uniform> frame: MeshFrameCB;
struct MeshMaterialCB {
#endif
    Ambient: vec4<f32>,
    DiffuseColor: vec4<f32>,
    SpecularColor: vec4<f32>,
    PBRParams: vec4<f32>,
    Intensities: vec4<f32>,
#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
    ParallaxSettings: vec4<f32>,
    ParallaxShadowSettings: vec4<f32>,
    Light0Direction: vec4<f32>,
#endif
    EmissiveColor: vec4<f32>,
    AlphaParams: vec4<f32>,
    ForwardParams: vec4<f32>,
    TexCoordSets: vec4<f32>,
    MaterialParams: vec4<f32>,
    MaterialParams2: vec4<f32>,
    MaterialParams3: vec4<f32>,
    MaterialParams4: vec4<f32>,
    MaterialParams5: vec4<f32>,
    MaterialParams6: vec4<f32>,
    MaterialParams7: vec4<f32>,
    MaterialParams8: vec4<f32>,
    MaterialParams9: vec4<f32>,
    BaseColorUVTransform0: vec4<f32>,
    BaseColorUVTransform1: vec4<f32>,
    NormalUVTransform0: vec4<f32>,
    NormalUVTransform1: vec4<f32>,
    MetallicUVTransform0: vec4<f32>,
    MetallicUVTransform1: vec4<f32>,
    EmissiveUVTransform0: vec4<f32>,
    EmissiveUVTransform1: vec4<f32>,
    SheenColorUVTransform0: vec4<f32>,
    SheenColorUVTransform1: vec4<f32>,
    SheenRoughnessUVTransform0: vec4<f32>,
    SheenRoughnessUVTransform1: vec4<f32>,
    ClearcoatUVTransform0: vec4<f32>,
    ClearcoatUVTransform1: vec4<f32>,
    ClearcoatRoughnessUVTransform0: vec4<f32>,
    ClearcoatRoughnessUVTransform1: vec4<f32>,
    OcclusionUVTransform0: vec4<f32>,
    OcclusionUVTransform1: vec4<f32>,
    SpecularFactorUVTransform0: vec4<f32>,
    SpecularFactorUVTransform1: vec4<f32>,
    SpecularColorUVTransform0: vec4<f32>,
    SpecularColorUVTransform1: vec4<f32>,
    TransmissionUVTransform0: vec4<f32>,
    TransmissionUVTransform1: vec4<f32>,
    LightmapUVTransform0: vec4<f32>,
    LightmapUVTransform1: vec4<f32>,
#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
    LightPositions: array<vec4<f32>, 128>,
    LightColors: array<vec4<f32>, 128>,
    LightRadius: array<vec4<f32>, 32>,
#endif
}
#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
@group(0) @binding(64) var<uniform> material: MeshMaterialCB;
#else
@group(0) @binding(66) var<uniform> material: MeshMaterialCB;
#endif

#ifdef DIFFUSE_MAP
@group(0) @binding(0) var TextureRGB: texture_2d<f32>;
#endif
#ifdef SPECULAR_MAP
@group(0) @binding(1) var TextureSpecular: texture_2d<f32>;
#endif
#ifdef GLOSS_MAP
@group(0) @binding(2) var TextureGloss: texture_2d<f32>;
#endif
#ifdef NORMAL_MAP
@group(0) @binding(3) var TextureNormal: texture_2d<f32>;
#endif
@group(0) @binding(4) var texEnv: texture_cube<f32>;
#ifdef HEIGHT_MAP
@group(0) @binding(5) var TextureHeight: texture_2d<f32>;
#endif
#ifdef METALLIC_MAP
@group(0) @binding(6) var TextureMetallic: texture_2d<f32>;
#endif
@group(0) @binding(7) var SceneDepthTex: texture_2d<f32>;
#ifdef EMISSIVE_MAP
@group(0) @binding(8) var EmissiveTex: texture_2d<f32>;
#endif
@group(0) @binding(9) var SceneColorTex: texture_2d<f32>;
@group(0) @binding(10) var texIBLDiffuse: texture_cube<f32>;
@group(0) @binding(11) var texIBLSpecular: texture_cube<f32>;
@group(0) @binding(12) var texIBLBRDF: texture_2d<f32>;
@group(0) @binding(13) var texIBLCharlie: texture_cube<f32>;
@group(0) @binding(14) var texIBLCharlieLUT: texture_2d<f32>;
@group(0) @binding(15) var texIBLSheenELUT: texture_2d<f32>;
#ifdef SHEEN_COLOR_MAP
@group(0) @binding(16) var SheenColorTex: texture_2d<f32>;
#endif
#ifdef SHEEN_ROUGHNESS_MAP
@group(0) @binding(17) var SheenRoughnessTex: texture_2d<f32>;
#endif
#ifdef CLEARCOAT_MAP
@group(0) @binding(18) var ClearcoatTex: texture_2d<f32>;
#endif
#ifdef CLEARCOAT_ROUGHNESS_MAP
@group(0) @binding(19) var ClearcoatRoughnessTex: texture_2d<f32>;
#endif
#ifdef OCCLUSION_MAP
@group(0) @binding(20) var OcclusionTex: texture_2d<f32>;
#endif
#ifdef SPECULAR_FACTOR_MAP
@group(0) @binding(21) var SpecularFactorTex: texture_2d<f32>;
#endif
#ifdef SPECULAR_COLOR_MAP
@group(0) @binding(22) var SpecularColorTex: texture_2d<f32>;
#endif
#ifdef TRANSMISSION_MAP
@group(0) @binding(23) var TransmissionTex: texture_2d<f32>;
#endif
#ifdef LIGHTMAP_MAP
@group(0) @binding(25) var LightmapTex: texture_2d<f32>;
#endif
@group(0) @binding(32) var MaterialSS: sampler;
@group(0) @binding(33) var SpecularSS: sampler;
@group(0) @binding(34) var GlossSS: sampler;
@group(0) @binding(35) var NormalSS: sampler;
@group(0) @binding(36) var EnvSS: sampler;
@group(0) @binding(37) var HeightSS: sampler;
@group(0) @binding(38) var MetallicSS: sampler;
@group(0) @binding(39) var LightmapSS: sampler;
@group(0) @binding(40) var EmissiveSS: sampler;
@group(0) @binding(41) var SceneColorSS: sampler;
@group(0) @binding(42) var IBLDiffuseSS: sampler;
@group(0) @binding(43) var IBLSpecularSS: sampler;
@group(0) @binding(44) var IBLBRDFSS: sampler;
@group(0) @binding(45) var IBLCharlieSS: sampler;
@group(0) @binding(46) var IBLCharlieLUTSS: sampler;
@group(0) @binding(47) var IBLSheenELUTSS: sampler;

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
struct FragmentInput {
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

fn GetForwardScreenUV(input: FragmentInput) -> vec2<f32> { return input.hposition.xy / material.ForwardParams.xy; }
fn LoadForwardSceneDepth(input: FragmentInput) -> f32 {
    let pixel = clamp(vec2<i32>(input.hposition.xy), vec2<i32>(0), vec2<i32>(material.ForwardParams.xy) - vec2<i32>(1));
    return textureLoad(SceneDepthTex, pixel, 0).r;
}
fn SignNotZero(value: vec2<f32>) -> vec2<f32> { return select(vec2<f32>(-1.0), vec2<f32>(1.0), value >= vec2<f32>(0.0)); }
fn EncodeOctahedralNormal(value: vec3<f32>) -> vec2<f32> {
    var normal = normalize(value);
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.000001);
    if (normal.z < 0.0) { normal = vec3<f32>((vec2<f32>(1.0) - abs(normal.yx)) * SignNotZero(normal.xy), normal.z); }
    return normal.xy * 0.5 + vec2<f32>(0.5);
}
const PBR_PI = 3.14159265359;
fn NormalDistribution(NdotH: f32, roughness: f32) -> f32 {
    let alpha = max(roughness * roughness, 0.001);
    let alphaSq = alpha * alpha;
    let factor = NdotH * NdotH * (alphaSq - 1.0) + 1.0;
    return alphaSq / max(PBR_PI * factor * factor, 0.000001);
}
fn FresnelCalc(VdotH: f32, specColor: vec3<f32>) -> vec3<f32> {
    return specColor + (vec3<f32>(1.0) - specColor) * pow(1.0 - VdotH, 5.0);
}
fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}
fn IBLGGXFresnel(NdotV: f32, roughness: f32, F0: vec3<f32>, brdfSample: vec2<f32>) -> vec3<f32> {
    let kSpecular = fresnelSchlickRoughness(NdotV, F0, roughness);
    let singleScatter = kSpecular * brdfSample.x + vec3<f32>(brdfSample.y);
    let energyMiss = clamp(1.0 - brdfSample.x - brdfSample.y, 0.0, 1.0);
    let averageFresnel = F0 + (vec3<f32>(1.0) - F0) / 21.0;
    let multiScatter = energyMiss * singleScatter * averageFresnel / max(vec3<f32>(1.0) - averageFresnel * energyMiss, vec3<f32>(0.001));
    return max(singleScatter + multiScatter, vec3<f32>(0.0));
}
fn VisibilityGGX(NdotL: f32, NdotV: f32, roughness: f32) -> f32 {
    let alpha = max(roughness * roughness, 0.001);
    let alphaSq = alpha * alpha;
    let ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSq) + alphaSq);
    let ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSq) + alphaSq);
    let ggx = ggxV + ggxL;
    if (ggx > 0.0) { return 0.5 / ggx; }
    return 0.0;
}
fn RangeAttenuation(range: f32, distanceToLight: f32) -> f32 {
    let distanceSq = max(distanceToLight * distanceToLight, 0.0001);
    if (range <= 0.0) { return 1.0 / distanceSq; }
    let normalizedDistance = distanceToLight / max(range, 0.0001);
    return clamp(1.0 - normalizedDistance * normalizedDistance * normalizedDistance * normalizedDistance, 0.0, 1.0) / distanceSq;
}
fn CalculateSpecular(specularColor: vec3<f32>, normal: vec3<f32>, view: vec3<f32>, halfvector: vec3<f32>, light: vec3<f32>, roughness: f32) -> vec3<f32> {
    let NdotH = max(dot(normal, halfvector), 0.0);
    let VdotH = clamp(dot(view, halfvector), 0.0, 1.0);
    let NdotL = clamp(dot(normal, light), 0.0, 1.0);
    let NdotV = clamp(dot(normal, view), 0.0, 1.0);
    return FresnelCalc(VdotH, specularColor) * NormalDistribution(NdotH, roughness) * VisibilityGGX(NdotL, NdotV, roughness) * NdotL;
}
fn CalculateDiffuse(albedoColor: vec3<f32>, normal: vec3<f32>, light: vec3<f32>) -> vec3<f32> { return albedoColor * clamp(dot(normal, light), 0.0, 1.0) / PBR_PI; }
fn CalculateClearcoat(normal: vec3<f32>, view: vec3<f32>, halfvector: vec3<f32>, light: vec3<f32>, roughness: f32) -> vec3<f32> {
    let NdotH = max(dot(normal, halfvector), 0.0);
    let NdotL = clamp(dot(normal, light), 0.0, 1.0);
    let NdotV = clamp(dot(normal, view), 0.0, 1.0);
    return vec3<f32>(NormalDistribution(NdotH, roughness) * VisibilityGGX(NdotL, NdotV, roughness) * NdotL);
}
fn Max3(value: vec3<f32>) -> f32 { return max(value.x, max(value.y, value.z)); }
fn LambdaSheenNumericHelper(value: f32, alphaG: f32) -> f32 {
    let oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
    let coeffA = mix(21.5473, 25.3245, oneMinusAlphaSq);
    let coeffB = mix(3.82987, 3.32435, oneMinusAlphaSq);
    let coeffC = mix(0.19823, 0.16801, oneMinusAlphaSq);
    let coeffD = mix(-1.97760, -1.27393, oneMinusAlphaSq);
    let coeffE = mix(-4.32054, -4.85967, oneMinusAlphaSq);
    return coeffA / (1.0 + coeffB * pow(value, coeffC)) + coeffD * value + coeffE;
}
fn LambdaSheen(cosTheta: f32, alphaG: f32) -> f32 {
    if (abs(cosTheta) < 0.5) { return exp(LambdaSheenNumericHelper(cosTheta, alphaG)); }
    return exp(2.0 * LambdaSheenNumericHelper(0.5, alphaG) - LambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
}
fn VisibilitySheen(NdotL: f32, NdotV: f32, roughness: f32) -> f32 {
    let sheenRoughness = max(roughness, 0.000001);
    let alphaG = sheenRoughness * sheenRoughness;
    let denominator = max((1.0 + LambdaSheen(NdotV, alphaG) + LambdaSheen(NdotL, alphaG)) * (4.0 * NdotV * NdotL), 0.000001);
    return clamp(1.0 / denominator, 0.0, 1.0);
}
fn DistributionCharlie(roughness: f32, NdotH: f32) -> f32 {
    let sheenRoughness = max(roughness, 0.000001);
    let invR = 1.0 / (sheenRoughness * sheenRoughness);
    let sin2h = max(1.0 - NdotH * NdotH, 0.0);
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * 3.1415926);
}
fn BRDFSpecularSheen(sheenColor: vec3<f32>, sheenRoughness: f32, NdotL: f32, NdotV: f32, NdotH: f32) -> vec3<f32> {
    return sheenColor * DistributionCharlie(sheenRoughness, NdotH) * VisibilitySheen(NdotL, NdotV, sheenRoughness);
}
fn AlbedoSheenScalingLUT(NdotV: f32, sheenRoughness: f32) -> f32 {
    return textureSampleLevel(texIBLSheenELUT, IBLSheenELUTSS, clamp(vec2<f32>(NdotV, sheenRoughness), vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).r;
}
fn CalculateSheenRadiance(sheenColor: vec3<f32>, sheenRoughness: f32, lightColor: vec3<f32>, intensity: f32, NdotL: f32, NdotV: f32, NdotH: f32) -> vec3<f32> {
    return lightColor * intensity * NdotL * BRDFSpecularSheen(sheenColor, sheenRoughness, NdotL, NdotV, NdotH);
}
fn GetIBLRadianceCharlie(normal: vec3<f32>, viewDir: vec3<f32>, sheenRoughness: f32, sheenColor: vec3<f32>, iblMaxMip: f32) -> vec3<f32> {
    let NdotV = max(dot(normal, viewDir), 0.0);
    let reflectedVec = reflect(-viewDir, normal) * vec3<f32>(-1.0, 1.0, -1.0);
    let brdf = textureSampleLevel(texIBLCharlieLUT, IBLCharlieLUTSS, clamp(vec2<f32>(NdotV, sheenRoughness), vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).b;
    return textureSampleLevel(texIBLCharlie, IBLCharlieSS, reflectedVec, sheenRoughness * iblMaxMip).rgb * sheenColor * brdf;
}
fn GetUV0(input: FragmentInput) -> vec2<f32> {
#ifdef USE_TEXCOORD0
    return input.texture0;
#elif defined(USE_TEXCOORD1)
    return input.texture1;
#elif defined(USE_TEXCOORD2)
    return input.texture2;
#elif defined(USE_TEXCOORD3)
    return input.texture3;
#else
    return vec2<f32>(0.0);
#endif
}
fn GetUV1(input: FragmentInput) -> vec2<f32> {
#ifdef USE_TEXCOORD1
    return input.texture1;
#elif defined(USE_TEXCOORD0)
    return input.texture0;
#else
    return vec2<f32>(0.0);
#endif
}
fn GetUV2(input: FragmentInput) -> vec2<f32> {
#ifdef USE_TEXCOORD2
    return input.texture2;
#elif defined(USE_TEXCOORD0)
    return input.texture0;
#else
    return vec2<f32>(0.0);
#endif
}
fn GetUV3(input: FragmentInput) -> vec2<f32> {
#ifdef USE_TEXCOORD3
    return input.texture3;
#elif defined(USE_TEXCOORD0)
    return input.texture0;
#else
    return vec2<f32>(0.0);
#endif
}
fn GetTexCoord(input: FragmentInput, texCoordSet: f32) -> vec2<f32> {
    let selected = i32(texCoordSet + 0.5);
    if (selected == 1) { return GetUV1(input); }
    if (selected == 2) { return GetUV2(input); }
    if (selected == 3) { return GetUV3(input); }
    return GetUV0(input);
}
fn MapShareBaseSet(mapSet: f32) -> bool { return abs(mapSet - material.TexCoordSets.x) < 0.5; }
fn ApplyUVTransform(uv: vec2<f32>, row0: vec4<f32>, row1: vec4<f32>) -> vec2<f32> { return vec2<f32>(dot(row0.xy, uv) + row0.z, dot(row1.xy, uv) + row1.z); }
fn LinearToStoredAlbedo(color: vec3<f32>) -> vec3<f32> { return pow(clamp(color, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(1.0 / 2.2)); }
fn StoredSRGBToLinear(color: vec3<f32>) -> vec3<f32> { return pow(max(color, vec3<f32>(0.0)), vec3<f32>(2.2)); }
fn SampleBaseColor(uv: vec2<f32>) -> vec4<f32> {
#if defined(DIFFUSE_MAP) && (defined(USE_TEXCOORD0) || defined(USE_TEXCOORD1) || defined(USE_TEXCOORD2) || defined(USE_TEXCOORD3))
    var color = textureSample(TextureRGB, MaterialSS, ApplyUVTransform(uv, material.BaseColorUVTransform0, material.BaseColorUVTransform1));
#ifdef GLTF_TANGENT_SPACE
    color *= vec4<f32>(LinearToStoredAlbedo(material.DiffuseColor.rgb), material.DiffuseColor.a);
#endif
    return color;
#else
#ifdef GLTF_TANGENT_SPACE
    return vec4<f32>(LinearToStoredAlbedo(material.DiffuseColor.rgb), material.DiffuseColor.a);
#else
    return material.DiffuseColor;
#endif
#endif
}
fn SampleEmissive(input: FragmentInput, uv: vec2<f32>) -> vec3<f32> {
    var emissive = material.EmissiveColor.rgb;
#ifdef EMISSIVE_MAP
    var emissiveUV = select(GetTexCoord(input, material.TexCoordSets.w), uv, MapShareBaseSet(material.TexCoordSets.w));
    emissiveUV = ApplyUVTransform(emissiveUV, material.EmissiveUVTransform0, material.EmissiveUVTransform1);
    var sampleColor = textureSample(EmissiveTex, EmissiveSS, emissiveUV).rgb;
#ifdef GLTF_TANGENT_SPACE
    sampleColor = StoredSRGBToLinear(sampleColor);
#endif
    emissive *= sampleColor;
#endif
    return emissive * material.MaterialParams.w;
}
fn SampleLightmap(input: FragmentInput) -> f32 {
#ifdef LIGHTMAP_MAP
    let uv = ApplyUVTransform(GetTexCoord(input, material.MaterialParams9.z), material.LightmapUVTransform0, material.LightmapUVTransform1);
    let lightmap = textureSample(LightmapTex, LightmapSS, uv).rgb;
    return max(dot(lightmap, vec3<f32>(0.2126, 0.7152, 0.0722)) * material.MaterialParams9.w, 0.0);
#else
    return 0.0;
#endif
}
fn ApplyAlphaMask(value: vec4<f32>) -> vec4<f32> {
    var color = value;
    if (material.AlphaParams.x > 0.5 && material.AlphaParams.x < 1.5) {
        if (color.a < material.AlphaParams.y) { discard; }
        color.a = 1.0;
    }
    return color;
}
fn GetPackedLightRadius(index: i32) -> f32 { return FRAME.LightRadius[index >> 2][index & 3]; }

struct Surface {
    color: vec4<f32>,
    normal: vec3<f32>,
    geoNormal: vec3<f32>,
    metallic: f32,
    roughness: f32,
    selfShadow: f32,
    uv: vec2<f32>,
    sheenColor: vec3<f32>,
    sheenRoughness: f32,
    clearcoatFactor: f32,
    clearcoatRoughness: f32,
    occlusion: f32,
    dielectricF0: vec3<f32>,
    specularWeight: f32,
    transmissionFactor: f32,
}
fn BuildSurface(input: FragmentInput, isFrontFace: bool) -> Surface {
    var surface: Surface;
    surface.metallic = material.PBRParams.x;
    surface.roughness = material.PBRParams.y;
    surface.selfShadow = 1.0;
    surface.occlusion = 1.0;
    surface.dielectricF0 = max(material.SpecularColor.rgb, vec3<f32>(0.0));
    surface.specularWeight = max(material.SpecularColor.w, 0.0);
    surface.transmissionFactor = clamp(material.AlphaParams.w, 0.0, 1.0);
    surface.sheenColor = clamp(material.MaterialParams4.rgb, vec3<f32>(0.0), vec3<f32>(1.0));
    surface.sheenRoughness = clamp(material.MaterialParams4.w, 0.0, 1.0);
    surface.clearcoatFactor = clamp(material.MaterialParams.x, 0.0, 1.0);
    surface.clearcoatRoughness = clamp(material.MaterialParams.y, 0.0, 1.0);
    var uv = GetTexCoord(input, material.TexCoordSets.x);
#ifdef USE_NORMALS
    surface.normal = normalize(input.hnormal.xyz);
#else
    surface.normal = vec3<f32>(0.0, 0.0, 1.0);
#endif
    let flipBackFace = material.AlphaParams.z > 0.5 && !isFrontFace;
    if (flipBackFace) { surface.normal = -surface.normal; }
    surface.geoNormal = surface.normal;
#if defined(HEIGHT_MAP) || defined(NORMAL_MAP)
    var tangent = normalize(input.htangent.xyz);
    var binormal = normalize(input.hbinormal.xyz);
    if (flipBackFace) { tangent = -tangent; binormal = -binormal; }
    let basis = mat3x3<f32>(tangent, binormal, surface.normal);
#endif
#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    let baseUVDx = dpdx(input.texture0);
    let baseUVDy = dpdy(input.texture0);
    let viewDir = normalize(transpose(basis) * normalize(FRAME.CameraPosition.xyz - input.WorldPos.xyz));
    let numLayers = mix(FRAME.ParallaxSettings.y, FRAME.ParallaxSettings.x, abs(viewDir.z));
    let layerDepth = 1.0 / numLayers;
    var previousDepth = 0.0;
    let deltaTexCoords = (-viewDir.xy * FRAME.ParallaxSettings.z / viewDir.z) * layerDepth * vec2<f32>(1.0, -1.0);
    var currentDepth = textureSampleGrad(TextureHeight, HeightSS, uv, dpdx(uv), dpdy(uv)).r;
    var currentRayZ = 1.0 - layerDepth;
    var previousRayZ = 1.0 - layerDepth;
    while (currentRayZ > currentDepth) {
        currentDepth = textureSampleGrad(TextureHeight, HeightSS, uv, baseUVDx, baseUVDy).r;
        previousDepth = currentDepth;
        uv += deltaTexCoords;
        previousRayZ = currentRayZ;
        currentRayZ -= layerDepth;
    }
    let weight = (previousDepth - previousRayZ) / (previousDepth - currentDepth + currentRayZ - previousRayZ);
    uv = (uv - deltaTexCoords) * weight + uv * (1.0 - weight);
#endif
    surface.color = ApplyAlphaMask(SampleBaseColor(uv));
#ifdef NORMAL_MAP
    let normalUV = ApplyUVTransform(select(GetTexCoord(input, material.TexCoordSets.y), uv, MapShareBaseSet(material.TexCoordSets.y)), material.NormalUVTransform0, material.NormalUVTransform1);
    var normalTex = textureSample(TextureNormal, NormalSS, normalUV).xyz * 2.0 - vec3<f32>(1.0);
    normalTex = normalize(vec3<f32>(normalTex.xy * material.MaterialParams9.y, normalTex.z));
#ifndef GLTF_TANGENT_SPACE
    normalTex.y = -normalTex.y;
#endif
    surface.normal = normalize(basis * normalTex);
#endif
#ifdef METALLIC_MAP
    let metallicUV = ApplyUVTransform(select(GetTexCoord(input, material.TexCoordSets.z), uv, MapShareBaseSet(material.TexCoordSets.z)), material.MetallicUVTransform0, material.MetallicUVTransform1);
    let sampleMR = textureSample(TextureMetallic, MetallicSS, metallicUV);
    surface.metallic = material.PBRParams.x * sampleMR.b;
    surface.roughness = material.PBRParams.y * sampleMR.g;
#elif defined(GLOSS_MAP)
    surface.roughness = textureSample(TextureGloss, GlossSS, uv).r;
#endif
    surface.roughness = clamp(surface.roughness, 0.04, 1.0);
    surface.metallic = clamp(surface.metallic, 0.0, 1.0);
#ifdef SPECULAR_FACTOR_MAP
    if (material.MaterialParams8.y > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams8.z), uv, MapShareBaseSet(material.MaterialParams8.z)), material.SpecularFactorUVTransform0, material.SpecularFactorUVTransform1);
        surface.specularWeight *= textureSample(SpecularFactorTex, MaterialSS, sampleUV).a;
    }
#endif
#ifdef SPECULAR_COLOR_MAP
    if (material.MaterialParams8.w > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams9.x), uv, MapShareBaseSet(material.MaterialParams9.x)), material.SpecularColorUVTransform0, material.SpecularColorUVTransform1);
        surface.dielectricF0 = min(surface.dielectricF0 * StoredSRGBToLinear(textureSample(SpecularColorTex, MaterialSS, sampleUV).rgb), vec3<f32>(1.0));
    }
#endif
    surface.specularWeight = clamp(surface.specularWeight, 0.0, 1.0);
#ifdef OCCLUSION_MAP
    if (material.MaterialParams7.x > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams7.z), uv, MapShareBaseSet(material.MaterialParams7.z)), material.OcclusionUVTransform0, material.OcclusionUVTransform1);
        let occlusion = textureSample(OcclusionTex, MaterialSS, sampleUV).r;
        surface.occlusion = clamp(1.0 + material.MaterialParams7.y * (occlusion - 1.0), 0.0, 1.0);
    }
#endif
#ifdef TRANSMISSION_MAP
    if (material.MaterialParams7.w > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams8.x), uv, MapShareBaseSet(material.MaterialParams8.x)), material.TransmissionUVTransform0, material.TransmissionUVTransform1);
        surface.transmissionFactor *= textureSample(TransmissionTex, MaterialSS, sampleUV).r;
    }
#endif
    surface.transmissionFactor = clamp(surface.transmissionFactor, 0.0, 1.0);
#ifdef SHEEN_COLOR_MAP
    if (material.MaterialParams5.x > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams5.z), uv, MapShareBaseSet(material.MaterialParams5.z)), material.SheenColorUVTransform0, material.SheenColorUVTransform1);
        surface.sheenColor *= StoredSRGBToLinear(textureSample(SheenColorTex, MaterialSS, sampleUV).rgb);
    }
#endif
#ifdef SHEEN_ROUGHNESS_MAP
    if (material.MaterialParams5.y > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams5.w), uv, MapShareBaseSet(material.MaterialParams5.w)), material.SheenRoughnessUVTransform0, material.SheenRoughnessUVTransform1);
        surface.sheenRoughness *= textureSample(SheenRoughnessTex, MaterialSS, sampleUV).a;
    }
#endif
    surface.sheenColor = clamp(surface.sheenColor, vec3<f32>(0.0), vec3<f32>(1.0));
    surface.sheenRoughness = clamp(surface.sheenRoughness, 0.0, 1.0);
#ifdef CLEARCOAT_MAP
    if (material.MaterialParams6.x > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams6.z), uv, MapShareBaseSet(material.MaterialParams6.z)), material.ClearcoatUVTransform0, material.ClearcoatUVTransform1);
        surface.clearcoatFactor *= textureSample(ClearcoatTex, MaterialSS, sampleUV).r;
    }
#endif
#ifdef CLEARCOAT_ROUGHNESS_MAP
    if (material.MaterialParams6.y > 0.5) {
        let sampleUV = ApplyUVTransform(select(GetTexCoord(input, material.MaterialParams6.w), uv, MapShareBaseSet(material.MaterialParams6.w)), material.ClearcoatRoughnessUVTransform0, material.ClearcoatRoughnessUVTransform1);
        surface.clearcoatRoughness *= textureSample(ClearcoatRoughnessTex, MaterialSS, sampleUV).g;
    }
#endif
    surface.clearcoatFactor = clamp(surface.clearcoatFactor, 0.0, 1.0);
    surface.clearcoatRoughness = clamp(surface.clearcoatRoughness, 0.0, 1.0);
#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    let gradientX = baseUVDx;
    let gradientY = baseUVDy;
    let startZ = textureSampleGrad(TextureHeight, HeightSS, uv, gradientX, gradientY).r;
    let shadowStrength = FRAME.ParallaxShadowSettings.w;
    if (shadowStrength > 0.001 && length(FRAME.Light0Direction.xyz) > 0.001) {
        let lightDirTS = normalize(transpose(basis) * normalize(-FRAME.Light0Direction.xyz));
        if (lightDirTS.z > 0.01) {
            let shadowLayers = mix(FRAME.ParallaxShadowSettings.y, FRAME.ParallaxShadowSettings.x, abs(lightDirTS.z));
            let layerStep = 1.0 / shadowLayers;
            let deltaUV = lightDirTS.xy * FRAME.ParallaxSettings.z / lightDirTS.z * layerStep * vec2<f32>(1.0, -1.0);
            var currentUV = uv;
            var shadowRayZ = startZ;
            for (var sampleIndex = 0; sampleIndex < i32(shadowLayers); sampleIndex++) {
                shadowRayZ += layerStep;
                currentUV += deltaUV;
                if (shadowRayZ >= 1.0) { break; }
                let height = textureSampleGrad(TextureHeight, HeightSS, currentUV, gradientX, gradientY).r;
                if (height > shadowRayZ) {
                    let penumbra = f32(sampleIndex + 1) / shadowLayers;
                    surface.selfShadow = min(surface.selfShadow, mix(0.0, penumbra, FRAME.ParallaxShadowSettings.z));
                }
            }
            surface.selfShadow = mix(1.0, surface.selfShadow, shadowStrength);
        }
    }
#endif
    surface.uv = uv;
    return surface;
}

#ifdef SIMPLE_COLOR
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> { return vec4<f32>(0.5, 0.5, 0.5, 1.0); }
#elif defined(G_BUFFER_PASS)
struct FragmentOutput {
    @location(0) color0: vec4<f32>,
    @location(1) color1: vec4<f32>,
    @location(2) color2: vec4<f32>,
    @location(3) color3: vec4<f32>,
    @location(4) color4: vec4<f32>,
    @location(5) color5: vec4<f32>,
    @location(6) color6: vec4<f32>,
    @builtin(frag_depth) depth: f32,
}
@fragment fn FS(input: FragmentInput, @builtin(front_facing) isFrontFace: bool) -> FragmentOutput {
    let surface = BuildSurface(input, isFrontFace);
    var result: FragmentOutput;
    result.color0 = vec4<f32>(surface.color.rgb, surface.specularWeight);
    result.color1 = vec4<f32>(surface.normal * 0.5 + vec3<f32>(0.5), surface.roughness);
    result.color2 = vec4<f32>(surface.metallic, surface.selfShadow, surface.clearcoatFactor, material.Intensities.w / 255.0);
    let packedMaterial = surface.clearcoatRoughness * 0.5 + select(0.0, 0.5, material.MaterialParams.z > 0.5);
    result.color3 = vec4<f32>(EncodeOctahedralNormal(surface.geoNormal), clamp(SampleLightmap(input), 0.0, 1.0), packedMaterial);
    result.depth = input.Pos.z / input.Pos.w;
    result.color4 = vec4<f32>(SampleEmissive(input, surface.uv), 0.0);
    result.color5 = vec4<f32>(surface.sheenColor, surface.sheenRoughness);
    result.color6 = vec4<f32>(surface.dielectricF0, surface.occlusion);
    return result;
}
#elif defined(SHADOW_MAP_PASS) || defined(DEPTH_PRE_PASS)
@fragment fn FS(input: FragmentInput) -> @builtin(frag_depth) f32 {
    let color = ApplyAlphaMask(SampleBaseColor(GetTexCoord(input, material.TexCoordSets.x)));
    return input.Pos.z / input.Pos.w;
}
#else
@fragment fn FS(input: FragmentInput, @builtin(front_facing) isFrontFace: bool) -> @location(0) vec4<f32> {
    var surface = BuildSurface(input, isFrontFace);
    let emissive = SampleEmissive(input, surface.uv);
    let lightmap = SampleLightmap(input);
#ifdef FORWARD_PASS
    if (material.ForwardParams.z > 0.5 && material.ForwardParams.x > 0.0 && material.ForwardParams.y > 0.0) {
        let sceneDepth = LoadForwardSceneDepth(input);
        if (sceneDepth > 0.0001 && input.Pos.z / input.Pos.w < sceneDepth - 0.000001) { discard; }
    }
#endif
    let albedo = StoredSRGBToLinear(surface.color.rgb);
    let eyeDir = normalize(FRAME.CameraPosition.xyz - input.WorldPos.xyz);
    let F0 = mix(surface.dielectricF0 * surface.specularWeight, albedo, surface.metallic);
    var directLight = vec3<f32>(0.0);
    let sheenStrength = Max3(surface.sheenColor);
    let hasSheenLUT = material.MaterialParams3.y > 0.5;
    for (var lightIndex = 0; lightIndex < i32(FRAME.CameraInfo.w); lightIndex++) {
        let lightPosition = FRAME.LightPositions[lightIndex];
        let lightColor = FRAME.LightColors[lightIndex];
        var attenuation = 1.0;
        var lightDir = normalize(-lightPosition.xyz);
        if (lightPosition.w >= 0.5) {
            attenuation = RangeAttenuation(GetPackedLightRadius(lightIndex) * 2.0, distance(lightPosition.xyz, input.WorldPos.xyz));
            lightDir = normalize(lightPosition.xyz - input.WorldPos.xyz);
        }
        if (attenuation <= 0.0) { continue; }
        let halfVec = normalize(eyeDir + lightDir);
        let radiance = lightColor.xyz * lightColor.w * attenuation;
        let diffuse = CalculateDiffuse(albedo, surface.normal, lightDir) * radiance;
        let specular = CalculateSpecular(F0, surface.normal, eyeDir, halfVec, lightDir, surface.roughness) * radiance;
        let fresnel = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0, 1.0), F0);
        let diffuseWeight = (vec3<f32>(1.0) - fresnel) * (1.0 - surface.metallic);
        let NdotL = max(dot(surface.normal, lightDir), 0.0);
        let NdotVLight = max(dot(surface.normal, eyeDir), 0.0);
        let NdotH = max(dot(surface.normal, halfVec), 0.0);
        var albedoSheenScaling = 1.0;
        var sheenLight = vec3<f32>(0.0);
        if (hasSheenLUT && sheenStrength > 0.0) {
            albedoSheenScaling = min(1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, surface.sheenRoughness), 1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotL, surface.sheenRoughness));
            sheenLight = CalculateSheenRadiance(surface.sheenColor, surface.sheenRoughness, lightColor.xyz, lightColor.w, NdotL, NdotVLight, NdotH) * attenuation;
        }
        var layerLight = sheenLight + (specular + diffuseWeight * diffuse) * albedoSheenScaling;
        if (surface.clearcoatFactor > 0.001) {
            let clearcoatF = FresnelCalc(clamp(dot(surface.normal, eyeDir), 0.0, 1.0), vec3<f32>(0.04)).x;
            let clearcoatLight = CalculateClearcoat(surface.normal, eyeDir, halfVec, lightDir, clamp(surface.clearcoatRoughness, 0.04, 1.0)) * radiance;
            layerLight = mix(layerLight, clearcoatLight, clamp(surface.clearcoatFactor * clearcoatF, 0.0, 1.0));
        }
        directLight += layerLight;
    }
    var finalColor = directLight * surface.selfShadow;
    let iblFactor = max(material.MaterialParams2.w, 0.0);
    let iblMaxMip = max(material.MaterialParams3.x, 0.0);
    let hasBrdfLUT = material.MaterialParams3.y > 0.5;
    let reflectedVec = reflect(-eyeDir, surface.normal) * vec3<f32>(-1.0, 1.0, -1.0);
    let NdotV = max(dot(surface.normal, eyeDir), 0.0);
    let kSpecular = clamp(fresnelSchlickRoughness(NdotV, F0, surface.roughness), vec3<f32>(0.0), vec3<f32>(1.0));
    let kDiffuseEnv = (vec3<f32>(1.0) - kSpecular) * (1.0 - surface.metallic);
    let envSpec = textureSampleLevel(texIBLSpecular, IBLSpecularSS, reflectedVec, surface.roughness * iblMaxMip).xyz;
    var specularIBL = kSpecular * (1.0 - surface.roughness) * (1.0 - surface.roughness);
    if (hasBrdfLUT) {
        let brdfSample = textureSampleLevel(texIBLBRDF, IBLBRDFSS, vec2<f32>(NdotV, surface.roughness), 0.0).rg;
        specularIBL = IBLGGXFresnel(NdotV, surface.roughness, F0, brdfSample);
    }
    var indirectLight = envSpec * specularIBL * iblFactor;
    let irradiance = textureSampleLevel(texIBLDiffuse, IBLDiffuseSS, surface.normal * vec3<f32>(-1.0, 1.0, -1.0), clamp(material.MaterialParams3.z, 0.0, iblMaxMip)).xyz;
    indirectLight += irradiance * albedo * kDiffuseEnv * iblFactor;
    indirectLight += albedo * kDiffuseEnv * lightmap;
    if (hasSheenLUT && sheenStrength > 0.0) {
        let scaling = 1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotV, surface.sheenRoughness);
        indirectLight = GetIBLRadianceCharlie(surface.normal, eyeDir, surface.sheenRoughness, surface.sheenColor, iblMaxMip) * iblFactor + indirectLight * scaling;
    }
    finalColor += indirectLight * surface.occlusion;
    if (surface.clearcoatFactor > 0.001) {
        surface.clearcoatRoughness = clamp(surface.clearcoatRoughness, 0.04, 1.0);
        let clearcoatSpec = textureSampleLevel(texIBLSpecular, IBLSpecularSS, reflectedVec, surface.clearcoatRoughness * iblMaxMip).xyz;
        let clearcoatAttenuation = select((1.0 - surface.clearcoatRoughness) * (1.0 - surface.clearcoatRoughness), 1.0, hasBrdfLUT);
        let clearcoatF = FresnelCalc(clamp(dot(surface.normal, eyeDir), 0.0, 1.0), vec3<f32>(0.04));
        finalColor = mix(finalColor, clearcoatSpec * clearcoatAttenuation * iblFactor, clamp(surface.clearcoatFactor * Max3(clearcoatF), 0.0, 1.0));
    }
    if (material.MaterialParams.z > 0.5) { finalColor = albedo; }
    let transmission = clamp(surface.transmissionFactor * material.MaterialParams2.x, 0.0, 1.0);
#ifdef FORWARD_PASS
    if (material.MaterialParams2.z > 0.5 && transmission > 0.001 && material.MaterialParams2.y > 0.0 && material.ForwardParams.x > 0.0 && material.ForwardParams.y > 0.0) {
        let offset = clamp(abs(material.ForwardParams.w - 1.0), 0.0, 1.0);
        let refractUV = clamp(GetForwardScreenUV(input) + surface.normal.xy * material.MaterialParams2.y * transmission * (0.5 + offset), vec2<f32>(0.0), vec2<f32>(1.0));
        finalColor = mix(finalColor, textureSampleLevel(SceneColorTex, SceneColorSS, refractUV, 0.0).rgb, transmission);
    }
#endif
    finalColor += emissive;
    var alpha = surface.color.a;
    if (transmission > 0.0 && alpha >= 0.999) { alpha = clamp(1.0 - transmission, 0.0, 1.0); }
    if (material.AlphaParams.x < 1.5 && transmission <= 0.0) { alpha = 1.0; }
    return vec4<f32>(finalColor, alpha);
}
#endif