cbuffer QuadFrameCB : register(b0) {
    float4x4 WVP;
	float4x4 World;  
	float4x4 WorldView;
	float4x4 WVPInverse;
	float4x4 WVPLight;
	float4x4 Projection;
	float4   CameraPosition;
	float4 	 CameraInfo;
	float4	 LightCameraPosition;
	float4 	 LightCameraInfo;
}

cbuffer QuadPassCB : register(b1) {
	float4	 LightPositions[128];
	float4	 LightColors[128];
  float4	 LightRadius[32];
	float4   brightness;
	float4   toogles;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
    float2 texture0  : TEXCOORD;
	float4 Pos		: TEXCOORD1;
	float4 PosCorner : TEXCOORD2;
	float2 ClipPos   : TEXCOORD3;
};

SamplerState SS  : register(s0);
SamplerState SS1 : register(s1);
SamplerState SS2 : register(s2);
SamplerState SS3 : register(s3);
SamplerState SS4 : register(s4);
SamplerState SS5 : register(s5);
SamplerState SS6 : register(s6);
SamplerState SS7 : register(s7);
SamplerState SS8 : register(s8);
SamplerState SS9 : register(s9);
SamplerState SS10 : register(s10);
SamplerState SS11 : register(s11);
SamplerState SS12 : register(s12);
SamplerState SS13 : register(s13);
SamplerState SS14 : register(s14);
SamplerState SS15 : register(s15);

static const float DEPTH_CLEAR_EPSILON = 0.0001f;

bool IsSceneDepthValid(float depth) {
	return depth > DEPTH_CLEAR_EPSILON;
}

float roundTo(float num,float decimals){
	float shift = pow(10.0,decimals);
	return round(num*shift) / shift;
}

float LinearToSRGBChannel(float value)
{
	value = saturate(value);
	return value <= 0.0031308f ? value * 12.92f : 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
}

float3 LinearToSRGB(float3 color)
{
	return float3(LinearToSRGBChannel(color.r), LinearToSRGBChannel(color.g), LinearToSRGBChannel(color.b));
}

float4 ReconstructPosition(float2 clipPos, float depth) {
	float4 position = mul(WVPInverse, float4(clipPos, depth, 1.0));
	position.xyz /= position.w;
	position.w = 1.0;
	return position;
}

float LinearizeDepth(float depth) {
	float znear = CameraInfo.x;
	float zfar = CameraInfo.y;
	return (znear * zfar) / (znear + depth * (zfar - znear));
}

float3 DecodeOctahedralNormal(float2 encoded)
{
	float2 f = encoded * 2.0f - 1.0f;
	float3 normal = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
	float t = max(-normal.z, 0.0f);
	normal.x += normal.x >= 0.0f ? -t : t;
	normal.y += normal.y >= 0.0f ? -t : t;
	return normalize(normal);
}

// PBR helper functions shared by deferred fullscreen and bounded light passes.
#if defined(DEFERRED_PASS) || defined(DEFERRED_LDR_PASS) || defined(DEFERRED_LIGHT_VOLUME_PASS)
static const float PBR_PI = 3.14159265359f;

float NormalDistribution(float NdotH, float roughness)
{
	float alpha = max(roughness * roughness, 0.001f);
	float alphaSq = alpha * alpha;
	float f = (NdotH * NdotH) * (alphaSq - 1.0f) + 1.0f;
	return alphaSq / max(PBR_PI * f * f, 0.000001f);
}

float3 FresnelCalc(float VdotH, float3 specColor)
{
	return (specColor + (1.0f - specColor) * pow(1.0f - VdotH, 5.0f));
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
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

float VisibilityGGX(float NdotL, float NdotV, float roughness)
{
	float alpha = max(roughness * roughness, 0.001f);
	float alphaSq = alpha * alpha;
	float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0f - alphaSq) + alphaSq);
	float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0f - alphaSq) + alphaSq);
	float ggx = ggxV + ggxL;
	return ggx > 0.0f ? 0.5f / ggx : 0.0f;
}

float RangeAttenuation(float range, float distanceToLight)
{
	float distanceSq = max(distanceToLight * distanceToLight, 0.0001f);
	if (range <= 0.0f)
		return 1.0f / distanceSq;
	float normalizedDistance = distanceToLight / max(range, 0.0001f);
	float falloff = saturate(1.0f - normalizedDistance * normalizedDistance * normalizedDistance * normalizedDistance);
	return falloff / distanceSq;
}

float3 CalculateSpecular(float3 specularColor, float3 normal, float3 view, float3 halfvector, float3 light, float roughness)
{
	float NdotH = max(dot(normal, halfvector), 0.0f);
	float VdotH = clamp(dot(view, halfvector), 0.0f, 1.0f);
	float NdotL = clamp(dot(normal, light), 0.0f, 1.0f);
	float NdotV = clamp(dot(normal, view), 0.0f, 1.0f);
	return FresnelCalc(VdotH, specularColor) * NormalDistribution(NdotH, roughness) * VisibilityGGX(NdotL, NdotV, roughness) * NdotL;
}

float3 CalculateDiffuse(float3 albedoColor, float3 normal, float3 light)
{
	return albedoColor * clamp(dot(normal, light), 0.0f, 1.0f) / PBR_PI;
}

float3 CalculateClearcoat(float3 normal, float3 view, float3 halfvector, float3 light, float clearcoatRoughness)
{
	float NdotH = max(dot(normal, halfvector), 0.0f);
	float NdotL = clamp(dot(normal, light), 0.0f, 1.0f);
	float NdotV = clamp(dot(normal, view), 0.0f, 1.0f);
	float brdf = NormalDistribution(NdotH, clearcoatRoughness) * VisibilityGGX(NdotL, NdotV, clearcoatRoughness) * NdotL;
	return float3(brdf, brdf, brdf);
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

float3 CalculateSheenRadiance(float3 sheenColor, float sheenRoughness, float3 lightColor, float intensity, float NdotL, float NdotV, float NdotH)
{
	return lightColor * intensity * NdotL * BRDFSpecularSheen(sheenColor, sheenRoughness, NdotL, NdotV, NdotH);
}
#endif

#ifdef DEFERRED_PASS
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
Texture2D tex5 : register(t5);
#endif
TextureCube texEnv : register(t6);
TextureCube texIBLDiffuse : register(t10);
TextureCube texIBLSpecular : register(t11);
Texture2D texIBLBRDF : register(t12);
TextureCube texIBLCharlie : register(t13);
Texture2D texIBLCharlieLUT : register(t14);
Texture2D texIBLSheenELUT : register(t15);
Texture2D tex6 : register(t7);
Texture2D tex7 : register(t8);
Texture2D tex8 : register(t9);

float AlbedoSheenScalingLUT(float NdotV, float sheenRoughness)
{
	return texIBLSheenELUT.SampleLevel(SS15, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).r;
}

float3 GetIBLRadianceCharlie(float3 normal, float3 viewDir, float sheenRoughness, float3 sheenColor, float iblMaxMip)
{
	float NdotV = max(dot(normal, viewDir), 0.0f);
	float lod = sheenRoughness * iblMaxMip;
	float3 reflectedVec = reflect(-viewDir, normal);
	reflectedVec.x = -reflectedVec.x;
	reflectedVec.z = -reflectedVec.z;
	float brdf = texIBLCharlieLUT.SampleLevel(SS14, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).b;
	float3 sheenLight = texIBLCharlie.SampleLevel(SS13, reflectedVec, lod).rgb;
	return sheenLight * sheenColor * brdf;
}

float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Final = float4(0.0,0.0,0.0,1.0);
	float Shadow = 1.0;

	float4 Albedo = tex0.SampleLevel(SS, input.texture0, 0.0f);
	float4 PBRData = tex2.SampleLevel(SS2, input.texture0, 0.0f);
	float4 SpecularOcclusionData = tex7.SampleLevel(SS8, input.texture0, 0.0f);
	float specularFactor = max(Albedo.a, 0.0f);

	Albedo.xyz = pow(Albedo.xyz, float3(2.2, 2.2, 2.2));

	float metallic = PBRData.r;
	float3 dielectricF0 = max(SpecularOcclusionData.rgb, float3(0.0f, 0.0f, 0.0f));
	float occlusion = saturate(SpecularOcclusionData.a);
	float3 F0 = lerp(dielectricF0 * specularFactor, Albedo.xyz, metallic);

	float depth = tex4.SampleLevel(SS4, input.texture0, 0.0f).r;
	float3 emissive = tex8.SampleLevel(SS9, input.texture0, 0.0f).rgb;

	float4 position = ReconstructPosition(input.ClipPos, depth);
	 
	float3 EyeDir = normalize(CameraPosition.xyz - position.xyz);

	int MatId = (int)(PBRData.a * 255.0 + 0.5);

	if(MatId == 0){
		// Sky: use the interpolated view ray (PosCorner from VS_Quad).
		float3 skyDir = normalize(input.PosCorner.xyz);
		skyDir.x = -skyDir.x;
		skyDir.z = -skyDir.z;
		float3 RefCol = texEnv.SampleLevel(SS6, skyDir, 0.0f).xyz;
		Final.xyz = RefCol.xyz * toogles.x;
	} else if(MatId > 0) {
		#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
		Shadow = tex5.SampleLevel(SS5, input.texture0, 0.0f).r;
		#endif

		float4 normalmap = tex1.SampleLevel(SS1, input.texture0, 0.0f);
		float3 normal = normalmap.xyz * 2.0 - 1.0;
		normal = normalize(normal);
		float rough = normalmap.a;
		float4 SheenData = tex6.SampleLevel(SS7, input.texture0, 0.0f);
		float3 sheenColor = saturate(SheenData.rgb);
		float sheenRoughness = saturate(SheenData.a);
		float sheenStrength = Max3(sheenColor);
		bool hasSheenLUT = brightness.w > 0.5f;
		float3 directLight = float3(0.0, 0.0, 0.0);

		float4 geoData = tex3.SampleLevel(SS3, input.texture0, 0.0f);
		float3 geoNormal = DecodeOctahedralNormal(geoData.xy);
		float lightmap = saturate(geoData.b);
		float packedMaterial = geoData.a;
		bool unlitMaterial = packedMaterial >= 0.5f;
		float clearcoatRoughness = unlitMaterial ? (packedMaterial - 0.5f) * 2.0f : packedMaterial * 2.0f;
		clearcoatRoughness = clamp(clearcoatRoughness, 0.04f, 1.0f);
		float clearcoatFactor = saturate(PBRData.b);

		float3 ReflectedVec = reflect(-EyeDir, normal.xyz);
		ReflectedVec.x = -ReflectedVec.x;
		ReflectedVec.z = -ReflectedVec.z;
		int NumLights = (int)CameraInfo.w;
		[loop] for(int i = 0; i < NumLights; i++){
			float lightType = LightPositions[i].w;
			float intensity = LightColors[i].w;

			if(lightType < 0.5) {
				// Directional light
				float3 LightDir = normalize(-LightPositions[i].xyz);
				float3 Half = normalize(EyeDir + LightDir);
				float3 lightRadiance = LightColors[i].xyz * intensity;
				float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * lightRadiance;
				float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * lightRadiance;

				float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
				float3 F = FresnelCalc(VdotH, F0);
				float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);

				float NdotL = max(dot(normal, LightDir), 0.0f);
				float NdotVLight = max(dot(normal, EyeDir), 0.0f);
				float NdotH = max(dot(normal, Half), 0.0f);
				float albedoSheenScaling = 1.0f;
				float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
				if (hasSheenLUT && sheenStrength > 0.0f) {
					albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
					                         1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
					sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH);
				}
				float3 layerLight = sheenLight + (SpecularRes.xyz + Kd * Diffuse) * albedoSheenScaling;
				if (clearcoatFactor > 0.001f) {
					float clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f)).x;
					float clearcoatWeight = saturate(clearcoatFactor * clearcoatF);
					float3 clearcoatLight = CalculateClearcoat(normal, EyeDir, Half, LightDir, clearcoatRoughness) * lightRadiance;
					layerLight = lerp(layerLight, clearcoatLight, clearcoatWeight);
				}
				directLight += layerLight;
			} else {
				// Point light
				float Rad = LightRadius[i >> 2][i & 3];
				float dist = distance(LightPositions[i].xyz, position.xyz);
				float attenuation = RangeAttenuation(Rad * 2.0f, dist);

				if(attenuation > 0.0f)
				{
					float3 LightDir = normalize(LightPositions[i].xyz - position.xyz);
					float3 Half = normalize(EyeDir + LightDir);
					float3 lightRadiance = LightColors[i].xyz * intensity * attenuation;
					float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * lightRadiance;
					float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * lightRadiance;

					float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
					float3 F = FresnelCalc(VdotH, F0);
					float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);

					float NdotL = max(dot(normal, LightDir), 0.0f);
					float NdotVLight = max(dot(normal, EyeDir), 0.0f);
					float NdotH = max(dot(normal, Half), 0.0f);
					float albedoSheenScaling = 1.0f;
					float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
					if (hasSheenLUT && sheenStrength > 0.0f) {
						albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
						                         1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
						sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH) * attenuation;
					}
					float3 layerLight = sheenLight + (SpecularRes.xyz + Kd * Diffuse) * albedoSheenScaling;
					if (clearcoatFactor > 0.001f) {
						float clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f)).x;
						float clearcoatWeight = saturate(clearcoatFactor * clearcoatF);
						float3 clearcoatLight = CalculateClearcoat(normal, EyeDir, Half, LightDir, clearcoatRoughness) * lightRadiance;
						layerLight = lerp(layerLight, clearcoatLight, clearcoatWeight);
					}
					directLight += layerLight;
				}
			}
		}

		// Apply shadow only to direct lighting
		float selfShadow = PBRData.g;
		Final.xyz += directLight * Shadow * selfShadow;

		// Indirect lighting (IBL + ambient) — NOT affected by shadow
		float iblMaxMip = max(toogles.w, 0.0f);
		bool hasBrdfLUT = brightness.w > 0.5f;
		float NdotV = max(dot(normal, EyeDir), 0.0f);
		float3 kSpecular = clamp(fresnelSchlickRoughness(NdotV, F0, rough), 0.0, 1.0);
		float3 kDiffuseEnv = (float3(1.0f, 1.0f, 1.0f) - kSpecular) * (1.0f - metallic);

		// Specular IBL: env reflection (toogles.z = IBL factor)
		float3 RefleCol = texIBLSpecular.SampleLevel(SS11, ReflectedVec, rough * iblMaxMip).xyz;
		float envAtten = (1.0f - rough) * (1.0f - rough);
		float2 brdfSample = hasBrdfLUT ? texIBLBRDF.SampleLevel(SS12, float2(NdotV, rough), 0.0f).rg : float2(0.0f, 0.0f);
		float3 specularIBL = hasBrdfLUT ? IBLGGXFresnel(NdotV, rough, F0, brdfSample) : kSpecular * envAtten;
		float3 indirectLight = RefleCol * specularIBL * toogles.z;

		// Diffuse IBL: approximate irradiance from env cubemap at high mip
		float3 irradianceDir = normal;
		irradianceDir.x = -irradianceDir.x;
		irradianceDir.z = -irradianceDir.z;
		float diffuseMip = clamp(brightness.z, 0.0f, iblMaxMip);
		float3 irradiance = texIBLDiffuse.SampleLevel(SS10, irradianceDir, diffuseMip).xyz;
		indirectLight += irradiance * Albedo.xyz * kDiffuseEnv * toogles.z;
		indirectLight += Albedo.xyz * kDiffuseEnv * lightmap;

		// PBR: avoid a constant ambient floor; rely on IBL + AO.
		if (hasSheenLUT && sheenStrength > 0.0f) {
			float albedoSheenScaling = 1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotV, sheenRoughness);
			float3 sheenIBL = GetIBLRadianceCharlie(normal, EyeDir, sheenRoughness, sheenColor, iblMaxMip) * toogles.z;
			indirectLight = sheenIBL + indirectLight * albedoSheenScaling;
		}
		Final.xyz += indirectLight * occlusion;
		if (clearcoatFactor > 0.001f) {
			float3 clearcoatSpec = texIBLSpecular.SampleLevel(SS11, ReflectedVec, clearcoatRoughness * iblMaxMip).xyz;
			float clearcoatAtten = hasBrdfLUT ? 1.0f : (1.0f - clearcoatRoughness) * (1.0f - clearcoatRoughness);
			float3 clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f));
			float clearcoatWeight = saturate(clearcoatFactor * max(clearcoatF.x, max(clearcoatF.y, clearcoatF.z)));
			Final.xyz = lerp(Final.xyz, clearcoatSpec * clearcoatAtten * toogles.z, clearcoatWeight);
		}
		Final.xyz += emissive;
		if (unlitMaterial) {
			Final.xyz = Albedo.xyz + emissive;
		}
	}

	return Final;
}
#elif defined(DEFERRED_LDR_PASS)
// LDR deferred — same as DEFERRED_PASS but clamps output to 0-1 (no HDR)
Texture2D tex0 : register(t0); // Albedo
Texture2D tex1 : register(t1); // Normal map
Texture2D tex2 : register(t2); // PBR data (specular, metallic, matID)
Texture2D tex3 : register(t3); // Packed geometric normal/material
Texture2D tex4 : register(t4); // Depth
#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
Texture2D tex5 : register(t5); // Shadow accumulation
#endif
TextureCube texEnv : register(t6);
TextureCube texIBLDiffuse : register(t10);
TextureCube texIBLSpecular : register(t11);
Texture2D texIBLBRDF : register(t12);
TextureCube texIBLCharlie : register(t13);
Texture2D texIBLCharlieLUT : register(t14);
Texture2D texIBLSheenELUT : register(t15);
Texture2D tex6 : register(t7);
Texture2D tex7 : register(t8);
Texture2D tex8 : register(t9);

float AlbedoSheenScalingLUT(float NdotV, float sheenRoughness)
{
	return texIBLSheenELUT.SampleLevel(SS15, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).r;
}

float3 GetIBLRadianceCharlie(float3 normal, float3 viewDir, float sheenRoughness, float3 sheenColor, float iblMaxMip)
{
	float NdotV = max(dot(normal, viewDir), 0.0f);
	float lod = sheenRoughness * iblMaxMip;
	float3 reflectedVec = reflect(-viewDir, normal);
	reflectedVec.x = -reflectedVec.x;
	reflectedVec.z = -reflectedVec.z;
	float brdf = texIBLCharlieLUT.SampleLevel(SS14, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).b;
	float3 sheenLight = texIBLCharlie.SampleLevel(SS13, reflectedVec, lod).rgb;
	return sheenLight * sheenColor * brdf;
}

float4 FS(VS_OUTPUT input) : SV_TARGET {
	float4 Final = float4(0, 0, 0, 1);
	float Shadow = 1.0;

	float4 Albedo = tex0.SampleLevel(SS, input.texture0, 0.0f);
	float4 PBRData = tex2.SampleLevel(SS2, input.texture0, 0.0f);
	float4 SpecularOcclusionData = tex7.SampleLevel(SS8, input.texture0, 0.0f);
	float specularFactor = max(Albedo.a, 0.0f);
	Albedo.xyz = pow(Albedo.xyz, float3(2.2f, 2.2f, 2.2f));
	float metallic = PBRData.r;
	float3 dielectricF0 = max(SpecularOcclusionData.rgb, float3(0.0f, 0.0f, 0.0f));
	float occlusion = saturate(SpecularOcclusionData.a);
	float3 F0 = lerp(dielectricF0 * specularFactor, Albedo.xyz, metallic);
	float depth = tex4.SampleLevel(SS4, input.texture0, 0.0f).r;
	float3 emissive = tex8.SampleLevel(SS9, input.texture0, 0.0f).rgb;

	// No geometry drawn at this pixel — let clear color show through
	if (depth <= 0.0001)
		discard;

	float4 position = ReconstructPosition(input.ClipPos, depth);

	float3 EyeDir = normalize(CameraPosition.xyz - position.xyz);
	int MatId = (int)(PBRData.a * 255.0);

	if (MatId == 0) {
		float3 EyeDir_mod = -EyeDir;
		EyeDir_mod.x = -EyeDir_mod.x;
		EyeDir_mod.z = -EyeDir_mod.z;
		float3 RefCol = texEnv.SampleLevel(SS6, EyeDir_mod, 0.0f).xyz;
		Final.xyz = RefCol.xyz * 2.0;
	} else if (MatId > 0) {
		#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
		Shadow = tex5.SampleLevel(SS5, input.texture0, 0.0f).r;
		#endif
		float4 normalmap = tex1.SampleLevel(SS1, input.texture0, 0.0f);
		float3 normal = normalize(normalmap.xyz * 2.0 - 1.0);
		float4 geoData = tex3.SampleLevel(SS3, input.texture0, 0.0f);
		float3 geoNormal = DecodeOctahedralNormal(geoData.xy);
		float lightmap = saturate(geoData.b);
		float packedMaterial = geoData.a;
		bool unlitMaterial = packedMaterial >= 0.5f;
		float clearcoatRoughness = unlitMaterial ? (packedMaterial - 0.5f) * 2.0f : packedMaterial * 2.0f;
		clearcoatRoughness = clamp(clearcoatRoughness, 0.04f, 1.0f);
		float clearcoatFactor = saturate(PBRData.b);
		float rough = normalmap.a;
		float4 SheenData = tex6.SampleLevel(SS7, input.texture0, 0.0f);
		float3 sheenColor = saturate(SheenData.rgb);
		float sheenRoughness = saturate(SheenData.a);
		float sheenStrength = Max3(sheenColor);
		bool hasSheenLUT = brightness.w > 0.5f;

		int NumLights = (int)CameraInfo.w;
		[loop] for (int i = 0; i < NumLights; i++) {
			float lightType = LightPositions[i].w;
			float intensity = LightColors[i].w;
			if (lightType < 0.5) {
				float3 LightDir = normalize(-LightPositions[i].xyz);
				float3 Half = normalize(EyeDir + LightDir);
				float3 lightRadiance = LightColors[i].xyz * intensity;
				float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * lightRadiance;
				float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * lightRadiance;
				float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
				float3 F = FresnelCalc(VdotH, F0);
				float3 Kd = (float3(1, 1, 1) - F) * (1.0f - metallic);
				float NdotL = max(dot(normal, LightDir), 0.0f);
				float NdotVLight = max(dot(normal, EyeDir), 0.0f);
				float NdotH = max(dot(normal, Half), 0.0f);
				float albedoSheenScaling = 1.0f;
				float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
				if (hasSheenLUT && sheenStrength > 0.0f) {
					albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
					                         1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
					sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH);
				}
				float3 layerLight = sheenLight + (SpecularRes.xyz + Kd * Diffuse) * albedoSheenScaling;
				if (clearcoatFactor > 0.001f) {
					float clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f)).x;
					float clearcoatWeight = saturate(clearcoatFactor * clearcoatF);
					float3 clearcoatLight = CalculateClearcoat(normal, EyeDir, Half, LightDir, clearcoatRoughness) * lightRadiance;
					layerLight = lerp(layerLight, clearcoatLight, clearcoatWeight);
				}
				Final.xyz += layerLight;
			} else {
				float Rad = LightRadius[i >> 2][i & 3];
				float dist = distance(LightPositions[i].xyz, position.xyz);
				float attenuation = RangeAttenuation(Rad * 2.0f, dist);
				if (attenuation > 0.0f) {
					float3 LightDir = normalize(LightPositions[i].xyz - position.xyz);
					float3 Half = normalize(EyeDir + LightDir);
					float3 lightRadiance = LightColors[i].xyz * intensity * attenuation;
					float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * lightRadiance;
					float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * lightRadiance;
					float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
					float3 F = FresnelCalc(VdotH, F0);
					float3 Kd = (float3(1, 1, 1) - F) * (1.0f - metallic);
					float NdotL = max(dot(normal, LightDir), 0.0f);
					float NdotVLight = max(dot(normal, EyeDir), 0.0f);
					float NdotH = max(dot(normal, Half), 0.0f);
					float albedoSheenScaling = 1.0f;
					float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
					if (hasSheenLUT && sheenStrength > 0.0f) {
						albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
						                         1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
						sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[i].xyz, intensity, NdotL, NdotVLight, NdotH) * attenuation;
					}
					float3 layerLight = sheenLight + (SpecularRes.xyz + Kd * Diffuse) * albedoSheenScaling;
					if (clearcoatFactor > 0.001f) {
						float clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f)).x;
						float clearcoatWeight = saturate(clearcoatFactor * clearcoatF);
						float3 clearcoatLight = CalculateClearcoat(normal, EyeDir, Half, LightDir, clearcoatRoughness) * lightRadiance;
						layerLight = lerp(layerLight, clearcoatLight, clearcoatWeight);
					}
					Final.xyz += layerLight;
				}
			}
		}
		float3 directLight = Final.xyz;

		// Reduced env reflections for LDR
		float3 ReflectedVec = reflect(-EyeDir, normal.xyz);
		ReflectedVec.x = -ReflectedVec.x; ReflectedVec.z = -ReflectedVec.z;
		float iblMaxMip = max(toogles.w, 0.0f);
		bool hasBrdfLUT = brightness.w > 0.5f;
		float NdotV = max(dot(normal, EyeDir), 0.0f);
		float3 kSpecular = clamp(fresnelSchlickRoughness(NdotV, F0, rough), 0.0, 1.0);
		float3 kDiffuseEnv = (float3(1.0f, 1.0f, 1.0f) - kSpecular) * (1.0f - metallic);
		float3 RefleCol = texIBLSpecular.SampleLevel(SS11, ReflectedVec, rough * iblMaxMip).xyz;
		float envAtten = (1.0f - rough) * (1.0f - rough);
		float2 brdfSample = hasBrdfLUT ? texIBLBRDF.SampleLevel(SS12, float2(NdotV, rough), 0.0f).rg : float2(0.0f, 0.0f);
		float3 specularIBL = hasBrdfLUT ? IBLGGXFresnel(NdotV, rough, F0, brdfSample) : kSpecular * envAtten;
		float3 ldrIBL = RefleCol * specularIBL * toogles.z;
		ldrIBL += Albedo.xyz * kDiffuseEnv * lightmap;
		if (hasSheenLUT && sheenStrength > 0.0f) {
			float albedoSheenScaling = 1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotV, sheenRoughness);
			float3 sheenIBL = GetIBLRadianceCharlie(normal, EyeDir, sheenRoughness, sheenColor, iblMaxMip) * toogles.z;
			ldrIBL = sheenIBL + ldrIBL * albedoSheenScaling;
		}
		Final.xyz += ldrIBL * occlusion;
		if (clearcoatFactor > 0.001f) {
			float3 clearcoatSpec = texIBLSpecular.SampleLevel(SS11, ReflectedVec, clearcoatRoughness * iblMaxMip).xyz;
			float clearcoatAtten = hasBrdfLUT ? 1.0f : (1.0f - clearcoatRoughness) * (1.0f - clearcoatRoughness);
			float3 clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f));
			float clearcoatWeight = saturate(clearcoatFactor * max(clearcoatF.x, max(clearcoatF.y, clearcoatF.z)));
			Final.xyz = lerp(Final.xyz, clearcoatSpec * clearcoatAtten * toogles.z, clearcoatWeight);
		}

		float selfShadow = PBRData.g;
		float3 indirectAccum = Final.xyz - directLight;
		Final.xyz = directLight * Shadow * selfShadow + indirectAccum;
		Final.xyz += emissive;
		if (unlitMaterial) {
			Final.xyz = Albedo.xyz + emissive;
		}
	}

	// Clamp to LDR — no tone mapping, just saturate
	Final.xyz = saturate(Final.xyz);
	return Final;
}
#elif defined(DEFERRED_LIGHT_VOLUME_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
Texture2D tex5 : register(t5);
#endif
TextureCube texEnv : register(t6);
TextureCube texIBLDiffuse : register(t10);
TextureCube texIBLSpecular : register(t11);
Texture2D texIBLBRDF : register(t12);
TextureCube texIBLCharlie : register(t13);
Texture2D texIBLCharlieLUT : register(t14);
Texture2D texIBLSheenELUT : register(t15);
Texture2D tex6 : register(t7);
Texture2D tex7 : register(t8);
Texture2D tex8 : register(t9);
Texture2D texTileHeaders : register(t16);
Texture2D texTileLightIndices : register(t17);

float AlbedoSheenScalingLUT(float NdotV, float sheenRoughness)
{
	return texIBLSheenELUT.SampleLevel(SS15, float2(saturate(NdotV), saturate(sheenRoughness)), 0.0f).r;
}

float4 FS(VS_OUTPUT input) : SV_TARGET {
	float4 Final = float4(0.0f, 0.0f, 0.0f, 1.0f);

	uint bufferWidth = 1;
	uint bufferHeight = 1;
	tex0.GetDimensions(bufferWidth, bufferHeight);
	float2 coords = input.hposition.xy / float2(max(bufferWidth, 1u), max(bufferHeight, 1u));
	float2 clipPos = float2(coords.x * 2.0f - 1.0f, 1.0f - coords.y * 2.0f);

	float4 Albedo = tex0.SampleLevel(SS, coords, 0.0f);
	float4 PBRData = tex2.SampleLevel(SS2, coords, 0.0f);
	float depth = tex4.SampleLevel(SS4, coords, 0.0f).r;
	if (!IsSceneDepthValid(depth)) {
		discard;
		return Final;
	}

	int MatId = (int)(PBRData.a * 255.0f + 0.5f);
	if (MatId <= 0) {
		discard;
		return Final;
	}

	float4 geoData = tex3.SampleLevel(SS3, coords, 0.0f);
	float packedMaterial = geoData.a;
	bool unlitMaterial = packedMaterial >= 0.5f;
	if (unlitMaterial) {
		discard;
		return Final;
	}

	float Shadow = 1.0f;
	#if defined(ENABLE_SHADOWS) || defined(ENABLE_SSAO)
	Shadow = tex5.SampleLevel(SS5, coords, 0.0f).r;
	#endif

	float4 SpecularOcclusionData = tex7.SampleLevel(SS8, coords, 0.0f);
	float specularFactor = max(Albedo.a, 0.0f);
	Albedo.xyz = pow(Albedo.xyz, float3(2.2f, 2.2f, 2.2f));

	float metallic = PBRData.r;
	float3 dielectricF0 = max(SpecularOcclusionData.rgb, float3(0.0f, 0.0f, 0.0f));
	float3 F0 = lerp(dielectricF0 * specularFactor, Albedo.xyz, metallic);

	float4 position = ReconstructPosition(clipPos, depth);
	float3 EyeDir = normalize(CameraPosition.xyz - position.xyz);

	float4 normalmap = tex1.SampleLevel(SS1, coords, 0.0f);
	float3 normal = normalize(normalmap.xyz * 2.0f - 1.0f);
	float rough = normalmap.a;
	float4 SheenData = tex6.SampleLevel(SS7, coords, 0.0f);
	float3 sheenColor = saturate(SheenData.rgb);
	float sheenRoughness = saturate(SheenData.a);
	float sheenStrength = Max3(sheenColor);
	bool hasSheenLUT = brightness.w > 0.5f;

	float clearcoatRoughness = clamp(packedMaterial * 2.0f, 0.04f, 1.0f);
	float clearcoatFactor = saturate(PBRData.b);

	float selfShadow = PBRData.g;
	int tileSize = max((int)(LightCameraInfo.x + 0.5f), 1);
	int tileCountX = max((int)(LightCameraInfo.y + 0.5f), 1);
	int tileCountY = max((int)(LightCameraInfo.z + 0.5f), 1);
	int maxTileLights = max((int)(LightCameraInfo.w + 0.5f), 1);
	int tileX = clamp((int)(input.hposition.x) / tileSize, 0, tileCountX - 1);
	int tileY = clamp((int)(input.hposition.y) / tileSize, 0, tileCountY - 1);
	int tileIndex = tileY * tileCountX + tileX;
	float4 tileHeader = texTileHeaders.Load(int3(tileX, tileY, 0));
	int tileLightCount = min((int)(tileHeader.y + 0.5f), maxTileLights);
	float3 directLight = float3(0.0f, 0.0f, 0.0f);

	[loop] for (int tileLight = 0; tileLight < tileLightCount; ++tileLight) {
		int lightIndex = (int)(texTileLightIndices.Load(int3(tileLight, tileIndex, 0)).x + 0.5f);
		float Rad = LightRadius[lightIndex >> 2][lightIndex & 3];
		float dist = distance(LightPositions[lightIndex].xyz, position.xyz);
		float attenuation = RangeAttenuation(Rad * 2.0f, dist);
		if (attenuation <= 0.0f)
			continue;

		float3 LightDir = normalize(LightPositions[lightIndex].xyz - position.xyz);
		float3 Half = normalize(EyeDir + LightDir);
		float intensity = LightColors[lightIndex].w;
		float3 lightRadiance = LightColors[lightIndex].xyz * intensity * attenuation;
		float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * lightRadiance;
		float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * lightRadiance;

		float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
		float3 F = FresnelCalc(VdotH, F0);
		float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);

		float NdotL = max(dot(normal, LightDir), 0.0f);
		float NdotVLight = max(dot(normal, EyeDir), 0.0f);
		float NdotH = max(dot(normal, Half), 0.0f);
		float albedoSheenScaling = 1.0f;
		float3 sheenLight = float3(0.0f, 0.0f, 0.0f);
		if (hasSheenLUT && sheenStrength > 0.0f) {
			albedoSheenScaling = min(1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotVLight, sheenRoughness),
			                         1.0f - sheenStrength * AlbedoSheenScalingLUT(NdotL, sheenRoughness));
			sheenLight = CalculateSheenRadiance(sheenColor, sheenRoughness, LightColors[lightIndex].xyz, intensity, NdotL, NdotVLight, NdotH) * attenuation;
		}

		float3 layerLight = sheenLight + (SpecularRes.xyz + Kd * Diffuse) * albedoSheenScaling;
		if (clearcoatFactor > 0.001f) {
			float clearcoatF = FresnelCalc(saturate(dot(normal, EyeDir)), float3(0.04f, 0.04f, 0.04f)).x;
			float clearcoatWeight = saturate(clearcoatFactor * clearcoatF);
			float3 clearcoatLight = CalculateClearcoat(normal, EyeDir, Half, LightDir, clearcoatRoughness) * lightRadiance;
			layerLight = lerp(layerLight, clearcoatLight, clearcoatWeight);
		}
		directLight += layerLight;
	}

	Final.xyz = directLight * Shadow * selfShadow;
	return Final;
}

#elif defined(SHADOW_COMP_PASS)
Texture2D tex0 : register(t0);
#ifdef ENABLE_SHADOWS
Texture2D tex1 : register(t1);
#endif
#ifdef ENABLE_SSAO
Texture2D tex2 : register(t2); // Packed geometric normals
Texture2D tex3 : register(t3); // Noise
#endif

#ifdef ENABLE_SHADOWS
float4 CalculateShadow(float4 position) {
	float4 FShadow = float4(1.0,1.0,1.0,1.0);

	float4 LightPos = mul(WVPLight, position);
	LightPos.xyz /= LightPos.w;
	float2 SHTC = LightPos.xy*0.5 + 0.5;
	SHTC.y = 1.0 - SHTC.y;

	if(SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x > 0.0 && SHTC.y > 0.0 && LightPos.w > 0.0 && LightPos.z > 0.0 && LightPos.z < 1.0) {
		float sum = 0.0;
		float x, y;
		float Total = 0.0;
		float Origin = brightness.x;
		[loop] for (y = -Origin; y <= Origin; y += 1.0) {
			[loop] for (x = -Origin; x <= Origin; x += 1.0) {
				float2 offset = (brightness.z / brightness.y) * float2(x, y);
				float2 sampleUV = SHTC.xy + offset;
				float Val_1;
				if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
					Val_1 = 0.0;
				} else {
					float depthSM = tex1.Sample(SS1, sampleUV);
					depthSM -= toogles.w;
					Val_1 = (LightPos.z < depthSM) ? 0.0 : 1.0;
				}
        Val_1 = Val_1 * (1.0 - toogles.x) + toogles.x;
				sum += Val_1;
				Total++;
			}
		}
		float shadowCoeff = sum / Total;
		FShadow = shadowCoeff * float4(1.0,1.0,1.0,1.0);
	} else {
    FShadow = toogles.x * float4(1.0,1.0,1.0,1.0);
	}

	return FShadow;
}
#endif

#ifdef ENABLE_SSAO
float3 GetNormal(float2 coords) {
	float4 normalmap = tex2.Sample(SS2, coords);
	return DecodeOctahedralNormal(normalmap.xy);
}

float GetOcclusion(float depth, float2 uv, float4 position, float3 normal) {
	float Radius = LightPositions[0].y;
	float2 Scale = float2(LightPositions[0].z / brightness.w, LightPositions[0].w / brightness.w);
	float3 pVec = tex3.Sample(SS3, Scale * uv).xyz * 2.0 - 1.0;

	float3 tangent = normalize(pVec - normal * dot(pVec, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 tbn = float3x3(tangent, bitangent, normal);

	float occlusion = 0.0;
	int KernelSize = (int)LightPositions[0].x;
	[loop] for (int i = 0; i < KernelSize; ++i) {
		float3 Spheresample = mul(LightPositions[i+1].xyz, tbn);
		Spheresample = Spheresample * Radius + position.xyz;
		float4 SpheresampleV = mul(WorldView, float4(Spheresample, 1.0));

		float4 offset = mul(Projection, float4(Spheresample, 1.0));
		offset.xy /= offset.w;
		float2 sampleClip = offset.xy;
		float2 sampleUV = sampleClip * 0.5 + 0.5;
		sampleUV.y = 1.0 - sampleUV.y;
		if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
			continue;

		float sampleDepth = tex0.Sample(SS, sampleUV).r;
		if (!IsSceneDepthValid(sampleDepth))
			continue;

		float4 new_position = ReconstructPosition(sampleClip, sampleDepth);

		float4 new_positionV = mul(WorldView, float4(new_position.xyz, 1.0));

		float rangeCheck = abs(SpheresampleV.z - new_positionV.z) < Radius ? 1.0 : 0.0;
		occlusion += ((new_positionV.z < SpheresampleV.z) ? 1.0 : 0.0) * rangeCheck;
	}

	occlusion = 1.0 - (occlusion / LightPositions[0].x);
	return occlusion;
}
#endif

float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Fcolor = float4(1.0,1.0,1.0,1.0);
	float depth = tex0.Sample( SS, input.texture0 );
	if (!IsSceneDepthValid(depth))
		return Fcolor;

	float4 position = ReconstructPosition(input.ClipPos, depth);

	#ifdef ENABLE_SHADOWS
		Fcolor = CalculateShadow(position);
	#endif

	#ifdef ENABLE_SSAO
		float3 normal = GetNormal(input.texture0);
		float Occlusion = GetOcclusion(depth, input.texture0.xy, position, normal);
		Fcolor *= Occlusion;
	#endif

	return Fcolor;
}
#elif defined(VERTICAL_BLUR_PASS)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	uint texWidth, texHeight;
	tex0.GetDimensions(texWidth, texHeight);
	float2 U = LightPositions[0].y / float2(max(1u, texWidth), max(1u, texHeight));
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -(((float)(KernelSize - 1)) * 0.5);
	float2 Texcoords;
	for(int i=0;i<KernelSize;i++){
		float V = Origin + (float)i;
		Texcoords.xy = float2(input.texture0.x ,input.texture0.y + V*U.y);
		Sum.xyz += LightPositions[i+1].x * tex0.SampleLevel( SS, Texcoords.xy, 0.0f ).xyz;
	}
	return Sum;
}
#elif defined(HORIZONTAL_BLUR_PASS)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	uint texWidth, texHeight;
	tex0.GetDimensions(texWidth, texHeight);
	float2 U = LightPositions[0].y / float2(max(1u, texWidth), max(1u, texHeight));
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -(((float)(KernelSize - 1)) * 0.5);
	float2 Texcoords;
	for(int i=0;i<KernelSize;i++){
		float H = Origin + (float)i;
		Texcoords.xy = float2(input.texture0.x + H*U.x ,input.texture0.y );
		Sum.xyz += LightPositions[i+1].x * tex0.SampleLevel( SS, Texcoords.xy, 0.0f ).xyz;
	}
	return Sum;
}
#elif defined(ONE_PASS_BLUR)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	uint texWidth, texHeight;
	tex0.GetDimensions(texWidth, texHeight);
	float2 U = LightPositions[0].y / float2(max(1u, texWidth), max(1u, texHeight));
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -(((float)(KernelSize - 1)) * 0.5);
	float2 Texcoords;	
	for(int i=0;i<KernelSize;i++){
		float H = Origin + (float)i;
		Texcoords.x = input.texture0.x + H*U.x;
		for(int j=0;j<KernelSize;j++){
			float V = Origin + (float)j;
			Texcoords.y = input.texture0.y + V*U.y;
			float weight = roundTo(LightPositions[i+1].x*LightPositions[j+1].x,6.0);
			Sum.xyz += weight * tex0.SampleLevel( SS, Texcoords.xy, 0.0f ).xyz;
		}
	}
	
	return Sum;
}
#elif defined(BRIGHT_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float3 color = tex0.Sample(SS, input.texture0).rgb;
	float avgLuminance = exp(tex1.Sample(SS1, float2(0.5f, 0.5f)).r);

	avgLuminance = max(avgLuminance, 0.001f);
	float keyValue = 1.03f - (2.0f / (2.0f + log10(avgLuminance + 1.0f)));
	float linearExposure = keyValue / avgLuminance;
	float exposureComp = LightPositions[0].y;
	float threshold = LightPositions[0].x;
	color = exp2(log2(max(linearExposure, 0.0001f)) + exposureComp - threshold) * color;

	float pixelLuminance = max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
	float whiteLevel = max(LightPositions[0].z, 0.001f);
	float toneMappedLuminance = pixelLuminance * (1.0f + pixelLuminance / (whiteLevel * whiteLevel)) / (1.0f + pixelLuminance);
	float3 toneMapped = toneMappedLuminance * (color / pixelLuminance);

	return float4(toneMapped, 1.0f);
}
#elif defined(ADAPT_LUMINANCE_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);

float FullAverageLogLuminance()
{
  float sumLogLum = 0.0f;
  [unroll] for (int y = 0; y < 8; ++y) {
    [unroll] for (int x = 0; x < 8; ++x) {
      float2 uv = (float2((float)x, (float)y) + 0.5f) * 0.125f;
      float3 color = tex1.SampleLevel(SS1, uv, 0.0f).rgb;
      float luminance = max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
      sumLogLum += log(luminance);
    }
  }
  return sumLogLum / 64.0f;
}

float RobustAverageLogLuminance()
{
  const float minLogLum = -4.60517019f; // log(0.01)
  const float maxLogLum =  2.77258872f; // log(16.0)
  float sumLogLum = 0.0f;
  float minSample =  1.0e20f;
  float maxSample = -1.0e20f;

  [unroll] for (int y = 0; y < 8; ++y) {
    [unroll] for (int x = 0; x < 8; ++x) {
      float2 uv = (float2((float)x, (float)y) + 0.5f) * 0.125f;
      float3 color = tex1.SampleLevel(SS1, uv, 0.0f).rgb;
      float luminance = max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
      float logLum = clamp(log(luminance), minLogLum, maxLogLum);
      sumLogLum += logLum;
      minSample = min(minSample, logLum);
      maxSample = max(maxSample, logLum);
    }
  }

  return (sumLogLum - minSample - maxSample) / 62.0f;
}

float4 FS(VS_OUTPUT input) : SV_TARGET {
  int mode = (int)(LightPositions[1].z + 0.5f);
  float currentLogLum = 0.0f;
  if (mode == 0) {
    currentLogLum = FullAverageLogLuminance();
  } else {
    currentLogLum = RobustAverageLogLuminance();
  }

  float currentLum = max(exp(currentLogLum), 0.0001f);
  if (mode != 0) {
    currentLum = clamp(currentLum, 0.05f, 16.0f);
  }

  float previousLum = max(exp(tex0.SampleLevel(SS, float2(0.5f, 0.5f), 0.0f).r), 0.0001f);
  float tau = max(LightPositions[1].x, 0.0f);
  float dt = max(LightPositions[1].y, 0.0f);
  float blend = 1.0f - exp(-dt * tau);
  float adaptedLum = lerp(previousLum, currentLum, saturate(blend));
  return float4(log(max(adaptedLum, 0.0001f)), 1.0f, 1.0f, 1.0f);
}
#elif defined(HDR_COMP_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
  float3 hdrColor = tex0.Sample(SS, input.texture0).rgb;
  float avgLuminance = exp(tex2.Sample(SS2, float2(0.5f, 0.5f)).r);

  avgLuminance = max(avgLuminance, 0.001f);
  float keyValue = 1.03f - (2.0f / (2.0f + log10(avgLuminance + 1.0f)));
  float linearExposure = keyValue / avgLuminance;
  float exposureComp = LightPositions[0].y;
  float3 color = exp2(log2(max(linearExposure, 0.0001f)) + exposureComp) * hdrColor;

  float pixelLuminance = max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
  float whiteLevel = max(LightPositions[0].z, 0.001f);
  float toneMappedLuminance = pixelLuminance * (1.0f + pixelLuminance / (whiteLevel * whiteLevel)) / (1.0f + pixelLuminance);
  float3 toneMapped = toneMappedLuminance * (color / pixelLuminance);

  return float4(toneMapped + LightPositions[0].x * tex1.Sample(SS1, input.texture0).rgb, 1.0f);
}
#elif defined(COC_PASS)
struct FS_OUT{
	float color0 : SV_TARGET0;
	float color1 : SV_TARGET1;
};
Texture2D tex0 : register(t0);
FS_OUT FS( VS_OUTPUT input ) : SV_TARGET {	
	float aperture = LightPositions[0].x;
	float focalLength = LightPositions[0].y;
	FS_OUT OUT;
	OUT.color0 = 0.0;
	OUT.color1 = 0.0;

	float z = tex0.Sample( SS, input.texture0.xy ).r;
	if (!IsSceneDepthValid(z))
		return OUT;

	float depthFocus;
  #ifdef AUTO_FOCUS
    depthFocus = tex0.Sample(SS, float2(0.5, 0.5)).r;// Auto Focus center
  #else
    depthFocus = LightPositions[0].z;
  #endif
	if (!IsSceneDepthValid(depthFocus))
		depthFocus = z;

	bool near = (z > depthFocus);
	float objectdistance = LinearizeDepth(z);
  float FocusPlane = LinearizeDepth(depthFocus);
	float denominator = objectdistance * (FocusPlane - focalLength);
	if (abs(denominator) <= 0.00001f)
		return OUT;
	float CoC = abs(aperture * (focalLength * (objectdistance - FocusPlane)) / denominator);
	if (near) {
    OUT.color0 = clamp(CoC, 0, LightPositions[0].w);
    OUT.color1 = 0;
	}
	else {
    OUT.color0 = 0;
    OUT.color1 = clamp(CoC, 0, LightPositions[0].w);
	}
	return OUT;
}
#elif defined(COMBINE_COC_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float FS(VS_OUTPUT input) : SV_TARGET{
  float CoC0 = tex0.Sample(SS, input.texture0).r;
  float CoC1 = tex1.Sample(SS1, input.texture0).r;
	float CoC =  2*max(CoC0, CoC1) - CoC0;
  return CoC;
}

#elif defined(DOF_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float dofblur = tex1.Sample(SS1, input.texture0).r;
  float4 color = tex0.Sample(SS, input.texture0);
	if (dofblur <= DEPTH_CLEAR_EPSILON)
		return color;
	float2 offset = float2(1.0 / LightPositions[0].z, 1.0 / LightPositions[0].w);
	int samplesSquared = max((int)LightPositions[0].y, 0);
	float total = 0.0;

	[loop] for (int i = -samplesSquared; i <= samplesSquared; i++) {
		[loop] for (int j = -samplesSquared; j <= samplesSquared; j++) {
			float2 tcoord = input.texture0 + float2(i, j) * offset * dofblur;
      color+= tex0.Sample(SS, tcoord);
			total++;
    }
  }
	color /= max(total, 1.0);
  color.a = 1.0;

  return color;
}
#elif defined(DOF_PASS_2)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
float dofblur = tex1.Sample(SS1, input.texture0).r;
float4 color = tex0.Sample(SS, input.texture0);
if (dofblur <= DEPTH_CLEAR_EPSILON)
	return color;
float2 offset = float2(1.0 / LightPositions[0].z, 1.0 / LightPositions[0].w);
int samplesSquared = max((int)LightPositions[0].x, 0);
float total = 0.0;

	[loop] for (int i = -samplesSquared; i <= samplesSquared; i++) {
	[loop] for (int j = -samplesSquared; j <= samplesSquared; j++) {
		float2 tcoord = input.texture0 + float2(i, j) * offset * dofblur;
    color += tex0.Sample(SS, tcoord);
		total++;
  }
}
color /= max(total, 1.0);
color.a = 1.0;

return color;
}

#elif defined(BACKBUFFER_PASS)
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET{
	float4 color = tex0.Sample(SS, input.texture0.xy);
	return float4(LinearToSRGB(color.rgb), color.a);
}

#elif defined(GOD_RAY_CALCULATION_PASS)
#define raysSamples 64
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float2 uv = input.texture0.xy;
  const float2 defaultPos = float2(0.8, 0.4);
  const float2 raysCenter = float2(0.435, 1.0 - 0.59);
  const float raySize = 0.2;
  float scale = (1.0 - defaultPos.y) * raySize;
  const float accum = 1.0 / (float)raysSamples;
  scale = lerp(1.0, 1.0 - accum, scale);
  float3 col = tex0.Sample(SS, uv).rgb;
  for (int i = 0; i < raysSamples; ++i) {
    uv = (uv - raysCenter) * scale + raysCenter;
    col += tex0.Sample(SS, uv).rgb;
  }
  float3 rays =  col * accum;
  rays = pow(rays, float3(0.4545, 0.4545, 0.4545));
  rays = smoothstep(defaultPos.x, 1.0, rays);
  rays *= toogles.x;
  return float4(rays , 1.0);
}

#elif defined(GOD_RAY_BLEND_PASS)
#define raysIntensity 0.25
#define raysSaturation 0.5
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float2 uv = input.texture0.xy;
  float3 col = tex0.Sample(SS, uv).rgb;
  float3 rays = tex1.Sample(SS1, uv).rgb;
  if (rays.x < 0.1 && rays.y < 0.1 && rays.z < 0.1)
    return float4(col,1.0);
  float3 colorWeights = float3(0.299, 0.587, 0.114);
  float tt = dot(rays, colorWeights);
  rays = lerp(float3(tt,tt,tt), rays, raysSaturation);
  rays = pow(rays, float3(2.2,2.2,2.2));

  col += rays * float3(raysIntensity, raysIntensity, raysIntensity);
  //col -= lerp(float3(0.0,0.0,0.0), float3(1.0,1.0,1.0) - rays, raysIntensity);

  return float4(col , 1.0);
}

#elif defined(SSAO_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float2 uv = input.texture0.xy;
  float3 col = tex0.Sample(SS, uv).rgb;
  float depth = tex1.Sample(SS1, uv);

  float ao = 0.0;
  float2 texSize = float2(1.0,1.0) / float2(1280 , 720);
  const float2 randVec[8] = {
    float2(0.1,0.3),
    float2(0.35,0.8249),
    float2(0.3489,0.15680),
    float2(0.230489,0.3458),
    float2(0.158,0.158),
    float2(0.237689,0.920469),
    float2(0.680462,0.8457),
    float2(0.89,0.0247895)
  };
  const float raius = 3;
  const float dVal = 0.01;
  for (int i =0 ; i<4; i++)
  {
    float val = randVec[i]* raius* -1 * texSize.x;
    float z = tex1.Sample(SS1, (float2(uv.x +  val,uv.y))).x;
		if (z - depth < dVal)
			ao += clamp((z - depth), 0.0, 1.0);
  }
  for (int i = 0; i<4; i++)
  { 
    float val = randVec[i+4] * raius * texSize.x;
    float z = tex1.Sample(SS1, (float2(uv.x + val, uv.y))).x;
		if (z - depth < dVal)
			ao += clamp((z - depth), 0.0, 1.0);
  }
  for (int i = 0; i<4; i++)
  {
    float val = randVec[i] * raius* -1 * texSize.y;
    float z = tex1.Sample(SS1, (float2( uv.x, uv.y + val))).y;
		if (z - depth < dVal)
			ao += clamp((z - depth), 0.0, 1.0);
  }
  for (int i = 0; i<4; i++)
  {
    float val = randVec[i+4] * raius*texSize.y;
    float z = tex1.Sample(SS1, (float2(uv.x, uv.y +  val))).y;
		if (z - depth < dVal)
			ao += clamp((z - depth), 0.0, 1.0);
  }
 // ao *= 50;
  ao = 1.0 - ao / 8.0;
  ao = pow(ao, 32);
  ao = clamp(ao, 0, 1);
  return float4(ao,ao,ao , 1.0);
}

#elif defined(RAY_MARCH)
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
//Get a random number
float rand(float x) {
  return tex2.Sample(SS2, x).r;
}
float rand2D(float2 x) {
  //return clamp (tex2.Sample(SS2, x).rgb,0,1);
  return frac(sin(dot(x.xy,
    float2(12.9898, 78.233)))*
    43758.5453123 *  (LightPositions[0].x*0.1)   );
}
//Generate noise
float noise(float x) {
  float i = floor(x);
  float f = frac(x);
  //float y = rand(i);
  float y = lerp(rand(i), rand(i + 1.0), smoothstep(0., 1., f));
  return y;
}
float noise2D( float2 st) {
  float2 i = floor(st);
  float2 f = frac(st);

  // Four corners in 2D of a tile
  float a = rand2D(i);
  float b = rand2D(i + float2(1.0, 0.0));
  float c = rand2D(i + float2(0.0, 1.0));
  float d = rand2D(i + float2(1.0, 1.0));

  // Smooth Interpolation

  // Cubic Hermine Curve.  Same as SmoothStep()
  //float2 u = f*f*(3.0 - 2.0*f);
  float2 u = smoothstep(0.,1.,f);

  // Mix 4 coorners porcentages
  return lerp(a, b, u.x) +
    (c - a)* u.y * (1.0 - u.x) +
    (d - b) * u.x * u.y;
}
//float noise3D(float3 x) {
//  float3 i = floor(x);
//  float3 f = fract(x);
//  //float y = rand(i);
//  float y = lerp(rand(i), rand(i + 1.0), smoothstep(0., 1., f));
//  return y;
//}
bool
IntersectBox(float3 rayO, float3 rayDir, float3 boxmin, float3 boxmax, out float tnear,
  out float tfar)
{
  // compute intersection of ray with all six bbox planes
  float3 invR = 1.0 / rayDir;
  float3 tbot = invR * (boxmin.xyz - rayO);
  float3 ttop = invR * (boxmax.xyz - rayO);
  // re-order intersections to find smallest and largest on each axis
  float3 tmin = min(ttop, tbot);
  float3 tmax = max(ttop, tbot);
  // find the largest tmin and the smallest tmax
  float2 t0 = max(tmin.xx, tmin.yz);
  tnear = max(t0.x, t0.y);
  t0 = min(tmax.xx, tmax.yz);
  tfar = min(t0.x, t0.y);
  // check for hit
  bool hit;
  if ((tnear > tfar))
    hit = false;
  else
    hit = true;
  return hit;
}
half4
Fire(half3 x)
{
  const half flameScale = 1;
  const half flameTrans = 1;
  //x = x*flameScale + flameTrans;
  // calculate radial distance in XZ plane
  half2 uv;
  uv.x = length(x.xz)/1.42;
  uv.y = x.y;//+ turbulence4(noiseSampler, noisePos) * noiseStrength;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return half4(0.0,1.0,0.0,1);
  half3 ret = tex1.Sample(SS1, uv.xy).rgb;
  //return float4(1,0,0,1);
  return half4(ret,0.1);
}
half4
Fire2(half3 x)
{
  const half flameScale = 1;
  const half flameTrans = 1;
  //x = x*flameScale + flameTrans;
  // calculate radial distance in XZ plane
  half2 uv;
  uv.x = length(x.xz) / 1.42;
  uv.y = x.y;//+ turbulence4(noiseSampler, noisePos) * noiseStrength;
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return half4(0.0, 0.0, 0.0, 0.0);
  //float3 ret = tex1.Sample(SS1, uv.xy).rgb;
  return half4(uv.y,0,0,uv.y);
  //return float4(ret, 0.1);
}

// returns signed distance to surface
half distanceFunc(half3 p)
{
  half3 sc = half3(0.5,0.5,0.5);
  // distance to sphere
  half d = length(p - sc);
  // offset distance with noise
  //d += fbm(p*_NoiseFreq + _NoiseAnim*iTime) * _NoiseAmp;
  return d;
}

// shade a point based on distance
half4 shade(half d)
{
  if (d >= 0.0 && d < 0.1) return (lerp(half4(3, 3, 3, 1), half4(1, 1, 0, 1), d / 0.1));
  if (d >= 0.1 && d < 0.2) return (lerp(half4(1, 1, 0, 1), half4(1, 0, 0, 1), (d - 0.1) / 0.1));
  if (d >= 0.2 && d < 0.3) return (lerp(half4(1, 0, 0, 1), half4(0, 0, 0, 0), (d - 0.2) / 0.1));
  if (d >= 0.4 && d < 0.5) return (lerp(half4(0, 0, 0, 0), half4(0, .5, 1, 0.2), (d - 0.3) / 0.1));
  //if (d >= 0.8 && d < 1.0) return (lerp(float4(0, .5, 1, .2), float4(0, 0, 0, 0), (d - 0.8) / 0.2));
  return half4(0, 0, 0, 0);
}
half4
Ball(half3 x)
{
  half time = LightPositions[0].x;
  half2 uv;
  uv.x = length(x.xz) / 1.42;
  uv.y = x.y;//+ turbulence4(noiseSampler, noisePos) * noiseStrength;
  half d = distanceFunc(x);
  float noiseVal = noise2D(uv*40.0);
  d = d+cos(time*noiseVal)*0.1;
  d = d + sin(time * 2 )*0.2*noiseVal;
  //return half4(noiseVal, noiseVal, noiseVal, 1);
  half4 rret = shade(d);
  //rret.a *=;
  return rret;
}
#define VOLUMEFUNC(x) Ball(x)
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float depth = tex0.Sample(SS, input.texture0);
	if (!IsSceneDepthValid(depth))
		return float4(0.0, 0.0, 0.0, 0.0);
	float4 position = ReconstructPosition(input.ClipPos, depth);
  const int steps = 64;
  half4 ray = (position- CameraPosition);
  half4 rayDir = normalize(ray);
  half rayLength = length(ray);

  half tnear;
  half tfar;
  half boxMin = -20;
  half boxMax = 20;
  half boxSize = boxMax - boxMin;
  bool hit = IntersectBox(CameraPosition,rayDir, boxMin, boxMax, tnear, tfar);
  if (!hit) discard;
  if (tfar < 0.0) discard;
  //if (tnear > rayLength) discard;//UNCOMMENT
  if (tnear < 0.0) tnear = 0.0;

  half4 intersectionNear =  CameraPosition + rayDir*tnear;
  half4 intersectionFar =  CameraPosition + rayDir*tfar;

  //March
  half4 c = 0;
  //float alpha = 0;
  half4 step = (intersectionNear- intersectionFar) / (steps - 1);
  half4 P = intersectionFar;
  [loop] for (int i = 0; i<steps; i++) {
    //if (c.a >= 1.0) break;
    half3 ppp = (P - boxMin) / boxSize; //0 - 1 normalized coords

    half4 s = VOLUMEFUNC(ppp);
    //alpha = s.a + (1.0 - s.a)*alpha;
    c = s.a*s.rgba + (1.0 - s.a)*c; //TODO: Front to back
    //s.rgb *= s.a;
    //c += c *(1.0 - s.a);
    P += step;
  }
  //c /= steps;
  return float4(c);
}
#elif defined(LIGHT_RAY_MARCHING)
#define G_SCATTERING -0.2
#define PI 3.14159265359
float ComputeScattering(float lightDotView)
{
  float result = 1.0f - G_SCATTERING * G_SCATTERING;
  result /= (4.0f * PI * pow(1.0f + G_SCATTERING * G_SCATTERING - (2.0f * G_SCATTERING) *      lightDotView, 1.5f));
  return result;
}
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  #ifndef ENABLE_GOD_RAYS
  return float4(0,0,0,1);
  #else
	float2 uv = input.texture0;
	float depth = tex0.SampleLevel(SS, uv, 0.0f);
if (!IsSceneDepthValid(depth))
	return float4(0,0,0,1);
float2 clipPos = input.ClipPos;
float4 position = ReconstructPosition(clipPos, depth);
int steps = max((int)LightPositions[0].y, 2);
float4 ray = (position - CameraPosition);
float4 rayDir = normalize(ray);

float4 intersectionNear = CameraPosition;
float4 intersectionFar = position;

float3 accumFog = 0.0f.xxx;

const float3 lightColor = float3(0.9803, 0.8392, 0.6470);
const float3 sunLightDir = normalize(LightColors[0].xyz);
float shadowBias = max(toogles.w, 0.0f);
// Avoid backend-dependent drift from accumulating P += step over many samples.
[loop] for (int i = 0; i<steps; i++) {
	float rayT = (float)i / (float)(steps - 1);
	float4 P = lerp(intersectionFar, intersectionNear, rayT);
	float4 LightPos = mul(WVPLight, P);
	LightPos.xyz /= LightPos.w;
  float2 SHTC = LightPos.xy*0.5 + 0.5;
	SHTC.y = 1.0f - SHTC.y;

	if (SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x > 0.0 && SHTC.y > 0.0 && LightPos.z > 0.0 && LightPos.z < 1.0)
  {
		float Val_1 = tex1.SampleLevel(SS1, SHTC, 0.0f);
		Val_1 -= shadowBias;
		bool accum = (LightPos.z >= Val_1);
		if (accum) {
			float3 scattering = lightColor * ComputeScattering(dot(rayDir.rgb, sunLightDir));
			accumFog += scattering ;
		}
  }
}
accumFog /= (float)steps;
accumFog = pow(accumFog, float3(0.4545, 0.4545, 0.4545));
accumFog *= toogles.x;
return float4(accumFog,1);
  #endif
}
#elif defined(LIGHT_ADD)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float raysIntensity = 1.6;
  float4 col = tex1.Sample(SS1, input.texture0.xy);
  float4 vol = tex0.Sample(SS, input.texture0.xy);
  vol.rgb = pow(vol.rgb, float3(2.2, 2.2, 2.2));
  col += vol * float4(raysIntensity, raysIntensity, raysIntensity, raysIntensity);
  //col -= lerp(float4(0,0,0,0), float4(1,1,1,1) - vol, raysIntensity);
	return float4(col.rgb, 1.0f);
}
#elif defined(FSQUAD_1_TEX)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {	
	return  tex0.Sample( SS, input.texture0.xy );

  /*float d = tex0.Sample(SS, input.texture0.xy).r;
  return  float4(d,d,d,1);*/
}
#elif defined(DEPTH_PRE_PASS)
float4 FS(VS_OUTPUT input) : SV_TARGET{
  return float4(0,0,0,1);
}
#elif defined(FSQUAD_2_TEX)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	return tex0.Sample( SS, input.texture0) + tex1.Sample( SS, input.texture0);
}
#elif defined(FSQUAD_3_TEX)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	//return tex0.Sample( SS, input.texture0);
	
	int mip = ((int)CameraPosition.w);
	float avgLuminance = dot( tex0.SampleLevel( SS, input.texture0 , mip).rgb , float3(0.299f, 0.587f, 0.114f) );
	return float4(avgLuminance,avgLuminance,avgLuminance,1.0);
}
#elif defined(FADE)
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	return float4(0.0, 0.0, 0.0, brightness.x);
}
#elif defined(LENS_FLARE_SUN)
float4 FS(VS_OUTPUT input) : SV_TARGET {
	float2 centered = input.texture0.xy * 2.0f - 1.0f;
	float dist = length(centered);
	float core = 1.0f - smoothstep(0.0f, 0.28f, dist);
	float halo = 1.0f - smoothstep(0.0f, 1.0f, dist);
	float mask = saturate(core + halo * 0.35f);
	float3 color = float3(1.0f, 0.78f, 0.42f) * halo + float3(1.0f, 0.98f, 0.86f) * core;
	return float4(color * mask, saturate(brightness.x));
}
#elif defined(LENS_FLARE_GHOST)
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET {
	float4 tex = tex0.Sample(SS, input.texture0.xy);
	float alpha = saturate(tex.a);
	clip(alpha - 0.003f);
	return float4(saturate(tex.rgb) * alpha, saturate(brightness.x));
}
#else
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	return tex0.Sample( SS, input.texture0);
}
#endif
