#ifdef ES_30
precision mediump float;
#endif

#ifdef DIFFUSE_MAP
uniform mediump sampler2D DiffuseTex;
#endif

#ifdef SPECULAR_MAP
uniform mediump sampler2D SpecularTex;
#endif

#ifdef GLOSS_MAP
uniform mediump sampler2D GlossTex;
#endif

#ifdef NORMAL_MAP
uniform mediump sampler2D NormalTex;
#endif

#ifdef HEIGHT_MAP
uniform mediump sampler2D HeightTex;
#endif

#ifdef METALLIC_MAP
uniform mediump sampler2D MetallicTex;
#endif

uniform highp sampler2D SceneDepthTex;

#ifdef EMISSIVE_MAP
uniform mediump sampler2D EmissiveTex;
#endif

uniform highp sampler2D SceneColorTex;

uniform mediump samplerCube texEnv;
uniform mediump samplerCube texIBLDiffuse;
uniform mediump samplerCube texIBLSpecular;
uniform mediump sampler2D texIBLBRDF;
uniform mediump samplerCube texIBLCharlie;
uniform mediump sampler2D texIBLCharlieLUT;
uniform mediump sampler2D texIBLSheenELUT;

#ifdef SHEEN_COLOR_MAP
uniform mediump sampler2D SheenColorTex;
#endif

#ifdef SHEEN_ROUGHNESS_MAP
uniform mediump sampler2D SheenRoughnessTex;
#endif

#ifdef CLEARCOAT_MAP
uniform mediump sampler2D ClearcoatTex;
#endif

#ifdef CLEARCOAT_ROUGHNESS_MAP
uniform mediump sampler2D ClearcoatRoughnessTex;
#endif

#ifdef OCCLUSION_MAP
uniform mediump sampler2D OcclusionTex;
#endif

#ifdef SPECULAR_FACTOR_MAP
uniform mediump sampler2D SpecularFactorTex;
#endif

#ifdef SPECULAR_COLOR_MAP
uniform mediump sampler2D SpecularColorTex;
#endif

#ifdef TRANSMISSION_MAP
uniform mediump sampler2D TransmissionTex;
#endif

uniform highp vec4 LightPos;
uniform highp vec4 LightColor;
uniform highp vec4 CameraPosition;
uniform highp vec4 CameraInfo;
uniform highp vec4 AmbientColor;
uniform highp vec4 DiffuseColor;
uniform highp vec4 SpecularColor;
uniform highp vec4 PBRParams;
uniform highp vec4 Intensities;
uniform highp vec4 ParallaxSettings;
uniform highp vec4 ParallaxShadowSettings;
uniform highp vec4 Light0Direction;
uniform highp vec4 EmissiveColor;
uniform highp vec4 AlphaParams;
uniform highp vec4 ForwardParams;
uniform highp vec4 TexCoordSets;
uniform highp vec4 MaterialParams;
uniform highp vec4 MaterialParams2;
uniform highp vec4 MaterialParams3;
uniform highp vec4 MaterialParams4;
uniform highp vec4 MaterialParams5;
uniform highp vec4 MaterialParams6;
uniform highp vec4 MaterialParams7;
uniform highp vec4 MaterialParams8;
uniform highp vec4 MaterialParams9;
uniform highp vec4 BaseColorUVTransform0;
uniform highp vec4 BaseColorUVTransform1;
uniform highp vec4 NormalUVTransform0;
uniform highp vec4 NormalUVTransform1;
uniform highp vec4 MetallicUVTransform0;
uniform highp vec4 MetallicUVTransform1;
uniform highp vec4 EmissiveUVTransform0;
uniform highp vec4 EmissiveUVTransform1;
uniform highp vec4 SheenColorUVTransform0;
uniform highp vec4 SheenColorUVTransform1;
uniform highp vec4 SheenRoughnessUVTransform0;
uniform highp vec4 SheenRoughnessUVTransform1;
uniform highp vec4 ClearcoatUVTransform0;
uniform highp vec4 ClearcoatUVTransform1;
uniform highp vec4 ClearcoatRoughnessUVTransform0;
uniform highp vec4 ClearcoatRoughnessUVTransform1;
uniform highp vec4 OcclusionUVTransform0;
uniform highp vec4 OcclusionUVTransform1;
uniform highp vec4 SpecularFactorUVTransform0;
uniform highp vec4 SpecularFactorUVTransform1;
uniform highp vec4 SpecularColorUVTransform0;
uniform highp vec4 SpecularColorUVTransform1;
uniform highp vec4 TransmissionUVTransform0;
uniform highp vec4 TransmissionUVTransform1;
uniform highp vec4 LightPositions[128];
uniform highp vec4 LightColors[128];
uniform highp vec4 LightRadius[32];

highp vec2 SignNotZero(highp vec2 value)
{
    return vec2(value.x >= 0.0 ? 1.0 : -1.0,
                value.y >= 0.0 ? 1.0 : -1.0);
}

highp vec2 EncodeOctahedralNormal(highp vec3 normal)
{
    normal = normalize(normal);
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.000001);
    if (normal.z < 0.0) {
        normal.xy = (vec2(1.0) - abs(normal.yx)) * SignNotZero(normal.xy);
    }
    return normal.xy * 0.5 + 0.5;
}

#ifdef USE_TEXCOORD0
    #ifdef ES_30
        in highp vec2 vecUVCoords;
    #else
        varying highp vec2 vecUVCoords;
    #endif
#endif

#ifdef USE_TEXCOORD1
    #ifdef ES_30
        in highp vec2 vecUVCoords1;
    #else
        varying highp vec2 vecUVCoords1;
    #endif
#endif

#ifdef USE_TEXCOORD2
    #ifdef ES_30
        in highp vec2 vecUVCoords2;
    #else
        varying highp vec2 vecUVCoords2;
    #endif
#endif

#ifdef USE_TEXCOORD3
    #ifdef ES_30
        in highp vec2 vecUVCoords3;
    #else
        varying highp vec2 vecUVCoords3;
    #endif
#endif

#ifdef USE_NORMALS
    #ifdef ES_30
        in highp vec4 hnormal;
    #else
        varying highp vec4 hnormal;
    #endif
#endif

#ifdef USE_TANGENTS
    #ifdef ES_30
        in highp vec4 htangent;
    #else
        varying highp vec4 htangent;
    #endif
#endif

#ifdef USE_BINORMALS
    #ifdef ES_30
        in highp vec4 hbinormal;
    #else
        varying highp vec4 hbinormal;
    #endif
#endif

#ifdef ES_30
    in highp vec4 Pos;
    in highp vec4 WorldPos;
#else
    varying highp vec4 Pos;
    varying highp vec4 WorldPos;
#endif

highp vec4 SampleTexture2D(mediump sampler2D tex, highp vec2 uv)
{
#ifdef ES_30
    return texture(tex, uv);
#else
    return texture2D(tex, uv);
#endif
}

highp vec4 SampleTexture2DLod(mediump sampler2D tex, highp vec2 uv, highp float lod)
{
#ifdef ES_30
    return textureLod(tex, uv, lod);
#else
    return texture2DLod(tex, uv, lod);
#endif
}

highp vec3 SampleCubeLod(mediump samplerCube tex, highp vec3 dir, highp float lod)
{
#ifdef ES_30
    return textureLod(tex, dir, lod).xyz;
#else
    return textureCubeLod(tex, dir, lod).xyz;
#endif
}

highp float LoadForwardSceneDepth()
{
#ifdef ES_30
    highp ivec2 pixel = clamp(ivec2(gl_FragCoord.xy), ivec2(0), ivec2(ForwardParams.xy) - ivec2(1));
    return texelFetch(SceneDepthTex, pixel, 0).r;
#else
    highp vec2 screenUV = gl_FragCoord.xy / ForwardParams.xy;
    return texture2D(SceneDepthTex, screenUV).r;
#endif
}

const highp float PBR_PI = 3.14159265359;

highp float NormalDistribution(highp float NdotH, highp float roughness)
{
    highp float alpha = max(roughness * roughness, 0.001);
    highp float alphaSq = alpha * alpha;
    highp float f = (NdotH * NdotH) * (alphaSq - 1.0) + 1.0;
    return alphaSq / max(PBR_PI * f * f, 0.000001);
}

highp vec3 FresnelCalc(highp float VdotH, highp vec3 specColor)
{
    return specColor + (vec3(1.0) - specColor) * pow(1.0 - VdotH, 5.0);
}

highp vec3 fresnelSchlickRoughness(highp float cosTheta, highp vec3 F0, highp float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

highp vec3 IBLGGXFresnel(highp float NdotV, highp float roughness, highp vec3 F0, highp vec2 brdfSample)
{
    highp vec3 kSpecular = fresnelSchlickRoughness(NdotV, F0, roughness);
    highp vec3 singleScatter = kSpecular * brdfSample.x + brdfSample.y;
    highp float energyMiss = clamp(1.0 - brdfSample.x - brdfSample.y, 0.0, 1.0);
    highp vec3 averageFresnel = F0 + (vec3(1.0) - F0) / 21.0;
    highp vec3 multiScatter = energyMiss * singleScatter * averageFresnel / max(vec3(1.0) - averageFresnel * energyMiss, vec3(0.001));
    return max(singleScatter + multiScatter, vec3(0.0));
}

highp float VisibilityGGX(highp float NdotL, highp float NdotV, highp float roughness)
{
    highp float alpha = max(roughness * roughness, 0.001);
    highp float alphaSq = alpha * alpha;
    highp float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaSq) + alphaSq);
    highp float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaSq) + alphaSq);
    highp float ggx = ggxV + ggxL;
    return ggx > 0.0 ? 0.5 / ggx : 0.0;
}

highp float RangeAttenuation(highp float range, highp float distanceToLight)
{
    highp float distanceSq = max(distanceToLight * distanceToLight, 0.0001);
    if (range <= 0.0)
        return 1.0 / distanceSq;
    highp float normalizedDistance = distanceToLight / max(range, 0.0001);
    highp float falloff = clamp(1.0 - normalizedDistance * normalizedDistance * normalizedDistance * normalizedDistance, 0.0, 1.0);
    return falloff / distanceSq;
}

highp vec3 CalculateSpecular(highp vec3 specularColor, highp vec3 normal, highp vec3 view, highp vec3 halfvector, highp vec3 light, highp float roughness)
{
    highp float NdotH = max(dot(normal, halfvector), 0.0);
    highp float VdotH = clamp(dot(view, halfvector), 0.0, 1.0);
    highp float NdotL = clamp(dot(normal, light), 0.0, 1.0);
    highp float NdotV = clamp(dot(normal, view), 0.0, 1.0);
    return FresnelCalc(VdotH, specularColor) * NormalDistribution(NdotH, roughness) * VisibilityGGX(NdotL, NdotV, roughness) * NdotL;
}

highp vec3 CalculateDiffuse(highp vec3 albedoColor, highp vec3 normal, highp vec3 light)
{
    return albedoColor * clamp(dot(normal, light), 0.0, 1.0) / PBR_PI;
}

highp vec3 CalculateClearcoat(highp vec3 normal, highp vec3 view, highp vec3 halfvector, highp vec3 light, highp float clearcoatRoughness)
{
    highp float NdotH = max(dot(normal, halfvector), 0.0);
    highp float NdotL = clamp(dot(normal, light), 0.0, 1.0);
    highp float NdotV = clamp(dot(normal, view), 0.0, 1.0);
    highp float brdf = NormalDistribution(NdotH, clearcoatRoughness) * VisibilityGGX(NdotL, NdotV, clearcoatRoughness) * NdotL;
    return vec3(brdf);
}

highp float Max3(highp vec3 value)
{
    return max(value.x, max(value.y, value.z));
}

highp float LambdaSheenNumericHelper(highp float x, highp float alphaG)
{
    highp float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
    highp float a = mix(21.5473, 25.3245, oneMinusAlphaSq);
    highp float b = mix(3.82987, 3.32435, oneMinusAlphaSq);
    highp float c = mix(0.19823, 0.16801, oneMinusAlphaSq);
    highp float d = mix(-1.97760, -1.27393, oneMinusAlphaSq);
    highp float e = mix(-4.32054, -4.85967, oneMinusAlphaSq);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}

highp float LambdaSheen(highp float cosTheta, highp float alphaG)
{
    if (abs(cosTheta) < 0.5) {
        return exp(LambdaSheenNumericHelper(cosTheta, alphaG));
    }
    return exp(2.0 * LambdaSheenNumericHelper(0.5, alphaG) - LambdaSheenNumericHelper(1.0 - cosTheta, alphaG));
}

highp float VisibilitySheen(highp float NdotL, highp float NdotV, highp float sheenRoughness)
{
    sheenRoughness = max(sheenRoughness, 0.000001);
    highp float alphaG = sheenRoughness * sheenRoughness;
    highp float denom = max((1.0 + LambdaSheen(NdotV, alphaG) + LambdaSheen(NdotL, alphaG)) * (4.0 * NdotV * NdotL), 0.000001);
    return clamp(1.0 / denom, 0.0, 1.0);
}

highp float DistributionCharlie(highp float sheenRoughness, highp float NdotH)
{
    sheenRoughness = max(sheenRoughness, 0.000001);
    highp float alphaG = sheenRoughness * sheenRoughness;
    highp float invR = 1.0 / alphaG;
    highp float cos2h = NdotH * NdotH;
    highp float sin2h = max(1.0 - cos2h, 0.0);
    return (2.0 + invR) * pow(sin2h, invR * 0.5) / (2.0 * 3.1415926);
}

highp vec3 BRDFSpecularSheen(highp vec3 sheenColor, highp float sheenRoughness, highp float NdotL, highp float NdotV, highp float NdotH)
{
    return sheenColor * DistributionCharlie(sheenRoughness, NdotH) * VisibilitySheen(NdotL, NdotV, sheenRoughness);
}

highp float AlbedoSheenScalingLUT(highp float NdotV, highp float sheenRoughness)
{
    return SampleTexture2DLod(texIBLSheenELUT, vec2(clamp(NdotV, 0.0, 1.0), clamp(sheenRoughness, 0.0, 1.0)), 0.0).r;
}

highp vec3 CalculateSheenRadiance(highp vec3 sheenColor, highp float sheenRoughness, highp vec3 lightColor, highp float intensity, highp float NdotL, highp float NdotV, highp float NdotH)
{
    return lightColor * intensity * NdotL * BRDFSpecularSheen(sheenColor, sheenRoughness, NdotL, NdotV, NdotH);
}

highp vec3 GetIBLRadianceCharlie(highp vec3 normal, highp vec3 viewDir, highp float sheenRoughness, highp vec3 sheenColor, highp float iblMaxMip)
{
    highp float NdotV = max(dot(normal, viewDir), 0.0);
    highp float lod = sheenRoughness * iblMaxMip;
    highp vec3 reflectedVec = reflect(-viewDir, normal);
    reflectedVec.x = -reflectedVec.x;
    reflectedVec.z = -reflectedVec.z;
    highp float brdf = SampleTexture2DLod(texIBLCharlieLUT, vec2(clamp(NdotV, 0.0, 1.0), clamp(sheenRoughness, 0.0, 1.0)), 0.0).b;
    highp vec3 sheenLight = SampleCubeLod(texIBLCharlie, reflectedVec, lod);
    return sheenLight * sheenColor * brdf;
}

highp vec2 GetUV0()
{
#ifdef USE_TEXCOORD0
    return vecUVCoords;
#elif defined(USE_TEXCOORD1)
    return vecUVCoords1;
#elif defined(USE_TEXCOORD2)
    return vecUVCoords2;
#elif defined(USE_TEXCOORD3)
    return vecUVCoords3;
#else
    return vec2(0.0, 0.0);
#endif
}

highp vec2 GetUV1()
{
#ifdef USE_TEXCOORD1
    return vecUVCoords1;
#elif defined(USE_TEXCOORD0)
    return vecUVCoords;
#else
    return vec2(0.0, 0.0);
#endif
}

highp vec2 GetUV2()
{
#ifdef USE_TEXCOORD2
    return vecUVCoords2;
#elif defined(USE_TEXCOORD0)
    return vecUVCoords;
#else
    return vec2(0.0, 0.0);
#endif
}

highp vec2 GetUV3()
{
#ifdef USE_TEXCOORD3
    return vecUVCoords3;
#elif defined(USE_TEXCOORD0)
    return vecUVCoords;
#else
    return vec2(0.0, 0.0);
#endif
}

highp vec2 GetTexCoord(highp float texCoordSet)
{
    highp int s = int(texCoordSet + 0.5);
    if (s == 1) return GetUV1();
    if (s == 2) return GetUV2();
    if (s == 3) return GetUV3();
    return GetUV0();
}

bool MapShareBaseSet(highp float mapSet)
{
    return abs(mapSet - TexCoordSets.x) < 0.5;
}

highp vec2 ApplyUVTransform(highp vec2 uv, highp vec4 row0, highp vec4 row1)
{
    return vec2(dot(row0.xy, uv) + row0.z, dot(row1.xy, uv) + row1.z);
}

highp vec3 LinearToStoredAlbedo(highp vec3 linearColor)
{
    return pow(clamp(linearColor, 0.0, 1.0), vec3(1.0 / 2.2));
}

highp vec3 StoredSRGBToLinear(highp vec3 storedColor)
{
    return pow(max(storedColor, vec3(0.0)), vec3(2.2));
}

highp vec4 SampleBaseColor(highp vec2 uv)
{
#if defined(DIFFUSE_MAP) && (defined(USE_TEXCOORD0) || defined(USE_TEXCOORD1) || defined(USE_TEXCOORD2) || defined(USE_TEXCOORD3))
    highp vec4 color = SampleTexture2D(DiffuseTex, ApplyUVTransform(uv, BaseColorUVTransform0, BaseColorUVTransform1));
    #ifdef GLTF_TANGENT_SPACE
    color.rgb *= LinearToStoredAlbedo(DiffuseColor.rgb);
    color.a *= DiffuseColor.a;
    #endif
    return color;
#else
    #ifdef GLTF_TANGENT_SPACE
    return vec4(LinearToStoredAlbedo(DiffuseColor.rgb), DiffuseColor.a);
    #else
    return DiffuseColor;
    #endif
#endif
}

highp vec3 SampleEmissive(highp vec2 uv)
{
    highp vec3 emissive = EmissiveColor.rgb;
#ifdef EMISSIVE_MAP
    highp vec2 emissiveUV = MapShareBaseSet(TexCoordSets.w) ? uv : GetTexCoord(TexCoordSets.w);
    emissiveUV = ApplyUVTransform(emissiveUV, EmissiveUVTransform0, EmissiveUVTransform1);
    highp vec3 emissiveSample = SampleTexture2D(EmissiveTex, emissiveUV).rgb;
    #ifdef GLTF_TANGENT_SPACE
    emissiveSample = StoredSRGBToLinear(emissiveSample);
    #endif
    emissive *= emissiveSample;
#endif
    return emissive * MaterialParams.w;
}

void ApplyAlphaMask(inout highp vec4 color)
{
    if (AlphaParams.x > 0.5 && AlphaParams.x < 1.5) {
        if (color.a < AlphaParams.y)
            discard;
        color.a = 1.0;
    }
}

highp float GetPackedLightRadius(highp int i)
{
    highp int packIndex = i / 4;
    highp int c = i - packIndex * 4;
    highp vec4 pack = LightRadius[packIndex];
    return c == 0 ? pack.x : (c == 1 ? pack.y : (c == 2 ? pack.z : pack.w));
}

void BuildSurface(bool isFrontFace, out highp vec4 color, out highp vec3 normal, out highp vec3 geoNormal,
                  out highp float metallic, out highp float roughness, out highp float selfShadow, out highp vec2 uv,
                  out highp vec3 sheenColor, out highp float sheenRoughness,
                  out highp float clearcoatFactor, out highp float clearcoatRoughness,
                  out highp float occlusion, out highp vec3 dielectricF0, out highp float specularWeight,
                  out highp float transmissionFactor)
{
    color = vec4(0.5, 0.5, 0.5, 1.0);
    metallic = PBRParams.x;
    roughness = PBRParams.y;
    selfShadow = 1.0;
    occlusion = 1.0;
    dielectricF0 = max(SpecularColor.rgb, vec3(0.0));
    specularWeight = max(SpecularColor.w, 0.0);
    transmissionFactor = clamp(AlphaParams.w, 0.0, 1.0);
    sheenColor = clamp(MaterialParams4.rgb, 0.0, 1.0);
    sheenRoughness = clamp(MaterialParams4.w, 0.0, 1.0);
    clearcoatFactor = clamp(MaterialParams.x, 0.0, 1.0);
    clearcoatRoughness = clamp(MaterialParams.y, 0.0, 1.0);
    uv = GetTexCoord(TexCoordSets.x);

#ifdef USE_NORMALS
    normal = normalize(hnormal.xyz);
#else
    normal = vec3(0.0, 0.0, 1.0);
#endif
    geoNormal = normal;
    bool flipBackFace = AlphaParams.z > 0.5 && !isFrontFace;
    if (flipBackFace) {
        normal = -normal;
        geoNormal = -geoNormal;
    }

#if defined(HEIGHT_MAP) || defined(NORMAL_MAP)
    highp vec3 tangent = normalize(htangent.xyz);
    highp vec3 binormal = normalize(hbinormal.xyz);
    if (flipBackFace) {
        tangent = -tangent;
        binormal = -binormal;
    }
    highp mat3 TBN = mat3(tangent, binormal, normal);
#endif

#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    highp float heightScale = ParallaxSettings.z;
    highp mat3 TBNTransposed = transpose(TBN);
    highp vec3 viewDir = TBNTransposed * normalize(CameraPosition.xyz - WorldPos.xyz);
    viewDir = normalize(viewDir);
    highp float minLayers = ParallaxSettings.x;
    highp float maxLayers = ParallaxSettings.y;
    highp float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDir)));
    highp float layerDepth = 1.0 / numLayers;
    highp float prevDepthMapValue = 0.0;
    highp vec2 P = -viewDir.xy * heightScale / viewDir.z;
    highp vec2 deltaTexCoords = P * layerDepth;
    deltaTexCoords.y = -deltaTexCoords.y;

    highp vec2 dxx = dFdx(uv);
    highp vec2 dyy = dFdy(uv);
    highp float currentDepthMapValue = textureGrad(HeightTex, uv, dxx, dyy).r;
    highp float currentRayZ = 1.0 - layerDepth;
    highp float prevRayZ = 1.0 - layerDepth;
    while (currentRayZ > currentDepthMapValue) {
        currentDepthMapValue = textureGrad(HeightTex, uv, dxx, dyy).r;
        prevDepthMapValue = currentDepthMapValue;
        uv += deltaTexCoords;
        prevRayZ = currentRayZ;
        currentRayZ -= layerDepth;
    }
    highp vec2 prevTexCoords = uv - deltaTexCoords;
    highp float weight = (prevDepthMapValue - prevRayZ) / (prevDepthMapValue - currentDepthMapValue + currentRayZ - prevRayZ);
    uv = prevTexCoords * weight + uv * (1.0 - weight);
#endif

    color = SampleBaseColor(uv);
    ApplyAlphaMask(color);

#ifdef NORMAL_MAP
    highp vec2 normalUV = MapShareBaseSet(TexCoordSets.y) ? uv : GetTexCoord(TexCoordSets.y);
    normalUV = ApplyUVTransform(normalUV, NormalUVTransform0, NormalUVTransform1);
    highp vec3 normalTex = SampleTexture2D(NormalTex, normalUV).xyz;
    normalTex = normalTex * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
    // glTF normalTexture.scale (default 1.0) — stored in MaterialParams9.y.
    highp float normalScale = MaterialParams9.y;
    normalTex.xy *= normalScale;
    normalTex = normalize(normalTex);
    #ifndef GLTF_TANGENT_SPACE
    normalTex.g = -normalTex.g;
    #endif
    normal = TBN * normalTex;
    normal = normalize(normal);
#endif

#ifdef METALLIC_MAP
    highp vec2 metallicUV = MapShareBaseSet(TexCoordSets.z) ? uv : GetTexCoord(TexCoordSets.z);
    metallicUV = ApplyUVTransform(metallicUV, MetallicUVTransform0, MetallicUVTransform1);
    highp vec4 mrSample = SampleTexture2D(MetallicTex, metallicUV);
    metallic = PBRParams.x * mrSample.b;
    roughness = PBRParams.y * mrSample.g;
#elif defined(GLOSS_MAP)
    roughness = SampleTexture2D(GlossTex, uv).r;
#endif
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

#ifdef SPECULAR_FACTOR_MAP
    if (MaterialParams8.y > 0.5) {
        highp vec2 specularFactorUV = MapShareBaseSet(MaterialParams8.z) ? uv : GetTexCoord(MaterialParams8.z);
        specularFactorUV = ApplyUVTransform(specularFactorUV, SpecularFactorUVTransform0, SpecularFactorUVTransform1);
        specularWeight *= SampleTexture2D(SpecularFactorTex, specularFactorUV).a;
    }
#endif
#ifdef SPECULAR_COLOR_MAP
    if (MaterialParams8.w > 0.5) {
        highp vec2 specularColorUV = MapShareBaseSet(MaterialParams9.x) ? uv : GetTexCoord(MaterialParams9.x);
        specularColorUV = ApplyUVTransform(specularColorUV, SpecularColorUVTransform0, SpecularColorUVTransform1);
        dielectricF0 = min(dielectricF0 * StoredSRGBToLinear(SampleTexture2D(SpecularColorTex, specularColorUV).rgb), vec3(1.0));
    }
#endif
    specularWeight = clamp(specularWeight, 0.0, 1.0);

#ifdef OCCLUSION_MAP
    if (MaterialParams7.x > 0.5) {
        highp vec2 occlusionUV = MapShareBaseSet(MaterialParams7.z) ? uv : GetTexCoord(MaterialParams7.z);
        occlusionUV = ApplyUVTransform(occlusionUV, OcclusionUVTransform0, OcclusionUVTransform1);
        highp float ao = SampleTexture2D(OcclusionTex, occlusionUV).r;
        occlusion = clamp(1.0 + MaterialParams7.y * (ao - 1.0), 0.0, 1.0);
    }
#endif

#ifdef TRANSMISSION_MAP
    if (MaterialParams7.w > 0.5) {
        highp vec2 transmissionUV = MapShareBaseSet(MaterialParams8.x) ? uv : GetTexCoord(MaterialParams8.x);
        transmissionUV = ApplyUVTransform(transmissionUV, TransmissionUVTransform0, TransmissionUVTransform1);
        transmissionFactor *= SampleTexture2D(TransmissionTex, transmissionUV).r;
    }
#endif
    transmissionFactor = clamp(transmissionFactor, 0.0, 1.0);

#ifdef SHEEN_COLOR_MAP
    if (MaterialParams5.x > 0.5) {
        highp vec2 sheenColorUV = MapShareBaseSet(MaterialParams5.z) ? uv : GetTexCoord(MaterialParams5.z);
        sheenColorUV = ApplyUVTransform(sheenColorUV, SheenColorUVTransform0, SheenColorUVTransform1);
        sheenColor *= pow(max(SampleTexture2D(SheenColorTex, sheenColorUV).rgb, vec3(0.0)), vec3(2.2));
    }
#endif
#ifdef SHEEN_ROUGHNESS_MAP
    if (MaterialParams5.y > 0.5) {
        highp vec2 sheenRoughnessUV = MapShareBaseSet(MaterialParams5.w) ? uv : GetTexCoord(MaterialParams5.w);
        sheenRoughnessUV = ApplyUVTransform(sheenRoughnessUV, SheenRoughnessUVTransform0, SheenRoughnessUVTransform1);
        sheenRoughness *= SampleTexture2D(SheenRoughnessTex, sheenRoughnessUV).a;
    }
#endif
    sheenColor = clamp(sheenColor, 0.0, 1.0);
    sheenRoughness = clamp(sheenRoughness, 0.0, 1.0);

#ifdef CLEARCOAT_MAP
    if (MaterialParams6.x > 0.5) {
        highp vec2 clearcoatUV = MapShareBaseSet(MaterialParams6.z) ? uv : GetTexCoord(MaterialParams6.z);
        clearcoatUV = ApplyUVTransform(clearcoatUV, ClearcoatUVTransform0, ClearcoatUVTransform1);
        clearcoatFactor *= SampleTexture2D(ClearcoatTex, clearcoatUV).r;
    }
#endif
#ifdef CLEARCOAT_ROUGHNESS_MAP
    if (MaterialParams6.y > 0.5) {
        highp vec2 clearcoatRoughnessUV = MapShareBaseSet(MaterialParams6.w) ? uv : GetTexCoord(MaterialParams6.w);
        clearcoatRoughnessUV = ApplyUVTransform(clearcoatRoughnessUV, ClearcoatRoughnessUVTransform0, ClearcoatRoughnessUVTransform1);
        clearcoatRoughness *= SampleTexture2D(ClearcoatRoughnessTex, clearcoatRoughnessUV).g;
    }
#endif
    clearcoatFactor = clamp(clearcoatFactor, 0.0, 1.0);
    clearcoatRoughness = clamp(clearcoatRoughness, 0.0, 1.0);

#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    highp vec2 ssDxx = dFdx(vecUVCoords);
    highp vec2 ssDyy = dFdy(vecUVCoords);
    highp float ssStartZ = textureGrad(HeightTex, uv, ssDxx, ssDyy).r;
    highp float shadowStrength = ParallaxShadowSettings.w;
    if (shadowStrength > 0.001) {
        highp float lightDirLen = length(Light0Direction.xyz);
        if (lightDirLen > 0.001) {
            highp mat3 TBN_t = transpose(TBN);
            highp vec3 lightDirTS = TBN_t * normalize(-Light0Direction.xyz);
            lightDirTS = normalize(lightDirTS);
            if (lightDirTS.z > 0.01) {
                highp float numShadowLayers = mix(ParallaxShadowSettings.y, ParallaxShadowSettings.x, abs(lightDirTS.z));
                highp float layerStep = 1.0 / numShadowLayers;
                highp vec2 P_light = lightDirTS.xy * ParallaxSettings.z / lightDirTS.z;
                highp vec2 deltaUV = P_light * layerStep;
                deltaUV.y = -deltaUV.y;
                highp vec2 currentUV = uv;
                highp float currentRayZ = ssStartZ;
                for (int si = 0; si < int(numShadowLayers); si++) {
                    currentRayZ += layerStep;
                    currentUV += deltaUV;
                    if (currentRayZ >= 1.0) break;
                    highp float h = textureGrad(HeightTex, currentUV, ssDxx, ssDyy).r;
                    if (h > currentRayZ) {
                        highp float penumbra = float(si + 1) / numShadowLayers;
                        selfShadow = min(selfShadow, mix(0.0, penumbra, ParallaxShadowSettings.z));
                    }
                }
                selfShadow = mix(1.0, selfShadow, shadowStrength);
            }
        }
    }
#endif
}

#ifdef SIMPLE_COLOR
#ifdef ES_30
layout(location = 0) out highp vec4 colorOut;
#endif
void main()
{
#ifdef ES_30
    colorOut = vec4(0.5, 0.5, 0.5, 1.0);
#else
    gl_FragColor = vec4(0.5, 0.5, 0.5, 1.0);
#endif
}
#elif defined(G_BUFFER_PASS)
#ifdef ES_30
layout(location = 0) out highp vec4 colorOut_0;
layout(location = 1) out highp vec4 colorOut_1;
layout(location = 2) out highp vec4 colorOut_2;
layout(location = 3) out highp vec4 colorOut_3;
layout(location = 4) out highp vec4 colorOut_4;
layout(location = 5) out highp vec4 colorOut_5;
layout(location = 6) out highp vec4 colorOut_6;
#endif
void main()
{
    highp vec4 color;
    highp vec3 normal;
    highp vec3 geoNormal;
    highp float metallic;
    highp float roughness;
    highp float selfShadow;
    highp vec3 sheenColor;
    highp float sheenRoughness;
    highp float clearcoatFactor;
    highp float clearcoatRoughness;
    highp float occlusion;
    highp vec3 dielectricF0;
    highp float specularWeight;
    highp float transmissionFactor;
    highp vec2 uv;
    BuildSurface(gl_FrontFacing, color, normal, geoNormal, metallic, roughness, selfShadow, uv, sheenColor, sheenRoughness, clearcoatFactor, clearcoatRoughness, occlusion, dielectricF0, specularWeight, transmissionFactor);
    highp float outDepth = Pos.z / Pos.w;
#ifdef ES_30
    colorOut_0 = vec4(color.rgb, specularWeight);
    colorOut_1 = vec4(normal * 0.5 + 0.5, roughness);
    colorOut_2 = vec4(metallic, selfShadow, clearcoatFactor, Intensities.w / 255.0);
    highp float packedMaterial = clearcoatRoughness * 0.5 + (MaterialParams.z > 0.5 ? 0.5 : 0.0);
    colorOut_3 = vec4(EncodeOctahedralNormal(geoNormal), 0.0, packedMaterial);
    colorOut_4 = vec4(SampleEmissive(uv), 0.0);
    colorOut_5 = vec4(sheenColor, sheenRoughness);
    colorOut_6 = vec4(dielectricF0, occlusion);
#else
    gl_FragData[0] = vec4(color.rgb, specularWeight);
    gl_FragData[1] = vec4(normal * 0.5 + 0.5, roughness);
    gl_FragData[2] = vec4(metallic, selfShadow, clearcoatFactor, Intensities.w / 255.0);
    highp float packedMaterial = clearcoatRoughness * 0.5 + (MaterialParams.z > 0.5 ? 0.5 : 0.0);
    gl_FragData[3] = vec4(EncodeOctahedralNormal(geoNormal), 0.0, packedMaterial);
    gl_FragData[4] = vec4(SampleEmissive(uv), 0.0);
    gl_FragData[5] = vec4(sheenColor, sheenRoughness);
    gl_FragData[6] = vec4(dielectricF0, occlusion);
#endif
    gl_FragDepth = outDepth;
}
#elif defined(SHADOW_MAP_PASS)
void main()
{
    highp vec4 color = SampleBaseColor(GetTexCoord(TexCoordSets.x));
    ApplyAlphaMask(color);
    gl_FragDepth = Pos.z / Pos.w;
}
#elif defined(RADIAL_DEPTH_PASS)
uniform highp mat4 WorldView;
void main()
{
    highp vec4 color = SampleBaseColor(GetTexCoord(TexCoordSets.x));
    ApplyAlphaMask(color);
    highp vec4 ff = WorldView * CameraPosition;
    gl_FragDepth = 1.0 - clamp(length(vec3(Pos.xyz - ff.xyz)) / CameraInfo.y, 0.0, 1.0);
}
#else
#ifdef ES_30
layout(location = 0) out highp vec4 colorOut;
#endif
void main()
{
    highp vec4 color;
    highp vec3 normal;
    highp vec3 geoNormal;
    highp float metallic;
    highp float roughness;
    highp float selfShadow;
    highp vec3 sheenColor;
    highp float sheenRoughness;
    highp float clearcoatFactor;
    highp float clearcoatRoughness;
    highp float occlusion;
    highp vec3 dielectricF0;
    highp float specularWeight;
    highp float transmissionFactor;
    highp vec2 uv;
    BuildSurface(gl_FrontFacing, color, normal, geoNormal, metallic, roughness, selfShadow, uv, sheenColor, sheenRoughness, clearcoatFactor, clearcoatRoughness, occlusion, dielectricF0, specularWeight, transmissionFactor);
    highp vec3 emissive = SampleEmissive(uv);

    if (ForwardParams.z > 0.5 && ForwardParams.x > 0.0 && ForwardParams.y > 0.0) {
        highp float sceneDepth = LoadForwardSceneDepth();
        highp float meshDepth = Pos.z / Pos.w;
        const highp float depthEpsilon = 0.000001;
        if (sceneDepth > 0.0001 && meshDepth < sceneDepth - depthEpsilon)
            discard;
    }

    highp vec3 albedo = pow(max(color.rgb, vec3(0.0)), vec3(2.2));
    highp vec3 eyeDir = normalize(CameraPosition.xyz - WorldPos.xyz);
    highp vec3 F0 = mix(dielectricF0 * specularWeight, albedo, metallic);
    highp vec3 directLight = vec3(0.0);
    highp float sheenStrength = Max3(sheenColor);
    bool hasSheenLUT = MaterialParams3.y > 0.5;
    highp int numLights = int(CameraInfo.w);

    for (int i = 0; i < 128; i++) {
        if (i >= numLights) break;
        highp float lightType = LightPositions[i].w;
        highp float intensity = LightColors[i].w;
        if (lightType < 0.5) {
            highp vec3 lightDir = normalize(-LightPositions[i].xyz);
            highp vec3 halfVec = normalize(eyeDir + lightDir);
            highp vec3 lightRadiance = LightColors[i].xyz * intensity;
            highp vec3 diffuse = CalculateDiffuse(albedo, normal, lightDir) * lightRadiance;
            highp vec3 specular = CalculateSpecular(F0, normal, eyeDir, halfVec, lightDir, roughness) * lightRadiance;
            highp vec3 F = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0, 1.0), F0);
            highp vec3 Kd = (vec3(1.0) - F) * (1.0 - metallic);
            highp float NdotL = max(dot(normal, lightDir), 0.0);
            highp float NdotVLight = max(dot(normal, eyeDir), 0.0);
            highp float NdotH = max(dot(normal, halfVec), 0.0);
            highp float albedoSheenScaling = 1.0;
            highp vec3 sheenLight = vec3(0.0);
            if (hasSheenLUT && sheenStrength > 0.0) {
                albedoSheenScaling = min(1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
                                         1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
                sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH);
            }
            highp vec3 layerLight = sheenLight + (specular + Kd * diffuse) * albedoSheenScaling;
            if (clearcoatFactor > 0.001) {
                highp float clearcoatF = FresnelCalc(clamp(dot(normal, eyeDir), 0.0, 1.0), vec3(0.04)).x;
                highp float clearcoatWeight = clamp(clearcoatFactor * clearcoatF, 0.0, 1.0);
                highp vec3 clearcoatLight = CalculateClearcoat(normal, eyeDir, halfVec, lightDir, clamp(clearcoatRoughness, 0.04, 1.0)) * lightRadiance;
                layerLight = mix(layerLight, clearcoatLight, clearcoatWeight);
            }
            directLight += layerLight;
        } else {
            highp float rad = GetPackedLightRadius(i);
            highp float dist = distance(LightPositions[i].xyz, WorldPos.xyz);
            highp float attenuation = RangeAttenuation(rad * 2.0, dist);
            if (attenuation > 0.0) {
                highp vec3 lightDir = normalize(LightPositions[i].xyz - WorldPos.xyz);
                highp vec3 halfVec = normalize(eyeDir + lightDir);
                highp vec3 lightRadiance = LightColors[i].xyz * intensity * attenuation;
                highp vec3 diffuse = CalculateDiffuse(albedo, normal, lightDir) * lightRadiance;
                highp vec3 specular = CalculateSpecular(F0, normal, eyeDir, halfVec, lightDir, roughness) * lightRadiance;
                highp vec3 F = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0, 1.0), F0);
                highp vec3 Kd = (vec3(1.0) - F) * (1.0 - metallic);
                highp float NdotL = max(dot(normal, lightDir), 0.0);
                highp float NdotVLight = max(dot(normal, eyeDir), 0.0);
                highp float NdotH = max(dot(normal, halfVec), 0.0);
                highp float albedoSheenScaling = 1.0;
                highp vec3 sheenLight = vec3(0.0);
                if (hasSheenLUT && sheenStrength > 0.0) {
                    albedoSheenScaling = min(1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
                                             1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
                    sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH) * attenuation;
                }
                    highp vec3 layerLight = sheenLight + (specular + Kd * diffuse) * albedoSheenScaling;
                    if (clearcoatFactor > 0.001) {
                        highp float clearcoatF = FresnelCalc(clamp(dot(normal, eyeDir), 0.0, 1.0), vec3(0.04)).x;
                        highp float clearcoatWeight = clamp(clearcoatFactor * clearcoatF, 0.0, 1.0);
                        highp vec3 clearcoatLight = CalculateClearcoat(normal, eyeDir, halfVec, lightDir, clamp(clearcoatRoughness, 0.04, 1.0)) * lightRadiance;
                        layerLight = mix(layerLight, clearcoatLight, clearcoatWeight);
                    }
                    directLight += layerLight;
            }
        }
    }

    highp vec3 finalColor = directLight * selfShadow;
    highp float iblFactor = max(MaterialParams2.w, 0.0);
    highp float iblMaxMip = max(MaterialParams3.x, 0.0);
    bool hasBrdfLUT = MaterialParams3.y > 0.5;
    highp vec3 reflectedVec = reflect(-eyeDir, normal);
    reflectedVec.x = -reflectedVec.x;
    reflectedVec.z = -reflectedVec.z;
    highp float NdotV = max(dot(normal, eyeDir), 0.0);
    highp vec3 kSpecular = clamp(fresnelSchlickRoughness(NdotV, F0, roughness), 0.0, 1.0);
    highp vec3 kDiffuseEnv = (vec3(1.0) - kSpecular) * (1.0 - metallic);
    highp vec3 envSpec = SampleCubeLod(texIBLSpecular, reflectedVec, roughness * iblMaxMip);
    highp float envAtten = (1.0 - roughness) * (1.0 - roughness);
    highp vec2 brdfSample = hasBrdfLUT ? SampleTexture2DLod(texIBLBRDF, vec2(NdotV, roughness), 0.0).rg : vec2(0.0);
    highp vec3 specularIBL = hasBrdfLUT ? IBLGGXFresnel(NdotV, roughness, F0, brdfSample) : kSpecular * envAtten;
    highp vec3 indirectLight = envSpec * specularIBL * iblFactor;
    highp vec3 irradianceDir = normal;
    irradianceDir.x = -irradianceDir.x;
    irradianceDir.z = -irradianceDir.z;
    highp float diffuseMip = clamp(MaterialParams3.z, 0.0, iblMaxMip);
    highp vec3 irradiance = SampleCubeLod(texIBLDiffuse, irradianceDir, diffuseMip);
    indirectLight += irradiance * albedo * kDiffuseEnv * iblFactor;
    if (hasSheenLUT && sheenStrength > 0.0) {
        highp float albedoSheenScaling = 1.0 - sheenStrength * AlbedoSheenScalingLUT(NdotV, sheenRoughness);
        highp vec3 sheenIBL = GetIBLRadianceCharlie(normal, eyeDir, sheenRoughness, sheenColor, iblMaxMip) * iblFactor;
        indirectLight = sheenIBL + indirectLight * albedoSheenScaling;
    }
    finalColor += indirectLight * occlusion;

    if (clearcoatFactor > 0.001) {
        clearcoatRoughness = clamp(clearcoatRoughness, 0.04, 1.0);
        highp vec3 clearcoatSpec = SampleCubeLod(texIBLSpecular, reflectedVec, clearcoatRoughness * iblMaxMip);
        highp float clearcoatAtten = hasBrdfLUT ? 1.0 : (1.0 - clearcoatRoughness) * (1.0 - clearcoatRoughness);
        highp vec3 clearcoatF = FresnelCalc(clamp(dot(normal, eyeDir), 0.0, 1.0), vec3(0.04));
        highp float clearcoatWeight = clamp(clearcoatFactor * max(clearcoatF.x, max(clearcoatF.y, clearcoatF.z)), 0.0, 1.0);
        finalColor = mix(finalColor, clearcoatSpec * clearcoatAtten * iblFactor, clearcoatWeight);
    }

    if (MaterialParams.z > 0.5) {
        finalColor = albedo;
    }

    highp float transmission = clamp(transmissionFactor * MaterialParams2.x, 0.0, 1.0);
    if (MaterialParams2.z > 0.5 && transmission > 0.001 && MaterialParams2.y > 0.0 && ForwardParams.x > 0.0 && ForwardParams.y > 0.0) {
        highp vec2 screenUV = gl_FragCoord.xy / ForwardParams.xy;
        highp float iorOffset = clamp(abs(ForwardParams.w - 1.0), 0.0, 1.0);
        highp vec2 refractUV = clamp(screenUV + normal.xy * MaterialParams2.y * transmission * (0.5 + iorOffset), vec2(0.0), vec2(1.0));
        highp vec3 sceneColor = SampleTexture2D(SceneColorTex, refractUV).rgb;
        finalColor = mix(finalColor, sceneColor, transmission);
    }
    finalColor += emissive;

    highp float alpha = color.a;
    if (transmission > 0.0 && alpha >= 0.999)
        alpha = clamp(1.0 - transmission, 0.0, 1.0);
    if (AlphaParams.x < 1.5 && transmission <= 0.0)
        alpha = 1.0;
#ifdef ES_30
    colorOut = vec4(finalColor, alpha);
#else
    gl_FragColor = vec4(finalColor, alpha);
#endif
}
#endif
