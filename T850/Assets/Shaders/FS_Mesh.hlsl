
cbuffer ConstantBuffer{
    float4x4 WVP;
	float4x4 World;  
	float4x4 WorldView;
	float4	 LightPos;
	float4 	 LightColor;
	float4   CameraPosition;
	float4 	 CameraInfo;
	float4	 Ambient;
	float4   DiffuseColor;
	float4   SpecularColor;
	float4   PBRParams;        // .x=metallic .y=roughness (fallbacks)
	float4   Intensities;      // .w=MatID
	float4   ParallaxSettings;
	float4   ParallaxShadowSettings;
	float4   Light0Direction;
}

#define PHONG 1
#define BLINN 2

#define SPECULAR_MODEL BLINN


#if   SPECULAR_MODEL == PHONG
#define USING_PHONG
#elif SPECULAR_MODEL == BLINN
#define USING_BLINN
#endif


#define AMBIENT
#define DIFFUSE
#define SPECULAR
#define FRESNEL 
/*

#ifdef DIFFUSE_MAP
#undef DIFFUSE_MAP
#endif

#ifdef SPECULAR_MAP
#undef SPECULAR_MAP
#endif

#ifdef GLOSS_MAP
#undef GLOSS_MAP
#endif 

#ifdef NORMAL_MAP
#undef NORMAL_MAP
#endif

*/

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

SamplerState SS;

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
	
#ifdef USE_NORMALS
	float4 hnormal   : NORMAL;
#endif

#ifdef USE_TANGENTS
	float4 htangent   : TANGENT;
#endif

#ifdef USE_BINORMALS
	float4 hbinormal : BINORMAL;
#endif

#ifdef USE_TEXCOORD0
    float2 texture0  : TEXCOORD;
#endif
	
	float4 Pos		: TEXCOORD1;
	
	float4 WorldPos		: TEXCOORD2;
};

#ifdef SIMPLE_COLOR
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	return float4(0.5,0.5,0.5,1.0);
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

FS_OUT FS( VS_OUTPUT input )   {
	float4  color 		= float4(0.5,0.5,0.5,1.0);
	float4  normal 		= float4(0.5,0.5,0.5,1.0);
	float   metallic    = PBRParams.x;
	float4  reflect		= float4(0.5,0.5,0.5,1.0);
	
	float roughness = PBRParams.y;

	normal.xyz   = normalize(input.hnormal).xyz;
	float3 geoNormal = normal.xyz;

#ifdef USE_TEXCOORD0
	float2 parallaxCoords = input.texture0;
#else
	float2 parallaxCoords = float2(0.0, 0.0);
#endif

	#if defined(HEIGHT_MAP) || defined(NORMAL_MAP)
		float3 tangent	 = normalize(input.htangent).xyz;
		float3 binormal	 = normalize(input.hbinormal).xyz;
		float3x3 TBN    = float3x3(tangent, binormal, normal.xyz);
	#endif
	#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX)
		float heightScale = ParallaxSettings.z;
		float3 viewDir = mul(TBN, normalize(CameraPosition.xyz - input.WorldPos.xyz));
		viewDir = normalize(viewDir);
		float minLayers = ParallaxSettings.x;
		float maxLayers = ParallaxSettings.y;
		float numLayers = lerp(maxLayers, minLayers, abs(dot(float3(0.0, 0.0, 1.0), viewDir)));
		float layerDepth = 1.0 / numLayers;
		float prevDepthMapValue = 0.0;
		float2 P = -viewDir.xy * heightScale / viewDir.z;
		float2 deltaTexCoords = P * layerDepth;
		deltaTexCoords.y = -deltaTexCoords.y;

		float currentDepthMapValue = TextureHeight.SampleGrad(SS, parallaxCoords, ddx(parallaxCoords), ddy(parallaxCoords)).r;
		float currentRayZ = 1.0 - layerDepth;
		float prevRayZ = 1.0 - layerDepth;
		[loop] while (currentRayZ > currentDepthMapValue) {
			currentDepthMapValue = TextureHeight.SampleGrad(SS, parallaxCoords, ddx(input.texture0), ddy(input.texture0)).r;
			prevDepthMapValue = currentDepthMapValue;
			parallaxCoords += deltaTexCoords;
			prevRayZ = currentRayZ;
			currentRayZ -= layerDepth;
		}
		float2 prevTexCoords = parallaxCoords - deltaTexCoords;
		float weight = (prevDepthMapValue - prevRayZ) / (prevDepthMapValue - currentDepthMapValue + currentRayZ - prevRayZ);
		parallaxCoords = prevTexCoords * weight + parallaxCoords * (1.0 - weight);
	#endif

	#ifdef DIFFUSE_MAP
		color = TextureRGB.Sample( SS, parallaxCoords );	
	#else
		color = DiffuseColor;
	#endif

	#ifdef NORMAL_MAP	
		float3 normalTex = TextureNormal.Sample( SS, parallaxCoords ).xyz;
		normalTex 		 = 	normalTex*float3(2.0,2.0,2.0) - float3(1.0,1.0,1.0);
		normalTex		 = normalize(normalTex);
		#ifndef GLTF_TANGENT_SPACE
		normalTex.g 	 = -normalTex.g;
		#endif
		normal.xyz		 = mul(normalTex,TBN);
		normal.xyz		 = normalize(normal.xyz);
	#endif

	#ifdef SPECULAR_MAP
		// Legacy: specular map ignored in metallic PBR workflow
	#endif
	
	#ifdef METALLIC_MAP
		// glTF metallic-roughness: B=metallic, G=roughness, multiply by uniform factors
		float4 mrSample = TextureMetallic.Sample( SS, parallaxCoords );
		metallic  = PBRParams.x * mrSample.b;
		roughness = PBRParams.y * mrSample.g;
	#elif defined(GLOSS_MAP)
		// Legacy: separate roughness texture in R channel
		roughness = TextureGloss.Sample( SS, parallaxCoords ).r;
	#endif
	
	float3  EyeDir   = normalize(CameraPosition-input.WorldPos).xyz;

	// Parallax self-shadowing: march from POM surface point toward the light
	float selfShadow = 1.0;
	#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX)
	{
		float2 ssDxx = ddx(input.texture0);
		float2 ssDyy = ddy(input.texture0);
		float ssStartZ = TextureHeight.SampleGrad(SS, parallaxCoords, ssDxx, ssDyy).r;

		float shadowStrength = ParallaxShadowSettings.w;
		if (shadowStrength > 0.001) {
			float lightDirLen = length(Light0Direction.xyz);
			if (lightDirLen > 0.001) {
				float3 lightDirTS = mul(TBN, normalize(-Light0Direction.xyz));
				lightDirTS = normalize(lightDirTS);

				if (lightDirTS.z > 0.01) {
					float numLayers = lerp(ParallaxShadowSettings.y, ParallaxShadowSettings.x, abs(lightDirTS.z));
					float layerStep = 1.0 / numLayers;
					float heightScale = ParallaxSettings.z;
					float2 P_light = lightDirTS.xy * heightScale / lightDirTS.z;
					float2 deltaUV = P_light * layerStep;
					deltaUV.y = -deltaUV.y;

					float2 currentUV = parallaxCoords;
					float currentRayZ = ssStartZ;
					float shadowSoftness = ParallaxShadowSettings.z;

					[loop] for (int si = 0; si < int(numLayers); si++) {
						currentRayZ += layerStep;
						currentUV += deltaUV;
						if (currentRayZ >= 1.0) break;
						float h = TextureHeight.SampleGrad(SS, currentUV, ssDxx, ssDyy).r;
						if (h > currentRayZ) {
							float penumbra = float(si + 1) / numLayers;
							selfShadow = min(selfShadow, lerp(0.0, penumbra, shadowSoftness));
						}
					}
					selfShadow = lerp(1.0, selfShadow, shadowStrength);
				}
			}
		}
	}
	#endif

	normal.xyz		 = normal.xyz*0.5 + 0.5;	


	FS_OUT fout;
	fout.color0.rgb = color.rgb;
	fout.color0.a 	= 0.0;
	
	fout.color1.rgb = normal.xyz;
	fout.color1.a 	= roughness;	
	
	fout.color2.r   = metallic;
	fout.color2.g   = selfShadow;
	fout.color2.b   = 0.0;
	fout.color2.a 	= Intensities.w / 255.0;
	
	fout.color3 = float4(geoNormal * 0.5 + 0.5, 0.0);
	
		fout.depth		= input.Pos.z / input.Pos.w;
		fout.color4		= float4(input.Pos.z / input.Pos.w, 0, 0, 0);
	
	return fout;	
}
#elif defined(SHADOW_MAP_PASS)
float FS( VS_OUTPUT input ) : SV_Depth  {
	return input.Pos.z/input.Pos.w;
}
#elif defined(DEPTH_PRE_PASS)
float4 FS(VS_OUTPUT input) : SV_Depth{
  return input.Pos.z / input.Pos.w;
}
#else
float4 FS( VS_OUTPUT input )  : SV_TARGET {
    float4  color = float4(0.5,0.5,0.5,1.0);
	float4  Final = float4(0.0,0.0,0.0,1.0);
#ifdef USE_TEXCOORD0
	#ifdef NO_LIGHT
		color = TextureRGB.Sample( SS, input.texture0 );	
	#else
		#ifdef DIFFUSE_MAP
		color = TextureRGB.Sample( SS, input.texture0 );	
		#else
		color = DiffuseColor;
		#endif
		
		#ifdef SPECULAR_MAP
		float4 specularmap = TextureSpecular.Sample( SS, input.texture0 );	
		#endif
		
		#ifdef USE_NORMALS

		float4 Ambiental = color*Ambient;
		
		float4  Lambert  = LightColor;
		float4  Specular = LightColor;
		float4  Fresnel	 = LightColor;
		float3	LightDir = normalize(LightPos-input.WorldPos).xyz;
		float3  EyeDir   = normalize(CameraPosition-input.WorldPos).xyz;
		float3	normal   = normalize(input.hnormal).xyz;  
		float   att		 = 1.0;
		
		#ifdef NORMAL_MAP	
			float3 normalTex = TextureNormal.Sample( SS, input.texture0 ).xyz;
			normalTex 		 = 	normalTex*float3(2.0,2.0,2.0) - float3(1.0,1.0,1.0);
			normalTex		 = normalize(normalTex);
			#ifndef GLTF_TANGENT_SPACE
			normalTex.g 	 = -normalTex.g;
			#endif
			float3 tangent	 = normalize(input.htangent).xyz;
			float3 binormal	 = normalize(input.hbinormal).xyz;
			float3x3	TBN  =  float3x3(tangent,binormal,normal);
			normal			 = mul(normalTex,TBN);
			normal			 = normalize(normal);
		#endif
		
		#ifdef DIFFUSE
			att		 	     = dot(normal,LightDir)*0.5 + 0.5;
			att				 = pow( att , 2.0 );	
			att				 = clamp( att , 0.0 , 1.0 );
			Lambert			*= color*att;
		#endif
		
		#ifdef SPECULAR
			float  specular  = 0.0;
			float specIntesivity = 0.8;
			float shinness = 8.0;

			#ifdef GLOSS_MAP
				shinness = TextureGloss.Sample( SS, input.texture0 ).r + shinness;
			#endif
			
		#ifdef USING_PHONG
			float3 	ReflectedLight = reflect(-LightDir,normal);
			//specular = max ( dot(ReflectedLight,EyeDir), 0.0);	
			specular = dot(ReflectedLight,EyeDir)*0.5 + 0.5;	
			specular = pow( specular ,shinness);		
		#elif defined(USING_BLINN)
			float3 ReflectedLight = normalize(EyeDir+LightDir); 
			specular = max ( dot(ReflectedLight,normal)*0.5 + 0.5, 0.0);	
			specular = pow( specular ,shinness);	
		#endif

			specular *= att;
			specular *= specIntesivity;
			Specular *= specular;
			
			#ifdef SPECULAR_MAP
				Specular.xyz *= specularmap.xyz;
			#endif		
		#endif
		
		
	#ifdef FRESNEL
		float  FresnelAtt	= dot(normal,EyeDir);
		float  FresnelIntensity = 1.0f;
		#ifdef SPECULAR_MAP
			float4 FresnelCol = float4(specularmap.xyz,1.0);
		#else
			float4 FresnelCol = float4(1.0,1.0,1.0,1.0);	
		#endif
		FresnelAtt		= abs(FresnelAtt);
		FresnelAtt 		= 1.0 - FresnelAtt;
		FresnelAtt 		= clamp( FresnelAtt , 0.0 , 1.0 );
		FresnelAtt		= pow( FresnelAtt , 4.0 );	
		FresnelAtt 		= clamp(FresnelAtt , 0.0 , 1.0 );
		Fresnel 		= FresnelCol*FresnelIntensity*FresnelAtt; 
	#endif
		
		#ifdef AMBIENT
			Final += Ambiental;
		#endif
		
		#ifdef DIFFUSE
			Final += Lambert;
		#endif
		
		#ifdef SPECULAR
			Final += Specular;
		#endif
		
		#ifdef SPECULAR
			Final += Specular;
		#endif
		
		#ifdef FRESNEL
			Final += Fresnel;
		#endif
		
		color = Final;
		#endif
	#endif
#endif		

	return color;
}

#endif