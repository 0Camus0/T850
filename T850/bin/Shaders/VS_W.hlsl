cbuffer ConstantBuffer{
    float4x4 WVP;   
}

struct VS_INPUT{
    float4 position : POSITION;
};

struct VS_OUTPUT{
    float4 hposition : SV_POSITION;
};

VS_OUTPUT VS( VS_INPUT input ){
    VS_OUTPUT OUT;
    OUT.hposition = mul(WVP, input.position);
    return OUT;
}
