cbuffer ConstantBuffer{
    float4x4 WVP;
	float4x4 World;  
	float4x4 WorldView;
	float4x4 WVPInverse;
	float4x4 WVPLight;
	float4	 LightPositions[128];
	float4	 LightColors[128];
  float4	 LightRadius[32];
	float4   CameraPosition;
	float4 	 CameraInfo;
	float4	 LightCameraPosition;
	float4 	 LightCameraInfo;

	float4   brightness;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
    float2 texture0  : TEXCOORD;
	float4 Pos		: TEXCOORD1;
	float4 PosCorner : VPOS;
};

SamplerState SS;

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
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Final = float4(0.0,0.0,0.0,1.0);
	float4 color  =  tex0.Sample( SS, input.texture0);
	float4 matId  =	tex3.Sample( SS, input.texture0);
	float depth = tex4.Sample( SS, input.texture0 ).r;
	
	#ifdef NON_LINEAR_DEPTH
		float4 position = mul(WVPInverse,float4( input.PosCorner.xy ,depth,1.0));
		position.xyz /= position.w;
	#else		
		float4 position = CameraPosition + input.PosCorner*depth;
	#endif
	 
	 float3  EyeDir   = normalize(CameraPosition-position).xyz;
	 
	if(matId.r == 1.0 && matId.g == 0.0){		
		float3 RefCol = texEnv.Sample( SS, -EyeDir ).zyx; //TODO: add technique
		Final.xyz = RefCol.xyz; //TODO: add technique
	}else{
		float cutoff = 0.8;
		float4 Lambert = float4(1.0,1.0,1.0,1.0);
		float4 Specular = float4(1.0,1.0,1.0,1.0);
		float4 Fresnel	 =  float4(1.0,1.0,1.0,1.0);
		float4 Ambient	 =  color;
	
		float4 normalmap = tex1.Sample( SS, input.texture0 );
		float3 normal = normalmap.xyz*2 - 1;
		normal = normalize(normal);

		float4 specularmap = tex2.Sample( SS, input.texture0);
		
		float3 ReflectedVec = normalize(reflect(-EyeDir,normal.xyz));	
		float ratio = 1.0/1.52;
		float3 R = refract(-EyeDir,normal.xyz,ratio);
		float3 RefleCol = float3(0.0,0.0,0.0) /*texEnv.Sample( SS, ReflectedVec ).zyx *//*TODO: add technique*/;
		float3 RefraCol = float3(1.0,1.0,1.0) /*texEnv.Sample( SS, R ).zyx*//* TODO: add technique*/;
		
		int NumLights = (int)CameraInfo.w;
    //float trueRad[128] = (float[128])LightRadius;
			for(int i=0;i<NumLights;i++){
        float Rad = LightRadius[i >> 2][i & 3];
				float dist = distance(LightPositions[i],position);
				if(dist < Rad*2.0){
					Lambert  = LightColors[i];
					Specular = LightColors[i];
					Fresnel	 =  LightColors[i];			
					
					float3	LightDir = normalize(LightPositions[i]-position).xyz;
					float   att		 = 1.0;
					att		 	     = dot(normal.xyz,LightDir)*0.5 + 0.5;;
					att				 = pow( att , 2.0 );	
					att				 = clamp( att , 0.0 , 1.0 );
					Lambert			*= color*att;
					
					float  specular  = 0.0;
					float specIntesivity = 1.5;
					float shinness = 4.0;	
										
					float3 ReflectedLight = normalize(EyeDir+LightDir); 
					specular = max ( dot(ReflectedLight,normal.xyz)*0.5 + 0.5, 0.0);	
					specular = pow( specular ,shinness);	

					specular *= att;
					specular *= specIntesivity;
					Specular *= specular;
					Specular.xyz *= specularmap.xyz;
									
					float d = max(dist - Rad, 0.0);
					float denom = d/Rad + 1.0;
					
					float attenuation = 1.0 / (denom*denom);
					 
					attenuation = (attenuation - cutoff) / (1.0 - cutoff);
					attenuation = clamp(attenuation, 0.0,1.0);
						
					Final += Lambert*attenuation;
					Final += Specular*attenuation;
				}
			}
		if(matId.b == 0.0){
			float  FresnelAtt	= dot(normal.xyz,EyeDir);
			float  FresnelIntensity = 6.0f;
			float4 FresnelCol = float4(RefleCol.xyz,1.0);

			FresnelAtt		= abs(FresnelAtt);
			FresnelAtt 		= 1.0 - FresnelAtt;
			FresnelAtt 		= clamp( FresnelAtt , 0.0 , 1.0 );
			FresnelAtt		= pow( FresnelAtt , 4.0 );	
			FresnelAtt 		= clamp(FresnelAtt , 0.0 , 1.0 );
			Fresnel 		= FresnelCol*FresnelIntensity*FresnelAtt;
		
			//Final += Fresnel;		
			//Final.xyz += 0.26*RefraCol.xyz;
			//Final = float4(1,0,1,1);
		}
		//Final += Ambient*0.2;
		Final.xyz *= tex5.Sample( SS, input.texture0).xyz;
		
	}
	return Final;

}
#elif defined(SHADOW_COMP_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
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
	
	float4 LightPos = mul(WVPLight , position);
#ifdef NON_LINEAR_DEPTH
	LightPos.xyz /= LightPos.w;
#else
	LightPos.xy /= LightPos.w;
	LightPos.z /= LightCameraInfo.y;
#endif
	float2 SHTC = LightPos.xy*0.5 + 0.5;
	
	if(SHTC.x < 1.0 && SHTC.y < 1.0 && SHTC.x  > 0.0 && SHTC.y > 0.0 && LightPos.w > 0.0 && LightPos.z < 1.0 ){
		SHTC.y = 1.0 - SHTC.y;				
		float depthSM = tex1.Sample( SS, SHTC );
		float depthPos = LightPos.z;
		depthSM += 0.000005;
		if( depthPos > depthSM)
			Fcolor = 0.25*float4(1.0,1.0,1.0,1.0);	  
		
	}else{
		Fcolor = float4(1.0,1.0,1.0,1.0);
	}
	
	  return Fcolor;
}
#elif defined(VERTICAL_BLUR_PASS)
Texture2D tex0 : register(t0);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 Sum = float4(0.0,0.0,0.0,1.0);
	float2 U = LightPositions[0].y*float2( 1.0/LightPositions[0].z,1.0/LightPositions[0].w);
	int KernelSize = (int)LightPositions[0].x;
	float Origin = -((((float)(KernelSize))-2.0)/2.0);
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
	float Origin = -((((float)(KernelSize))-2.0)/2.0);
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
	float Origin = -((((float)(KernelSize))-2.0)/2.0);
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
float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 color = tex0.Sample( SS, input.texture0);
	
	float FLum = dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
	
	if(FLum < 0.7)//0.7
		color = float4(0.0,0.0,0.0,1.0);
		
	return color;
	
	
}
#elif defined(HDR_COMP_PASS)
Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
float4 FS( VS_OUTPUT input ) : SV_TARGET {
/*
	int mip = ((int)CameraPosition.w) - 1;
	float3 color = tex0.Sample( SS, input.texture0).rgb;
	float avgLuminance = dot( tex0.SampleLevel( SS, input.texture0 , mip).rgb , float3(0.299f, 0.587f, 0.114f) );

	// exposure
	float keyValue = LightPositions[0].y;
	float BloomFac = LightPositions[0].x;
	float linearExposure = keyValue / avgLuminance;
//	float exposure = max(linearExposure, 0.0001f);
    color *= linearExposure;

	// filmic tonemapping
    color = max(0, color - 0.004f);
    color = (color * (6.2f * color + 0.5f)) / (color * (6.2f * color + 1.7f)+ 0.06f);
    // result has 1/2.2 baked in
    float4 FCol = float4(pow(color, 2.2f),1.0);
	
	
	float3 Bloom = tex1.Sample( SS, input.texture0).rgb*BloomFac;
	

	return float4(FCol * (BloomFac-Bloom) + Bloom, 1.0f);
	*/
	
	
	int mip = ((int)CameraPosition.w) - 1;
	float4 color = tex0.Sample( SS, input.texture0);
	float avgLuminance = dot( tex0.SampleLevel( SS, input.texture0 , mip).rgb , float3(0.299f, 0.587f, 0.114f) );
    float exposure = 0;
	     	
    avgLuminance = max(avgLuminance, 0.001f);
    float keyValue = 0;
    keyValue = 1.03f - (2.0f / (2 + log10(avgLuminance + 1)));

    float linearExposure = (keyValue / avgLuminance);
    exposure = log2(max(linearExposure, 0.0001f));
    color = exp2(exposure) * color;
    
 	
 	float pixelLuminance = max(dot(color.rgb, float3(0.299f, 0.587f, 0.114f)), 0.0001f);
    float toneMappedLuminance = log10(1 + pixelLuminance) / log10(1.0 + LightPositions[0].y);
	color = toneMappedLuminance * pow(color / pixelLuminance, 1.0f); 
	color.a = 1.0;
	return color + LightPositions[0].x*tex1.Sample( SS, input.texture0);
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
  if (LightPositions[1].x)
    depthFocus = tex0.Sample(SS, float2(0.5, 0.5)).r;// Auto Focus center
  else
    depthFocus = LightPositions[0].z;

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

#elif defined(VIGNETTE_PASS)
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float Falloff = 0.45;
  float4 color;
  color.x =  tex0.Sample(SS, input.texture0.xy + 0.0019).r;
  color.y = tex0.Sample(SS, input.texture0.xy).g;
  color.z = tex0.Sample(SS, input.texture0.xy  - 0.0019).b;
  float2 uv = input.texture0.xy;
  float e = 1-max((distance(uv, float2(0.5, 0.5)) - Falloff) * 1.25, 0.0);;
  return float4(color.rgb * e , 1.0);
}

#elif defined(GOD_RAY_CALCULATION_PASS)
#define raysSamples 64
Texture2D tex0 : register(t0);
float4 FS(VS_OUTPUT input) : SV_TARGET{
  float2 uv = input.texture0.xy;
  const float2 defaultPos = float2(0.8, 0.2);
  const float2 raysCenter = float2(0.435, 0.99);
  const float raySize = 3.0;
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
  for (int i = 0; i<steps; i++) {
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
  //return float4(1,0,1,1);
  float depth = tex0.Sample(SS, input.texture0);
#ifdef NON_LINEAR_DEPTH
float4 position = mul(WVPInverse,float4(input.PosCorner.xy ,depth,1.0));
position.xyz /= position.w;
#else		
float4 position = CameraPosition + input.PosCorner*depth;
#endif
const int steps = 128;
float4 ray = (position - CameraPosition);
float4 rayDir = normalize(ray);
float rayLength = length(ray);

float4 intersectionNear = CameraPosition;
float4 intersectionFar = position;

//March
float4 c = 0;
float4 step = (intersectionNear - intersectionFar) / (steps - 1);
float4 P = intersectionFar;
float3 accumFog = 0.0f.xxx;

float4 LightPos = mul(WVPLight, position);
LightPos.xy /= LightPos.w;
LightPos.z /= LightCameraInfo.y;
float2 SHTC = LightPos.xy*0.5 + 0.5;
SHTC.y = 1.0 - SHTC.y;
for (int i = 0; i<steps; i++) {
  float4 LightPos = mul(WVPLight, P);
  LightPos.xy /= LightPos.w;
  LightPos.z /= LightCameraInfo.y;
  float2 SHTC = LightPos.xy*0.5 + 0.5;
  SHTC.y = 1.0 - SHTC.y;
  float depthValue = tex1.Sample(SS, SHTC);

  if (depthValue > LightPos.z-0.00005)
  {
    float4 sunDir = normalize(P - LightCameraPosition);
    float3 scattering = ComputeScattering(dot(rayDir.rgb, sunDir.rgb)).xxx * float3(0.9803, 0.8392, 0.6470);
    accumFog += scattering ;
  }
  P += step;
}
accumFog /= steps;
accumFog = pow(accumFog, float3(0.4545, 0.4545, 0.4545));
return float4(accumFog,1);
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
