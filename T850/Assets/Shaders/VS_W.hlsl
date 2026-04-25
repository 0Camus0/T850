cbuffer ConstantBuffer{
    float4x4 WVP;   
}

#ifdef USE_SKINNING_QT
cbuffer BoneBuffer{
	float4 BoneQuats[256];
	float4 BoneTrans[256];
}
#elif defined(USE_SKINNING)
cbuffer BoneBuffer{
	float4x4 BoneMatrices[256];
}
#endif

struct VS_INPUT{
    float4 position : POSITION;
#if defined(USE_SKINNING) || defined(USE_SKINNING_QT)
	float4 joints   : BLENDINDICES;
	float4 weights  : BLENDWEIGHT;
#endif
};

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

VS_OUTPUT VS( VS_INPUT input ){
    VS_OUTPUT OUT;
#ifdef USE_SKINNING_QT
	int4 idx = int4(input.joints);
	float4 q = BoneQuats[idx.x] * input.weights.x
	          + BoneQuats[idx.y] * input.weights.y
	          + BoneQuats[idx.z] * input.weights.z
	          + BoneQuats[idx.w] * input.weights.w;
	q = normalize(q);
	float3 t = BoneTrans[idx.x].xyz * input.weights.x
	         + BoneTrans[idx.y].xyz * input.weights.y
	         + BoneTrans[idx.z].xyz * input.weights.z
	         + BoneTrans[idx.w].xyz * input.weights.w;
	float3 p = input.position.xyz;
	float3 u = q.xyz;
	float  s = q.w;
	p = p + 2.0 * cross(u, cross(u, p) + s * p);
	input.position = float4(p + t, 1.0);
#elif defined(USE_SKINNING)
	int4 idx = int4(input.joints);
	float4x4 skinMatrix = BoneMatrices[idx.x] * input.weights.x
	                     + BoneMatrices[idx.y] * input.weights.y
	                     + BoneMatrices[idx.z] * input.weights.z
	                     + BoneMatrices[idx.w] * input.weights.w;
	input.position = mul(skinMatrix, input.position);
#endif
    OUT.hposition = mul(WVP, input.position);
    return OUT;
}
