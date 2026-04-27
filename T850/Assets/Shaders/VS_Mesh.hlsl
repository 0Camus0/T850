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
#ifdef USE_SKINNING_QT
	float4   BoneQuats[256];
	float4   BoneTrans[256];
#elif defined(USE_SKINNING)
	float4x4 BoneMatrices[256];
#endif
}

#ifdef USE_SKINNING_TEXTURE
Texture2D<float4> BoneTexture : register(t7);

float4x4 getBoneMatrix(int index) {
	uint texW, texH;
	BoneTexture.GetDimensions(texW, texH);
	int pixelIndex = index * 4;
	int tw = (int)texW;
	float4 r0 = BoneTexture.Load(int3(pixelIndex     % tw, pixelIndex     / tw, 0));
	float4 r1 = BoneTexture.Load(int3((pixelIndex+1) % tw, (pixelIndex+1) / tw, 0));
	float4 r2 = BoneTexture.Load(int3((pixelIndex+2) % tw, (pixelIndex+2) / tw, 0));
	float4 r3 = BoneTexture.Load(int3((pixelIndex+3) % tw, (pixelIndex+3) / tw, 0));
	return float4x4(r0, r1, r2, r3);
}
#endif

struct VS_INPUT{
    float4 position : POSITION;
	
#ifdef USE_NORMALS
	float4 normal   : NORMAL;
#endif

#ifdef USE_TANGENTS
	float4 tangent  : TANGENT;
#endif

#ifdef USE_BINORMALS
	float4 binormal  : BINORMAL;
#endif

#ifdef USE_TEXCOORD0
	float2 texture0 : TEXCOORD0;
#endif

#ifdef USE_TEXCOORD1
	float2 texture1 : TEXCOORD1;
#endif

#if defined(USE_SKINNING) || defined(USE_SKINNING_QT) || defined(USE_SKINNING_TEXTURE)
	float4 joints   : BLENDINDICES;
	float4 weights  : BLENDWEIGHT;
#endif
};

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
	float2 texture0  : TEXCOORD0;
#endif

#ifdef USE_TEXCOORD1
	float2 texture1  : TEXCOORD3;
#endif

	float4 Pos		: TEXCOORD1;
	
	float4 WorldPos		: TEXCOORD2;
};

VS_OUTPUT VS( VS_INPUT input ){
    VS_OUTPUT OUT;

#ifdef USE_SKINNING_TEXTURE
	// Texture-based skinning: read bone matrices from RGBA32F texture
	int4 idx = int4(input.joints);
	float4x4 skinMatrix = getBoneMatrix(idx.x) * input.weights.x
	                     + getBoneMatrix(idx.y) * input.weights.y
	                     + getBoneMatrix(idx.z) * input.weights.z
	                     + getBoneMatrix(idx.w) * input.weights.w;
	// Texture stores row-major → float4x4 rows are correct → row-vector multiply
	input.position = mul(input.position, skinMatrix);
#ifdef USE_NORMALS
	input.normal.xyz = mul(input.normal.xyz, (float3x3)skinMatrix);
#endif
#ifdef USE_TANGENTS
	input.tangent.xyz = mul(input.tangent.xyz, (float3x3)skinMatrix);
#endif
#ifdef USE_BINORMALS
	input.binormal.xyz = mul(input.binormal.xyz, (float3x3)skinMatrix);
#endif

#elif defined(USE_SKINNING_QT)
	// Quaternion+Translation skinning: 2 vec4/bone instead of 4x4 matrix
	int4 idx = int4(input.joints);
	// Blend quaternions (NLERP — normalize after weighted sum)
	float4 q = BoneQuats[idx.x] * input.weights.x
	          + BoneQuats[idx.y] * input.weights.y
	          + BoneQuats[idx.z] * input.weights.z
	          + BoneQuats[idx.w] * input.weights.w;
	q = normalize(q);
	// Blend translations
	float3 t = BoneTrans[idx.x].xyz * input.weights.x
	         + BoneTrans[idx.y].xyz * input.weights.y
	         + BoneTrans[idx.z].xyz * input.weights.z
	         + BoneTrans[idx.w].xyz * input.weights.w;
	// Apply quaternion rotation: v' = q * v * q^(-1)
	// Optimized: v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
	float3 p = input.position.xyz;
	float3 u = q.xyz;
	float  s = q.w;
	p = p + 2.0 * cross(u, cross(u, p) + s * p);
	input.position = float4(p + t, 1.0);
#ifdef USE_NORMALS
	float3 n = input.normal.xyz;
	input.normal.xyz = n + 2.0 * cross(u, cross(u, n) + s * n);
#endif
#ifdef USE_TANGENTS
	float3 tg = input.tangent.xyz;
	input.tangent.xyz = tg + 2.0 * cross(u, cross(u, tg) + s * tg);
#endif
#ifdef USE_BINORMALS
	float3 bn = input.binormal.xyz;
	input.binormal.xyz = bn + 2.0 * cross(u, cross(u, bn) + s * bn);
#endif

#elif defined(USE_SKINNING)
	int4 idx = int4(input.joints);
	float4x4 skinMatrix = BoneMatrices[idx.x] * input.weights.x
	                     + BoneMatrices[idx.y] * input.weights.y
	                     + BoneMatrices[idx.z] * input.weights.z
	                     + BoneMatrices[idx.w] * input.weights.w;
	input.position = mul(skinMatrix, input.position);
#ifdef USE_NORMALS
	input.normal.xyz = mul((float3x3)skinMatrix, input.normal.xyz);
#endif
#ifdef USE_TANGENTS
	input.tangent.xyz = mul((float3x3)skinMatrix, input.tangent.xyz);
#endif
#ifdef USE_BINORMALS
	input.binormal.xyz = mul((float3x3)skinMatrix, input.binormal.xyz);
#endif
#endif

#ifdef USE_TEXCOORD0
    OUT.texture0 = input.texture0;
#endif

#ifdef USE_TEXCOORD1
	OUT.texture1 = input.texture1;
#endif

#ifdef SHADOW_MAP_PASS
	OUT.hposition = mul( WVP , input.position );
	OUT.Pos		  = OUT.hposition;
#else	
    OUT.hposition = mul( WVP , input.position );
	
	float3x3 RotWorld = (float3x3)World;
	
#ifdef USE_NORMALS	
	OUT.hnormal  = float4(normalize( mul( RotWorld, input.normal.xyz ) ) , 1.0);
#endif

#ifdef USE_TANGENTS	
	OUT.htangent = float4(normalize( mul( RotWorld , input.tangent.xyz ) ) , 1.0);
#endif

#ifdef USE_BINORMALS	
	OUT.hbinormal = float4(normalize( mul( RotWorld , input.binormal.xyz ) ) , 1.0);
#endif
	
	OUT.Pos = mul( WVP , input.position );

	OUT.WorldPos = mul( World , input.position );
#endif
    return OUT;
}
