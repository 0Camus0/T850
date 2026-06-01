// Flat line fragment shader — always visible, no depth testing.
// Used for skeleton/bone debug visualization that should draw on top.
cbuffer ConstantBuffer : register(b0) {
    float4x4 WVP;
    float4   LineColor;
    float4   DepthParams;
}

struct VS_OUTPUT{
    float4 hposition  : SV_POSITION;
    float  linearDepth : TEXCOORD0;
};

float4 FS( VS_OUTPUT input ) : SV_TARGET {
    return LineColor;
}
