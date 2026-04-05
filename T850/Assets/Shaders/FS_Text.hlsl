cbuffer ConstantBuffer{
    float4 color;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
    float2 texture0  : TEXCOORD;
};

SamplerState SS;
Texture2D tex0 : register(t0);

float4 FS( VS_OUTPUT input ) : SV_TARGET {	
	float alpha = tex0.Sample( SS, input.texture0.xy ).r;
	return  float4(color.xyz,alpha);
}
