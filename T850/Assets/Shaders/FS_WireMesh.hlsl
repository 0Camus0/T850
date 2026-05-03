
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
	float4   PBRParams;
	float4   Intensities;
	float4   ParallaxSettings;
	float4   ParallaxShadowSettings;
	float4   Light0Direction;
}

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

Texture2D depthTex : register(t0);
SamplerState depthSampler : register(s0);

float4 FS( VS_OUTPUT input ) : SV_TARGET {
	// Manual depth test against GBuffer depth
    float2 screenUV = input.hposition.xy * float2(1.0/CameraInfo.z, 1.0/CameraInfo.w);
    float sceneDepth = depthTex.Sample(depthSampler, screenUV).r;
	float wireDepth = input.Pos.z / input.Pos.w;
	if (sceneDepth > 0.0001 && wireDepth < sceneDepth * 0.995)
        discard;
    return DiffuseColor;
}
