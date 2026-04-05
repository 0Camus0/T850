cbuffer ConstantBuffer{
    float4x4 World;
    float4x4 WVP;
}

Texture2D TextureRGB : register(t0);

SamplerState SS;

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
	float4 hnormal   : NORMAL;
    float2 texture0  : TEXCOORD;
};

float4 FS( VS_OUTPUT input ) : SV_TARGET  {
    float4 color;
	color = TextureRGB.Sample( SS, input.texture0 );	
    return color;
}
