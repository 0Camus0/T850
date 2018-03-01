

uniform highp mat4 WVP;
uniform highp mat4 World;
uniform highp mat4 WorldView;
uniform highp mat4 WVPInverse;
uniform highp mat4 WVPLight;
uniform highp mat4 Projection;
uniform highp vec4 LightPositions[128];
uniform highp vec4 LightColors[128];
uniform highp vec4 LightRadius[32];
uniform highp vec4 CameraPosition;
uniform highp vec4 CameraInfo;
uniform highp vec4 LightCameraPosition;
uniform highp vec4 LightCameraInfo;
uniform highp vec4   brightness;
uniform highp vec4   toogles;

#define ENABLE_PCF 1


highp float roundTo(highp float num,highp float decimals){
	highp float shift = pow(10.0,decimals);
	return round(num*shift) / shift;
}

#ifdef ES_30
	in highp vec2 vecUVCoords;
	in highp vec4 Pos;
	in highp vec4 PosCorner;
#else
	varying highp vec2 vecUVCoords;
	varying highp vec4 Pos;
	varying highp vec4 PosCorner;
#endif

#ifdef ES_30
	layout(location = 0) out highp vec4 colorOut;
#endif

#ifdef DEFERRED_PASS
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
uniform mediump sampler2D tex2;
uniform mediump sampler2D tex3;
uniform mediump sampler2D tex4;
uniform mediump sampler2D tex5;
uniform mediump samplerCube texEnv;

highp vec3 NormalDistribution(highp float NdotH, highp float roughness)
{
	//Using GGX
	highp float a = roughness*roughness;
	highp float a2 = a*a;

	highp float NdotH2 = NdotH*NdotH;

	highp float Num = a2;
	highp float Denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
	Denom = 3.1415926 * Denom * Denom;

	highp float res = Num / Denom;

	return vec3(res, res, res);
}

highp vec3 FresnelCalc(highp float VdotH, highp vec3 specColor)
{
	//Using Schlick
	return (specColor + (1.0f - specColor) * pow(1.0f - VdotH, 5.0f));
}

highp vec3 fresnelSchlickRoughness(highp float cosTheta, highp vec3 F0, highp float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}   


highp float GeometrySchlickGGX(highp float Ndot, highp float roughness)
{
	highp float r = (roughness + 1.0);
    highp float k = (r*r) / 8.0;

    highp float Num   = Ndot;
    highp float Denom = Ndot * (1.0 - k) + k;
	
    return clamp(Num / Denom, 0.0f, 1.0f);
}

highp vec3 GeometricShadowing(highp float NdotL, highp float NdotV, highp float roughness)
{
	//Using Geometry Smith
	highp float ggx1 = GeometrySchlickGGX(NdotL, roughness);
	highp float ggx2 = GeometrySchlickGGX(NdotV, roughness);

	highp float res = clamp(ggx1 * ggx2, 0.0f, 1.0f);

	return vec3(res, res, res);
}

highp vec3 CalculateSpecular(mediump vec3 specularColor, highp vec3 normal, highp vec3 view, highp vec3 halfvector, highp vec3 light, highp float roughness)
{
	highp float NdotH = max(dot(normal, halfvector), 0.0f);
	highp float VdotH = clamp(dot(view, halfvector), 0.0f, 1.0f);
	highp float NdotL = clamp(dot(normal, light), 0.0f, 1.0f);
	highp float NdotV = clamp(dot(normal, view), 0.0f, 1.0f);

	highp vec3 Num = FresnelCalc(VdotH, specularColor)*NormalDistribution(NdotH, roughness)*GeometricShadowing(NdotL, NdotV, roughness);
	highp float denomRes = (4.0f * (NdotL*NdotV) + 0.01f);
	highp vec3 Denom = vec3(denomRes, denomRes, denomRes);

	return (Num/Denom);
}

highp vec3 CalculateDiffuse(highp vec3 albedoColor, highp vec3 normal, highp vec3 light)
{
	mediump float att			 = 1.0;
	att		 	     = dot(normal, light)*0.5 + 0.5;
	att				 = pow( att , 2.0 );	
	att				 = clamp( att , 0.0 , 1.0 );
	return albedoColor*clamp(dot(normal, light), 0.0f, 1.0f);
}

void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;

	lowp vec4 Final  =  vec4(0.0,0.0,0.0,1.0);
	lowp float Shadow = 1.0;

	highp vec4 ToLineal = vec4(2.2f, 2.2f, 2.2f, 2.2f);
	highp vec4 TosRGB = vec4(1.0f/2.2f, 1.0f/2.2f, 1.0f/2.2f, 1.0f/2.2f);

	#ifdef ES_30
		highp vec4 Albedo  =  texture(tex0,coords, 0.0f);
		highp vec4 SpecularColor = texture(tex2, coords);

		Albedo.xyz = pow(Albedo.xyz, ToLineal.xyz);
		//SpecularColor.xyz = pow(SpecularColor.xyz, ToLineal.xyz);

		lowp vec4 matId  =	texture(tex3,coords);
		highp float depth = texture(tex4,coords).r;
	#else
		highp vec4 Albedo  =  texture2D(tex0,coords);
		highp vec4 SpecularColor = texture2D(tex2, coords);

		//Albedo.xyz = pow(Albedo, ToLineal.xyz);
		//SpecularColor.xyz = pow(SpecularColor, ToLineal.xyz);

		lowp vec4 matId  =	texture2D(tex3,coords);
		highp float depth = texture2D(tex4,coords).r;
	#endif

		
#ifdef NON_LINEAR_DEPTH
		highp vec2 vcoord = coords *2.0 - 1.0;
		highp vec4 position = WVPInverse*vec4(vcoord ,depth,1.0);
		position.xyz /= position.w;  
#else	
		highp vec4 position = CameraPosition + PosCorner*depth;
#endif

	highp vec3 EyeDir = normalize(CameraPosition-position).xyz;

	int MatId = int(SpecularColor.a*255.0);
	
	if(MatId == 0){
		highp vec3 EyeDir_mod = -EyeDir;
		EyeDir_mod.x =  -EyeDir_mod.x;
		EyeDir_mod.z =  -EyeDir_mod.z;
		#ifdef ES_30
			mediump vec3 RefCol = texture( texEnv, EyeDir_mod ).zyx;
		#else
			mediump vec3 RefCol = textureCube( texEnv, EyeDir_mod ).zyx;
		#endif
			
		Final.xyz = RefCol.xyz*2.0;
	}else if(MatId > 0){

#ifdef ES_30
		Shadow = texture(tex5, coords).r;
#else
		Shadow = texture2D(tex5, coords).r;
#endif

		highp float cutoff = 0.8;


		#ifdef ES_30
			highp vec4 normalmap = texture(tex1,coords);
		#else
			highp vec4 normalmap = texture2D(tex1,coords);
		#endif	
		
		highp vec3 normal = normalmap.xyz*2.0 - 1.0;
		normal = normalize(normal);
		
		highp vec3 ReflectedVec = reflect(-EyeDir, normal.xyz);	
		ReflectedVec.y = ReflectedVec.y;
		ReflectedVec.x = -ReflectedVec.x;
		ReflectedVec.z = -ReflectedVec.z;

		highp float ratio = 1.0/1.52;
		highp vec3 R = refract(-EyeDir,normal.xyz,ratio);
		
		#ifdef ES_30
			 /*texture( texEnv, ReflectedVec ).zyx*/
		#else
			mediump vec3 RefleCol = textureCube( texEnv, ReflectedVec ).zyx;
		#endif		
		
		#ifdef ES_30
			mediump vec3 RefraCol = vec3(1.0,1.0,1.0) /*texture( texEnv, R ).zyx*/;
		#else
			mediump vec3 RefraCol = vec3(1.0,1.0,1.0) /*textureCube( texEnv, R ).zyx*/;
		#endif	

		highp float rough = normalmap.a;

		highp int NumLights =  int(CameraInfo.w);
			for(highp int i=0;i<NumLights;i++){
				highp float Rad = LightRadius[i >> 2][i & 3];
				highp float dist = distance(LightPositions[i],position);

				if(dist < (Rad*2.0))
				{	
					highp float gloss = normalmap.a;

					highp vec3 LightDir = normalize(LightPositions[i]-position).xyz;

					highp vec3 Half = normalize(EyeDir + LightDir);

					highp vec3 Diffuse = CalculateDiffuse(Albedo.xyz, normal, LightDir)*LightColors[i].xyz;

					highp vec3 SpecularRes = CalculateSpecular(Albedo.xyz, normal, EyeDir, Half, LightDir, rough)*LightColors[i].xyz;
									
					highp vec3 Ks = SpecularRes;
					highp vec3 Kd = vec3(1.0f, 1.0f, 1.0f) - SpecularRes;

					highp float d = max(dist - Rad, 0.0);
					highp float denom = d/Rad + 1.0;
					
					highp float attenuation = 1.0 / (denom*denom);
					 
					attenuation = (attenuation - cutoff) / (1.0 - cutoff);
					attenuation = max(attenuation, 0.0);

					Final.xyz += SpecularRes.xyz*attenuation + attenuation*Kd*Diffuse;
				}
			}
		if(MatId == 3 || MatId == 4){

#ifdef ES_30
			lowp vec4 fresnelColor = texture(tex3, coords);
#else
			lowp vec4 fresnelColor = texture2D(tex3, coords);

#endif	
			highp float  FresnelAtt	= clamp(abs(dot(normal.xyz,EyeDir)),0.0,1.0);
			highp float  FresnelIntensity = fresnelColor.a;
			lowp vec4 FresnelCol = vec4(fresnelColor.xyz,1.0);
		
			FresnelAtt 		= 1.0 - FresnelAtt;
			FresnelAtt		= pow( FresnelAtt , 5.0 );	
			FresnelAtt 		= clamp(FresnelAtt , 0.0 , 1.0 );
			//Fresnel 		= FresnelCol*FresnelIntensity*FresnelAtt;
		
			//Final += Fresnel;
		}		

			highp vec3 kSpecular = clamp( fresnelSchlickRoughness(max(dot(normal, EyeDir), 0.0f), Albedo.xyz, rough) , 0.0 , 1.0 );
	 		highp vec3 RefleCol = texture( texEnv, ReflectedVec , rough*4.0f).zyx;

	 		Final.xyz += RefleCol*kSpecular.xyz;

			Final.xyz *= Shadow;

			//Final.xyz = vec3(rough, rough, rough);
	}
	
#ifdef ES_30
	colorOut = Final;
#else
	gl_FragColor = Final;
#endif
	
}
#elif defined(SHADOW_COMP_PASS)
uniform mediump sampler2D tex0;
#if defined OMNIDIRECTIONAL_SH
uniform mediump samplerCube tex1;
#else
uniform mediump sampler2DShadow  tex1;
#endif
uniform mediump sampler2D tex2; // Normals
uniform mediump sampler2D tex3; // Noise

highp vec4 CalculateShadow(highp vec4 position){
highp vec4 FShadow = vec4(1.0,1.0,1.0,1.0);

	#if defined OMNIDIRECTIONAL_SH
		const highp vec3 sampleOffsetDirections[20] = vec3[]
		(
		   vec3( 1,  1,  1), vec3( 1, -1,  1), vec3(-1, -1,  1), vec3(-1,  1,  1), 
		   vec3( 1,  1, -1), vec3( 1, -1, -1), vec3(-1, -1, -1), vec3(-1,  1, -1),
		   vec3( 1,  1,  0), vec3( 1, -1,  0), vec3(-1, -1,  0), vec3(-1,  1,  0),
		   vec3( 1,  0,  1), vec3(-1,  0,  1), vec3( 1,  0, -1), vec3(-1,  0, -1),
		   vec3( 0,  1,  1), vec3( 0, -1,  1), vec3( 0, -1, -1), vec3( 0,  1, -1)
		); 
		highp float diskRadius = 0.15; 
		highp vec4 new_positionV = WorldView* vec4(position.xyz, 1.0);
		highp vec3 fragToLight =  new_positionV.xyz - vec4(WorldView * vec4(LightCameraPosition.xyz,1.0)).xyz;
		highp float depthPos = length(fragToLight); 
		fragToLight =  position.xyz - LightCameraPosition.xyz;
		fragToLight.y = -fragToLight.y;
		highp float depthSM = 0.0;
		highp float shadowVal = 0.0;
		int samples = 20;
		for (highp int i = 0; i < samples; i += 1){
			highp vec3 nfragToLight =  fragToLight +  sampleOffsetDirections[i] * diskRadius; 
			depthSM = texture(tex1, nfragToLight ).r;
			depthSM = depthSM * LightCameraInfo.y;
			if( depthPos - 0.055  > depthSM)
				shadowVal += 1.0;
			//shadowVal *= 0.75;
			//shadowVal += 0.25;
		}
		shadowVal /= float(samples);
		shadowVal *=  0.75;
		shadowVal =  (1.0 - shadowVal) ;
		FShadow = shadowVal*vec4(1.0,1.0,1.0,1.0);//texture(tex1, fragToLight ).rrrr;
	#else
	highp vec4 LightPos = WVPLight*position;
	#ifdef NON_LINEAR_DEPTH
		LightPos.xyz /= LightPos.w;
	#else
		LightPos.xy /= LightPos.w;
		LightPos.z /= LightCameraInfo.y;
	#endif
	highp vec2 SHTC = LightPos.xy*0.5 + 0.5;
	
	if(SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x  > 0.0 && SHTC.y > 0.0 && LightPos.w > 0.0 && LightPos.z < 1.0 ){
		#if ENABLE_PCF
			highp float sum = 0.0;
			highp float x, y;
			highp float Total = 0.0;
			highp float Origin = brightness.x;			
			highp float depthPos = LightPos.z;
			for (y = -Origin; y <= Origin; y += 1.0){
				for (x = -Origin; x <= Origin; x += 1.0){
					highp float Val_1;					
					#ifdef ES_30
						highp vec3 Coords_Final = vec3(SHTC.xy + (brightness.z / brightness.y)*vec2(x, y), LightPos.z);
						Val_1 = texture(tex1, Coords_Final);
					#else
						highp vec4 Coords_Final = vec4(SHTC.xy + (brightness.z / brightness.y)*vec2(x, y), LightPos.z, LightPos.w);
						Val_1 = shadow2DProj(tex1, Coords_Final).r;
					#endif
						Val_1 *= 0.75;
						Val_1 += 0.25;
						sum += Val_1; //(depthPos > Val_1) ? 0.25 : 1.0;
					Total++;
				}
			}

			highp float shadowCoeff = sum / Total;
			FShadow = shadowCoeff*vec4(1.0,1.0,1.0,1.0);
			
		/*
			highp vec3 Coords_Final = vec3(SHTC.xy, LightPos.z);
			highp float Val_1 = texture(tex1, Coords_Final,0.001);
			Val_1 *= 0.75;
			Val_1 += 0.25;
			FShadow = Val_1*vec4(1.0, 1.0, 1.0, 1.0);
			*/
		#else	
			highp float depthSM;
			#ifdef ES_30
				highp float depthSM_1 = texture(tex1,SHTC).r;
				//highp float depthSM_2 = texture(tex4,SHTC).r;
				depthSM = (depthSM_1+depthSM_2)/2.0;
			#else
				highp float depthSM_1 = texture2D(tex1,SHTC).r;
				highp float depthSM_2 = texture2D(tex4,SHTC).r;
				depthSM = (depthSM_1+depthSM_2)/2.0;
			#endif
		
			highp float depthPos = LightPos.z;

			if( depthPos  > depthSM)
				FShadow = 0.25*vec4(1.0,1.0,1.0,1.0);
		#endif
		
	}else{
		FShadow = 0.25*vec4(1.0,1.0,1.0,1.0);
	}
	#endif
	return FShadow;
}

highp vec3 GetNormal(highp vec2 coords){
	#ifdef ES_30
		highp vec4 normalmap = texture(tex2,coords);
	#else
		highp vec4 normalmap = texture2D(tex2,coords);
	#endif	
		
	highp vec3 normal = normalmap.xyz*2.0 - 1.0;
	normal = normalize(normal);

	return normal;
}

highp float GetOcclusion(highp float depth,highp vec2 uv, highp vec4 position, highp vec3 normal){
	highp float Radius = LightPositions[0].y;
	
	highp vec2 Scale = vec2(LightPositions[0].z/brightness.w,LightPositions[0].w/brightness.w);
	
	#ifdef ES_30
		highp vec3 pVec = texture(tex3,Scale*uv).xyz*2.0 - 1.0;
	#else
		highp vec3 pVec = texture2D(tex3,Scale*uv).xyz*2.0 - 1.0;
	#endif

	highp vec3 tangent = normalize(pVec - normal * dot(pVec, normal));
    highp vec3 bitangent = cross(normal, tangent);
	highp mat3 tbn = mat3(tangent, bitangent, normal);
	
	highp float occlusion = 0.0;
	highp int KernelSize = int(LightPositions[0].x);
	for (int i = 0; i < KernelSize; ++i) {
	   highp vec3 Spheresample = tbn * LightPositions[i+1].xyz;
	   Spheresample = Spheresample * Radius + position.xyz;
	   highp vec4 SpheresampleV = WorldView * vec4(Spheresample, 1.0);
	      		
	   highp vec4 offset = Projection * vec4(Spheresample, 1.0);
	   offset.xy /= offset.w;
	   offset.xy = offset.xy * 0.5 + 0.5;
	   
	   #ifdef ES_30
			highp float sampleDepth = texture(tex0,offset.xy).r;
	   #else
			highp float sampleDepth = texture2D(tex0,offset.xy).r;
	   #endif
	    
		#ifdef NON_LINEAR_DEPTH
			highp vec4 new_position = WVPInverse*vec4( PosCorner.xy ,sampleDepth,1.0);
			new_position.xyz /= new_position.w;
			new_position.w = 1.0;
		#else
			highp vec4 new_position = CameraPosition + PosCorner*sampleDepth;
		#endif
			
	  highp vec4 new_positionV = WorldView * vec4(new_position.xyz, 1.0);

			
      highp float rangeCheck = abs(SpheresampleV.z - new_positionV.z) < Radius ? 1.0 : 0.0;
	  occlusion += ((new_positionV.z < SpheresampleV.z ) ? 1.0 : 0.0) * rangeCheck;
	}
	
	occlusion = 1.0 - (occlusion / LightPositions[0].x);
	return occlusion;
}

void main(){
	highp vec4 Fcolor = vec4(1.0,1.0,1.0,1.0);
	
	
	highp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	
	#ifdef ES_30
		highp float depth = texture(tex0,coords).r;
	#else
		highp float depth = texture2D(tex0,coords).r;
	#endif
	
	#ifdef NON_LINEAR_DEPTH
		highp vec4 position = WVPInverse*vec4( PosCorner.xy ,depth,1.0);
		position.xyz /= position.w;
		position.w = 1.0;
	#else		
		highp vec4 position = CameraPosition + PosCorner*depth;
	#endif

	if (toogles.x == 1.0){
		Fcolor = CalculateShadow(position);
	}

	if (toogles.y == 1.0) {
		highp vec3 normal = GetNormal(coords);
		highp float Occlusion = GetOcclusion(depth, coords.xy, position, normal);
		Fcolor *= Occlusion;
	}

	
	#ifdef ES_30
		colorOut = Fcolor;
	#else
		gl_FragColor = Fcolor;
	#endif

}
#elif defined(VERTICAL_BLUR_PASS)
uniform mediump sampler2D tex0;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	
	mediump vec4 Sum = vec4(0.0,0.0,0.0,1.0);
	mediump vec2 U = LightPositions[0].y*vec2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	highp int KernelSize = int(LightPositions[0].x);
	highp float Origin = -(float(KernelSize)-2.0)/2.0;
	mediump float V = (Origin);
	mediump vec2 Texcoords;
	for(mediump int i=1;i<(KernelSize-1);i++){	
		Texcoords.xy = vec2(coords.x ,coords.y + V*U.y);
		#ifdef ES_30
			Sum.xyz += LightPositions[i+1].x * texture( tex0, Texcoords.xy ).xyz;
		#else
			Sum.xyz += LightPositions[i+1].x * texture2D( tex0, Texcoords.xy ).xyz;
		#endif
		V++;
	}

	#ifdef ES_30
		colorOut = Sum;
	#else
		gl_FragColor = Sum;
	#endif
}
#elif defined(HORIZONTAL_BLUR_PASS)
uniform mediump sampler2D tex0;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	
	mediump vec4 Sum = vec4(0.0,0.0,0.0,1.0);
	mediump vec2 U = LightPositions[0].y*vec2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	highp int KernelSize = int(LightPositions[0].x);
	highp float Origin = -(float(KernelSize)-2.0)/2.0;
	mediump float H = Origin;
	mediump vec2 Texcoords;
	for(mediump int i=1;i<(KernelSize-1);i++){	
		Texcoords.xy = vec2(coords.x + H*U.x ,coords.y );
		#ifdef ES_30
			Sum.xyz += LightPositions[i+1].x * texture( tex0, Texcoords.xy ).xyz;
		#else
			Sum.xyz += LightPositions[i+1].x * texture2D( tex0, Texcoords.xy ).xyz;
		#endif
		H++;
	}
	
	#ifdef ES_30
		colorOut = Sum;
	#else
		gl_FragColor = Sum;
	#endif
}
#elif defined(ONE_PASS_BLUR)
uniform mediump sampler2D tex0;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	
	mediump vec4 Sum = vec4(0.0,0.0,0.0,1.0);
	mediump vec2 U = LightPositions[0].y*vec2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	highp int KernelSize = int(LightPositions[0].x);
	highp float Origin = -(float(KernelSize)-2.0)/2.0;
	mediump float H = Origin;
	mediump float V = Origin;
	mediump vec2 Texcoords;	
	for(mediump int i=1;i<(KernelSize-1);i++){		
		Texcoords.x = coords.x + H*U.x;
		V = float(Origin);
		for(mediump int j=1;j<(KernelSize-1);j++){
			Texcoords.y = coords.y + V*U.y;
			mediump float weight = roundTo(LightPositions[i+1].x*LightPositions[j+1].x,6.0);
			#ifdef ES_30
				Sum.xyz += weight * texture( tex0, Texcoords.xy ).xyz;
			#else
				Sum.xyz += weight * texture2D( tex0, Texcoords.xy ).xyz;
			#endif
			V++;
		}
		H++;
	}
	
	#ifdef ES_30
		colorOut = Sum;
	#else
		gl_FragColor = Sum;
	#endif
}
#elif defined(BRIGHT_PASS)
uniform mediump sampler2D tex0;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	
	#ifdef ES_30
		mediump vec4 Col = texture( tex0, coords);
	#else
		mediump vec4 Col = texture2D( tex0, coords );
	#endif
	
	mediump float lum = dot( Col.rgb, vec3( 0.299, 0.587, 0.114 ) );

    if( lum < 0.7 )
        Col = vec4( 0.0f, 0.0f, 0.0f, 1.0f );
	else{
		//Col.rgb *= Col.rgb;
	}
	
	#ifdef ES_30
		colorOut = Col;
	#else
		gl_FragColor = Col;
	#endif
}
#elif defined(HDR_COMP_PASS)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
uniform mediump sampler2D tex2;
void main(){
//	lowp vec2 coords = vecUVCoords;
//	coords.y = 1.0 - coords.y;
//	
//	#ifdef ES_30
//		colorOut = texture(tex0,coords);
//	#else
//		gl_FragColor = textureLod(tex0,coords,0);
//	#endif


 	highp vec2 uv = vecUVCoords;
 	uv.y = 1.0 - uv.y;
	int mip = int(CameraPosition.w) - 1;
	highp vec4 color = texture( tex0, uv);
	highp float avgLuminance = dot( textureLod( tex0, uv.xy , float(mip)).rgb , vec3(0.299f, 0.587f, 0.114f) );
    highp float exposure = 0.0;
	
	const highp float log10mul = 1.0/log(10.0);

    avgLuminance = max(avgLuminance, 0.001f);
    highp float keyValue = 0.0;
    keyValue = 1.03f - (2.0f / (2.0 + log10mul*log(avgLuminance + 1.0)));

    highp float linearExposure = (keyValue / avgLuminance);
    exposure = log2(max(linearExposure, 0.0001f));
    color = exp2(exposure) * color;
    
 	
 	highp float pixelLuminance = max(dot(color.rgb, vec3(0.299f, 0.587f, 0.114f)), 0.0001f);
    highp float toneMappedLuminance =( log10mul*log(1.0 + pixelLuminance)) / (log10mul*log(1.0 + LightPositions[0].y));
	color = toneMappedLuminance * pow(color / pixelLuminance, vec4(1.0,1.0,1.0,1.0)); 
	color.a = 1.0;

	#ifdef ES_30
		colorOut = color + LightPositions[0].x*texture( tex1, uv);
	#else
		gl_FragColor = color + LightPositions[0].x*texture2D( tex1, uv);
	#endif
}



#elif defined(RAY_MARCH)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
//Get a random number
highp float rand2D(highp vec2 x) {
  return fract(sin(dot(x.xy,
    vec2(12.9898, 78.233)))*
    43758.5453123 *  (LightPositions[0].x*0.1)   );
}
//Generate noise
highp float noise2D( highp vec2 st) {
  highp vec2 i = floor(st);
  highp vec2 f = fract(st);
  highp float a = rand2D(i);
  highp float b = rand2D(i + vec2(1.0, 0.0));
  highp float c = rand2D(i + vec2(0.0, 1.0));
  highp float d = rand2D(i + vec2(1.0, 1.0));

  highp vec2 u = smoothstep(0.,1.,f);
  return mix(a, b, u.x) +
    (c - a)* u.y * (1.0 - u.x) +
    (d - b) * u.x * u.y;
}
bool
IntersectBox(highp vec3 rayO, highp vec3 rayDir, highp vec3 boxmin, highp vec3 boxmax, out highp float tnear,
  out highp float tfar)
{
  // compute intersection of ray with all six bbox planes
  highp vec3 invR = vec3(1.0,1.0,1.0) / rayDir.xyz;
  highp vec3 tbot = invR * (boxmin.xyz - rayO);
  highp vec3 ttop = invR * (boxmax.xyz - rayO);
  // re-order intersections to find smallest and largest on each axis
  highp vec3 tmin = min(ttop, tbot);
  highp vec3 tmax = max(ttop, tbot);
  // find the largest tmin and the smallest tmax
  highp vec2 t0 = max(tmin.xx, tmin.yz);
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
highp float distanceFunc(highp vec3 p)
{
  highp vec3 sc = vec3(0.5,0.5,0.5);
  highp float d = length(p - sc);
  return d;
}

highp vec4 shade(highp float d)
{
  if (d >= 0.0 && d < 0.3) return (mix(vec4(0, .5, 1, .2), vec4(0, 0, 0, 0), (d - 0.6) / 0.2));
  return vec4(0, 0, 0, 0);
}
highp vec4
Ball(highp vec3 x)
{
  highp float time = LightPositions[0].x;
  highp vec2 uv;
  uv.x = length(x.xz) / 1.42;
  uv.y = x.y;
  highp float d = distanceFunc(x);
  highp float noiseVal = noise2D(uv*40.0);
  d = d+cos(time*noiseVal)*0.1;
  d = d + sin(time * 2.0 )*0.2*noiseVal;
  highp vec4 rret = shade(d);
  return rret;
}
#define VOLUMEFUNC(x) Ball(x)
void main(){
  highp vec2 uv = vecUVCoords.xy;
  uv.y = 1.0 - uv.y;
  highp float depth = texture(tex0, uv).r;
	#ifdef NON_LINEAR_DEPTH
		highp vec4 position = WVPInverse*vec4( PosCorner.xy ,depth,1.0);
		position.xyz /= position.w;
		position.w = 1.0;
	#else		
		highp vec4 position = CameraPosition + PosCorner*depth;
	#endif


  const int steps = 64;
  highp vec4 ray = (position- CameraPosition);
  highp vec4 rayDir = normalize(ray);
  highp float rayLength = length(ray);

  highp float tnear;
  highp float tfar;
  highp vec3 boxMin = vec3(-2.0);
  highp vec3 boxMax = vec3(2.0);
  highp vec3 volPos = toogles.xyz;
  highp vec3 mulT = volPos ;
  boxMin += mulT;
  boxMax += mulT;
  highp float boxSize = boxMax.x - boxMin.x;
  bool hit = IntersectBox(CameraPosition.xyz,rayDir.xyz, boxMin, boxMax, tnear, tfar);
  if (!hit) discard;
  if (tfar < 0.0) discard;
  if (tnear > rayLength) discard;//UNCOMMENT
  if (tnear < 0.0) tnear = 0.0;

  highp vec4 intersectionNear =  CameraPosition + rayDir*tnear;
  highp vec4 intersectionFar =  CameraPosition + rayDir*tfar;

  //March
  highp vec4 c = vec4(0.0,0.0,0.0,0.0);
  highp vec4 step = (intersectionNear- intersectionFar) / vec4(float(steps) - 1.0);
  highp vec4 P = intersectionFar;
  for (int i = 0; i<steps; i++) {
    highp vec3 ppp = (P.xyz - boxMin) / boxSize; //0 - 1 normalized coords
    highp vec4 s = VOLUMEFUNC(ppp);
    c = s.a*s.rgba + (1.0 - s.a)*c; //TODO: Front to back
    P += step;
  }
   colorOut = c;
}

///////////
#elif defined(COC_PASS)
uniform mediump sampler2D tex0;
#ifdef ES_30
	//layout(location = 0) out highp vec4 colorOut;
	layout(location = 1) out highp vec4 colorOut_1;
#endif
void main() {
    lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	highp float aperture = LightPositions[0].x;
	highp float focalLength = LightPositions[0].y;
    highp float depthFocus;
  if (LightPositions[1].x == 1.0)
  	#ifdef ES_30
    depthFocus = texture(tex0, vec2(0.5, 0.5)).r;// Auto Focus center
	#else
	depthFocus = texture2D(tex0, vec2(0.5, 0.5)).r;// Auto Focus center
	#endif
  else
    depthFocus = LightPositions[0].z;
	#ifdef ES_30
	highp float z = texture( tex0, coords ).r;
	#else 
	highp float z = texture2D( tex0, coords ).r;
	#endif
	bool near = (z < depthFocus);
	highp float znear = CameraInfo.x;
	highp float zfar = CameraInfo.y;
    highp float multi = -zfar * znear;
    highp float multi2 = (zfar - znear);
	highp float objectdistance = multi  / (z * multi2 - zfar);
    highp float FocusPlane =     multi  / (depthFocus * multi2 - zfar);
	highp float CoC = abs(aperture * (focalLength * (objectdistance - FocusPlane)) /
          (objectdistance * (FocusPlane - focalLength)));
	if (near) {
	#ifdef ES_30
		colorOut.r = clamp(CoC, 0.0, LightPositions[0].w);
		colorOut_1.r = 0.0;
	#else 
		gl_FragData[0].r = clamp(CoC, 0.0, LightPositions[0].w);
	    gl_FragData[1].r = 0.0;
	#endif
	}
	else {
	#ifdef ES_30
		colorOut_1.r = clamp(CoC, 0.0, LightPositions[0].w);
		colorOut.r = 0.0;
	#else 
		gl_FragData[1].r = clamp(CoC, 0.0, LightPositions[0].w);
		gl_FragData[0].r = 0.0;
	#endif
	}

}

#elif defined(COMBINE_COC_PASS)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
void main() {
  lowp vec2 coords = vecUVCoords;
  coords.y = 1.0 - coords.y;
  #ifdef ES_30
  highp float CoC0 = texture(tex0, coords).r;
  highp float CoC1 = texture(tex1, coords).r;
  #else
  highp float CoC0 = texture2D(tex0, coords).r;
  highp float CoC1 = texture2D(tex1, coords).r;
  #endif
  highp float CoC =  2.0*max(CoC0, CoC1) - CoC0;

#ifdef ES_30
	colorOut.r = CoC;
#else
	gl_FragColor.r = CoC;
#endif
}

#elif defined(DOF_PASS)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
void main(){
  lowp vec2 coords = vecUVCoords;
  coords.y = 1.0 - coords.y;
  #ifdef ES_30
  highp float dofblur = texture(tex1, coords).r;
  highp vec4 color =  texture(tex0, coords);
  #else
  highp float dofblur = texture2D(tex1, coords).r;
  highp vec4 color =  texture2D(tex0, coords);
  #endif

  highp vec2 Offset = vec2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
  highp float Tot = 0.0;
  highp float Samples_squared = LightPositions[0].y;
  for (highp float i = -Samples_squared; i <= Samples_squared; i++) {
    for (highp float j = -Samples_squared; j <= Samples_squared; j++) {
      lowp vec2  tcoord = coords + vec2(i,j) *Offset* dofblur;
	  #ifdef ES_30
		color+= texture(tex0, tcoord);
	  #else
		color+= texture2D(tex0, tcoord);
	  #endif
	  Tot++;
    }
  }
  color /= Tot;
  color.a = 1.0;

#ifdef ES_30
	colorOut = color;
#else
	gl_FragColor = color;
#endif
}
#elif defined(DOF_PASS_2)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
void main(){
lowp vec2 coords = vecUVCoords;
coords.y = 1.0 - coords.y;
#ifdef ES_30
highp float dofblur = texture(tex1, coords).r;
highp vec4 color =  texture(tex0, coords);
#else
highp float dofblur = texture2D(tex1, coords).r;
highp vec4 color =  texture2D(tex0, coords);
#endif

highp float Tot = 0.0;
highp float Samples_squared = LightPositions[0].x;
highp vec2 Offset = vec2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
for (highp float i = -Samples_squared; i <= Samples_squared; i++) {
  for (highp float j = -Samples_squared; j <= Samples_squared; j++) {
    lowp vec2  tcoord = coords + vec2(i,j) *Offset* dofblur;
	#ifdef ES_30
		color += texture(tex0, tcoord);
	#else
		color += texture2D(tex0, tcoord);
	#endif
	Tot++;
  }
}
color /= Tot;
color.a = 1.0;
#ifdef ES_30
	colorOut = color;
#else
	gl_FragColor = color;
#endif
}
//////////////
#elif defined(VIGNETTE_PASS)
uniform mediump sampler2D tex0;
void main() {
  const highp float Falloff = 0.45;
  highp vec4 color;
  lowp vec2 coords = vecUVCoords;
  coords.y = 1.0 - coords.y;
  color.x =  texture(tex0, coords.xy + 0.0019).r;
  color.y = texture(tex0, coords.xy).g;
  color.z = texture(tex0, coords.xy  - 0.0019).b;
  highp vec2 uv = coords.xy;
  highp float e = 1.0-max((distance(uv, vec2(0.5, 0.5)) - Falloff) * 1.25, 0.0);
#ifdef ES_30
	colorOut = vec4(color.rgb * e , 1.0);
#else
	gl_FragColor = vec4(color.rgb * e , 1.0);
#endif

}

#elif defined(GOD_RAY_CALCULATION_PASS)
#define raysSamples 64
uniform mediump sampler2D tex0;
void main() {
  lowp vec2 uv = vecUVCoords.xy;
  uv.y = 1.0 - uv.y;
  const highp vec2 defaultPos = vec2(0.8, 0.4);
  const highp vec2 raysCenter = vec2(0.435, 0.59);
  const highp float raySize = 0.2;
  highp float scale = (1.0 - defaultPos.y) * raySize;
  const highp float accum = 1.0 / float(raysSamples);
  scale = mix(1.0, 1.0 - accum, scale);
  highp vec3 col = texture(tex0, uv).rgb;
  for (int i = 0; i < raysSamples; ++i) {
    uv = (uv - raysCenter) * scale + raysCenter;
    col += texture(tex0, uv).rgb;
  }
  highp vec3 rays =  col * vec3(accum,accum,accum);
  rays = pow(rays,  vec3(0.4545,0.4545,0.4545));
  rays = smoothstep(defaultPos.x, 1.0, rays);

  #ifdef ES_30
	colorOut = vec4(rays , 1.0);
#else
	gl_FragColor = vec4(rays , 1.0);
#endif

}

#elif defined(GOD_RAY_BLEND_PASS)
#define raysIntensity 0.25
#define raysSaturation 0.5
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
void main(){
  lowp vec2 uv = vecUVCoords.xy;
  uv.y = 1.0 - uv.y;
  highp vec3 col = texture(tex0, uv).rgb;
  highp vec3 rays = texture(tex1, uv).rgb;
  if (rays.x < 0.1 && rays.y < 0.1 && rays.z < 0.1){
	#ifdef ES_30
		colorOut = vec4(col , 1.0);
	#else
		gl_FragColor = vec4(col , 1.0);
	#endif
  }
  else {
	  highp vec3 colorWeights = vec3(0.299, 0.587, 0.114);
	  highp float tt = dot(rays, colorWeights);
	  rays = mix(vec3(tt,tt,tt), rays, raysSaturation);
	  rays = pow(rays, vec3(2.2,2.2,2.2));

	  col += rays * vec3(raysIntensity, raysIntensity, raysIntensity);
	  //col -= mix(vec3(0.0,0.0,0.0), vec3(1.0,1.0,1.0) - rays, raysIntensity);

	#ifdef ES_30
		colorOut = vec4(col , 1.0);
	#else
		gl_FragColor = vec4(col , 1.0);
	#endif
	}
}

#elif defined(LIGHT_RAY_MARCHING)
#define G_SCATTERING -0.2
#define PI 3.14159265359
highp float ComputeScattering(highp float lightDotView)
{
  highp float result = 1.0f - G_SCATTERING * G_SCATTERING;
  result /= (4.0f * PI * pow(1.0 + G_SCATTERING * G_SCATTERING - (2.0f * G_SCATTERING) *      lightDotView, 1.5));
  return result;
}
uniform mediump sampler2D tex0;
uniform mediump sampler2DShadow  tex1;
void main(){
  const highp float ditherPattern[16] = float[]( 0.0f, 0.5f, 0.125f, 0.625f,0.75f, 0.22f, 0.875f, 0.375f,0.1875f, 0.6875f, 0.0625f, 0.5625, 0.9375f, 0.4375f, 0.8125f, 0.3125);
  highp vec2 uv = vecUVCoords.xy;
  uv.y = 1.0 - uv.y;
  highp float depth = texture(tex0, uv).r;
#ifdef NON_LINEAR_DEPTH
 highp vec4 position = WVPInverse*vec4(PosCorner.xy ,depth,1.0);
 position.xyz /= position.w;
#else		
 highp vec4 position = CameraPosition + PosCorner*depth;
#endif
 int steps = int(LightPositions[0].y);
 highp vec4 ray = vec4(position - CameraPosition);
 highp vec4 rayDir = normalize(ray);
 highp float rayLength = length(ray);

 highp vec4 intersectionNear = CameraPosition;
 highp vec4 intersectionFar = position;

//March
 highp vec4 step = (intersectionNear - intersectionFar) / float(steps - 1);
 highp vec4 P = intersectionFar;
 //int xD = int(mod(position.x, 4.0));
 //int yD = int(mod(position.y, 4.0));
 //P += step * ditherPattern[xD + yD * 4];
 highp vec4 accumFog = vec4(0.0,0.0,0.0,0.0);

highp vec4 LightPos;
highp vec2 SHTC;
highp float depthValue;
highp vec4 sunDir;
highp vec3 scattering;
const highp vec3 lightColor = vec3(0.9803, 0.8392, 0.9470);
for (int i = 0; i<steps; i++) {
  LightPos = WVPLight* P;
  LightPos.xy /= LightPos.w;
  LightPos.z /= LightCameraInfo.y;
  SHTC = LightPos.xy*0.5 + 0.5;

  highp vec3 Coords_Final = vec3(SHTC.xy, LightPos.z);
  highp float Val_1 = texture(tex1, Coords_Final, 0.0005);

  if (Val_1 > 0.0 && SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x  > 0.0 && SHTC.y > 0.0 && LightPos.w > 0.0 && LightPos.z < 1.0) {
    sunDir = normalize(P - LightCameraPosition);
    scattering = lightColor * ComputeScattering(dot(rayDir.rgb, sunDir.rgb));
    accumFog.rgb += scattering ;
  }
  P += step;
}
accumFog /= vec4(steps,steps,steps,steps);
accumFog = pow(accumFog,  vec4(0.4545,0.4545,0.4545,0.4545));

  #ifdef ES_30
	colorOut = vec4(accumFog.rgb , 1.0);
#else
	gl_FragColor = vec4(accumFog.rgb , 1.0);
#endif
}
#elif defined(LIGHT_ADD)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
void main() {
  lowp vec2 uv = vecUVCoords.xy;
  uv.y = 1.0 - uv.y;





  const highp float raysIntensity = 1.0;
  highp vec4 col = texture(tex1, uv.xy);
  highp vec4 vol = texture(tex0, uv.xy);
  //vol.rgb = pow(vol.rgb, vec3(2.2,2.2,2.2));
  col += vol * vec4(raysIntensity, raysIntensity, raysIntensity, raysIntensity);
  //col -= lerp(vec4(0.0,0.0,0.0,0.0), vec4(1.0,1.0,1.0,1.0) - vol, raysIntensity);

    #ifdef ES_30
	colorOut = vec4(col.rgb , 1.0);
#else
	gl_FragColor = vec4(col.rgb , 1.0);
#endif
}





////////////////

#elif defined(FADE)
void main(){
	#ifdef ES_30
		colorOut = vec4( 0.0,0.0,0.0, brightness.x);
	#else
		gl_FragColor = vec4( 1.0,1.0,1.0, brightness.x);
	#endif
}
#elif defined(FSQUAD_1_TEX)
uniform mediump sampler2D tex0;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	#ifdef ES_30
		colorOut = texture(tex0,coords) * brightness.x;
	#else
		gl_FragColor = texture2D(tex0,coords) * brightness.x;
	#endif
}
#elif defined(FSQUAD_2_TEX)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;
	
	#ifdef ES_30
		colorOut = texture(tex0,coords) + texture(tex1,coords);
	#else
		gl_FragColor =/* texture2D(tex0,coords) + */ texture2D(tex1,coords);
	#endif
}
#elif defined(FSQUAD_3_TEX)
uniform mediump sampler2D tex0;
uniform mediump sampler2D tex1;
uniform mediump sampler2D tex2;
void main(){
	lowp vec2 coords = vecUVCoords;
	coords.y = 1.0 - coords.y;

	#ifdef ES_30
		colorOut = vec4(texture(tex0, coords).rrr,1.0);
	#else
		gl_FragColor = vec4(texture2D(tex0,coords).rrr, 1.0);
	#endif
}
#else
void main(){
	#ifdef ES_30
		colorOut = vec4(1.0,0.0,1.0,1.0);
	#else
		gl_FragColor = vec4(1.0,0.0,1.0,1.0);
	#endif
}
#endif




