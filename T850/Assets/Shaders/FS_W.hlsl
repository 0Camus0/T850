cbuffer ConstantBuffer{
    float4x4 WVP;
    float4 LineColor;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

float4 FS( VS_OUTPUT input ) : SV_TARGET  {
    float4 color = LineColor;
    return color;
}
