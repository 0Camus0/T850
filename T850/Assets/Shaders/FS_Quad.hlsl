cbuffer ConstantBuffer{
    float4x4 WVP;
	float4x4 World;  
	float4x4 WorldView;
	float4x4 WVPInverse;
	float4x4 WVPLight;
	float4x4 Projection;
	float4	 LightPositions[128];
	float4	 LightColors[128];
  float4	 LightRadius[32];
	float4   CameraPosition;
	float4 	 CameraInfo;
	float4	 LightCameraPosition;
	float4 	 LightCameraInfo;

	float4   brightness;
	float4   toogles;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
    float2 texture0  : TEXCOORD;
	float4 Pos		: TEXCOORD1;
	float4 PosCorner : VPOS;
};

SamplerState SS  : register(s0);
SamplerState SS1 : register(s1);

float roundTo(float num,float decimals){
	float shift = pow(10.0,decimals);
	return round(num*shift) / shift;
}

#ifdef DEFERRED_PASS
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
Texture2D tex3 : register(t3);
Texture2D tex4 : register(t4);
Texture2D tex5 : register(t5);
TextureCube texEnv : register(t6);

float3 NormalDistribution(float NdotH, float roughness)
{
	// GGX
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH2 = NdotH * NdotH;
	float Num = a2;
	float Denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
	Denom = 3.1415926 * Denom * Denom;
	float res = Num / Denom;
	return float3(res, res, res);
}

float3 FresnelCalc(float VdotH, float3 specColor)
{
	// Schlick
	return (specColor + (1.0f - specColor) * pow(1.0f - VdotH, 5.0f));
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float GeometrySchlickGGX(float Ndot, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;
	float Num = Ndot;
	float Denom = Ndot * (1.0 - k) + k;
	return clamp(Num / Denom, 0.0f, 1.0f);
}

float3 GeometricShadowing(float NdotL, float NdotV, float roughness)
{
	// Geometry Smith
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float res = clamp(ggx1 * ggx2, 0.0f, 1.0f);
	return float3(res, res, res);
}

float3 CalculateSpecular(float3 specularColor, float3 normal, float3 view, float3 halfvector, float3 light, float roughness)
{
	float NdotH = max(dot(normal, halfvector), 0.0f);
	float VdotH = clamp(dot(view, halfvector), 0.0f, 1.0f);
	float NdotL = clamp(dot(normal, light), 0.0f, 1.0f);
	float NdotV = clamp(dot(normal, view), 0.0f, 1.0f);

	float3 Num = FresnelCalc(VdotH, specularColor) * NormalDistribution(NdotH, roughness) * GeometricShadowing(NdotL, NdotV, roughness);
	float denomRes = (4.0f * (NdotL * NdotV) + 0.01f);
	float3 Denom = float3(denomRes, denomRes, denomRes);

	return (Num / Denom);
}

float3 CalculateDiffuse(float3 albedoColor, float3 normal, float3 light)
{
	return albedoColor * clamp(dot(normal, light), 0.0f, 1.0f);
}

float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Final = float4(0.0,0.0,0.0,1.0);
	float Shadow = 1.0;

	float4 Albedo = tex0.Sample(SS, input.texture0);
	float4 PBRData = tex2.Sample(SS, input.texture0);

	Albedo.xyz = pow(Albedo.xyz, float3(2.2, 2.2, 2.2));

	float metallic = PBRData.r;
	float3 F0 = lerp(float3(0.04, 0.04, 0.04), Albedo.xyz, metallic);

	float depth = tex4.Sample(SS, input.texture0).r;

	#ifdef NON_LINEAR_DEPTH
		float4 position = mul(WVPInverse,float4( input.PosCorner.xy ,depth,1.0));
		position.xyz /= position.w;
	#else		
		float4 position = CameraPosition + input.PosCorner*depth;
	#endif
	 
	float3 EyeDir = normalize(CameraPosition - position).xyz;

	int MatId = (int)(PBRData.a * 255.0);

	if(MatId == 0){
		float3 EyeDir_mod = -EyeDir;
		EyeDir_mod.x = -EyeDir_mod.x;
		EyeDir_mod.z = -EyeDir_mod.z;
		float3 RefCol = texEnv.Sample(SS, EyeDir_mod).xyz;
		Final.xyz = RefCol.xyz * 2.0;
	} else if(MatId > 0) {
		Shadow = tex5.Sample(SS, input.texture0).r;

		float cutoff = 0.8;

		float4 normalmap = tex1.Sample(SS, input.texture0);
		float3 normal = normalmap.xyz * 2.0 - 1.0;
		normal = normalize(normal);

		float3 geoNormal = tex3.Sample(SS, input.texture0).xyz * 2.0 - 1.0;
		geoNormal = normalize(geoNormal);

		float3 ReflectedVec = reflect(-EyeDir, normal.xyz);
		ReflectedVec.x = -ReflectedVec.x;
		ReflectedVec.z = -ReflectedVec.z;

		float rough = normalmap.a;

		int NumLights = (int)CameraInfo.w;
		[loop] for(int i = 0; i < NumLights; i++){
			float lightType = LightPositions[i].w;
			float intensity = LightColors[i].w;

			if(lightType < 0.5) {
				// Directional light
				float3 LightDir = normalize(-LightPositions[i].xyz);
				float3 Half = normalize(EyeDir + LightDir);

				float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * LightColors[i].xyz * intensity;
				float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * LightColors[i].xyz * intensity;

				float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
				float3 F = FresnelCalc(VdotH, F0);
				float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);

				float geoHorizon = saturate(dot(geoNormal, LightDir));
				Final.xyz += (SpecularRes.xyz + Kd * Diffuse) * geoHorizon;
			} else {
				// Point light
				float Rad = LightRadius[i >> 2][i & 3];
				float dist = distance(LightPositions[i], position);

				if(dist < (Rad * 2.0))
				{
					float3 LightDir = normalize(LightPositions[i] - position).xyz;
					float3 Half = normalize(EyeDir + LightDir);

					float3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir) * LightColors[i].xyz * intensity;
					float3 SpecularRes = CalculateSpecular(F0, normal, EyeDir, Half, LightDir, rough) * LightColors[i].xyz * intensity;

					float VdotH = clamp(dot(EyeDir, Half), 0.0f, 1.0f);
					float3 F = FresnelCalc(VdotH, F0);
					float3 Kd = (float3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);

					float d = max(dist - Rad, 0.0);
					float denom = d / Rad + 1.0;

					float attenuation = 1.0 / (denom * denom);
					attenuation = (attenuation - cutoff) / (1.0 - cutoff);
					attenuation = max(attenuation, 0.0);

					float geoHorizon = saturate(dot(geoNormal, LightDir));
					Final.xyz += (SpecularRes.xyz * attenuation + attenuation * Kd * Diffuse) * geoHorizon;
				}
			}
		}

		float3 kSpecular = clamp(fresnelSchlickRoughness(max(dot(normal, EyeDir), 0.0f), F0, rough), 0.0, 1.0);
		float3 RefleCol = texEnv.SampleLevel(SS, ReflectedVec, rough * 4.0f).xyz;
		float envAtten = (1.0f - rough) * (1.0f - rough);

    Final.xyz += RefleCol * kSpecular.xyz * envAtten * toogles.x;

		Final.xyz *= Shadow;
	}
	return Final;
}
#elif defined(SHADOW_COMP_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2); // Normals (geometric)
Texture2D tex3 : register(t3); // Noise

float4 CalculateShadow(float4 position) {
	float4 FShadow = float4(1.0,1.0,1.0,1.0);

	float4 LightPos = mul(WVPLight, position);
#ifdef NON_LINEAR_DEPTH
	LightPos.xyz /= LightPos.w;
#else
	LightPos.xy /= LightPos.w;
	LightPos.z /= LightCameraInfo.y;
#endif
	float2 SHTC = LightPos.xy*0.5 + 0.5;
	SHTC.y = 1.0 - SHTC.y;

	if(SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x > 0.0 && SHTC.y > 0.0 && LightPos.w > 0.0 && LightPos.z < 1.0) {
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
					depthSM += toogles.w;
					Val_1 = (LightPos.z > depthSM) ? 0.0 : 1.0;
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

float3 GetNormal(float2 coords) {
	float4 normalmap = tex2.Sample(SS, coords);
	float3 normal = normalmap.xyz * 2.0 - 1.0;
	normal = normalize(normal);
	return normal;
}

float GetOcclusion(float depth, float2 uv, float4 position, float3 normal, float4 posCorner) {
	float Radius = LightPositions[0].y;
	float2 Scale = float2(LightPositions[0].z / brightness.w, LightPositions[0].w / brightness.w);
	float3 pVec = tex3.Sample(SS, Scale * uv).xyz * 2.0 - 1.0;

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
		offset.xy = offset.xy * 0.5 + 0.5;
		offset.y = 1.0 - offset.y;

		float sampleDepth = tex0.Sample(SS, offset.xy).r;

	#ifdef NON_LINEAR_DEPTH
		float4 new_position = mul(WVPInverse, float4(posCorner.xy, sampleDepth, 1.0));
		new_position.xyz /= new_position.w;
		new_position.w = 1.0;
	#else
		float4 new_position = CameraPosition + posCorner * sampleDepth;
	#endif

		float4 new_positionV = mul(WorldView, float4(new_position.xyz, 1.0));

		float rangeCheck = abs(SpheresampleV.z - new_positionV.z) < Radius ? 1.0 : 0.0;
		occlusion += ((new_positionV.z < SpheresampleV.z) ? 1.0 : 0.0) * rangeCheck;
	}

	occlusion = 1.0 - (occlusion / LightPositions[0].x);
	return occlusion;
}

float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Fcolor = float4(1.0,1.0,1.0,1.0);
	float depth = tex0.Sample( SS, input.texture0 );

	#ifdef NON_LINEAR_DEPTH
		float4 position = mul(WVPInverse,float4( input.PosCorner.xy ,depth,1.0));
		position.xyz /= position.w;
		position.w = 1.0;
	#else		
		float4 position = CameraPosition + input.PosCorner*depth;
	#endif

	#ifdef ENABLE_SHADOWS
		Fcolor = CalculateShadow(position);
	#endif

	#ifdef ENABLE_SSAO
		float3 normal = GetNormal(input.texture0);
		float Occlusion = GetOcclusion(depth, input.texture0.xy, position, normal, input.PosCorner);
		Fcolor *= Occlusion;
	#endif

	return Fcolor;
}
#elif defined(VERTICAL_BLUR_PASS)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	float2 U = LightPositions[0].y*float2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -((((float)(KernelSize))-3.0)/2.0);
	float V = Origin;
	float2 Texcoords;
	for(int i=1;i<(KernelSize-1);i++){	
		Texcoords.xy = float2(input.texture0.x ,input.texture0.y + V*U.y);
		Sum.xyz += LightPositions[i+1].x * tex0.Sample( SS, Texcoords.xy ).xyz;
		V++;
	}
	return Sum;
}
#elif defined(HORIZONTAL_BLUR_PASS)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	float2 U = LightPositions[0].y*float2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -((((float)(KernelSize))-3.0)/2.0);
	float H = Origin;
	float2 Texcoords;
	for(int i=1;i<(KernelSize-1);i++){	
		Texcoords.xy = float2(input.texture0.x + H*U.x ,input.texture0.y );
		Sum.xyz += LightPositions[i+1].x * tex0.Sample( SS, Texcoords.xy ).xyz;
		H++;
	}
	return Sum;
}
#elif defined(ONE_PASS_BLUR)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	float2 U = LightPositions[0].y*float2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -((((float)(KernelSize))-3.0)/2.0);
	float H = Origin;
	float V = Origin;
	float2 Texcoords;	
	for(int i=1;i<(KernelSize-1);i++){		
		Texcoords.x = input.texture0.x + H*U.x;
		V = Origin;
		for(int j=1;j<(KernelSize-1);j++){
			Texcoords.y = input.texture0.y + V*U.y;
			float weight = roundTo(LightPositions[i+1].x*LightPositions[j+1].x,6.0);
			Sum.xyz += weight * tex0.Sample( SS, Texcoords.xy ).xyz;
			V++;
		}
		H++;
	}
	
	return Sum;
}
#elif defined(BRIGHT_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float3 color = tex0.Sample(SS, input.texture0).rgb;
	float avgLuminance = exp(tex1.Sample(SS, float2(0.5f, 0.5f)).r);

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
#elif defined(LUMINANCE_MAP_PASS)
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET {
  float3 color = tex0.Sample(SS, input.texture0).rgb;
  float luminance = max(dot(color.rgb, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
  float logLum = log(luminance);
  return float4(logLum, 1.0f, 1.0f, 1.0f);
}
#elif defined(ADAPT_LUMINANCE_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET {
  float2 center = float2(0.5f, 0.5f);
  float lastLum = exp(tex0.Sample(SS, center).r);
  float currentLogLum = tex1.SampleLevel(SS, center, CameraPosition.w).r;
  float currentLum = exp(currentLogLum);

  float dt = max(LightPositions[1].y, 0.0f);
  float tau = max(LightPositions[1].x, 0.0001f);
  float adaptedLum = lastLum + (currentLum - lastLum) * (1.0f - exp(-dt * tau));
  float adaptedLogLum = log(max(adaptedLum, 0.0001f));
  return float4(adaptedLogLum, 1.0f, 1.0f, 1.0f);
}
#elif defined(HDR_COMP_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
  float3 hdrColor = tex0.Sample(SS, input.texture0).rgb;
  float avgLuminance = exp(tex2.Sample(SS, float2(0.5f, 0.5f)).r);

  avgLuminance = max(avgLuminance, 0.001f);
  float keyValue = 1.03f - (2.0f / (2.0f + log10(avgLuminance + 1.0f)));
  float linearExposure = keyValue / avgLuminance;
  float exposureComp = LightPositions[0].y;
  float3 color = exp2(log2(max(linearExposure, 0.0001f)) + exposureComp) * hdrColor;

  float pixelLuminance = max(dot(color, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
  float whiteLevel = max(LightPositions[0].z, 0.001f);
  float toneMappedLuminance = pixelLuminance * (1.0f + pixelLuminance / (whiteLevel * whiteLevel)) / (1.0f + pixelLuminance);
  float3 toneMapped = toneMappedLuminance * (color / pixelLuminance);

  return float4(toneMapped + LightPositions[0].x * tex1.Sample(SS, input.texture0).rgb, 1.0f);
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
  float depthFocus;
  #ifdef AUTO_FOCUS
    depthFocus = tex0.Sample(SS, float2(0.5, 0.5)).r;// Auto Focus center
  #else
    depthFocus = LightPositions[0].z;
  #endif

	FS_OUT OUT;
	float z = tex0.Sample( SS, input.texture0.xy ).r;
	bool near = (z < depthFocus);
	float znear = CameraInfo.x;
	float zfar = CameraInfo.y;
  float multi = -zfar * znear;
  float multi2 = (zfar - znear);
	float objectdistance = multi  / (z * multi2 - zfar);
  float FocusPlane =     multi  / (depthFocus * multi2 - zfar);
	float CoC = abs(aperture * (focalLength * (objectdistance - FocusPlane)) /
          (objectdistance * (FocusPlane - focalLength)));
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
  float CoC1 = tex1.Sample(SS, input.texture0).r;
 // float CoC =  2*max(CoC0, CoC1) - CoC0;
  float CoC =  1.5*max(CoC0, CoC1);
  return CoC;
}

#elif defined(DOF_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float dofblur = tex1.Sample(SS, input.texture0).r;
  float4 color = tex0.Sample(SS, input.texture0);
  float offsetX = 1 / 1280.0;
  float offsetY = 1 / 720.0;

  for (int i = -3; i <= 3; i++) {
    for (int j = -3; j <= 3; j++) {
      float2 tcoord = input.texture0 + float2(i*offsetX, j*offsetY) * dofblur;
      color+= tex0.Sample(SS, tcoord);
    }
  }
  color /= 49.0;
  color.a = 1.0;

  return color;
}
#elif defined(DOF_PASS_2)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
float4 FS(VS_OUTPUT input) : SV_TARGET{
float dofblur = tex1.Sample(SS, input.texture0).r;
float4 color = tex0.Sample(SS, input.texture0);
float offsetX = 1 / 1280.0;
float offsetY = 1 / 720.0;

for (int i = -1; i <= 1; i++) {
  for (int j = -1; j <= 1; j++) {
    float2 tcoord = input.texture0 + float2(i*offsetX, j*offsetY)*dofblur;
    color += tex0.Sample(SS, tcoord);
  }
}
color /= 9.0;
color.a = 1.0;

return color;
}

#elif defined(BACKBUFFER_PASS)
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  return tex0.Sample(SS, input.texture0.xy);
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
  float3 rays = tex1.Sample(SS, uv).rgb;
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
  float depth = tex1.Sample(SS, uv);

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
    float z = tex1.Sample(SS, (float2(uv.x +  val,uv.y))).x;	
    if (depth - z < dVal)
      ao += clamp((depth - z), 0.0, 1.0);
  }
  for (int i = 0; i<4; i++)
  { 
    float val = randVec[i+4] * raius * texSize.x;
    float z = tex1.Sample(SS, (float2(uv.x + val, uv.y))).x;
    if (depth - z < dVal)
      ao += clamp((depth - z), 0.0, 1.0);
  }
  for (int i = 0; i<4; i++)
  {
    float val = randVec[i] * raius* -1 * texSize.y;
    float z = tex1.Sample(SS, (float2( uv.x, uv.y + val))).y;
    if (depth - z < dVal)
      ao += clamp((depth - z), 0.0, 1.0);
  }
  for (int i = 0; i<4; i++)
  {
    float val = randVec[i+4] * raius*texSize.y;
    float z = tex1.Sample(SS, (float2(uv.x, uv.y +  val))).y;
    if (depth - z < dVal)
      ao += clamp(( depth-z), 0.0, 1.0);
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
  return tex2.Sample(SS, x).r;
}
float rand2D(float2 x) {
  //return clamp (tex2.Sample(SS, x).rgb,0,1);
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
  half3 ret = tex1.Sample(SS, uv.xy).rgb;
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
  //float3 ret = tex1.Sample(SS, uv.xy).rgb;
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
#ifdef NON_LINEAR_DEPTH
  float4 position = mul(WVPInverse,float4(input.PosCorner.xy ,depth,1.0));
  position.xyz /= position.w;
#else		
  float4 position = CameraPosition + input.PosCorner*depth;
#endif
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
  float depth = tex0.Sample(SS, input.texture0);
#ifdef NON_LINEAR_DEPTH
float4 position = mul(WVPInverse,float4(input.PosCorner.xy ,depth,1.0));
position.xyz /= position.w;
#else		
float4 position = CameraPosition + input.PosCorner*depth;
#endif
int steps = (int)LightPositions[0].y;
float4 ray = (position - CameraPosition);
float4 rayDir = normalize(ray);
float rayLength = length(ray);

float4 intersectionNear = CameraPosition;
float4 intersectionFar = position;

//March
float4 step = (intersectionNear - intersectionFar) / (float)(steps - 1);
float4 P = intersectionFar;
float3 accumFog = 0.0f.xxx;

const float3 lightColor = float3(0.9803, 0.8392, 0.6470);
[loop] for (int i = 0; i<steps; i++) {
  float4 LightPos = mul(WVPLight, P);
  LightPos.xy /= LightPos.w;
  LightPos.z /= LightCameraInfo.y;
  float2 SHTC = LightPos.xy*0.5 + 0.5;
  SHTC.y = 1.0 - SHTC.y;
  float depthValue = tex1.Sample(SS1, SHTC);
  depthValue += 0.00005;

  if (depthValue > LightPos.z && SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x > 0.0 && SHTC.y > 0.0 && LightPos.w > 0.0 && LightPos.z < 1.0)
  {
    float4 sunDir = normalize(P - LightCameraPosition);
    float3 scattering = lightColor * ComputeScattering(dot(rayDir.rgb, sunDir.rgb));
    accumFog += scattering ;
  }
  P += step;
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
  float4 col = tex1.Sample(SS, input.texture0.xy);
  float4 vol = tex0.Sample(SS, input.texture0.xy);
  vol.rgb = pow(vol.rgb, float3(2.2, 2.2, 2.2));
  col += vol * float4(raysIntensity, raysIntensity, raysIntensity, raysIntensity);
  //col -= lerp(float4(0,0,0,0), float4(1,1,1,1) - vol, raysIntensity);
  return   col;
}
#elif defined(FSQUAD_1_TEX)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {	
	return  tex0.Sample( SS, input.texture0.xy ) * brightness.x;

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
#else
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	return tex0.Sample( SS, input.texture0);
}
#endif
