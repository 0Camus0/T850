
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

float4 FS( VS_OUTPUT input ) : SV_TARGET {
    return DiffuseColor;
}
