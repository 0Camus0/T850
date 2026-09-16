#ifdef NO_ENVIRONMENT
#define T850_SAMPLE_ENVIRONMENT(texture, sampler, coords, level) vec4<f32>(0.0)
#else
#define T850_SAMPLE_ENVIRONMENT(texture, sampler, coords, level) textureSampleLevel(texture, sampler, coords, level)
#endif

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
struct QuadPassCB {
    LightPositions: array<vec4<f32>, 128>,
    LightColors: array<vec4<f32>, 128>,
    LightRadius: array<vec4<f32>, 32>,
    brightness: vec4<f32>,
    toogles: vec4<f32>,
}
struct ShadowSamplingCB {
    ShadowViewProjection: array<mat4x4<f32>, 6>,
    ShadowSplitDepths: array<vec4<f32>, 2>,
    ShadowAtlasScaleBias: array<vec4<f32>, 6>,
    ShadowParams0: vec4<f32>,
    ShadowParams1: vec4<f32>,
}
@group(0) @binding(64) var<uniform> frame: QuadFrameCB;
@group(0) @binding(65) var<uniform> quadPass: QuadPassCB;
@group(0) @binding(66) var<uniform> shadows: ShadowSamplingCB;
@group(0) @binding(0) var tex0: texture_2d<f32>;
@group(0) @binding(1) var tex1: texture_2d<f32>;
@group(0) @binding(2) var tex2: texture_2d<f32>;
@group(0) @binding(3) var tex3: texture_2d<f32>;
@group(0) @binding(4) var tex4: texture_2d<f32>;
@group(0) @binding(5) var tex5: texture_2d<f32>;
@group(0) @binding(6) var texEnv: texture_cube<f32>;
@group(0) @binding(7) var tex6: texture_2d<f32>;
@group(0) @binding(8) var tex7: texture_2d<f32>;
@group(0) @binding(9) var tex8: texture_2d<f32>;
@group(0) @binding(10) var texIBLDiffuse: texture_cube<f32>;
@group(0) @binding(11) var texIBLSpecular: texture_cube<f32>;
@group(0) @binding(12) var texIBLBRDF: texture_2d<f32>;
@group(0) @binding(13) var texIBLCharlie: texture_cube<f32>;
@group(0) @binding(14) var texIBLCharlieLUT: texture_2d<f32>;
@group(0) @binding(15) var texIBLSheenELUT: texture_2d<f32>;
@group(0) @binding(16) var texTileHeaders: texture_2d<f32>;
@group(0) @binding(17) var texTileLightIndices: texture_2d<f32>;
@group(0) @binding(32) var SS: sampler;
@group(0) @binding(33) var SS1: sampler;
@group(0) @binding(34) var SS2: sampler;
@group(0) @binding(35) var SS3: sampler;
@group(0) @binding(36) var SS4: sampler;
@group(0) @binding(37) var SS5: sampler;
@group(0) @binding(38) var SS6: sampler;
@group(0) @binding(39) var SS7: sampler;
@group(0) @binding(40) var SS8: sampler;
@group(0) @binding(41) var SS9: sampler;
@group(0) @binding(42) var SS10: sampler;
@group(0) @binding(43) var SS11: sampler;
@group(0) @binding(44) var SS12: sampler;
@group(0) @binding(45) var SS13: sampler;
@group(0) @binding(46) var SS14: sampler;
@group(0) @binding(47) var SS15: sampler;
struct FragmentInput {
    @builtin(position) hposition: vec4<f32>,
    @location(0) texture0: vec2<f32>,
    @location(1) Pos: vec4<f32>,
    @location(2) PosCorner: vec4<f32>,
    @location(3) ClipPos: vec2<f32>,
}
const DEPTH_CLEAR_EPSILON = 0.0001;
fn IsSceneDepthValid(depth: f32) -> bool { return depth > DEPTH_CLEAR_EPSILON; }
fn roundTo(num: f32, decimals: f32) -> f32 {
    let shift = pow(10.0, decimals);
    return round(num * shift) / shift;
}
fn LinearToSRGBChannel(input: f32) -> f32 {
    let value = clamp(input, 0.0, 1.0);
    if (value <= 0.0031308) { return value * 12.92; }
    return 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
fn LinearToSRGB(color: vec3<f32>) -> vec3<f32> {
    return vec3<f32>(LinearToSRGBChannel(color.r), LinearToSRGBChannel(color.g), LinearToSRGBChannel(color.b));
}
fn ReconstructPosition(clipPos: vec2<f32>, depth: f32) -> vec4<f32> {
    let position = frame.WVPInverse * vec4<f32>(clipPos, depth, 1.0);
    return vec4<f32>(position.xyz / position.w, 1.0);
}
fn LinearizeDepth(depth: f32) -> f32 {
    return frame.CameraInfo.x * frame.CameraInfo.y / (frame.CameraInfo.x + depth * (frame.CameraInfo.y - frame.CameraInfo.x));
}
fn DecodeOctahedralNormal(encoded: vec2<f32>) -> vec3<f32> {
    let projected = encoded * 2.0 - vec2<f32>(1.0);
    var normal = vec3<f32>(projected, 1.0 - abs(projected.x) - abs(projected.y));
    let fold = max(-normal.z, 0.0);
    normal.x += select(fold, -fold, normal.x >= 0.0);
    normal.y += select(fold, -fold, normal.y >= 0.0);
    return normalize(normal);
}

const PBR_PI = 3.14159265359;
fn NormalDistribution(NdotH: f32, roughness: f32) -> f32 {
    let alpha = max(roughness * roughness, 0.001);
    let alphaSq = alpha * alpha;
    let factor = NdotH * NdotH * (alphaSq - 1.0) + 1.0;
    return alphaSq / max(PBR_PI * factor * factor, 0.000001);
}
fn FresnelCalc(VdotH: f32, specColor: vec3<f32>) -> vec3<f32> { return specColor + (vec3<f32>(1.0) - specColor) * pow(1.0 - VdotH, 5.0); }
fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3<f32>, roughness: f32) -> vec3<f32> {
    return F0 + (max(vec3<f32>(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}
fn IBLGGXFresnel(NdotV: f32, roughness: f32, F0: vec3<f32>, brdfSample: vec2<f32>) -> vec3<f32> {
    let singleScatter = fresnelSchlickRoughness(NdotV, F0, roughness) * brdfSample.x + vec3<f32>(brdfSample.y);
    let energyMiss = clamp(1.0 - brdfSample.x - brdfSample.y, 0.0, 1.0);
    let averageFresnel = F0 + (vec3<f32>(1.0) - F0) / 21.0;
    let multiScatter = energyMiss * singleScatter * averageFresnel / max(vec3<f32>(1.0) - averageFresnel * energyMiss, vec3<f32>(0.001));
    return max(singleScatter + multiScatter, vec3<f32>(0.0));
}
fn VisibilityGGX(NdotL: f32, NdotV: f32, roughness: f32) -> f32 {
    let alpha = max(roughness * roughness, 0.001);
    let alphaSq = alpha * alpha;
    let ggx = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSq) + alphaSq) + NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSq) + alphaSq);
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
    let NdotL = clamp(dot(normal, light), 0.0, 1.0);
    return FresnelCalc(clamp(dot(view, halfvector), 0.0, 1.0), specularColor) * NormalDistribution(max(dot(normal, halfvector), 0.0), roughness)
        * VisibilityGGX(NdotL, clamp(dot(normal, view), 0.0, 1.0), roughness) * NdotL;
}
fn CalculateDiffuse(albedo: vec3<f32>, normal: vec3<f32>, light: vec3<f32>) -> vec3<f32> { return albedo * clamp(dot(normal, light), 0.0, 1.0) / PBR_PI; }
fn CalculateClearcoat(normal: vec3<f32>, view: vec3<f32>, halfvector: vec3<f32>, light: vec3<f32>, roughness: f32) -> vec3<f32> {
    let NdotL = clamp(dot(normal, light), 0.0, 1.0);
    return vec3<f32>(NormalDistribution(max(dot(normal, halfvector), 0.0), roughness) * VisibilityGGX(NdotL, clamp(dot(normal, view), 0.0, 1.0), roughness) * NdotL);
}
fn Max3(value: vec3<f32>) -> f32 { return max(value.x, max(value.y, value.z)); }
fn LambdaSheenNumericHelper(value: f32, alphaG: f32) -> f32 {
    let alpha = (1.0 - alphaG) * (1.0 - alphaG);
    return mix(21.5473, 25.3245, alpha) / (1.0 + mix(3.82987, 3.32435, alpha) * pow(value, mix(0.19823, 0.16801, alpha)))
        + mix(-1.97760, -1.27393, alpha) * value + mix(-4.32054, -4.85967, alpha);
}
fn LambdaSheen(cosTheta: f32, alphaG: f32) -> f32 {
    if (abs(cosTheta) < 0.5) { return exp(LambdaSheenNumericHelper(cosTheta, alphaG)); }
    return exp(2.0 * LambdaSheenNumericHelper(0.5, alphaG) - LambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
}
fn VisibilitySheen(NdotL: f32, NdotV: f32, roughness: f32) -> f32 {
    let sheenRoughness = max(roughness, 0.000001);
    let alphaG = sheenRoughness * sheenRoughness;
    return clamp(1.0 / max((1.0 + LambdaSheen(NdotV, alphaG) + LambdaSheen(NdotL, alphaG)) * (4.0 * NdotV * NdotL), 0.000001), 0.0, 1.0);
}
fn DistributionCharlie(roughness: f32, NdotH: f32) -> f32 {
    let sheenRoughness = max(roughness, 0.000001);
    let invR = 1.0 / (sheenRoughness * sheenRoughness);
    return (2.0 + invR) * pow(max(1.0 - NdotH * NdotH, 0.0), invR * 0.5) / (2.0 * 3.1415926);
}
fn BRDFSpecularSheen(color: vec3<f32>, roughness: f32, NdotL: f32, NdotV: f32, NdotH: f32) -> vec3<f32> {
    return color * DistributionCharlie(roughness, NdotH) * VisibilitySheen(NdotL, NdotV, roughness);
}
fn CalculateSheenRadiance(color: vec3<f32>, roughness: f32, lightColor: vec3<f32>, intensity: f32, NdotL: f32, NdotV: f32, NdotH: f32) -> vec3<f32> {
    return lightColor * intensity * NdotL * BRDFSpecularSheen(color, roughness, NdotL, NdotV, NdotH);
}
fn AlbedoSheenScalingLUT(NdotV: f32, roughness: f32) -> f32 {
    return T850_SAMPLE_ENVIRONMENT(texIBLSheenELUT, SS15, clamp(vec2<f32>(NdotV, roughness), vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).r;
}
fn GetIBLRadianceCharlie(normal: vec3<f32>, viewDir: vec3<f32>, roughness: f32, color: vec3<f32>, maxMip: f32) -> vec3<f32> {
    let NdotV = max(dot(normal, viewDir), 0.0);
    let reflected = reflect(-viewDir, normal) * vec3<f32>(-1.0, 1.0, -1.0);
    let brdf = T850_SAMPLE_ENVIRONMENT(texIBLCharlieLUT, SS14, clamp(vec2<f32>(NdotV, roughness), vec2<f32>(0.0), vec2<f32>(1.0)), 0.0).b;
    return T850_SAMPLE_ENVIRONMENT(texIBLCharlie, SS13, reflected, roughness * maxMip).rgb * color * brdf;
}
struct DeferredSurface {
    position: vec3<f32>,
    albedo: vec3<f32>,
    normal: vec3<f32>,
    eyeDir: vec3<f32>,
    F0: vec3<f32>,
    metallic: f32,
    roughness: f32,
    sheenColor: vec3<f32>,
    sheenRoughness: f32,
    clearcoatFactor: f32,
    clearcoatRoughness: f32,
}
fn DeferredLight(surface: DeferredSurface, index: i32, attenuation: f32, lightDir: vec3<f32>) -> vec3<f32> {
    let lightColor = quadPass.LightColors[index];
    let halfVec = normalize(surface.eyeDir + lightDir);
    let radiance = lightColor.xyz * lightColor.w * attenuation;
    let diffuse = CalculateDiffuse(surface.albedo, surface.normal, lightDir) * radiance;
    let specular = CalculateSpecular(surface.F0, surface.normal, surface.eyeDir, halfVec, lightDir, surface.roughness) * radiance;
    let fresnel = FresnelCalc(clamp(dot(surface.eyeDir, halfVec), 0.0, 1.0), surface.F0);
    let diffuseWeight = (vec3<f32>(1.0) - fresnel) * (1.0 - surface.metallic);
    let NdotL = max(dot(surface.normal, lightDir), 0.0);
    let NdotV = max(dot(surface.normal, surface.eyeDir), 0.0);
    let NdotH = max(dot(surface.normal, halfVec), 0.0);
    let sheenStrength = Max3(surface.sheenColor);
    var scaling = 1.0;
    var sheen = vec3<f32>(0.0);
    if (quadPass.brightness.w > 0.5 && sheenStrength > 0.0) {
        scaling = min(1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotV, surface.sheenRoughness), 1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotL, surface.sheenRoughness));
        sheen = CalculateSheenRadiance(surface.sheenColor, surface.sheenRoughness, lightColor.xyz, lightColor.w, NdotL, NdotV, NdotH) * attenuation;
    }
    var layer = sheen + (specular + diffuseWeight * diffuse) * scaling;
    if (surface.clearcoatFactor > 0.001) {
        let weight = clamp(surface.clearcoatFactor * FresnelCalc(clamp(dot(surface.normal, surface.eyeDir), 0.0, 1.0), vec3<f32>(0.04)).x, 0.0, 1.0);
        layer = mix(layer, CalculateClearcoat(surface.normal, surface.eyeDir, halfVec, lightDir, surface.clearcoatRoughness) * radiance, weight);
    }
    return layer;
}

fn GetShadowSplit(boundary: i32) -> f32 {
    if (boundary < 4) { return shadows.ShadowSplitDepths[0][boundary]; }
    return shadows.ShadowSplitDepths[1][boundary - 4];
}
fn GetCascadeIndex(viewDepth: f32) -> i32 {
    let viewCount = clamp(i32(shadows.ShadowParams0.x), 1, 6);
    var result = 0;
    for (var boundary = 0; boundary < 5; boundary++) {
        if (boundary < viewCount - 1 && viewDepth > GetShadowSplit(boundary)) { result++; }
    }
    return min(result, viewCount - 1);
}
fn GetCascadeDebugSplit(boundary: i32) -> f32 { return GetShadowSplit(boundary); }
fn GetCascadeDebugIndex(viewDepth: f32) -> i32 { return GetCascadeIndex(viewDepth); }
fn CalculateShadow(position: vec4<f32>, viewDepth: f32) -> vec4<f32> {
    let playerClip = frame.WVPLight * position;
    if (playerClip.w <= 0.0) { return vec4<f32>(1.0); }
    let projected = playerClip.xyz / playerClip.w;
    if (abs(projected.x) > 1.0 || abs(projected.y) > 1.0 || projected.z < 0.0 || projected.z > 1.0) { return vec4<f32>(1.0); }
    let cascade = GetCascadeIndex(viewDepth);
    let lightPosition = shadows.ShadowViewProjection[cascade] * position;
    let lightClip = lightPosition.xyz / lightPosition.w;
    let shadowUV = lightClip.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
    if (all(shadowUV > vec2<f32>(0.0)) && all(shadowUV < vec2<f32>(1.0)) && lightPosition.w > 0.0 && lightClip.z > 0.0 && lightClip.z < 1.0) {
        let scaleBias = shadows.ShadowAtlasScaleBias[cascade];
        let atlasUV = shadowUV * scaleBias.xy + scaleBias.zw;
        let texel = vec2<f32>(1.0) / shadows.ShadowParams0.yz;
        let tileMin = scaleBias.zw + 0.5 * texel;
        let tileMax = scaleBias.zw + scaleBias.xy - 0.5 * texel;
        var sum = 0.0;
        var total = 0.0;
        for (var row = -quadPass.brightness.x; row <= quadPass.brightness.x; row += 1.0) {
            for (var column = -quadPass.brightness.x; column <= quadPass.brightness.x; column += 1.0) {
                let sampleUV = atlasUV + quadPass.brightness.z * texel * vec2<f32>(column, row);
                var value = 0.0;
                if (all(sampleUV >= tileMin) && all(sampleUV <= tileMax)) {
                    let depth = textureSampleLevel(tex1, SS1, sampleUV, 0.0).r - shadows.ShadowParams1.z;
                    value = select(1.0, 0.0, lightClip.z < depth);
                }
                sum += value * (1.0 - shadows.ShadowParams1.w) + shadows.ShadowParams1.w;
                total += 1.0;
            }
        }
        return vec4<f32>(sum / total);
    }
    return vec4<f32>(shadows.ShadowParams1.w);
}
fn GetNormal(coords: vec2<f32>) -> vec3<f32> { return DecodeOctahedralNormal(textureSampleLevel(tex2, SS2, coords, 0.0).xy); }
fn GetOcclusion(depth: f32, uv: vec2<f32>, position: vec4<f32>, normal: vec3<f32>, uvDx: vec2<f32>, uvDy: vec2<f32>) -> f32 {
    let radius = quadPass.LightPositions[0].y;
    let scale = quadPass.LightPositions[0].zw / quadPass.brightness.w;
    let randomVector = textureSampleGrad(tex3, SS3, scale * uv, scale * uvDx, scale * uvDy).xyz * 2.0 - vec3<f32>(1.0);
    let tangent = normalize(randomVector - normal * dot(randomVector, normal));
    let basis = mat3x3<f32>(tangent, cross(normal, tangent), normal);
    var occlusion = 0.0;
    let viewDepth = LinearizeDepth(depth);
    let centerClip = uv * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0);
    let centerProjected = vec4<f32>(centerClip, depth, 1.0) * viewDepth;
    for (var index = 0; index < i32(quadPass.LightPositions[0].x); index++) {
        let sampleDelta = basis * quadPass.LightPositions[index + 1].xyz * radius;
        let sampleViewDepth = viewDepth + (frame.WorldView * vec4<f32>(sampleDelta, 0.0)).z;
        let offset = centerProjected + frame.Projection * vec4<f32>(sampleDelta, 0.0);
        let sampleClip = offset.xy / offset.w;
        let sampleUV = sampleClip * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
        if (any(sampleUV < vec2<f32>(0.0)) || any(sampleUV > vec2<f32>(1.0))) { continue; }
        let sampleDepth = textureSampleLevel(tex0, SS, sampleUV, 0.0).r;
        if (!IsSceneDepthValid(sampleDepth)) { continue; }
        let surfaceViewDepth = LinearizeDepth(sampleDepth);
        if (surfaceViewDepth < sampleViewDepth && abs(sampleViewDepth - surfaceViewDepth) < radius) { occlusion += 1.0; }
    }
    return 1.0 - occlusion / quadPass.LightPositions[0].x;
}

fn FullAverageLogLuminance() -> f32 {
    var total = 0.0;
    for (var row = 0; row < 8; row++) {
        for (var column = 0; column < 8; column++) {
            let uv = (vec2<f32>(f32(column), f32(row)) + vec2<f32>(0.5)) * 0.125;
            let color = textureSampleLevel(tex1, SS1, uv, 0.0).rgb;
            total += log(max(dot(color, vec3<f32>(0.299, 0.587, 0.114)), 0.0001));
        }
    }
    return total / 64.0;
}
fn RobustAverageLogLuminance() -> f32 {
    var total = 0.0;
    var minimum = 1.0e20;
    var maximum = -1.0e20;
    for (var row = 0; row < 8; row++) {
        for (var column = 0; column < 8; column++) {
            let uv = (vec2<f32>(f32(column), f32(row)) + vec2<f32>(0.5)) * 0.125;
            let color = textureSampleLevel(tex1, SS1, uv, 0.0).rgb;
            let value = clamp(log(max(dot(color, vec3<f32>(0.299, 0.587, 0.114)), 0.0001)), -4.60517019, 2.77258872);
            total += value;
            minimum = min(minimum, value);
            maximum = max(maximum, value);
        }
    }
    return (total - minimum - maximum) / 62.0;
}
fn rand(value: f32) -> f32 { return textureSample(tex2, SS2, vec2<f32>(value)).r; }
fn rand2D(value: vec2<f32>) -> f32 { return fract(sin(dot(value, vec2<f32>(12.9898, 78.233))) * 43758.5453123 * (quadPass.LightPositions[0].x * 0.1)); }
fn noise(value: f32) -> f32 { return mix(rand(floor(value)), rand(floor(value) + 1.0), smoothstep(0.0, 1.0, fract(value))); }
fn noise2D(value: vec2<f32>) -> f32 {
    let cell = floor(value);
    let fraction = smoothstep(vec2<f32>(0.0), vec2<f32>(1.0), fract(value));
    let cornerA = rand2D(cell);
    let cornerB = rand2D(cell + vec2<f32>(1.0, 0.0));
    let cornerC = rand2D(cell + vec2<f32>(0.0, 1.0));
    let cornerD = rand2D(cell + vec2<f32>(1.0, 1.0));
    return mix(cornerA, cornerB, fraction.x) + (cornerC - cornerA) * fraction.y * (1.0 - fraction.x) + (cornerD - cornerB) * fraction.x * fraction.y;
}
fn IntersectBox(origin: vec3<f32>, direction: vec3<f32>, boxMin: vec3<f32>, boxMax: vec3<f32>) -> vec2<f32> {
    let bottom = (boxMin - origin) / direction;
    let top = (boxMax - origin) / direction;
    let minimum = min(top, bottom);
    let maximum = max(top, bottom);
    return vec2<f32>(Max3(minimum), min(maximum.x, min(maximum.y, maximum.z)));
}
fn IntersectGodRaysBox(origin: vec3<f32>, direction: vec3<f32>, boxMin: vec3<f32>, boxMax: vec3<f32>) -> vec2<f32> {
    let fallback = select(vec3<f32>(0.000001), vec3<f32>(-0.000001), direction < vec3<f32>(0.0));
    return IntersectBox(origin, select(direction, fallback, abs(direction) < vec3<f32>(0.000001)), boxMin, boxMax);
}
fn Fire(value: vec3<f32>) -> vec4<f32> {
    let uv = vec2<f32>(length(value.xz) / 1.42, value.y);
    if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0))) { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }
    return vec4<f32>(textureSample(tex1, SS1, uv).rgb, 0.1);
}
fn Fire2(value: vec3<f32>) -> vec4<f32> {
    let uv = vec2<f32>(length(value.xz) / 1.42, value.y);
    if (any(uv < vec2<f32>(0.0)) || any(uv > vec2<f32>(1.0))) { return vec4<f32>(0.0); }
    return vec4<f32>(uv.y, 0.0, 0.0, uv.y);
}
fn distanceFunc(point: vec3<f32>) -> f32 { return length(point - vec3<f32>(0.5)); }
fn shade(distance: f32) -> vec4<f32> {
    if (distance >= 0.0 && distance < 0.1) { return mix(vec4<f32>(3.0, 3.0, 3.0, 1.0), vec4<f32>(1.0, 1.0, 0.0, 1.0), distance / 0.1); }
    if (distance >= 0.1 && distance < 0.2) { return mix(vec4<f32>(1.0, 1.0, 0.0, 1.0), vec4<f32>(1.0, 0.0, 0.0, 1.0), (distance - 0.1) / 0.1); }
    if (distance >= 0.2 && distance < 0.3) { return mix(vec4<f32>(1.0, 0.0, 0.0, 1.0), vec4<f32>(0.0), (distance - 0.2) / 0.1); }
    if (distance >= 0.4 && distance < 0.5) { return mix(vec4<f32>(0.0), vec4<f32>(0.0, 0.5, 1.0, 0.2), (distance - 0.3) / 0.1); }
    return vec4<f32>(0.0);
}
fn Ball(point: vec3<f32>) -> vec4<f32> {
    let time = quadPass.LightPositions[0].x;
    let noiseValue = noise2D(vec2<f32>(length(point.xz) / 1.42, point.y) * 40.0);
    let distance = distanceFunc(point) + cos(time * noiseValue) * 0.1 + sin(time * 2.0) * 0.2 * noiseValue;
    return shade(distance);
}
fn ComputeScattering(lightDotView: f32) -> f32 {
    let scattering = -0.2;
    return (1.0 - scattering * scattering) / (4.0 * PBR_PI * pow(1.0 + scattering * scattering - 2.0 * scattering * lightDotView, 1.5));
}

#if defined(DEFERRED_PASS) || defined(DEFERRED_LDR_PASS) || defined(DEFERRED_LIGHT_VOLUME_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    var coords = input.texture0;
    var clipPos = input.ClipPos;
#ifdef DEFERRED_LIGHT_VOLUME_PASS
    coords = input.hposition.xy / vec2<f32>(max(textureDimensions(tex0), vec2<u32>(1)));
    clipPos = coords * vec2<f32>(2.0, -2.0) + vec2<f32>(-1.0, 1.0);
#endif
    let albedoSample = textureSampleLevel(tex0, SS, coords, 0.0);
    let pbr = textureSampleLevel(tex2, SS2, coords, 0.0);
    let specularOcclusion = textureSampleLevel(tex7, SS8, coords, 0.0);
    let depth = textureSampleLevel(tex4, SS4, coords, 0.0).r;
    let position = ReconstructPosition(clipPos, depth);
    let eyeDir = normalize(frame.CameraPosition.xyz - position.xyz);
#ifdef DEFERRED_LDR_PASS
    if (!IsSceneDepthValid(depth)) { discard; }
    let materialId = i32(pbr.a * 255.0);
#else
    let materialId = i32(pbr.a * 255.0 + 0.5);
#endif
#ifdef DEFERRED_LIGHT_VOLUME_PASS
    if (!IsSceneDepthValid(depth) || materialId <= 0) { discard; }
#endif
    var finalColor = vec3<f32>(0.0);
    if (materialId == 0) {
#ifdef DEFERRED_LDR_PASS
        finalColor = T850_SAMPLE_ENVIRONMENT(texEnv, SS6, -eyeDir * vec3<f32>(-1.0, 1.0, -1.0), 0.0).xyz * 2.0;
#elif defined(DEFERRED_PASS)
        finalColor = T850_SAMPLE_ENVIRONMENT(texEnv, SS6, normalize(input.PosCorner.xyz) * vec3<f32>(-1.0, 1.0, -1.0), 0.0).xyz * quadPass.toogles.x;
#endif
    } else if (materialId > 0) {
        let normalSample = textureSampleLevel(tex1, SS1, coords, 0.0);
        let geoData = textureSampleLevel(tex3, SS3, coords, 0.0);
        let sheenData = textureSampleLevel(tex6, SS7, coords, 0.0);
        let unlit = geoData.a >= 0.5;
#ifdef DEFERRED_LIGHT_VOLUME_PASS
        if (unlit) { discard; }
#endif
        var surface: DeferredSurface;
        surface.position = position.xyz;
        surface.eyeDir = eyeDir;
        surface.albedo = pow(albedoSample.rgb, vec3<f32>(2.2));
        surface.normal = normalize(normalSample.xyz * 2.0 - vec3<f32>(1.0));
        surface.roughness = normalSample.a;
        surface.metallic = pbr.r;
        surface.F0 = mix(max(specularOcclusion.rgb, vec3<f32>(0.0)) * max(albedoSample.a, 0.0), surface.albedo, pbr.r);
        surface.sheenColor = clamp(sheenData.rgb, vec3<f32>(0.0), vec3<f32>(1.0));
        surface.sheenRoughness = clamp(sheenData.a, 0.0, 1.0);
        surface.clearcoatFactor = clamp(pbr.b, 0.0, 1.0);
        surface.clearcoatRoughness = clamp(select(geoData.a, geoData.a - 0.5, unlit) * 2.0, 0.04, 1.0);
        var shadow = 1.0;
#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
        shadow = textureSampleLevel(tex5, SS5, coords, 0.0).r;
#endif
        var directLight = vec3<f32>(0.0);
#ifdef DEFERRED_LIGHT_VOLUME_PASS
        let tileSize = max(i32(frame.LightCameraInfo.x + 0.5), 1);
        let tileCountX = max(i32(frame.LightCameraInfo.y + 0.5), 1);
        let tileCountY = max(i32(frame.LightCameraInfo.z + 0.5), 1);
        let maxTileLights = max(i32(frame.LightCameraInfo.w + 0.5), 1);
        let tile = clamp(vec2<i32>(input.hposition.xy) / tileSize, vec2<i32>(0), vec2<i32>(tileCountX - 1, tileCountY - 1));
        let tileIndex = tile.y * tileCountX + tile.x;
        let lightCount = min(i32(textureLoad(texTileHeaders, tile, 0).y + 0.5), maxTileLights);
        for (var tileLight = 0; tileLight < lightCount; tileLight++) {
            let index = i32(textureLoad(texTileLightIndices, vec2<i32>(tileLight, tileIndex), 0).x + 0.5);
            let lightPosition = quadPass.LightPositions[index].xyz;
            let attenuation = RangeAttenuation(quadPass.LightRadius[index >> 2][index & 3] * 2.0, distance(lightPosition, position.xyz));
            if (attenuation > 0.0) { directLight += DeferredLight(surface, index, attenuation, normalize(lightPosition - position.xyz)); }
        }
        finalColor = directLight * shadow * pbr.g;
#else
        for (var index = 0; index < i32(frame.CameraInfo.w); index++) {
            let light = quadPass.LightPositions[index];
            if (light.w < 0.5) { directLight += DeferredLight(surface, index, 1.0, normalize(-light.xyz)); }
            else {
                let attenuation = RangeAttenuation(quadPass.LightRadius[index >> 2][index & 3] * 2.0, distance(light.xyz, position.xyz));
                if (attenuation > 0.0) { directLight += DeferredLight(surface, index, attenuation, normalize(light.xyz - position.xyz)); }
            }
        }
        let maxMip = max(quadPass.toogles.w, 0.0);
        let hasLUT = quadPass.brightness.w > 0.5;
        let NdotV = max(dot(surface.normal, eyeDir), 0.0);
        let specularWeight = clamp(fresnelSchlickRoughness(NdotV, surface.F0, surface.roughness), vec3<f32>(0.0), vec3<f32>(1.0));
        let diffuseWeight = (vec3<f32>(1.0) - specularWeight) * (1.0 - surface.metallic);
        let reflected = reflect(-eyeDir, surface.normal) * vec3<f32>(-1.0, 1.0, -1.0);
        let reflection = T850_SAMPLE_ENVIRONMENT(texIBLSpecular, SS11, reflected, surface.roughness * maxMip).xyz;
        var specularIBL = specularWeight * (1.0 - surface.roughness) * (1.0 - surface.roughness);
        if (hasLUT) { specularIBL = IBLGGXFresnel(NdotV, surface.roughness, surface.F0, T850_SAMPLE_ENVIRONMENT(texIBLBRDF, SS12, vec2<f32>(NdotV, surface.roughness), 0.0).rg); }
        var indirectLight = reflection * specularIBL * quadPass.toogles.z;
#ifdef DEFERRED_PASS
        let irradiance = T850_SAMPLE_ENVIRONMENT(texIBLDiffuse, SS10, surface.normal * vec3<f32>(-1.0, 1.0, -1.0), clamp(quadPass.brightness.z, 0.0, maxMip)).xyz;
        indirectLight += irradiance * surface.albedo * diffuseWeight * quadPass.toogles.z;
#endif
        indirectLight += surface.albedo * diffuseWeight * clamp(geoData.b, 0.0, 1.0);
        let sheenStrength = Max3(surface.sheenColor);
        if (hasLUT && sheenStrength > 0.0) {
            let scaling = 1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotV, surface.sheenRoughness);
            indirectLight = GetIBLRadianceCharlie(surface.normal, eyeDir, surface.sheenRoughness, surface.sheenColor, maxMip) * quadPass.toogles.z + indirectLight * scaling;
        }
#ifdef DEFERRED_LDR_PASS
        finalColor = directLight + indirectLight * clamp(specularOcclusion.a, 0.0, 1.0);
#else
        finalColor = directLight * shadow * pbr.g + indirectLight * clamp(specularOcclusion.a, 0.0, 1.0);
#endif
        if (surface.clearcoatFactor > 0.001) {
            let coat = T850_SAMPLE_ENVIRONMENT(texIBLSpecular, SS11, reflected, surface.clearcoatRoughness * maxMip).xyz;
            let attenuation = select((1.0 - surface.clearcoatRoughness) * (1.0 - surface.clearcoatRoughness), 1.0, hasLUT);
            let weight = clamp(surface.clearcoatFactor * Max3(FresnelCalc(clamp(dot(surface.normal, eyeDir), 0.0, 1.0), vec3<f32>(0.04))), 0.0, 1.0);
            finalColor = mix(finalColor, coat * attenuation * quadPass.toogles.z, weight);
        }
#ifdef DEFERRED_LDR_PASS
        finalColor = directLight * shadow * pbr.g + (finalColor - directLight);
#endif
        let emissive = textureSampleLevel(tex8, SS9, coords, 0.0).rgb;
        finalColor += emissive;
        if (unlit) { finalColor = surface.albedo + emissive; }
#endif
    }
#ifdef DEFERRED_LDR_PASS
    finalColor = clamp(finalColor, vec3<f32>(0.0), vec3<f32>(1.0));
#endif
    return vec4<f32>(finalColor, 1.0);
}
#elif defined(CASCADE_DEBUG_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    if (quadPass.toogles.x < 0.5) { return vec4<f32>(0.0); }
    let depth = textureSample(tex0, SS, input.texture0).r;
    if (!IsSceneDepthValid(depth)) { return vec4<f32>(0.0); }
    let position = ReconstructPosition(input.ClipPos, depth);
    let playerClip = frame.WVPLight * position;
    if (playerClip.w <= 0.0) { return vec4<f32>(0.0); }
    let projected = playerClip.xyz / playerClip.w;
    if (abs(projected.x) > 1.0 || abs(projected.y) > 1.0 || projected.z < 0.0 || projected.z > 1.0) { return vec4<f32>(0.0); }
    let cascade = GetCascadeDebugIndex((frame.World * position).z);
    return vec4<f32>(quadPass.LightColors[cascade].xyz, clamp(quadPass.toogles.y, 0.0, 1.0));
}
#elif defined(SHADOW_COMP_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
#ifdef ENABLE_SSAO
    let uvDx = dpdx(input.texture0);
    let uvDy = dpdy(input.texture0);
#endif
    let depth = textureSampleLevel(tex0, SS, input.texture0, 0.0).r;
    if (!IsSceneDepthValid(depth)) { return vec4<f32>(1.0); }
    let position = ReconstructPosition(input.ClipPos, depth);
    var color = vec4<f32>(1.0);
#ifdef ENABLE_SHADOWS
    color = CalculateShadow(position, (frame.World * position).z);
#endif
#ifdef ENABLE_SSAO
    color *= GetOcclusion(depth, input.texture0, position, GetNormal(input.texture0), uvDx, uvDy);
#endif
    return color;
}
#elif defined(VERTICAL_BLUR_PASS) || defined(HORIZONTAL_BLUR_PASS) || defined(ONE_PASS_BLUR)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let texel = quadPass.LightPositions[0].y / vec2<f32>(max(textureDimensions(tex0), vec2<u32>(1)));
    let kernelSize = i32(quadPass.LightPositions[0].x);
    let origin = -f32(kernelSize - 1) * 0.5;
    var sum = vec3<f32>(0.0);
    for (var index = 0; index < kernelSize; index++) {
        let offset = origin + f32(index);
#ifdef ONE_PASS_BLUR
        for (var row = 0; row < kernelSize; row++) {
            let coords = input.texture0 + vec2<f32>(offset * texel.x, (origin + f32(row)) * texel.y);
            sum += roundTo(quadPass.LightPositions[index + 1].x * quadPass.LightPositions[row + 1].x, 6.0) * textureSampleLevel(tex0, SS, coords, 0.0).xyz;
        }
#else
#ifdef VERTICAL_BLUR_PASS
        let coords = input.texture0 + vec2<f32>(0.0, offset * texel.y);
#else
        let coords = input.texture0 + vec2<f32>(offset * texel.x, 0.0);
#endif
        sum += quadPass.LightPositions[index + 1].x * textureSampleLevel(tex0, SS, coords, 0.0).xyz;
#endif
    }
    return vec4<f32>(sum, 1.0);
}
#elif defined(BRIGHT_PASS) || defined(HDR_COMP_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    var color = textureSample(tex0, SS, input.texture0).rgb;
#ifdef BRIGHT_PASS
    let average = max(exp(textureSample(tex1, SS1, vec2<f32>(0.5)).r), 0.001);
    let threshold = quadPass.LightPositions[0].x;
#else
    let average = max(exp(textureSample(tex2, SS2, vec2<f32>(0.5)).r), 0.001);
    let threshold = 0.0;
#endif
    let keyValue = 1.03 - 2.0 / (2.0 + log(average + 1.0) / log(10.0));
    color *= exp2(log2(max(keyValue / average, 0.0001)) + quadPass.LightPositions[0].y - threshold);
    let luminance = max(dot(color, vec3<f32>(0.299, 0.587, 0.114)), 0.0001);
    let white = max(quadPass.LightPositions[0].z, 0.001);
    var mapped = luminance * (1.0 + luminance / (white * white)) / (1.0 + luminance) * (color / luminance);
#ifdef HDR_COMP_PASS
    mapped += quadPass.LightPositions[0].x * textureSample(tex1, SS1, input.texture0).rgb;
#endif
    return vec4<f32>(mapped, 1.0);
}
#elif defined(ADAPT_LUMINANCE_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let mode = i32(quadPass.LightPositions[1].z + 0.5);
    var currentLog = 0.0;
    if (mode == 0) { currentLog = FullAverageLogLuminance(); }
    else { currentLog = RobustAverageLogLuminance(); }
    var current = max(exp(currentLog), 0.0001);
    if (mode != 0) { current = clamp(current, 0.05, 16.0); }
    let previous = max(exp(textureSampleLevel(tex0, SS, vec2<f32>(0.5), 0.0).r), 0.0001);
    let blend = 1.0 - exp(-max(quadPass.LightPositions[1].y, 0.0) * max(quadPass.LightPositions[1].x, 0.0));
    return vec4<f32>(log(max(mix(previous, current, clamp(blend, 0.0, 1.0)), 0.0001)), 1.0, 1.0, 1.0);
}
#elif defined(COC_PASS)
struct CoCOutput {
    @location(0) color0: f32,
    @location(1) color1: f32,
}
@fragment fn FS(input: FragmentInput) -> CoCOutput {
    var result: CoCOutput;
    let depth = textureSampleLevel(tex0, SS, input.texture0, 0.0).r;
    if (!IsSceneDepthValid(depth)) { return result; }
#ifdef AUTO_FOCUS
    var focus = textureSampleLevel(tex0, SS, vec2<f32>(0.5), 0.0).r;
    if (quadPass.LightPositions[1].z > 0.5 && !IsSceneDepthValid(focus) && quadPass.LightPositions[1].w > 0.0) {
        var best = 0.0;
        for (var row = -2; row <= 2; row++) {
            for (var column = -2; column <= 2; column++) {
                let coords = clamp(vec2<f32>(0.5) + vec2<f32>(f32(column), f32(row)) * (quadPass.LightPositions[1].w * 0.5), vec2<f32>(0.0), vec2<f32>(1.0));
                let candidate = textureSampleLevel(tex0, SS, coords, 0.0).r;
                if (IsSceneDepthValid(candidate)) { best = max(best, candidate); }
            }
        }
        focus = best;
    }
#else
    var focus = quadPass.LightPositions[0].z;
#endif
    if (!IsSceneDepthValid(focus)) { focus = select(depth, 0.0, quadPass.LightPositions[1].z > 0.5); }
    if (!IsSceneDepthValid(focus)) { return result; }
    let objectDistance = LinearizeDepth(depth);
    let focusPlane = LinearizeDepth(focus);
    var coc = 0.0;
    if (quadPass.LightPositions[1].z > 0.5) {
        coc = clamp((abs(objectDistance - focusPlane) - max(quadPass.LightPositions[1].x, 0.0)) / max(quadPass.LightPositions[1].y, 0.0001), 0.0, 1.0) * quadPass.LightPositions[0].w;
    } else {
        let denominator = objectDistance * (focusPlane - quadPass.LightPositions[0].y);
        if (abs(denominator) <= 0.00001) { return result; }
        coc = abs(quadPass.LightPositions[0].x * quadPass.LightPositions[0].y * (objectDistance - focusPlane) / denominator);
    }
    if (depth > focus) { result.color0 = clamp(coc, 0.0, quadPass.LightPositions[0].w); }
    else { result.color1 = clamp(coc, 0.0, quadPass.LightPositions[0].w); }
    return result;
}
#elif defined(COMBINE_COC_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) f32 {
    let first = textureSample(tex0, SS, input.texture0).r;
    let second = textureSample(tex1, SS1, input.texture0).r;
    return select(2.0 * max(first, second) - first, max(first, second), quadPass.LightPositions[1].z > 0.5);
}
#elif defined(DOF_PASS) || defined(DOF_PASS_2)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let blur = textureSampleLevel(tex1, SS1, input.texture0, 0.0).r;
    let color = textureSampleLevel(tex0, SS, input.texture0, 0.0);
    if (blur <= DEPTH_CLEAR_EPSILON) { return color; }
    let offset = vec2<f32>(1.0) / quadPass.LightPositions[0].zw;
#ifdef DOF_PASS
    let samples = max(i32(quadPass.LightPositions[0].y), 0);
#else
    let samples = max(i32(quadPass.LightPositions[0].x), 0);
#endif
    var sum = vec4<f32>(0.0);
    var total = 0.0;
    for (var column = -samples; column <= samples; column++) {
        for (var row = -samples; row <= samples; row++) {
            sum += textureSampleLevel(tex0, SS, input.texture0 + vec2<f32>(f32(column), f32(row)) * offset * blur, 0.0);
            total += 1.0;
        }
    }
    let result = select(color + sum, sum, quadPass.LightPositions[1].z > 0.5) / max(total, 1.0);
    return vec4<f32>(result.rgb, 1.0);
}
#elif defined(BACKBUFFER_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let color = textureSample(tex0, SS, input.texture0);
    return vec4<f32>(LinearToSRGB(color.rgb), color.a);
}
#elif defined(GOD_RAY_CALCULATION_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    var uv = input.texture0;
    let center = vec2<f32>(0.435, 1.0 - 0.59);
    let scale = mix(1.0, 1.0 - 1.0 / 64.0, (1.0 - 0.4) * 0.2);
    var color = textureSample(tex0, SS, uv).rgb;
    for (var index = 0; index < 64; index++) {
        uv = (uv - center) * scale + center;
        color += textureSample(tex0, SS, uv).rgb;
    }
    let rays = smoothstep(vec3<f32>(0.8), vec3<f32>(1.0), pow(color / 64.0, vec3<f32>(0.4545))) * quadPass.toogles.x;
    return vec4<f32>(rays, 1.0);
}
#elif defined(GOD_RAY_BLEND_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let color = textureSample(tex0, SS, input.texture0).rgb;
    var rays = textureSample(tex1, SS1, input.texture0).rgb;
    if (all(rays < vec3<f32>(0.1))) { return vec4<f32>(color, 1.0); }
    let luminance = dot(rays, vec3<f32>(0.299, 0.587, 0.114));
    rays = pow(mix(vec3<f32>(luminance), rays, 0.5), vec3<f32>(2.2));
    return vec4<f32>(color + rays * 0.25, 1.0);
}
#elif defined(SSAO_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let uv = input.texture0;
    let depth = textureSample(tex1, SS1, uv).r;
    let randomX = array<f32, 8>(0.1, 0.35, 0.3489, 0.230489, 0.158, 0.237689, 0.680462, 0.89);
    var occlusion = 0.0;
    for (var axis = 0; axis < 2; axis++) {
        for (var side = 0; side < 2; side++) {
            for (var index = 0; index < 4; index++) {
                let sign = select(-1.0, 1.0, side == 1);
                let offset = randomX[index + side * 4] * 3.0 * sign / select(1280.0, 720.0, axis == 1);
                var value = 0.0;
                if (axis == 0) { value = textureSample(tex1, SS1, uv + vec2<f32>(offset, 0.0)).x; }
                else { value = textureSample(tex1, SS1, uv + vec2<f32>(0.0, offset)).y; }
                if (value - depth < 0.01) { occlusion += clamp(value - depth, 0.0, 1.0); }
            }
        }
    }
    let result = clamp(pow(1.0 - occlusion / 8.0, 32.0), 0.0, 1.0);
    return vec4<f32>(vec3<f32>(result), 1.0);
}
#elif defined(RAY_MARCH)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let depth = textureSample(tex0, SS, input.texture0).r;
    if (!IsSceneDepthValid(depth)) { return vec4<f32>(0.0); }
    let position = ReconstructPosition(input.ClipPos, depth);
    let rayDir = normalize(position - frame.CameraPosition);
    var interval = IntersectBox(frame.CameraPosition.xyz, rayDir.xyz, vec3<f32>(-20.0), vec3<f32>(20.0));
    if (interval.x > interval.y || interval.y < 0.0) { discard; }
    interval.x = max(interval.x, 0.0);
    let nearPosition = frame.CameraPosition + rayDir * interval.x;
    let farPosition = frame.CameraPosition + rayDir * interval.y;
    let increment = (nearPosition - farPosition) / 63.0;
    var point = farPosition;
    var color = vec4<f32>(0.0);
    for (var index = 0; index < 64; index++) {
        let sampleColor = Ball((point.xyz + vec3<f32>(20.0)) / 40.0);
        color = sampleColor.a * sampleColor + (1.0 - sampleColor.a) * color;
        point += increment;
    }
    return color;
}
#elif defined(LIGHT_RAY_MARCHING)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
#ifndef ENABLE_GOD_RAYS
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
#else
    let depth = textureSampleLevel(tex0, SS, input.texture0, 0.0).r;
    if (!IsSceneDepthValid(depth)) { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }
    let position = ReconstructPosition(input.ClipPos, depth);
    let steps = max(i32(quadPass.LightPositions[0].y), 2);
    let ray = position - frame.CameraPosition;
    let rayDir = normalize(ray);
    let rayLength = length(ray.xyz);
    var nearPosition = frame.CameraPosition;
    var farPosition = position;
    if (quadPass.LightPositions[1].w > 0.5) {
        let center = quadPass.LightPositions[1].xyz;
        let halfExtents = max(abs(quadPass.LightPositions[2].xyz), vec3<f32>(0.001));
        var interval = IntersectGodRaysBox(frame.CameraPosition.xyz, rayDir.xyz, center - halfExtents, center + halfExtents);
        if (interval.y < max(interval.x, 0.0)) { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }
        interval = clamp(interval, vec2<f32>(0.0), vec2<f32>(rayLength));
        if (interval.y <= interval.x) { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }
        nearPosition = frame.CameraPosition + rayDir * interval.x;
        farPosition = frame.CameraPosition + rayDir * interval.y;
    }
    var accumulated = vec3<f32>(0.0);
    let sunDirection = normalize(quadPass.LightColors[0].xyz);
    for (var index = 0; index < steps; index++) {
        let point = mix(farPosition, nearPosition, f32(index) / f32(steps - 1));
        let lightPosition = frame.WVPLight * point;
        let lightClip = lightPosition.xyz / lightPosition.w;
        let coords = lightClip.xy * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
        if (all(coords > vec2<f32>(0.0)) && all(coords < vec2<f32>(1.0)) && lightClip.z > 0.0 && lightClip.z < 1.0) {
            let sampleDepth = textureSampleLevel(tex1, SS1, coords, 0.0).r - max(quadPass.toogles.w, 0.0);
            if (lightClip.z >= sampleDepth) { accumulated += vec3<f32>(0.9803, 0.8392, 0.6470) * ComputeScattering(dot(rayDir.xyz, sunDirection)); }
        }
    }
    return vec4<f32>(pow(accumulated / f32(steps), vec3<f32>(0.4545)) * quadPass.toogles.x, 1.0);
#endif
}
#elif defined(LIGHT_ADD)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let color = textureSample(tex1, SS1, input.texture0);
    let volume = textureSample(tex0, SS, input.texture0);
    return vec4<f32>(color.rgb + pow(volume.rgb, vec3<f32>(2.2)) * 1.6, 1.0);
}
#elif defined(DEPTH_PRE_PASS)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }
#elif defined(FSQUAD_2_TEX)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> { return textureSample(tex0, SS, input.texture0) + textureSample(tex1, SS, input.texture0); }
#elif defined(FSQUAD_3_TEX)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let average = dot(textureSampleLevel(tex0, SS, input.texture0, f32(i32(frame.CameraPosition.w))).rgb, vec3<f32>(0.299, 0.587, 0.114));
    return vec4<f32>(vec3<f32>(average), 1.0);
}
#elif defined(FADE)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, quadPass.brightness.x); }
#elif defined(LENS_FLARE_SUN)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let distance = length(input.texture0 * 2.0 - vec2<f32>(1.0));
    let core = 1.0 - smoothstep(0.0, 0.28, distance);
    let halo = 1.0 - smoothstep(0.0, 1.0, distance);
    let mask = clamp(core + halo * 0.35, 0.0, 1.0);
    let color = vec3<f32>(1.0, 0.78, 0.42) * halo + vec3<f32>(1.0, 0.98, 0.86) * core;
    return vec4<f32>(color * mask, clamp(quadPass.brightness.x, 0.0, 1.0));
}
#elif defined(LENS_FLARE_GHOST)
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> {
    let color = textureSample(tex0, SS, input.texture0);
    let alpha = clamp(color.a, 0.0, 1.0);
    if (alpha < 0.003) { discard; }
    return vec4<f32>(clamp(color.rgb, vec3<f32>(0.0), vec3<f32>(1.0)) * alpha, clamp(quadPass.brightness.x, 0.0, 1.0));
}
#else
@fragment fn FS(input: FragmentInput) -> @location(0) vec4<f32> { return textureSample(tex0, SS, input.texture0); }
#endif