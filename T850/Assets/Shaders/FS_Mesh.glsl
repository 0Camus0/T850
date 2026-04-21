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

uniform highp vec4 LightPos;
uniform highp vec4 LightColor;
uniform highp vec4 CameraPosition;
uniform highp vec4 CameraInfo;
uniform highp vec4 AmbientColor;
uniform highp vec4 DiffuseColor;
uniform highp vec4 SpecularColor;
uniform highp vec4 PBRParams;        // .x=metallic .y=roughness (fallbacks)
uniform highp vec4 Intensities;      // .w=MatID
uniform highp vec4 ParallaxSettings;
uniform highp vec4 ParallaxShadowSettings;
uniform highp vec4 Light0Direction;


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

#ifdef USE_TEXCOORD0
	#ifdef ES_30
		in highp vec2 vecUVCoords;
	#else
		varying highp vec2 vecUVCoords;
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

#ifdef SIMPLE_COLOR
	#ifdef ES_30
	layout(location = 0) out highp vec4 colorOut;
	#endif
void main(){
	#ifdef ES_30
		colorOut = vec4(0.5,0.5,0.5,1.0);
	#else
		gl_FragColor = vec4(0.5,0.5,0.5,1.0);
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
void main(){
	lowp vec4 color    = vec4(0.5,0.5,0.5,1.0);
	highp vec4 normal   = vec4(0.5,0.5,0.5,1.0);
	mediump float metallic = PBRParams.x;
	lowp vec4 reflect  = vec4(0.5,0.5,0.5,1.0);

	mediump float roughness = PBRParams.y;

	normal.xyz   = normalize(hnormal).xyz;  
	highp vec3 geoNormal = normal.xyz;
	highp vec2 parallaxCoords =  vecUVCoords;
	#if defined HEIGHT_MAP || defined NORMAL_MAP
		lowp vec3 tangent	 = normalize(htangent).xyz;
		lowp vec3 binormal	 = normalize(hbinormal).xyz;
		lowp mat3	TBN 	 = mat3(tangent,binormal,normal);

	#endif
	#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX)
		highp float heightScale = ParallaxSettings.z;
		lowp mat3 TBN_transposed = transpose(TBN);
		highp vec3 viewDir   = TBN_transposed * normalize( CameraPosition.xyz-WorldPos.xyz);
		viewDir = normalize(viewDir);
		highp float minLayers = ParallaxSettings.x;
		highp float maxLayers = ParallaxSettings.y;
		highp float numLayers = mix(maxLayers, minLayers, abs(dot(vec3(0.0, 0.0, 1.0), viewDir)));
		highp float layerDepth = 1.0 / numLayers;
		//highp float currentLayerDepth = 0.0;
		highp float prevDepthMapValue = 0.0;
		//viewDir.y = - viewDir.y;
		highp vec2 P = -viewDir.xy * heightScale / viewDir.z; 
		highp vec2 deltaTexCoords = P * layerDepth;
		deltaTexCoords.y = - deltaTexCoords.y;

		highp vec2 dxx = dFdx(parallaxCoords);
		highp vec2 dyy = dFdy(parallaxCoords);
		highp float currentDepthMapValue = textureGrad(HeightTex, parallaxCoords, dxx,dyy).r;
		highp float currentRayZ = 1.0 - layerDepth;
		highp float prevRayZ = 1.0 - layerDepth;
		while(currentRayZ > currentDepthMapValue)
		{
			currentDepthMapValue = textureGrad(HeightTex, parallaxCoords, dxx,dyy).r; 
			prevDepthMapValue = currentDepthMapValue;
			parallaxCoords += deltaTexCoords;
			//currentLayerDepth += layerDepth;
			prevRayZ =  currentRayZ;
			currentRayZ -= layerDepth;
		}
			highp vec2 prevTexCoords = parallaxCoords - deltaTexCoords;
			highp float weight = (prevDepthMapValue - prevRayZ) /(prevDepthMapValue - currentDepthMapValue + currentRayZ - prevRayZ);
			parallaxCoords = prevTexCoords * weight + parallaxCoords * (1.0 - weight);
		//parallaxCoords = vecUVCoords;
	#endif

	#ifdef DIFFUSE_MAP
		#ifdef ES_30
			#ifdef HEIGHT_MAP
			//color = vec4(texture(HeightTex,vecUVCoords).r,0.0,0.0,1.0);
			#endif
			color = texture(DiffuseTex,parallaxCoords);
		#else
			color = texture2D(DiffuseTex,parallaxCoords);
		#endif
	#else
			color = DiffuseColor;
	#endif

	#ifdef NORMAL_MAP	
		#ifdef ES_30
			highp vec3 normalTex  = texture(NormalTex,parallaxCoords).xyz;
		#else
			highp vec3 normalTex  = texture2D(NormalTex,parallaxCoords).xyz;
		#endif
		normalTex 		 	 = normalTex*vec3(2.0,2.0,2.0) - vec3(1.0,1.0,1.0);
		normalTex		 	 = normalize(normalTex);
		#ifndef GLTF_TANGENT_SPACE
		normalTex.g 	 	 = -normalTex.g;
		#endif

		normal.xyz		 	 = TBN*normalTex;
		normal.xyz		 	 = normalize(normal.xyz);
	#endif

	#ifdef SPECULAR_MAP
		// Legacy: specular map ignored in metallic PBR workflow
	#endif

	#ifdef METALLIC_MAP
		// glTF metallic-roughness: B=metallic, G=roughness, multiply by uniform factors
		#ifdef ES_30
			vec4 mrSample = texture(MetallicTex,parallaxCoords);
		#else
			vec4 mrSample = texture2D(MetallicTex,parallaxCoords);
		#endif
		metallic  = PBRParams.x * mrSample.b;
		roughness = PBRParams.y * mrSample.g;
	#elif defined(GLOSS_MAP)
		// Legacy: separate roughness texture in R channel
		#ifdef ES_30
			roughness = texture(GlossTex,parallaxCoords).r;
		#else
			roughness = texture2D(GlossTex,parallaxCoords).r;
		#endif
	#endif
			
		normal.xyz		 = normal.xyz*0.5 + 0.5;

	// Parallax self-shadowing: march from POM surface point toward the light
	highp float selfShadow = 1.0;
	#if defined(HEIGHT_MAP) && defined(ENABLE_PARALLAX)
	{
		highp vec2 ssDxx = dFdx(vecUVCoords);
		highp vec2 ssDyy = dFdy(vecUVCoords);
		highp float ssStartZ = textureGrad(HeightTex, parallaxCoords, ssDxx, ssDyy).r;

		highp float shadowStrength = ParallaxShadowSettings.w;
		if (shadowStrength > 0.001) {
			highp float lightDirLen = length(Light0Direction.xyz);
			if (lightDirLen > 0.001) {
				lowp mat3 TBN_t = transpose(TBN);
				highp vec3 lightDirTS = TBN_t * normalize(-Light0Direction.xyz);
				lightDirTS = normalize(lightDirTS);

				if (lightDirTS.z > 0.01) {
					highp float numLayers = mix(ParallaxShadowSettings.y, ParallaxShadowSettings.x, abs(lightDirTS.z));
					highp float layerStep = 1.0 / numLayers;
					highp float heightScale = ParallaxSettings.z;
					highp vec2 P_light = lightDirTS.xy * heightScale / lightDirTS.z;
					highp vec2 deltaUV = P_light * layerStep;
					deltaUV.y = -deltaUV.y;

					highp vec2 currentUV = parallaxCoords;
					highp float currentRayZ = ssStartZ;
					highp float shadowSoftness = ParallaxShadowSettings.z;

					for (int si = 0; si < int(numLayers); si++) {
						currentRayZ += layerStep;
						currentUV += deltaUV;
						if (currentRayZ >= 1.0) break;
						highp float h = textureGrad(HeightTex, currentUV, ssDxx, ssDyy).r;
						if (h > currentRayZ) {
							highp float penumbra = float(si + 1) / numLayers;
							selfShadow = min(selfShadow, mix(0.0, penumbra, shadowSoftness));
						}
					}
					selfShadow = mix(1.0, selfShadow, shadowStrength);
				}
			}
		}
	}
	#endif
	
	#ifdef ES_30
		colorOut_0.rgb  = color.rgb;
		colorOut_0.a 	= 0.0;
		colorOut_1.rgb  = normal.xyz;
		colorOut_1.a 	= roughness;

		colorOut_2.r    = metallic;
		colorOut_2.g    = selfShadow;
		colorOut_2.b    = 0.0;
		// Mat Id: 0=NoLight, 1=NoNormalMap, 2=NormalMap
		colorOut_2.a = Intensities.w / 255.0;

		colorOut_3	= vec4(geoNormal * 0.5 + 0.5, 0.0);

		#ifdef NON_LINEAR_DEPTH
			colorOut_4	= vec4(Pos.z / Pos.w, 0.0, 0.0, 0.0);
		#else
			colorOut_4	= vec4(Pos.z / CameraInfo.y, 0.0, 0.0, 0.0);
		#endif
	#else
		gl_FragData[0].rgb  = color.rgb;
		gl_FragData[0].a 	= 0.0;
		
		gl_FragData[1].rgb  = normal.xyz;
		gl_FragData[1].a 	= roughness;

		gl_FragData[2].r    = metallic;
		gl_FragData[2].g    = selfShadow;
		gl_FragData[2].b    = 0.0;
		// Mat Id: 0=NoLight, 1=NoNormalMap, 2=NormalMap
		gl_FragData[2].a = Intensities.w / 255.0;

		gl_FragData[3]	= vec4(geoNormal * 0.5 + 0.5, 0.0);
		gl_FragData[4]	= vec4(Pos.z / CameraInfo.y, 0.0, 0.0, 0.0);

		#ifdef NON_LINEAR_DEPTH
			gl_FragDepth	= Pos.z / Pos.w;
		#else
			gl_FragDepth = Pos.z / CameraInfo.y;
		#endif
	#endif
	
}
#elif defined(SHADOW_MAP_PASS)
void main(){
#ifdef NON_LINEAR_DEPTH
	gl_FragDepth = Pos.z / Pos.w;
#else
	gl_FragDepth = Pos.z / CameraInfo.y;
#endif
}
#elif defined(RADIAL_DEPTH_PASS)
uniform highp mat4 WorldView; 
void main(){
    highp vec4 ff  = WorldView*CameraPosition;
	gl_FragDepth = length(vec3(Pos.xyz - ff.xyz)) / CameraInfo.y;
}
#else
	#ifdef ES_30
		layout(location = 0) out highp vec4 colorOut;
	#endif
void main(){

	lowp vec4 color = vec4(0.5,0.5,0.5,1.0);
	lowp vec4 Final = vec4(0.0,0.0,0.0,1.0);
	
#ifdef USE_TEXCOORD0
	#ifdef NO_LIGHT
		#ifdef ES_30
			color = texture(DiffuseTex,vecUVCoords);
		#else
			color = texture2D(DiffuseTex,vecUVCoords);
		#endif
	#else
		#ifdef DIFFUSE_MAP
			#ifdef ES_30
				color = texture(DiffuseTex,vecUVCoords);
			#else
				color = texture2D(DiffuseTex,vecUVCoords);
			#endif
		#endif
		
		#ifdef SPECULAR_MAP
			#ifdef ES_30
				lowp vec4 specularmap = texture(SpecularTex,vecUVCoords);
			#else
				lowp vec4 specularmap = texture2D(SpecularTex,vecUVCoords);
			#endif
		#endif
		
		#ifdef USE_NORMALS
			lowp vec4 Ambiental = color*AmbientColor;
		
			lowp vec4   Lambert  = LightColor;
			lowp vec4 	Specular = LightColor;
			lowp vec4	Fresnel  = LightColor;
			lowp vec3	LightDir = normalize(LightPos-WorldPos).xyz;
			lowp vec3   EyeDir   = normalize(CameraPosition-WorldPos).xyz;
			lowp vec3	normal   = normalize(hnormal).xyz;  
			mediump float att			 = 1.0;
			
			
		#ifdef NORMAL_MAP	
			#ifdef ES_30
				lowp vec3 normalTex = texture(NormalTex,vecUVCoords).xyz;
			#else
				lowp vec3 normalTex = texture2D(NormalTex,vecUVCoords).xyz;
			#endif
			normalTex 		 = normalTex*vec3(2.0,2.0,2.0) - vec3(1.0,1.0,1.0);
			normalTex		 = normalize(normalTex);
			#ifndef GLTF_TANGENT_SPACE
			normalTex.g 	 = -normalTex.g;
			#endif
			lowp vec3 tangent	 = normalize(htangent).xyz;
			lowp vec3 binormal	 = normalize(hbinormal).xyz;
			lowp mat3	TBN = mat3(tangent,binormal,normal);
			normal			 = TBN*normalTex;
			normal			 = normalize(normal);
		#endif
		
		#ifdef DIFFUSE
			att		 	     = dot(normal,LightDir)*0.5 + 0.5;
			att				 = pow( att , 2.0 );	
			att				 = clamp( att , 0.0 , 1.0 );
			Lambert			*= color*att;
		#endif
		
		#ifdef SPECULAR
			highp float specular  = 0.0;
			highp float specIntesivity = 0.8;
			highp float shinness = 8.0;
			
			#ifdef GLOSS_MAP
				#ifdef ES_30
					shinness = texture(GlossTex,vecUVCoords).r + shinness;
				#else
					shinness = texture2D(GlossTex,vecUVCoords).r + shinness;
				#endif
			#endif

		#ifdef USING_PHONG
			lowp vec3 ReflectedLight = reflect(-LightDir,normal);
			//specular = max ( dot(ReflectedLight,EyeDir), 0.0);	
			specular =  dot(ReflectedLight,EyeDir)*0.5 + 0.5;	
			specular = pow( specular ,shinness);		
		#elif defined(USING_BLINN)
			lowp vec3 ReflectedLight = normalize(EyeDir+LightDir); 
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
		mediump float  FresnelAtt	= dot(normal,EyeDir);
		lowp float  FresnelIntensity = 1.0;
		#ifdef SPECULAR_MAP
			lowp vec4 FresnelCol = vec4(specularmap.xyz,1.0);
		#else
			lowp vec4 FresnelCol = vec4(1.0,1.0,1.0,1.0);	
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
		
		#ifdef FRESNEL
			Final += Fresnel;
		#endif
		
		color = Final;
		#endif
	#endif
#endif

	#ifdef ES_30
		colorOut = color;
	#else
		gl_FragColor = color;
	#endif
}

#endif