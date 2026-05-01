cbuffer ConstantBuffer{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldView;
    float4   LightPos;
    float4   LightColor;
    float4   CameraPosition;
    float4   CameraInfo;
    float4   Ambient;
    float4   DiffuseColor;
    float4   SpecularColor;
    float4   PBRParams;
    float4   Intensities;
    float4   ParallaxSettings;
    float4   ParallaxShadowSettings;
    float4   Light0Direction;
    float4   EmissiveColor;
    float4   AlphaParams;
    float4   ForwardParams;
    float4   TexCoordSets;
    float4   MaterialParams;
    float4   MaterialParams2;
    float4   MaterialParams3;
    float4   MaterialParams4;
    float4   MaterialParams5;
    float4   MaterialParams6;
    float4   MaterialParams7;
    float4   MaterialParams8;
    float4   MaterialParams9;
    float4   BaseColorUVTransform0;
    float4   BaseColorUVTransform1;
    float4   NormalUVTransform0;
    float4   NormalUVTransform1;
    float4   MetallicUVTransform0;
    float4   MetallicUVTransform1;
    float4   EmissiveUVTransform0;
    float4   EmissiveUVTransform1;
    float4   SheenColorUVTransform0;
    float4   SheenColorUVTransform1;
    float4   SheenRoughnessUVTransform0;
    float4   SheenRoughnessUVTransform1;
    float4   ClearcoatUVTransform0;
    float4   ClearcoatUVTransform1;
    float4   ClearcoatRoughnessUVTransform0;
    float4   ClearcoatRoughnessUVTransform1;
    float4   OcclusionUVTransform0;
    float4   OcclusionUVTransform1;
    float4   SpecularFactorUVTransform0;
    float4   SpecularFactorUVTransform1;
    float4   SpecularColorUVTransform0;
    float4   SpecularColorUVTransform1;
    float4   TransmissionUVTransform0;
    float4   TransmissionUVTransform1;
    float4   LightPositions[128];
    float4   LightColors[128];
    float4   LightRadius[32];
}

#ifdef DIFFUSE_MAP
Texture2D TextureRGB : register(t0);
#endif

#ifdef SPECULAR_MAP
Texture2D TextureSpecular : register(t1);
#endif

#ifdef GLOSS_MAP
Texture2D TextureGloss : register(t2);
#endif

#ifdef NORMAL_MAP
Texture2D TextureNormal : register(t3);
#endif

TextureCube texEnv : register(t4);

#ifdef HEIGHT_MAP
Texture2D TextureHeight : register(t5);
#endif

#ifdef METALLIC_MAP
Texture2D TextureMetallic : register(t6);
#endif

Texture2D SceneDepthTex : register(t7);

#ifdef EMISSIVE_MAP
Texture2D EmissiveTex : register(t8);
#endif

Texture2D SceneColorTex : register(t9);
TextureCube texIBLDiffuse : register(t10);
TextureCube texIBLSpecular : register(t11);
Texture2D texIBLBRDF : register(t12);
TextureCube texIBLCharlie : register(t13);
Texture2D texIBLCharlieLUT : register(t14);
Texture2D texIBLSheenELUT : register(t15);

#ifdef SHEEN_COLOR_MAP
Texture2D SheenColorTex : register(t16);
#endif

#ifdef SHEEN_ROUGHNESS_MAP
Texture2D SheenRoughnessTex : register(t17);
#endif

#ifdef CLEARCOAT_MAP
Texture2D ClearcoatTex : register(t18);
#endif

#ifdef CLEARCOAT_ROUGHNESS_MAP
Texture2D ClearcoatRoughnessTex : register(t19);
#endif

#ifdef OCCLUSION_MAP
Texture2D OcclusionTex : register(t20);
#endif

#ifdef SPECULAR_FACTOR_MAP
Texture2D SpecularFactorTex : register(t21);
#endif

#ifdef SPECULAR_COLOR_MAP
Texture2D SpecularColorTex : register(t22);
#endif

#ifdef TRANSMISSION_MAP
Texture2D TransmissionTex : register(t23);
#endif

SamplerState MaterialSS : register(s0);
SamplerState ClampSS : register(s1);

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
#ifdef USE_NORMALS
    float4 hnormal   : NORMAL;
#endif
#ifdef USE_TANGENTS
    float4 htangent  : TANGENT;
#endif
#ifdef USE_BINORMALS
    float4 hbinormal : BINORMAL;
#endif
#ifdef USE_TEXCOORD0
    float2 texture0  : TEXCOORD0;
#endif
#ifdef USE_TEXCOORD1
    float2 texture1  : TEXCOORD3;
#endif
#ifdef USE_TEXCOORD2
    float2 texture2  : TEXCOORD4;
#endif
#ifdef USE_TEXCOORD3
    float2 texture3  : TEXCOORD5;
#endif
    float4 Pos       : TEXCOORD1;
    float4 WorldPos  : TEXCOORD2;
};

float2 GetForwardScreenUV(VS_OUTPUT input)
{
    float2 screenUV = input.hposition.xy / ForwardParams.xy;
    return screenUV;
}

float2 SignNotZero(float2 value)
{
    return float2(value.x >= 0.0f ? 1.0f : -1.0f,
                  value.y >= 0.0f ? 1.0f : -1.0f);
}

float2 EncodeOctahedralNormal(float3 normal)
{
    normal = normalize(normal);
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.000001f);
    if (normal.z < 0.0f) {
        normal.xy = (1.0f - abs(normal.yx)) * SignNotZero(normal.xy);
    }
    return normal.xy * 0.5f + 0.5f;
}

float3 NormalDistribution(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = 3.1415926f * denom * denom;
    float res = a2 / max(denom, 0.0001f);
    return float3(res, res, res);
}

float3 FresnelCalc(float VdotH, float3 specColor)
{
    return specColor + (1.0f - specColor) * pow(1.0f - VdotH, 5.0f);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 IBLGGXFresnel(float NdotV, float roughness, float3 F0, float2 brdfSample)
{
    float3 kSpecular = fresnelSchlickRoughness(NdotV, F0, roughness);
    float3 singleScatter = kSpecular * brdfSample.x + brdfSample.y;
    float energyMiss = saturate(1.0f - brdfSample.x - brdfSample.y);
    float3 averageFresnel = F0 + (1.0f - F0) / 21.0f;
    float3 multiScatter = energyMiss * singleScatter * averageFresnel / max(float3(1.0f, 1.0f, 1.0f) - averageFresnel * energyMiss, float3(0.001f, 0.001f, 0.001f));
    return max(singleScatter + multiScatter, float3(0.0f, 0.0f, 0.0f));
}

float GeometrySchlickGGX(float Ndot, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return clamp(Ndot / (Ndot * (1.0f - k) + k), 0.0f, 1.0f);
}

float3 GeometricShadowing(float NdotL, float NdotV, float roughness)
{
    float res = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
    return float3(clamp(res, 0.0f, 1.0f), clamp(res, 0.0f, 1.0f), clamp(res, 0.0f, 1.0f));
}

float3 CalculateSpecular(float3 specularColor, float3 normal, float3 view, float3 halfvector, float3 light, float roughness)
{
    float NdotH = max(dot(normal, halfvector), 0.0f);
    float VdotH = clamp(dot(view, halfvector), 0.0f, 1.0f);
    float NdotL = clamp(dot(normal, light), 0.0f, 1.0f);
    float NdotV = clamp(dot(normal, view), 0.0f, 1.0f);
    float3 numerator = FresnelCalc(VdotH, specularColor) * NormalDistribution(NdotH, roughness) * GeometricShadowing(NdotL, NdotV, roughness);
    return numerator / (4.0f * (NdotL * NdotV) + 0.01f);
}

float3 CalculateDiffuse(float3 albedoColor, float3 normal, float3 light)
{
    return albedoColor * clamp(dot(normal, light), 0.0f, 1.0f);
}

float Max3(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

float LambdaSheenNumericHelper(float x, float alphaG)
{
    float oneMinusAlphaSq = (1.0f - alphaG) * (1.0f - alphaG);
    float a = lerp(21.5473f, 25.3245f, oneMinusAlphaSq);
    float b = lerp(3.82987f, 3.32435f, oneMinusAlphaSq);
    float c = lerp(0.19823f, 0.16801f, oneMinusAlphaSq);
    float d = lerp(-1.97760f, -1.27393f, oneMinusAlphaSq);
    float e = lerp(-4.32054f, -4.85967f, oneMinusAlphaSq);
    return a / (1.0f + b * pow(x, c)) + d * x + e;
}

float LambdaSheen(float cosTheta, float alphaG)
{
    if (abs(cosTheta) < 0.5f)
        return exp(LambdaSheenNumericHelper(cosTheta, alphaG));
    return exp(2.0f * LambdaSheenNumericHelper(0.5f, alphaG) - LambdaSheenNumericHelper(1.0f - cosTheta, alphaG));
}

float VisibilitySheen(float NdotL, float NdotV, float sheenRoughness)
{
    sheenRoughness = max(sheenRoughness, 0.000001f);
    float alphaG = sheenRoughness * sheenRoughness;
    float denom = max((1.0f + LambdaSheen(NdotV, alphaG) + LambdaSheen(NdotL, alphaG)) * (4.0f * NdotV * NdotL), 0.000001f);
    return clamp(1.0f / denom, 0.0f, 1.0f);
}

float DistributionCharlie(float sheenRoughness, float NdotH)
{
    sheenRoughness = max(sheenRoughness, 0.000001f);
    float alphaG = sheenRoughness * sheenRoughness;
    float invR = 1.0f / alphaG;
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0f - cos2h, 0.0f);
    return (2.0f + invR) * pow(sin2h, invR * 0.5f) / (2.0f * 3.1415926f);
}

float3 BRDFSpecularSheen(float3 sheenColor, float sheenRoughness, float NdotL, float NdotV, float NdotH)
{
    return sheenColor * DistributionCharlie(sheenRoughness, NdotH) * VisibilitySheen(NdotL, NdotV, sheenRoughness);
}

float AlbedoSheenScalingLUT(float NdotV, float sheenRoughness)
{
    return texIBLSheenELUT.SampleLevel(ClampSS, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).r;
}

float3 CalculateSheenRadiance(float3 sheenColor, float sheenRoughness, float3 lightColor, float intensity, float NdotL, float NdotV, float NdotH)
{
    return lightColor * intensity * NdotL * BRDFSpecularSheen(sheenColor, sheenRoughness, NdotL, NdotV, NdotH);
}

float3 GetIBLRadianceCharlie(float3 normal, float3 viewDir, float sheenRoughness, float3 sheenColor, float iblMaxMip)
{
    float NdotV = max(dot(normal, viewDir), 0.0f);
    float lod = sheenRoughness * iblMaxMip;
    float3 reflectedVec = reflect(-viewDir, normal);
    reflectedVec.x = -reflectedVec.x;
    reflectedVec.z = -reflectedVec.z;
    float brdf = texIBLCharlieLUT.SampleLevel(ClampSS, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).b;
    float3 sheenLight = texIBLCharlie.SampleLevel(ClampSS, reflectedVec, lod).rgb;
    return sheenLight * sheenColor * brdf;
}

float2 GetUV0(VS_OUTPUT input)
{
#ifdef USE_TEXCOORD0
    return input.texture0;
#elif defined(USE_TEXCOORD1)
    return input.texture1;
#elif defined(USE_TEXCOORD2)
    return input.texture2;
#elif defined(USE_TEXCOORD3)
    return input.texture3;
#else
    return float2(0.0f, 0.0f);
#endif
}

float2 GetUV1(VS_OUTPUT input)
{
#ifdef USE_TEXCOORD1
    return input.texture1;
#elif defined(USE_TEXCOORD0)
    return input.texture0;
#else
    return float2(0.0f, 0.0f);
#endif
}

float2 GetUV2(VS_OUTPUT input)
{
#ifdef USE_TEXCOORD2
    return input.texture2;
#elif defined(USE_TEXCOORD0)
    return input.texture0;
#else
    return float2(0.0f, 0.0f);
#endif
}

float2 GetUV3(VS_OUTPUT input)
{
#ifdef USE_TEXCOORD3
    return input.texture3;
#elif defined(USE_TEXCOORD0)
    return input.texture0;
#else
    return float2(0.0f, 0.0f);
#endif
}

float2 GetTexCoord(VS_OUTPUT input, float texCoordSet)
{
    int s = int(texCoordSet + 0.5f);
    if (s == 1) return GetUV1(input);
    if (s == 2) return GetUV2(input);
    if (s == 3) return GetUV3(input);
    return GetUV0(input);
}

// Returns true if the per-map UV set matches the base-color UV set, in
// which case parallax-perturbed `uv` should be reused instead of the
// raw vertex UV.
bool MapShareBaseSet(float mapSet)
{
    return abs(mapSet - TexCoordSets.x) < 0.5f;
}

float2 ApplyUVTransform(float2 uv, float4 row0, float4 row1)
{
    return float2(dot(row0.xy, uv) + row0.z, dot(row1.xy, uv) + row1.z);
}

float3 LinearToStoredAlbedo(float3 linearColor)
{
    return pow(saturate(linearColor), float3(1.0f / 2.2f, 1.0f / 2.2f, 1.0f / 2.2f));
}

float3 StoredSRGBToLinear(float3 storedColor)
{
    return pow(max(storedColor, float3(0.0f, 0.0f, 0.0f)), float3(2.2f, 2.2f, 2.2f));
}

float4 SampleBaseColor(float2 uv)
{
#if defined(DIFFUSE_MAP) && (defined(USE_TEXCOORD0) || defined(USE_TEXCOORD1) || defined(USE_TEXCOORD2) || defined(USE_TEXCOORD3))
    float4 color = TextureRGB.Sample(MaterialSS, ApplyUVTransform(uv, BaseColorUVTransform0, BaseColorUVTransform1));
    #ifdef GLTF_TANGENT_SPACE
    color.rgb *= LinearToStoredAlbedo(DiffuseColor.rgb);
    color.a *= DiffuseColor.a;
    #endif
    return color;
#else
    #ifdef GLTF_TANGENT_SPACE
    return float4(LinearToStoredAlbedo(DiffuseColor.rgb), DiffuseColor.a);
    #else
    return DiffuseColor;
    #endif
#endif
}

float3 SampleEmissive(VS_OUTPUT input, float2 uv)
{
    float3 emissive = EmissiveColor.rgb;
#ifdef EMISSIVE_MAP
    float2 emissiveUV = MapShareBaseSet(TexCoordSets.w) ? uv : GetTexCoord(input, TexCoordSets.w);
    emissiveUV = ApplyUVTransform(emissiveUV, EmissiveUVTransform0, EmissiveUVTransform1);
    emissive *= EmissiveTex.Sample(MaterialSS, emissiveUV).rgb;
#endif
    return emissive * MaterialParams.w;
}

void ApplyAlphaMask(inout float4 color)
{
    if (AlphaParams.x > 0.5f && AlphaParams.x < 1.5f) {
        if (color.a < AlphaParams.y)
            discard;
        color.a = 1.0f;
    }
}

float GetPackedLightRadius(int i)
{
    float4 pack = LightRadius[i >> 2];
    int c = i & 3;
    return c == 0 ? pack.x : (c == 1 ? pack.y : (c == 2 ? pack.z : pack.w));
}

void BuildSurface(VS_OUTPUT input, out float4 color, out float3 normal, out float3 geoNormal,
                  out float metallic, out float roughness, out float selfShadow, out float2 uv,
                  out float3 sheenColor, out float sheenRoughness,
                  out float clearcoatFactor, out float clearcoatRoughness,
                  out float occlusion, out float3 dielectricF0, out float specularWeight,
                  out float transmissionFactor)
{
    color = float4(0.5f, 0.5f, 0.5f, 1.0f);
    metallic = PBRParams.x;
    roughness = PBRParams.y;
    selfShadow = 1.0f;
    occlusion = 1.0f;
    dielectricF0 = max(SpecularColor.rgb, float3(0.0f, 0.0f, 0.0f));
    specularWeight = max(SpecularColor.w, 0.0f);
    transmissionFactor = saturate(AlphaParams.w);
    sheenColor = saturate(MaterialParams4.rgb);
    sheenRoughness = clamp(MaterialParams4.w, 0.0f, 1.0f);
    clearcoatFactor = saturate(MaterialParams.x);
    clearcoatRoughness = saturate(MaterialParams.y);
    uv = GetTexCoord(input, TexCoordSets.x);

#ifdef USE_NORMALS
    normal = normalize(input.hnormal.xyz);
#else
    normal = float3(0.0f, 0.0f, 1.0f);
#endif
    geoNormal = normal;

#if defined(HEIGHT_MAP) || defined(NORMAL_MAP)
    float3 tangent = normalize(input.htangent.xyz);
    float3 binormal = normalize(input.hbinormal.xyz);
    float3x3 TBN = float3x3(tangent, binormal, normal);
#endif

#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    float heightScale = ParallaxSettings.z;
    float3 viewDir = mul(TBN, normalize(CameraPosition.xyz - input.WorldPos.xyz));
    viewDir = normalize(viewDir);
    float minLayers = ParallaxSettings.x;
    float maxLayers = ParallaxSettings.y;
    float numLayers = lerp(maxLayers, minLayers, abs(dot(float3(0.0f, 0.0f, 1.0f), viewDir)));
    float layerDepth = 1.0f / numLayers;
    float prevDepthMapValue = 0.0f;
    float2 P = -viewDir.xy * heightScale / viewDir.z;
    float2 deltaTexCoords = P * layerDepth;
    deltaTexCoords.y = -deltaTexCoords.y;

    float currentDepthMapValue = TextureHeight.SampleGrad(MaterialSS, uv, ddx(uv), ddy(uv)).r;
    float currentRayZ = 1.0f - layerDepth;
    float prevRayZ = 1.0f - layerDepth;
    [loop] while (currentRayZ > currentDepthMapValue) {
        currentDepthMapValue = TextureHeight.SampleGrad(MaterialSS, uv, ddx(input.texture0), ddy(input.texture0)).r;
        prevDepthMapValue = currentDepthMapValue;
        uv += deltaTexCoords;
        prevRayZ = currentRayZ;
        currentRayZ -= layerDepth;
    }
    float2 prevTexCoords = uv - deltaTexCoords;
    float weight = (prevDepthMapValue - prevRayZ) / (prevDepthMapValue - currentDepthMapValue + currentRayZ - prevRayZ);
    uv = prevTexCoords * weight + uv * (1.0f - weight);
#endif

    color = SampleBaseColor(uv);
    ApplyAlphaMask(color);

#ifdef NORMAL_MAP
    float2 normalUV = MapShareBaseSet(TexCoordSets.y) ? uv : GetTexCoord(input, TexCoordSets.y);
    normalUV = ApplyUVTransform(normalUV, NormalUVTransform0, NormalUVTransform1);
    float3 normalTex = TextureNormal.Sample(MaterialSS, normalUV).xyz;
    normalTex = normalTex * float3(2.0f, 2.0f, 2.0f) - float3(1.0f, 1.0f, 1.0f);
    // glTF normalTexture.scale (default 1.0) — stored in MaterialParams9.y.
    // Spec: scale applies to .xy only, .z is reconstructed/normalized after.
    float normalScale = MaterialParams9.y;
    normalTex.xy *= normalScale;
    normalTex = normalize(normalTex);
    #ifndef GLTF_TANGENT_SPACE
    normalTex.g = -normalTex.g;
    #endif
    normal = mul(normalTex, TBN);
    normal = normalize(normal);
#endif

#ifdef METALLIC_MAP
    float2 metallicUV = MapShareBaseSet(TexCoordSets.z) ? uv : GetTexCoord(input, TexCoordSets.z);
    metallicUV = ApplyUVTransform(metallicUV, MetallicUVTransform0, MetallicUVTransform1);
    float4 mrSample = TextureMetallic.Sample(MaterialSS, metallicUV);
    metallic = PBRParams.x * mrSample.b;
    roughness = PBRParams.y * mrSample.g;
#elif defined(GLOSS_MAP)
    roughness = TextureGloss.Sample(MaterialSS, uv).r;
#endif
    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic = clamp(metallic, 0.0f, 1.0f);

#ifdef SPECULAR_FACTOR_MAP
    if (MaterialParams8.y > 0.5f) {
        float2 specularFactorUV = MapShareBaseSet(MaterialParams8.z) ? uv : GetTexCoord(input, MaterialParams8.z);
        specularFactorUV = ApplyUVTransform(specularFactorUV, SpecularFactorUVTransform0, SpecularFactorUVTransform1);
        specularWeight *= SpecularFactorTex.Sample(MaterialSS, specularFactorUV).a;
    }
#endif
#ifdef SPECULAR_COLOR_MAP
    if (MaterialParams8.w > 0.5f) {
        float2 specularColorUV = MapShareBaseSet(MaterialParams9.x) ? uv : GetTexCoord(input, MaterialParams9.x);
        specularColorUV = ApplyUVTransform(specularColorUV, SpecularColorUVTransform0, SpecularColorUVTransform1);
        dielectricF0 = min(dielectricF0 * StoredSRGBToLinear(SpecularColorTex.Sample(MaterialSS, specularColorUV).rgb), float3(1.0f, 1.0f, 1.0f));
    }
#endif
    specularWeight = saturate(specularWeight);

#ifdef OCCLUSION_MAP
    if (MaterialParams7.x > 0.5f) {
        float2 occlusionUV = MapShareBaseSet(MaterialParams7.z) ? uv : GetTexCoord(input, MaterialParams7.z);
        occlusionUV = ApplyUVTransform(occlusionUV, OcclusionUVTransform0, OcclusionUVTransform1);
        float ao = OcclusionTex.Sample(MaterialSS, occlusionUV).r;
        occlusion = saturate(1.0f + MaterialParams7.y * (ao - 1.0f));
    }
#endif

#ifdef TRANSMISSION_MAP
    if (MaterialParams7.w > 0.5f) {
        float2 transmissionUV = MapShareBaseSet(MaterialParams8.x) ? uv : GetTexCoord(input, MaterialParams8.x);
        transmissionUV = ApplyUVTransform(transmissionUV, TransmissionUVTransform0, TransmissionUVTransform1);
        transmissionFactor *= TransmissionTex.Sample(MaterialSS, transmissionUV).r;
    }
#endif
    transmissionFactor = saturate(transmissionFactor);

#ifdef SHEEN_COLOR_MAP
    if (MaterialParams5.x > 0.5f) {
        float2 sheenColorUV = MapShareBaseSet(MaterialParams5.z) ? uv : GetTexCoord(input, MaterialParams5.z);
        sheenColorUV = ApplyUVTransform(sheenColorUV, SheenColorUVTransform0, SheenColorUVTransform1);
        sheenColor *= pow(max(SheenColorTex.Sample(MaterialSS, sheenColorUV).rgb, float3(0.0f, 0.0f, 0.0f)), float3(2.2f, 2.2f, 2.2f));
    }
#endif
#ifdef SHEEN_ROUGHNESS_MAP
    if (MaterialParams5.y > 0.5f) {
        float2 sheenRoughnessUV = MapShareBaseSet(MaterialParams5.w) ? uv : GetTexCoord(input, MaterialParams5.w);
        sheenRoughnessUV = ApplyUVTransform(sheenRoughnessUV, SheenRoughnessUVTransform0, SheenRoughnessUVTransform1);
        sheenRoughness *= SheenRoughnessTex.Sample(MaterialSS, sheenRoughnessUV).a;
    }
#endif
    sheenColor = saturate(sheenColor);
    sheenRoughness = clamp(sheenRoughness, 0.0f, 1.0f);

#ifdef CLEARCOAT_MAP
    if (MaterialParams6.x > 0.5f) {
        float2 clearcoatUV = MapShareBaseSet(MaterialParams6.z) ? uv : GetTexCoord(input, MaterialParams6.z);
        clearcoatUV = ApplyUVTransform(clearcoatUV, ClearcoatUVTransform0, ClearcoatUVTransform1);
        clearcoatFactor *= ClearcoatTex.Sample(MaterialSS, clearcoatUV).r;
    }
#endif
#ifdef CLEARCOAT_ROUGHNESS_MAP
    if (MaterialParams6.y > 0.5f) {
        float2 clearcoatRoughnessUV = MapShareBaseSet(MaterialParams6.w) ? uv : GetTexCoord(input, MaterialParams6.w);
        clearcoatRoughnessUV = ApplyUVTransform(clearcoatRoughnessUV, ClearcoatRoughnessUVTransform0, ClearcoatRoughnessUVTransform1);
        clearcoatRoughness *= ClearcoatRoughnessTex.Sample(MaterialSS, clearcoatRoughnessUV).g;
    }
#endif
    clearcoatFactor = saturate(clearcoatFactor);
    clearcoatRoughness = saturate(clearcoatRoughness);

#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    float2 ssDxx = ddx(input.texture0);
    float2 ssDyy = ddy(input.texture0);
    float ssStartZ = TextureHeight.SampleGrad(MaterialSS, uv, ssDxx, ssDyy).r;
    float shadowStrength = ParallaxShadowSettings.w;
    if (shadowStrength > 0.001f) {
        float lightDirLen = length(Light0Direction.xyz);
        if (lightDirLen > 0.001f) {
            float3 lightDirTS = mul(TBN, normalize(-Light0Direction.xyz));
            lightDirTS = normalize(lightDirTS);
            if (lightDirTS.z > 0.01f) {
                float numLayers = lerp(ParallaxShadowSettings.y, ParallaxShadowSettings.x, abs(lightDirTS.z));
                float layerStep = 1.0f / numLayers;
                float2 P_light = lightDirTS.xy * ParallaxSettings.z / lightDirTS.z;
                float2 deltaUV = P_light * layerStep;
                deltaUV.y = -deltaUV.y;
                float2 currentUV = uv;
                float currentRayZ = ssStartZ;
                [loop] for (int si = 0; si < int(numLayers); si++) {
                    currentRayZ += layerStep;
                    currentUV += deltaUV;
                    if (currentRayZ >= 1.0f) break;
                    float h = TextureHeight.SampleGrad(MaterialSS, currentUV, ssDxx, ssDyy).r;
                    if (h > currentRayZ) {
                        float penumbra = float(si + 1) / numLayers;
                        selfShadow = min(selfShadow, lerp(0.0f, penumbra, ParallaxShadowSettings.z));
                    }
                }
                selfShadow = lerp(1.0f, selfShadow, shadowStrength);
            }
        }
    }
#endif
}

#ifdef SIMPLE_COLOR
float4 FS(VS_OUTPUT input) : SV_TARGET
{
    return float4(0.5f, 0.5f, 0.5f, 1.0f);
}
#elif defined(G_BUFFER_PASS)
struct FS_OUT{
    float4 color0 : SV_TARGET0;
    float4 color1 : SV_TARGET1;
    float4 color2 : SV_TARGET2;
    float4 color3 : SV_TARGET3;
    float4 color4 : SV_TARGET4;
    float4 color5 : SV_TARGET5;
    float4 color6 : SV_TARGET6;
    float  depth  : SV_Depth;
};

FS_OUT FS(VS_OUTPUT input)
{
    float4 color;
    float3 normal;
    float3 geoNormal;
    float metallic;
    float roughness;
    float selfShadow;
    float3 sheenColor;
    float sheenRoughness;
    float clearcoatFactor;
    float clearcoatRoughness;
    float occlusion;
    float3 dielectricF0;
    float specularWeight;
    float transmissionFactor;
    float2 uv;
    BuildSurface(input, color, normal, geoNormal, metallic, roughness, selfShadow, uv, sheenColor, sheenRoughness, clearcoatFactor, clearcoatRoughness, occlusion, dielectricF0, specularWeight, transmissionFactor);

    FS_OUT fout;
    fout.color0.rgb = color.rgb;
    fout.color0.a = specularWeight;
    fout.color1.rgb = normal * 0.5f + 0.5f;
    fout.color1.a = roughness;
    fout.color2.r = metallic;
    fout.color2.g = selfShadow;
    fout.color2.b = clearcoatFactor;
    fout.color2.a = Intensities.w / 255.0f;
    float packedMaterial = clearcoatRoughness * 0.5f + (MaterialParams.z > 0.5f ? 0.5f : 0.0f);
    fout.color3 = float4(EncodeOctahedralNormal(geoNormal), 0.0f, packedMaterial);
    fout.depth = input.Pos.z / input.Pos.w;
    fout.color4 = float4(SampleEmissive(input, uv), 0.0f);
    fout.color5 = float4(sheenColor, sheenRoughness);
    fout.color6 = float4(dielectricF0, occlusion);
    return fout;
}
#elif defined(SHADOW_MAP_PASS)
float FS(VS_OUTPUT input) : SV_Depth
{
    float4 color = SampleBaseColor(GetTexCoord(input, TexCoordSets.x));
    ApplyAlphaMask(color);
    return input.Pos.z / input.Pos.w;
}
#elif defined(DEPTH_PRE_PASS)
float FS(VS_OUTPUT input) : SV_Depth
{
    float4 color = SampleBaseColor(GetTexCoord(input, TexCoordSets.x));
    ApplyAlphaMask(color);
    return input.Pos.z / input.Pos.w;
}
#else
float4 FS(VS_OUTPUT input) : SV_TARGET
{
    float4 color;
    float3 normal;
    float3 geoNormal;
    float metallic;
    float roughness;
    float selfShadow;
    float3 sheenColor;
    float sheenRoughness;
    float clearcoatFactor;
    float clearcoatRoughness;
    float occlusion;
    float3 dielectricF0;
    float specularWeight;
    float transmissionFactor;
    float2 uv;
    BuildSurface(input, color, normal, geoNormal, metallic, roughness, selfShadow, uv, sheenColor, sheenRoughness, clearcoatFactor, clearcoatRoughness, occlusion, dielectricF0, specularWeight, transmissionFactor);
    float3 emissive = SampleEmissive(input, uv);

    if (ForwardParams.z > 0.5f && ForwardParams.x > 0.0f && ForwardParams.y > 0.0f) {
        float2 screenUV = GetForwardScreenUV(input);
        float sceneDepth = SceneDepthTex.Sample(ClampSS, screenUV).r;
        float meshDepth = input.Pos.z / input.Pos.w;
        const float depthEpsilon = 0.000001f;
        if (sceneDepth > 0.0001f && meshDepth < sceneDepth - depthEpsilon)
            discard;
    }

    float3 albedo = pow(max(color.rgb, float3(0.0f, 0.0f, 0.0f)), float3(2.2f, 2.2f, 2.2f));
    float3 eyeDir = normalize(CameraPosition.xyz - input.WorldPos.xyz);
    float3 F0 = lerp(dielectricF0 * specularWeight, albedo, metallic);
    float3 directLight = float3(0.0f, 0.0f, 0.0f);
    float sheenStrength = Max3(sheenColor);
    bool hasSheenLUT = MaterialParams3.y > 0.5f;
    int numLights = (int)CameraInfo.w;

    [loop] for (int i = 0; i < numLights; i++) {
        float lightType = LightPositions[i].w;
        float intensity = LightColors[i].w;
        if (lightType < 0.5f) {
            float3 lightDir = normalize(-LightPositions[i].xyz);
            float3 halfVec = normalize(eyeDir + lightDir);
            float3 diffuse = CalculateDiffuse(albedo, normal, lightDir) * LightColors[i].xyz * intensity;
            float3 specular = CalculateSpecular(F0, normal, eyeDir, halfVec, lightDir, roughness) * LightColors[i].xyz * intensity;
            float3 F = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0f, 1.0f), F0);
            float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
            float NdotL = max(dot(normal, lightDir), 0.0f);
            float NdotVLight = max(dot(normal, eyeDir), 0.0f);
            float NdotH = max(dot(normal, halfVec), 0.0f);
            float albedoSheenScaling = 1.0f;
            float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
            if (hasSheenLUT && sheenStrength > 0.0f) {
                albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
                                         1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
                sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH);
            }
            directLight += (sheenLight + (specular + Kd * diffuse) * albedoSheenScaling) * saturate(dot(geoNormal, lightDir));
        } else {
            float rad = GetPackedLightRadius(i);
            float dist = distance(LightPositions[i].xyz, input.WorldPos.xyz);
            if (dist < rad * 2.0f) {
                float3 lightDir = normalize(LightPositions[i].xyz - input.WorldPos.xyz);
                float3 halfVec = normalize(eyeDir + lightDir);
                float3 diffuse = CalculateDiffuse(albedo, normal, lightDir) * LightColors[i].xyz * intensity;
                float3 specular = CalculateSpecular(F0, normal, eyeDir, halfVec, lightDir, roughness) * LightColors[i].xyz * intensity;
                float3 F = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0f, 1.0f), F0);
                float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
                float d = max(dist - rad, 0.0f);
                float denom = d / max(rad, 0.0001f) + 1.0f;
                float attenuation = 1.0f / (denom * denom);
                attenuation = max((attenuation - 0.8f) / 0.2f, 0.0f);
                float NdotL = max(dot(normal, lightDir), 0.0f);
                float NdotVLight = max(dot(normal, eyeDir), 0.0f);
                float NdotH = max(dot(normal, halfVec), 0.0f);
                float albedoSheenScaling = 1.0f;
                float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
                if (hasSheenLUT && sheenStrength > 0.0f) {
                    albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
                                             1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
                    sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH) * attenuation;
                }
                directLight += (sheenLight + (specular * attenuation + attenuation * Kd * diffuse) * albedoSheenScaling) * saturate(dot(geoNormal, lightDir));
            }
        }
    }

    float3 finalColor = directLight * selfShadow;
    float iblFactor = max(MaterialParams2.w, 0.0f);
    float iblMaxMip = max(MaterialParams3.x, 0.0f);
    bool hasBrdfLUT = MaterialParams3.y > 0.5f;
    float3 reflectedVec = reflect(-eyeDir, normal);
    reflectedVec.x = -reflectedVec.x;
    reflectedVec.z = -reflectedVec.z;
    float NdotV = max(dot(normal, eyeDir), 0.0f);
    float3 kSpecular = clamp(fresnelSchlickRoughness(NdotV, F0, roughness), 0.0f, 1.0f);
    float3 kDiffuseEnv = (float3(1.0f, 1.0f, 1.0f) - kSpecular) * (1.0f - metallic);
    float3 envSpec = texIBLSpecular.SampleLevel(ClampSS, reflectedVec, roughness * iblMaxMip).xyz;
    float envAtten = (1.0f - roughness) * (1.0f - roughness);
    float2 brdfSample = hasBrdfLUT ? texIBLBRDF.SampleLevel(ClampSS, float2(NdotV, roughness), 0.0f).rg : float2(0.0f, 0.0f);
    float3 specularIBL = hasBrdfLUT ? IBLGGXFresnel(NdotV, roughness, F0, brdfSample) : kSpecular * envAtten;
    float3 indirectLight = envSpec * specularIBL * iblFactor;
    float3 irradianceDir = normal;
    irradianceDir.x = -irradianceDir.x;
    irradianceDir.z = -irradianceDir.z;
    float diffuseMip = clamp(MaterialParams3.z, 0.0f, iblMaxMip);
    float3 irradiance = texIBLDiffuse.SampleLevel(ClampSS, irradianceDir, diffuseMip).xyz;
    indirectLight += irradiance * albedo * kDiffuseEnv * iblFactor;
    indirectLight += albedo * Ambient.rgb * kDiffuseEnv;
    if (hasSheenLUT && sheenStrength > 0.0f) {
        float albedoSheenScaling = 1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotV, sheenRoughness);
        float3 sheenIBL = GetIBLRadianceCharlie(normal, eyeDir, sheenRoughness, sheenColor, iblMaxMip) * iblFactor;
        indirectLight = sheenIBL + indirectLight * albedoSheenScaling;
    }
    finalColor += indirectLight * occlusion;

    if (clearcoatFactor > 0.001f) {
        clearcoatRoughness = clamp(clearcoatRoughness, 0.04f, 1.0f);
        float3 clearcoatSpec = texIBLSpecular.SampleLevel(ClampSS, reflectedVec, clearcoatRoughness * iblMaxMip).xyz;
        float clearcoatAtten = hasBrdfLUT ? 1.0f : (1.0f - clearcoatRoughness) * (1.0f - clearcoatRoughness);
        float3 clearcoatF = FresnelCalc(saturate(dot(normal, eyeDir)), float3(0.04f, 0.04f, 0.04f));
        float clearcoatWeight = saturate(clearcoatFactor * max(clearcoatF.x, max(clearcoatF.y, clearcoatF.z)));
        finalColor = lerp(finalColor, clearcoatSpec * clearcoatAtten * iblFactor, clearcoatWeight);
    }

    if (MaterialParams.z > 0.5f) {
        finalColor = albedo;
    }

    float transmission = saturate(transmissionFactor * MaterialParams2.x);
    if (MaterialParams2.z > 0.5f && transmission > 0.001f && MaterialParams2.y > 0.0f && ForwardParams.x > 0.0f && ForwardParams.y > 0.0f) {
        float2 screenUV = GetForwardScreenUV(input);
        float iorOffset = saturate(abs(ForwardParams.w - 1.0f));
        float2 refractUV = saturate(screenUV + normal.xy * MaterialParams2.y * transmission * (0.5f + iorOffset));
        float3 sceneColor = SceneColorTex.Sample(ClampSS, refractUV).rgb;
        finalColor = lerp(finalColor, sceneColor, transmission);
    }
    finalColor += emissive;

    float alpha = color.a;
    if (transmission > 0.0f && alpha >= 0.999f)
        alpha = saturate(1.0f - transmission);
    if (AlphaParams.x < 1.5f && transmission <= 0.0f)
        alpha = 1.0f;
    return float4(finalColor, alpha);
}
#endif
