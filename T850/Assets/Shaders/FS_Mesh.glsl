#ifdef ES_30
precision mediump float;
#endif

uniform mediump sampler2D DiffuseTex;

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

uniform mediump samplerCube texEnv;

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
uniform highp vec4 LightPositions[128];
uniform highp vec4 LightColors[128];
uniform highp vec4 LightRadius[32];

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

highp vec3 SampleCubeLod(mediump samplerCube tex, highp vec3 dir, highp float lod)
{
#ifdef ES_30
    return texture(tex, dir, lod).xyz;
#else
    return textureCube(tex, dir).xyz;
#endif
}

highp vec3 NormalDistribution(highp float NdotH, highp float roughness)
{
    highp float a = roughness * roughness;
    highp float a2 = a * a;
    highp float NdotH2 = NdotH * NdotH;
    highp float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.1415926 * denom * denom;
    highp float res = a2 / max(denom, 0.0001);
    return vec3(res, res, res);
}

highp vec3 FresnelCalc(highp float VdotH, highp vec3 specColor)
{
    return specColor + (vec3(1.0) - specColor) * pow(1.0 - VdotH, 5.0);
}

highp vec3 fresnelSchlickRoughness(highp float cosTheta, highp vec3 F0, highp float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

highp float GeometrySchlickGGX(highp float Ndot, highp float roughness)
{
    highp float r = roughness + 1.0;
    highp float k = (r * r) / 8.0;
    return clamp(Ndot / (Ndot * (1.0 - k) + k), 0.0, 1.0);
}

highp vec3 GeometricShadowing(highp float NdotL, highp float NdotV, highp float roughness)
{
    highp float res = GeometrySchlickGGX(NdotL, roughness) * GeometrySchlickGGX(NdotV, roughness);
    return vec3(clamp(res, 0.0, 1.0));
}

highp vec3 CalculateSpecular(highp vec3 specularColor, highp vec3 normal, highp vec3 view, highp vec3 halfvector, highp vec3 light, highp float roughness)
{
    highp float NdotH = max(dot(normal, halfvector), 0.0);
    highp float VdotH = clamp(dot(view, halfvector), 0.0, 1.0);
    highp float NdotL = clamp(dot(normal, light), 0.0, 1.0);
    highp float NdotV = clamp(dot(normal, view), 0.0, 1.0);
    highp vec3 numerator = FresnelCalc(VdotH, specularColor) * NormalDistribution(NdotH, roughness) * GeometricShadowing(NdotL, NdotV, roughness);
    return numerator / (4.0 * (NdotL * NdotV) + 0.01);
}

highp vec3 CalculateDiffuse(highp vec3 albedoColor, highp vec3 normal, highp vec3 light)
{
    return albedoColor * clamp(dot(normal, light), 0.0, 1.0);
}

highp vec2 GetUV0()
{
#ifdef USE_TEXCOORD0
    return vecUVCoords;
#elif defined(USE_TEXCOORD1)
    return vecUVCoords1;
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

highp vec2 GetTexCoord(highp float texCoordSet)
{
    return texCoordSet > 0.5 ? GetUV1() : GetUV0();
}

highp vec4 SampleBaseColor(highp vec2 uv)
{
#if defined(DIFFUSE_MAP) && (defined(USE_TEXCOORD0) || defined(USE_TEXCOORD1))
    highp vec4 color = SampleTexture2D(DiffuseTex, uv);
    #ifdef GLTF_TANGENT_SPACE
    color *= DiffuseColor;
    #endif
    return color;
#else
    return DiffuseColor;
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

void BuildSurface(out highp vec4 color, out highp vec3 normal, out highp vec3 geoNormal,
                  out highp float metallic, out highp float roughness, out highp float selfShadow, out highp vec2 uv)
{
    color = vec4(0.5, 0.5, 0.5, 1.0);
    metallic = PBRParams.x;
    roughness = PBRParams.y;
    selfShadow = 1.0;
    uv = GetTexCoord(TexCoordSets.x);

#ifdef USE_NORMALS
    normal = normalize(hnormal.xyz);
#else
    normal = vec3(0.0, 0.0, 1.0);
#endif
    geoNormal = normal;

#if defined(HEIGHT_MAP) || defined(NORMAL_MAP)
    highp vec3 tangent = normalize(htangent.xyz);
    highp vec3 binormal = normalize(hbinormal.xyz);
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
    highp vec2 normalUV = TexCoordSets.y > 0.5 ? GetTexCoord(TexCoordSets.y) : uv;
    highp vec3 normalTex = SampleTexture2D(NormalTex, normalUV).xyz;
    normalTex = normalTex * vec3(2.0, 2.0, 2.0) - vec3(1.0, 1.0, 1.0);
    normalTex = normalize(normalTex);
    #ifndef GLTF_TANGENT_SPACE
    normalTex.g = -normalTex.g;
    #endif
    normal = TBN * normalTex;
    normal = normalize(normal);
#endif

#ifdef METALLIC_MAP
    highp vec2 metallicUV = TexCoordSets.z > 0.5 ? GetTexCoord(TexCoordSets.z) : uv;
    highp vec4 mrSample = SampleTexture2D(MetallicTex, metallicUV);
    metallic = PBRParams.x * mrSample.b;
    roughness = PBRParams.y * mrSample.g;
#elif defined(GLOSS_MAP)
    roughness = SampleTexture2D(GlossTex, uv).r;
#endif
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

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
#endif
void main()
{
    highp vec4 color;
    highp vec3 normal;
    highp vec3 geoNormal;
    highp float metallic;
    highp float roughness;
    highp float selfShadow;
    highp vec2 uv;
    BuildSurface(color, normal, geoNormal, metallic, roughness, selfShadow, uv);
    highp float outDepth = Pos.z / Pos.w;
#ifdef ES_30
    colorOut_0 = vec4(color.rgb, 0.0);
    colorOut_1 = vec4(normal * 0.5 + 0.5, roughness);
    colorOut_2 = vec4(metallic, selfShadow, 0.0, Intensities.w / 255.0);
    colorOut_3 = vec4(geoNormal * 0.5 + 0.5, 0.0);
    colorOut_4 = vec4(outDepth, 0.0, 0.0, 0.0);
#else
    gl_FragData[0] = vec4(color.rgb, 0.0);
    gl_FragData[1] = vec4(normal * 0.5 + 0.5, roughness);
    gl_FragData[2] = vec4(metallic, selfShadow, 0.0, Intensities.w / 255.0);
    gl_FragData[3] = vec4(geoNormal * 0.5 + 0.5, 0.0);
    gl_FragData[4] = vec4(outDepth, 0.0, 0.0, 0.0);
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
    highp vec2 uv;
    BuildSurface(color, normal, geoNormal, metallic, roughness, selfShadow, uv);

    if (ForwardParams.z > 0.5 && ForwardParams.x > 0.0 && ForwardParams.y > 0.0) {
        highp vec2 screenUV = gl_FragCoord.xy / ForwardParams.xy;
        highp float sceneDepth = SampleTexture2D(SceneDepthTex, screenUV).r;
        highp float meshDepth = Pos.z / Pos.w;
        if (sceneDepth > 0.0001 && meshDepth < sceneDepth - 0.00001)
            discard;
    }

    highp vec3 albedo = pow(max(color.rgb, vec3(0.0)), vec3(2.2));
    highp vec3 eyeDir = normalize(CameraPosition.xyz - WorldPos.xyz);
    highp vec3 F0 = mix(vec3(0.04), albedo, metallic);
    highp vec3 directLight = vec3(0.0);
    highp int numLights = int(CameraInfo.w);

    for (int i = 0; i < 128; i++) {
        if (i >= numLights) break;
        highp float lightType = LightPositions[i].w;
        highp float intensity = LightColors[i].w;
        if (lightType < 0.5) {
            highp vec3 lightDir = normalize(-LightPositions[i].xyz);
            highp vec3 halfVec = normalize(eyeDir + lightDir);
            highp vec3 diffuse = CalculateDiffuse(albedo, normal, lightDir) * LightColors[i].xyz * intensity;
            highp vec3 specular = CalculateSpecular(F0, normal, eyeDir, halfVec, lightDir, roughness) * LightColors[i].xyz * intensity;
            highp vec3 F = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0, 1.0), F0);
            highp vec3 Kd = (vec3(1.0) - F) * (1.0 - metallic);
            directLight += (specular + Kd * diffuse) * clamp(dot(geoNormal, lightDir), 0.0, 1.0);
        } else {
            highp float rad = GetPackedLightRadius(i);
            highp float dist = distance(LightPositions[i].xyz, WorldPos.xyz);
            if (dist < rad * 2.0) {
                highp vec3 lightDir = normalize(LightPositions[i].xyz - WorldPos.xyz);
                highp vec3 halfVec = normalize(eyeDir + lightDir);
                highp vec3 diffuse = CalculateDiffuse(albedo, normal, lightDir) * LightColors[i].xyz * intensity;
                highp vec3 specular = CalculateSpecular(F0, normal, eyeDir, halfVec, lightDir, roughness) * LightColors[i].xyz * intensity;
                highp vec3 F = FresnelCalc(clamp(dot(eyeDir, halfVec), 0.0, 1.0), F0);
                highp vec3 Kd = (vec3(1.0) - F) * (1.0 - metallic);
                highp float d = max(dist - rad, 0.0);
                highp float denom = d / max(rad, 0.0001) + 1.0;
                highp float attenuation = 1.0 / (denom * denom);
                attenuation = max((attenuation - 0.8) / 0.2, 0.0);
                directLight += (specular * attenuation + attenuation * Kd * diffuse) * clamp(dot(geoNormal, lightDir), 0.0, 1.0);
            }
        }
    }

    highp vec3 finalColor = directLight * selfShadow;
    highp vec3 reflectedVec = reflect(-eyeDir, normal);
    reflectedVec.x = -reflectedVec.x;
    reflectedVec.z = -reflectedVec.z;
    highp vec3 kSpecular = clamp(fresnelSchlickRoughness(max(dot(normal, eyeDir), 0.0), F0, roughness), 0.0, 1.0);
    highp vec3 kDiffuseEnv = (vec3(1.0) - kSpecular) * (1.0 - metallic);
    highp vec3 envSpec = SampleCubeLod(texEnv, reflectedVec, roughness * 4.0);
    highp float envAtten = (1.0 - roughness) * (1.0 - roughness);
    finalColor += envSpec * kSpecular * envAtten;
    highp vec3 irradianceDir = normal;
    irradianceDir.x = -irradianceDir.x;
    irradianceDir.z = -irradianceDir.z;
    highp vec3 irradiance = SampleCubeLod(texEnv, irradianceDir, 6.0);
    finalColor += irradiance * albedo * kDiffuseEnv;
    finalColor += albedo * AmbientColor.rgb * kDiffuseEnv;

    highp vec3 emissive = EmissiveColor.rgb;
#ifdef EMISSIVE_MAP
    highp vec2 emissiveUV = TexCoordSets.w > 0.5 ? GetTexCoord(TexCoordSets.w) : uv;
    emissive *= SampleTexture2D(EmissiveTex, emissiveUV).rgb;
#endif
    finalColor += emissive;

    highp float alpha = color.a;
    if (AlphaParams.w > 0.0 && alpha >= 0.999)
        alpha = clamp(1.0 - AlphaParams.w * 0.65, 0.0, 1.0);
    if (AlphaParams.x < 1.5 && AlphaParams.w <= 0.0)
        alpha = 1.0;
#ifdef ES_30
    colorOut = vec4(finalColor, alpha);
#else
    gl_FragColor = vec4(finalColor, alpha);
#endif
}
#endif
