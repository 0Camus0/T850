cbuffer ConstantBuffer{
    float4x4 WVP;   
}

#ifdef USE_SKINNING
cbuffer BoneBuffer{
	float4x4 BoneMatrices[128];
}
#endif

struct VS_INPUT{
    float4 position : POSITION;
#ifdef USE_SKINNING
	float4 joints   : BLENDINDICES;
	float4 weights  : BLENDWEIGHT;
#endif
};

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

VS_OUTPUT VS( VS_INPUT input ){
    VS_OUTPUT OUT;
#ifdef USE_SKINNING
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
