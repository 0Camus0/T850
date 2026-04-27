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
    float4   LightPositions[128];
    float4   LightColors[128];
    float4   LightRadius[32];
}

Texture2D TextureRGB : register(t0);

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

SamplerState SS;

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
    float4 Pos       : TEXCOORD1;
    float4 WorldPos  : TEXCOORD2;
};

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

float2 GetUV0(VS_OUTPUT input)
{
#ifdef USE_TEXCOORD0
    return input.texture0;
#elif defined(USE_TEXCOORD1)
    return input.texture1;
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

float2 GetTexCoord(VS_OUTPUT input, float texCoordSet)
{
    return texCoordSet > 0.5f ? GetUV1(input) : GetUV0(input);
}

float4 SampleBaseColor(float2 uv)
{
#if defined(DIFFUSE_MAP) && (defined(USE_TEXCOORD0) || defined(USE_TEXCOORD1))
    float4 color = TextureRGB.Sample(SS, uv);
    #ifdef GLTF_TANGENT_SPACE
    color *= DiffuseColor;
    #endif
    return color;
#else
    return DiffuseColor;
#endif
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
                  out float metallic, out float roughness, out float selfShadow, out float2 uv)
{
    color = float4(0.5f, 0.5f, 0.5f, 1.0f);
    metallic = PBRParams.x;
    roughness = PBRParams.y;
    selfShadow = 1.0f;
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

    float currentDepthMapValue = TextureHeight.SampleGrad(SS, uv, ddx(uv), ddy(uv)).r;
    float currentRayZ = 1.0f - layerDepth;
    float prevRayZ = 1.0f - layerDepth;
    [loop] while (currentRayZ > currentDepthMapValue) {
        currentDepthMapValue = TextureHeight.SampleGrad(SS, uv, ddx(input.texture0), ddy(input.texture0)).r;
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
    float2 normalUV = TexCoordSets.y > 0.5f ? GetTexCoord(input, TexCoordSets.y) : uv;
    float3 normalTex = TextureNormal.Sample(SS, normalUV).xyz;
    normalTex = normalTex * float3(2.0f, 2.0f, 2.0f) - float3(1.0f, 1.0f, 1.0f);
    normalTex = normalize(normalTex);
    #ifndef GLTF_TANGENT_SPACE
    normalTex.g = -normalTex.g;
    #endif
    normal = mul(normalTex, TBN);
    normal = normalize(normal);
#endif

#ifdef METALLIC_MAP
    float2 metallicUV = TexCoordSets.z > 0.5f ? GetTexCoord(input, TexCoordSets.z) : uv;
    float4 mrSample = TextureMetallic.Sample(SS, metallicUV);
    metallic = PBRParams.x * mrSample.b;
    roughness = PBRParams.y * mrSample.g;
#elif defined(GLOSS_MAP)
    roughness = TextureGloss.Sample(SS, uv).r;
#endif
    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic = clamp(metallic, 0.0f, 1.0f);

#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX) && defined(USE_TEXCOORD0)
    float2 ssDxx = ddx(input.texture0);
    float2 ssDyy = ddy(input.texture0);
    float ssStartZ = TextureHeight.SampleGrad(SS, uv, ssDxx, ssDyy).r;
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
                    float h = TextureHeight.SampleGrad(SS, currentUV, ssDxx, ssDyy).r;
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
    float2 uv;
    BuildSurface(input, color, normal, geoNormal, metallic, roughness, selfShadow, uv);

    FS_OUT fout;
    fout.color0.rgb = color.rgb;
    fout.color0.a = 0.0f;
    fout.color1.rgb = normal * 0.5f + 0.5f;
    fout.color1.a = roughness;
    fout.color2.r = metallic;
    fout.color2.g = selfShadow;
    fout.color2.b = 0.0f;
    fout.color2.a = Intensities.w / 255.0f;
    fout.color3 = float4(geoNormal * 0.5f + 0.5f, 0.0f);
    fout.depth = input.Pos.z / input.Pos.w;
    fout.color4 = float4(input.Pos.z / input.Pos.w, 0.0f, 0.0f, 0.0f);
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
    float2 uv;
    BuildSurface(input, color, normal, geoNormal, metallic, roughness, selfShadow, uv);

    if (ForwardParams.z > 0.5f && ForwardParams.x > 0.0f && ForwardParams.y > 0.0f) {
        float2 screenUV = input.hposition.xy / ForwardParams.xy;
        float sceneDepth = SceneDepthTex.Sample(SS, screenUV).r;
        float meshDepth = input.Pos.z / input.Pos.w;
        if (sceneDepth > 0.0001f && meshDepth < sceneDepth - 0.00001f)
            discard;
    }

    float3 albedo = pow(max(color.rgb, float3(0.0f, 0.0f, 0.0f)), float3(2.2f, 2.2f, 2.2f));
    float3 eyeDir = normalize(CameraPosition.xyz - input.WorldPos.xyz);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 directLight = float3(0.0f, 0.0f, 0.0f);
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
            directLight += (specular + Kd * diffuse) * saturate(dot(geoNormal, lightDir));
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
                directLight += (specular * attenuation + attenuation * Kd * diffuse) * saturate(dot(geoNormal, lightDir));
            }
        }
    }

    float3 finalColor = directLight * selfShadow;
    float3 reflectedVec = reflect(-eyeDir, normal);
    reflectedVec.x = -reflectedVec.x;
    reflectedVec.z = -reflectedVec.z;
    float3 kSpecular = clamp(fresnelSchlickRoughness(max(dot(normal, eyeDir), 0.0f), F0, roughness), 0.0f, 1.0f);
    float3 kDiffuseEnv = (float3(1.0f, 1.0f, 1.0f) - kSpecular) * (1.0f - metallic);
    float3 envSpec = texEnv.SampleLevel(SS, reflectedVec, roughness * 4.0f).xyz;
    float envAtten = (1.0f - roughness) * (1.0f - roughness);
    finalColor += envSpec * kSpecular * envAtten;
    float3 irradianceDir = normal;
    irradianceDir.x = -irradianceDir.x;
    irradianceDir.z = -irradianceDir.z;
    float3 irradiance = texEnv.SampleLevel(SS, irradianceDir, 6.0f).xyz;
    finalColor += irradiance * albedo * kDiffuseEnv;
    finalColor += albedo * Ambient.rgb * kDiffuseEnv;

    float3 emissive = EmissiveColor.rgb;
#ifdef EMISSIVE_MAP
    float2 emissiveUV = TexCoordSets.w > 0.5f ? GetTexCoord(input, TexCoordSets.w) : uv;
    emissive *= EmissiveTex.Sample(SS, emissiveUV).rgb;
#endif
    finalColor += emissive;

    float alpha = color.a;
    if (AlphaParams.w > 0.0f && alpha >= 0.999f)
        alpha = saturate(1.0f - AlphaParams.w * 0.65f);
    if (AlphaParams.x < 1.5f && AlphaParams.w <= 0.0f)
        alpha = 1.0f;
    return float4(finalColor, alpha);
}
#endif
