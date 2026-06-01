// Editor line vertex shader — depth-tested wireframe overlay.
// Outputs screen UV and clip depth for comparison with GBuffer depth.
cbuffer ConstantBuffer : register(b0) {
    float4x4 WVP;
    float4   LineColor;
    float4   DepthParams;  // x=1/viewW, y=1/viewH, z=farPlane, w=unused
}

struct VS_INPUT{
    float4 position : POSITION;
};

struct VS_OUTPUT{
    float4 hposition  : SV_POSITION;
    float  clipDepth : TEXCOORD0;
    float2 screenUV : TEXCOORD1;
};

VS_OUTPUT VS( VS_INPUT input ){
    VS_OUTPUT OUT;
    OUT.hposition = mul(WVP, input.position);
    OUT.clipDepth = OUT.hposition.z / OUT.hposition.w;
    float2 ndc = OUT.hposition.xy / OUT.hposition.w;
    OUT.screenUV = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return OUT;
}
