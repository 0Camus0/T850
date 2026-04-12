cbuffer ConstantBuffer{
    float4 tint;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
    float2 texture0  : TEXCOORD;
};

SamplerState SS;
Texture2D tex0 : register(t0);

float4 FS( VS_OUTPUT input ) : SV_TARGET {
	float4 texColor = tex0.Sample( SS, input.texture0.xy );
	return  float4(texColor.rgb * tint.rgb, texColor.a);
}
