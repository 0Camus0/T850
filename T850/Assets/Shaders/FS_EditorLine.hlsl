// Editor line shader — see VS_EditorLine.hlsl for the cbuffer layout.
cbuffer ConstantBuffer{
    float4x4 WVP;
    float4   LineColor;
}

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

float4 FS( VS_OUTPUT input ) : SV_TARGET  {
    return LineColor;
}
