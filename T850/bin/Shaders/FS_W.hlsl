cbuffer ConstantBuffer{
    float4x4 WVP;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

float4 FS( VS_OUTPUT input ) : SV_TARGET  {
    float4 color =  float4(1.0,0.0,1.0,1.0);	
    return color;
}
