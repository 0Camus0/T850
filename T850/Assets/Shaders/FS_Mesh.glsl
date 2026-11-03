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

#ifdef FLUID_SURFACE
// GL binds textures by uniform NAME (RenderMesh::Draw), so these must match the names
// the scene uploads: the solver's surface, the static pool floor and the tiling water
// normal map. All three are fetched with texelFetch/nearest where the HLSL says so.
uniform highp sampler2D FluidSurfaceTexture;
uniform highp sampler2D FluidFloorTexture;
uniform mediump sampler2D FluidNormalTex;
#endif

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

#ifdef LIGHTMAP_MAP
uniform mediump sampler2D LightmapTex;
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
uniform highp vec4 LightmapUVTransform0;
uniform highp vec4 LightmapUVTransform1;
uniform highp vec4 LightPositions[128];
uniform highp vec4 LightColors[128];
uniform highp vec4 LightRadius[32];
// Fragment-side extras for the water composite. These are the tail of
// RenderMesh::CBuffer and must keep that order: GL assigns uniform offsets by
// declaration order (the vertex shader is parsed first).
uniform highp mat4 CameraVP;
uniform highp vec4 WaterOptics0;
uniform highp vec4 WaterOptics1;
uniform highp vec4 WaterOptics2;
uniform highp vec4 WaterOptics3;
uniform highp vec4 WaterOptics4;
uniform highp vec4 WaterOptics5;
uniform highp vec4 WaterWaves0;
uniform highp vec4 WaterWaves1;
uniform highp vec4 WaterWaves2;
uniform highp vec4 WaterReflect0;
uniform highp vec4 WaterReflect1;
uniform highp vec4 WaterReflect2;
uniform highp vec4 WaterFoam0;
uniform highp vec4 WaterFoam1;
uniform highp vec4 WaterFoam2;
uniform highp vec4 WaterFoam3;
uniform highp vec4 WaterRipple0;
uniform highp vec4 WaterRipple1;
uniform highp vec4 WaterRipple2;
uniform highp vec4 WaterRipple3;
uniform highp vec4 WaterRipple4;
uniform highp vec4 WaterRipple5;
uniform highp vec4 WaterMisc0;
uniform highp vec4 WaterMisc1;

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

highp float SampleLightmap()
{
#ifdef LIGHTMAP_MAP
    highp vec2 lightmapUV = GetTexCoord(MaterialParams9.z);
    lightmapUV = ApplyUVTransform(lightmapUV, LightmapUVTransform0, LightmapUVTransform1);
    highp vec3 lightmap = SampleTexture2D(LightmapTex, lightmapUV).rgb;
    highp float luminance = dot(lightmap, vec3(0.2126, 0.7152, 0.0722));
    return max(luminance * MaterialParams9.w, 0.0);
#else
    return 0.0;
#endif
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

#ifdef FLUID_SURFACE
// ── Pool water helpers ────────────────────────────────────────────────────────
// Hand-ported from the HLSL FLUID_SURFACE blocks (FS_Mesh.hlsl). HLSL `input.WorldPos`
// is the `WorldPos` varying, `Load(int3(p,0))` is `texelFetch`, `SampleLevel` is
// `textureLod`, `SampleGrad` is `textureGrad`, `mul(M, v)` is `M * v`, `lerp` is
// `mix` and `saturate(x)` is `clamp(x, 0.0, 1.0)`. Keep the two in step: the self-tests
// compare their output on different backends.

// True when opaque geometry at `sampleUV` sits NEARER to the camera than this water
// fragment. Depth conventions differ per backend, so they are derived from the water's
// own known position in front of the opaque basin floor.
bool SceneIsNearerAt(highp vec2 sampleUV)
{
    const highp float epsilon = 0.0001;
    highp ivec2 maxPixel = ivec2(int(ForwardParams.x) - 1, int(ForwardParams.y) - 1);
    highp ivec2 px = clamp(ivec2(sampleUV * ForwardParams.xy), ivec2(0), maxPixel);
    highp ivec2 selfPx = clamp(ivec2(gl_FragCoord.xy), ivec2(0), maxPixel);
    highp float sampled = texelFetch(SceneDepthTex, px, 0).r;
    highp float opaqueHere = texelFetch(SceneDepthTex, selfPx, 0).r;
    if (sampled <= 0.0 || opaqueHere <= 0.0)
        return false;
    highp float selfZ = Pos.z / Pos.w;
    highp float depthSign = (selfZ < opaqueHere) ? -1.0 : 1.0;
    return (sampled - selfZ) * depthSign > epsilon;
}

// One directional sine octave. Called six times explicitly: dynamic indexing into a
// local const array is not reliably unrolled and silently collapses every octave onto
// element 0.
highp vec2 WaterRippleOctave(highp vec2 p, highp float timeSeconds, highp vec2 dir,
                             highp float waveLength, highp float amplitude, highp float speed,
                             highp float fadeDistance, highp float viewDistance)
{
    highp float k = 6.28318530718 / waveLength;
    highp float phase = dot(dir, p) * k + timeSeconds * WaterWaves0.y * speed * k;
    highp float octaveFade = clamp(1.0 - viewDistance / fadeDistance, 0.0, 1.0);
    return dir * (amplitude * WaterWaves0.x * k * cos(phase)) * octaveFade;
}

#define T8_RIPPLE_OCTAVES(FN, ACCUM, P, T, D)                                                          \
    ACCUM = FN(P, T, vec2( 0.86,  0.51), WaterRipple0.y, WaterRipple0.x, WaterRipple0.z, WaterRipple0.w, D); \
    ACCUM += FN(P, T, vec2(-0.60,  0.80), WaterRipple1.y, WaterRipple1.x, WaterRipple1.z, WaterRipple1.w, D); \
    ACCUM += FN(P, T, vec2( 0.31, -0.95), WaterRipple2.y, WaterRipple2.x, WaterRipple2.z, WaterRipple2.w, D); \
    ACCUM += FN(P, T, vec2( 0.98, -0.20), WaterRipple3.y, WaterRipple3.x, WaterRipple3.z, WaterRipple3.w, D); \
    ACCUM += FN(P, T, vec2(-0.87, -0.49), WaterRipple4.y, WaterRipple4.x, WaterRipple4.z, WaterRipple4.w, D); \
    ACCUM += FN(P, T, vec2( 0.42,  0.91), WaterRipple5.y, WaterRipple5.x, WaterRipple5.z, WaterRipple5.w, D)

highp vec2 WaterRippleGradient(highp vec2 p, highp float timeSeconds, highp float viewDistance)
{
    highp vec2 gradient = vec2(0.0);
    T8_RIPPLE_OCTAVES(WaterRippleOctave, gradient, p, timeSeconds, viewDistance);
    return gradient;
}

// Gradient of only the three longest octaves: the fine capillary detail comes from the
// tiling normal map, so including the finest octaves here would double-count it.
highp vec2 WaterRippleGradientCoarse(highp vec2 p, highp float timeSeconds, highp float viewDistance)
{
    highp vec2 gradient = WaterRippleOctave(p, timeSeconds, vec2( 0.86,  0.51), WaterRipple0.y, WaterRipple0.x, WaterRipple0.z, WaterRipple0.w, viewDistance);
    gradient += WaterRippleOctave(p, timeSeconds, vec2(-0.60,  0.80), WaterRipple1.y, WaterRipple1.x, WaterRipple1.z, WaterRipple1.w, viewDistance);
    gradient += WaterRippleOctave(p, timeSeconds, vec2( 0.31, -0.95), WaterRipple2.y, WaterRipple2.x, WaterRipple2.z, WaterRipple2.w, viewDistance);
    return gradient;
}

// Procedural breakup so foam reads as froth rather than a smooth gradient.
highp float WaterFoamNoise(highp vec2 p, highp float timeSeconds, highp float viewDistance)
{
    highp float fade = clamp(1.0 - viewDistance / max(WaterFoam3.z, 0.001), 0.0, 1.0);
    highp float n = sin(p.x * WaterFoam2.x + timeSeconds * 2.6) * sin(p.y * WaterFoam2.y - timeSeconds * 2.1);
    n += 0.55 * sin(p.x * WaterFoam2.z - timeSeconds * 3.4) * sin(p.y * WaterFoam2.w + timeSeconds * 3.0) * fade;
    n += 0.30 * sin(p.x * WaterFoam3.x + timeSeconds * 4.1) * sin(p.y * WaterFoam3.y - timeSeconds * 3.7) * fade * fade;
    return clamp(0.5 + 0.42 * n, 0.0, 1.0);
}

// Slope of the tiling normal map, in m/m. Two layers at different tile scales and drift
// rates so the pattern never visibly repeats; textureGrad lets the GPU pick the mip.
highp vec2 WaterNormalMapGradient(highp vec2 uv, highp float timeSeconds)
{
    highp vec2 uvA = uv * WaterWaves1.xy + WaterWaves1.zw * timeSeconds;
    highp vec2 uvB = uv * WaterWaves2.xy + WaterWaves2.zw * timeSeconds;
    highp vec3 a = textureGrad(FluidNormalTex, uvA, dFdx(uvA), dFdy(uvA)).xyz;
    highp vec3 b = textureGrad(FluidNormalTex, uvB, dFdx(uvB), dFdy(uvB)).xyz;
    // An unbound texture reads as zeros, which decodes to a -1 normal and a wild slope.
    a = (dot(a, a) < 0.01) ? vec3(0.0, 0.0, 1.0) : a * 2.0 - 1.0;
    b = (dot(b, b) < 0.01) ? vec3(0.0, 0.0, 1.0) : b * 2.0 - 1.0;
    // Match the green flip the engine applies to glTF normal maps.
    a.y = -a.y;
    b.y = -b.y;
    highp vec2 slopeA = a.xy / max(a.z, 0.2);
    highp vec2 slopeB = b.xy / max(b.z, 0.2);
    return (slopeA + slopeB) * 0.5 * WaterWaves0.z;
}

// Screen-space reflection trace. Marches the reflected ray in world space and compares
// NDC depths directly. Returns rgb and a confidence in .a.
highp vec4 WaterTraceScreenSpace(highp vec3 origin, highp vec3 dir, highp vec3 fallback,
                                 highp vec2 distortion, highp float ndcYSign, highp float depthSign)
{
    highp float travelled = 0.0;
    highp float stepLen = WaterReflect2.y;
    highp vec3 prevPos = origin;
    highp vec2 prevUV = vec2(0.0);
    highp float prevDiff = 0.0;
    bool havePrev = false;
    // Not `const`: GLSL requires constant expressions in const initializers, unlike HLSL.
    int stepCount = int(WaterMisc0.x);

    for (int i = 0; i < 128; ++i) {
        if (i >= stepCount) break;
        travelled += stepLen;
        stepLen = min(stepLen * WaterReflect2.z, WaterReflect2.w);
        highp vec3 pos = origin + dir * travelled;
        highp vec4 clip = CameraVP * vec4(pos, 1.0);
        if (clip.w <= 0.001)
            return vec4(fallback, 0.0);
        highp vec2 uv = (clip.xy / clip.w) * vec2(0.5, ndcYSign * 0.5) + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
            return vec4(fallback, 0.0);
        highp ivec2 px = clamp(ivec2(uv * ForwardParams.xy), ivec2(0),
                               ivec2(int(ForwardParams.x) - 1, int(ForwardParams.y) - 1));
        highp float sceneZ = texelFetch(SceneDepthTex, px, 0).r;
        highp float rayZ = clip.z / clip.w;
        if (sceneZ <= 0.0001) {
            prevPos = pos;
            prevUV = uv;
            havePrev = false;
            continue;
        }
        highp float diff = (rayZ - sceneZ) * depthSign;
        if (havePrev && prevDiff > 0.0 && diff <= 0.0) {
            highp vec3 lo = prevPos;
            highp vec3 hi = pos;
            highp vec2 hitUV = uv;
            highp float loDiff = prevDiff;
            for (int k = 0; k < 5; ++k) {
                highp vec3 mid = (lo + hi) * 0.5;
                highp vec4 c2 = CameraVP * vec4(mid, 1.0);
                highp vec2 muv = (c2.xy / c2.w) * vec2(0.5, ndcYSign * 0.5) + 0.5;
                highp ivec2 mpx = clamp(ivec2(muv * ForwardParams.xy), ivec2(0),
                                        ivec2(int(ForwardParams.x) - 1, int(ForwardParams.y) - 1));
                highp float mScene = texelFetch(SceneDepthTex, mpx, 0).r;
                hitUV = muv;
                highp float mDiff = (c2.z / c2.w - mScene) * depthSign;
                if (mDiff > 0.0)
                    lo = mid;
                else
                    hi = mid;
            }
            highp vec2 travel = abs(hitUV - prevUV);
            highp float stepGap = max(travel.x, travel.y);
            highp float stepConfidence = clamp(1.0 - stepGap * 8.0, 0.0, 1.0);
            highp vec2 edge = min(hitUV, 1.0 - hitUV);
            highp float edgeConfidence = clamp(min(edge.x, edge.y) * 24.0, 0.0, 1.0);
            highp float distanceConfidence = clamp(1.0 - travelled / max(WaterReflect2.x, 0.001), 0.0, 1.0);
            highp float confidence = stepConfidence * edgeConfidence * distanceConfidence;
            highp vec2 sampleUV = clamp(hitUV + distortion, 0.0, 1.0);
            highp vec2 texelStep = 1.0 / max(ForwardParams.xy, vec2(1.0));
            highp float blurRadius = WaterReflect1.z + WaterReflect1.w * (1.0 - distanceConfidence);
            highp vec3 reflectedColor = SampleTexture2DLod(SceneColorTex, sampleUV, 0.0).rgb * 0.36;
            reflectedColor += SampleTexture2DLod(SceneColorTex, clamp(sampleUV + vec2(texelStep.x, 0.0) * blurRadius, 0.0, 1.0), 0.0).rgb * 0.16;
            reflectedColor += SampleTexture2DLod(SceneColorTex, clamp(sampleUV - vec2(texelStep.x, 0.0) * blurRadius, 0.0, 1.0), 0.0).rgb * 0.16;
            reflectedColor += SampleTexture2DLod(SceneColorTex, clamp(sampleUV + vec2(0.0, texelStep.y) * blurRadius, 0.0, 1.0), 0.0).rgb * 0.16;
            reflectedColor += SampleTexture2DLod(SceneColorTex, clamp(sampleUV - vec2(0.0, texelStep.y) * blurRadius, 0.0, 1.0), 0.0).rgb * 0.16;
            return vec4(reflectedColor, confidence);
        }
        prevPos = pos;
        prevUV = uv;
        prevDiff = diff;
        havePrev = true;
    }
    return vec4(fallback, 0.0);
}
#endif

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
    highp float lightmap = SampleLightmap();
    colorOut_3 = vec4(EncodeOctahedralNormal(geoNormal), clamp(lightmap, 0.0, 1.0), packedMaterial);
    colorOut_4 = vec4(SampleEmissive(uv), 0.0);
    colorOut_5 = vec4(sheenColor, sheenRoughness);
    colorOut_6 = vec4(dielectricF0, occlusion);
#else
    gl_FragData[0] = vec4(color.rgb, specularWeight);
    gl_FragData[1] = vec4(normal * 0.5 + 0.5, roughness);
    gl_FragData[2] = vec4(metallic, selfShadow, clearcoatFactor, Intensities.w / 255.0);
    highp float packedMaterial = clearcoatRoughness * 0.5 + (MaterialParams.z > 0.5 ? 0.5 : 0.0);
    highp float lightmap = SampleLightmap();
    gl_FragData[3] = vec4(EncodeOctahedralNormal(geoNormal), clamp(lightmap, 0.0, 1.0), packedMaterial);
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

#ifdef FLUID_SURFACE
    // Solver surface: height (m), foam, slopeX, slopeZ. The slopes are height
    // differences in metres across one cell, so dividing by the spacing recovers a
    // dimensionless gradient.
    bool fluidOpaque = false;
    highp vec4 fluidSurface = vec4(0.0);
    {
        highp ivec2 fluidSize = textureSize(FluidSurfaceTexture, 0);
        // Bilinear reconstruction with the SAME texel-centre convention as the vertex
        // shader: a plain texelFetch snaps the shading normal to whole texels while the
        // geometry is smooth, which reads as flickering blocks.
        highp vec2 gridPos = clamp(uv, 0.0, 1.0) * vec2(fluidSize) - 0.5;
        highp ivec2 base = clamp(ivec2(floor(gridPos)), ivec2(0),
                                 ivec2(max(fluidSize.x - 2, 0), max(fluidSize.y - 2, 0)));
        highp vec2 frac = clamp(gridPos - vec2(base), 0.0, 1.0);
        highp vec4 c00 = texelFetch(FluidSurfaceTexture, base + ivec2(0, 0), 0);
        highp vec4 c10 = texelFetch(FluidSurfaceTexture, base + ivec2(1, 0), 0);
        highp vec4 c01 = texelFetch(FluidSurfaceTexture, base + ivec2(0, 1), 0);
        highp vec4 c11 = texelFetch(FluidSurfaceTexture, base + ivec2(1, 1), 0);
        fluidSurface = mix(mix(c00, c10, frac.x), mix(c01, c11, frac.x), frac.y);
    }
    {
        highp float fluidViewDistance = length(CameraPosition.xyz - WorldPos.xyz);
        highp float fluidCellSize = max(WaterMisc1.z, 0.001);
        highp vec2 heightGradient = fluidSurface.zw * (2.0 / fluidCellSize);
        if (WaterMisc0.w > 0.5) {
            heightGradient += WaterRippleGradientCoarse(WorldPos.xz, MaterialParams3.w, fluidViewDistance);
            if (WaterMisc0.y > 0.5)
                heightGradient += WaterNormalMapGradient(uv, MaterialParams3.w);
        }
        heightGradient *= max(WaterMisc1.y, 0.0);
        // A pool wave is never steeper than a few degrees; an unbounded gradient feeds a
        // wildly wrong normal into specular and reflection, which flashes.
        heightGradient = clamp(heightGradient, -4.0, 4.0);
        normal = normalize(vec3(-heightGradient.x, 1.0, -heightGradient.y));
    }
#endif

#ifdef FORWARD_PASS
    if (ForwardParams.z > 0.5 && ForwardParams.x > 0.0 && ForwardParams.y > 0.0) {
        highp float sceneDepth = LoadForwardSceneDepth();
        highp float meshDepth = Pos.z / Pos.w;
        const highp float depthEpsilon = 0.000001;
        if (sceneDepth > 0.0001 && meshDepth < sceneDepth - depthEpsilon)
            discard;
    }
    #endif

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
    indirectLight += albedo * kDiffuseEnv * SampleLightmap();
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
#ifdef FORWARD_PASS
    if (MaterialParams2.z > 0.5 && transmission > 0.001 && MaterialParams2.y > 0.0 && ForwardParams.x > 0.0 && ForwardParams.y > 0.0) {
        highp vec2 screenUV = gl_FragCoord.xy / ForwardParams.xy;
        highp float iorOffset = clamp(abs(ForwardParams.w - 1.0), 0.0, 1.0);
        highp vec2 refractUV = clamp(screenUV + normal.xy * MaterialParams2.y * transmission * (0.5 + iorOffset), vec2(0.0), vec2(1.0));
        highp vec3 sceneColor = SampleTexture2D(SceneColorTex, refractUV).rgb;
        finalColor = mix(finalColor, sceneColor, transmission);
    }
#endif
#ifdef FLUID_SURFACE
    // Crysis-style forward water: a dielectric sheet over a shallow body. Refraction,
    // Beer-Lambert absorption, sky reflection and a sun glint are composited by hand,
    // so the result is written fully opaque.
    {
        // Fresnel from the macro normal, not the rippled one: a rough surface's Fresnel is
        // an average over its microfacets. Half-way toward the solver's slope so crests
        // change reflectivity without a grazing facet spiking it.
        highp float fluidCellSize = max(WaterMisc1.z, 0.001);
        highp vec2 solverGradient = fluidSurface.zw * (2.0 / fluidCellSize) * max(WaterMisc1.y, 0.0);
        solverGradient = clamp(solverGradient, -4.0, 4.0);
        highp vec3 fresnelNormalSolver = normalize(vec3(-solverGradient.x, 1.0, -solverGradient.y));
        highp vec3 fresnelNormal = normalize(mix(vec3(0.0, 1.0, 0.0), fresnelNormalSolver, 0.5));
        highp float nDotEye = clamp(dot(fresnelNormal, eyeDir), 0.0, 1.0);
        highp float fluidFresnel = clamp((0.02 + 0.98 * pow(1.0 - nDotEye, 5.0)) *
                                         WaterOptics4.x + WaterOptics4.y, 0.0, 1.0);

        // Optical path length from the SIMULATED column: an analytic camera-height formula
        // made absorption camera-dependent and slid dark patches across the pool.
        highp float fluidViewDistance = length(CameraPosition.xyz - WorldPos.xyz);
        highp float rawHeight = fluidSurface.x;
        highp float columnDepth;
        if (WaterOptics5.w > 0.5) {
            highp ivec2 floorSize = textureSize(FluidFloorTexture, 0);
            // Nearest texel on purpose: the floor is DISCONTINUOUS at every step edge, so
            // filtering invents a ramp that tints the step face as if it were deep water.
            highp vec2 floorGrid = clamp(uv, 0.0, 1.0) * vec2(floorSize);
            highp ivec2 floorCell = clamp(ivec2(floorGrid), ivec2(0),
                                          ivec2(max(floorSize.x - 1, 0), max(floorSize.y - 1, 0)));
            highp float floorY = texelFetch(FluidFloorTexture, floorCell, 0).x;
            columnDepth = max(WaterOptics5.z + rawHeight - floorY, 0.02);
        } else {
            columnDepth = max(WaterOptics5.y + rawHeight, 0.02);
        }
        highp float cosTheta = max(abs(eyeDir.y), 0.25);
        highp float thickness = clamp(columnDepth / cosTheta, 0.0, 8.0);

        // Refraction, faded out towards the basin border so the deck and coping never
        // smear into the water.
        highp float fluidTime = MaterialParams3.w;
        highp vec2 borderDistance = min(uv, 1.0 - uv);
        highp float borderFade = clamp(min(borderDistance.x, borderDistance.y) /
                                       max(WaterOptics2.w, 0.0001), 0.0, 1.0);
        highp vec2 screenUV = gl_FragCoord.xy / ForwardParams.xy;
        highp vec2 refractOffset = vec2(normal.x, -normal.z) * WaterOptics1.w * borderFade *
                                   (0.6 + 0.4 * clamp(thickness / 3.0, 0.0, 1.0));
        highp vec2 refractUV = clamp(screenUV + refractOffset, 0.0, 1.0);
        // Depth-test the refracted lookup: without it the ball above the water and the
        // coping smear their colour into the surface.
        if (SceneIsNearerAt(refractUV))
            refractUV = screenUV;
        highp vec3 floorColor = SampleTexture2DLod(SceneColorTex, refractUV, 0.0).rgb;

        // Caustics from the SIMULATED surface slope, not from procedural octaves: a flat
        // patch focuses light, a curved one spreads it.
        highp vec2 fluidSlope = fluidSurface.zw * (2.0 / max(WaterMisc1.z, 0.001));
        highp float rippleSlope = length(fluidSlope);
        highp float causticWeb = pow(clamp(1.0 - rippleSlope * WaterOptics3.z, 0.0, 1.0), WaterOptics3.y);
        highp vec3 floorLit = floorColor * (WaterOptics3.w + WaterOptics3.x * causticWeb);

        // Beer-Lambert absorption: red dies first and blue survives, which turns deep water
        // cyan. The scattered body term fills in the loss.
        highp vec3 transmittance = exp(-WaterOptics0.xyz * thickness);
        highp vec3 bodyColor = mix(WaterOptics1.xyz, WaterOptics2.xyz,
                                   clamp(thickness / max(WaterOptics0.w, 0.01), 0.0, 1.0));
        highp vec3 waterColor = floorLit * transmittance + bodyColor * (1.0 - transmittance);

        // Volumetric scattering: the sun's light that entered the water and left it again
        // towards the eye. Without it dusk water looks like flat tinted glass.
        highp vec3 fluidSunDir = normalize(Light0Direction.xyz);
        highp float sunWrap = clamp(dot(eyeDir, -fluidSunDir) * 0.5 + 0.5, 0.0, 1.0);
        highp vec3 sunTint = LightColors[0].xyz * LightColors[0].w;
        highp vec3 scatterVolume = sunTint * (WaterOptics4.z * sunWrap +
                                              WaterOptics4.w * pow(clamp(dot(normal, -fluidSunDir), 0.0, 1.0), 2.0));
        waterColor += scatterVolume * (1.0 - exp(-thickness * WaterOptics5.x));

        highp vec3 reflected = reflect(-eyeDir, normal);
        highp vec3 reflectRay = normalize(vec3(reflected.x, max(reflected.y, 0.01), reflected.z));
        reflected.x = -reflected.x;
        reflected.z = -reflected.z;
        highp float roughMip = clamp(0.04 * iblMaxMip, 0.0, iblMaxMip);
        highp vec3 reflection = SampleCubeLod(texIBLSpecular, reflected, roughMip) *
                                iblFactor * WaterReflect0.x;
        reflection += directLight * WaterReflect0.y;
        // Screen-space reflections pick up what a cubemap cannot: the deck, the house, and
        // anything else within the reflected ray's reach. Where the trace fails the IBL
        // sky stands in.
        if (WaterMisc0.z > 0.5) {
            highp vec2 distortion = vec2(normal.x, -normal.z) * WaterReflect1.y;
            highp vec4 clipSelf = CameraVP * vec4(WorldPos.xyz, 1.0);
            highp vec2 clipNdc = clipSelf.xy / clipSelf.w;
            highp vec2 screenUVOwn = gl_FragCoord.xy / ForwardParams.xy;
            // Take both candidate conversions of THIS fragment's clip position and keep the
            // one that reproduces its actual screen position: exact and backend-independent.
            highp vec2 uvMinus = clipNdc * vec2(0.5, -0.5) + 0.5;
            highp vec2 uvPlus = clipNdc * vec2(0.5, 0.5) + 0.5;
            highp float ndcYSign = (length(uvMinus - screenUVOwn) <= length(uvPlus - screenUVOwn))
                                   ? -1.0 : 1.0;
            highp ivec2 selfPx = clamp(ivec2(gl_FragCoord.xy), ivec2(0),
                                       ivec2(int(ForwardParams.x) - 1, int(ForwardParams.y) - 1));
            highp float selfSceneZ = texelFetch(SceneDepthTex, selfPx, 0).r;
            highp float depthSign = (clipSelf.z / clipSelf.w > selfSceneZ) ? 1.0 : -1.0;
            highp vec4 ssr = WaterTraceScreenSpace(WorldPos.xyz + normal * 0.002, reflectRay,
                                                   reflection, distortion, ndcYSign, depthSign);
            reflection = mix(reflection, ssr.rgb, ssr.a * WaterReflect1.x);
        }
        if (numLights > 0 && LightPositions[0].w < 0.5) {
            highp vec3 sunDir = normalize(-LightPositions[0].xyz);
            highp vec3 halfVec = normalize(eyeDir + sunDir);
            highp float sunGlint = pow(clamp(dot(normal, halfVec), 0.0, 1.0), WaterReflect0.z) * LightColors[0].w;
            reflection += LightColors[0].xyz * sunGlint * WaterReflect0.w;
        }

        finalColor = mix(waterColor, reflection, fluidFresnel);

        // Foam only where the water meets the basin walls and steps: gating by the border
        // distance rather than by depth keeps it off the open surface.
        highp float shoreFoam = clamp(1.0 - borderFade * WaterFoam0.y, 0.0, 1.0);
        highp float foam = clamp((clamp(fluidSurface.y, 0.0, 1.0) * WaterFoam0.x + shoreFoam) *
                                 WaterFoamNoise(WorldPos.xz, fluidTime, fluidViewDistance) *
                                 WaterFoam0.z, 0.0, 1.0);
        finalColor = mix(finalColor, WaterFoam1.rgb, foam * WaterFoam0.w);
        fluidOpaque = true;
    }
#endif
    finalColor += emissive;

    highp float alpha = color.a;
    if (transmission > 0.0 && alpha >= 0.999)
        alpha = clamp(1.0 - transmission, 0.0, 1.0);
    if (AlphaParams.x < 1.5 && transmission <= 0.0)
        alpha = 1.0;
#ifdef FLUID_SURFACE
    // The water composite already contains the background, so it must replace rather
    // than blend.
    if (fluidOpaque) alpha = 1.0;
#endif
#ifdef ES_30
    colorOut = vec4(finalColor, alpha);
#else
    gl_FragColor = vec4(finalColor, alpha);
#endif
}
#endif
