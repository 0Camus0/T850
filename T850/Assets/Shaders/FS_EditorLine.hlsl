// Editor line fragment shader — depth-tested wireframe overlay.
// Samples GBuffer COLOR4 (linear depth) and discards fragments behind geometry.
// A small bias pushes wireframe slightly in front of the surface to avoid Z-fighting.
cbuffer ConstantBuffer{
    float4x4 WVP;
    float4   LineColor;
    float4   DepthParams;  // x=1/viewW, y=1/viewH, z=farPlane, w=depthBias
}

Texture2D depthTex : register(t0);
SamplerState SS : register(s0);

struct VS_OUTPUT{
    float4 hposition  : SV_POSITION;
    float  linearDepth : TEXCOORD0;
};

float4 FS( VS_OUTPUT input ) : SV_TARGET {
    float2 screenUV = input.hposition.xy * DepthParams.xy;
    float sceneDepth = depthTex.Sample(SS, screenUV).r;

    // Proportional bias: scale with depth so it works at any distance/model size
    float wireDepth = input.linearDepth * (1.0 - DepthParams.w);

    // Discard if wireframe is behind scene geometry
    if (wireDepth > sceneDepth && sceneDepth > 0.0001)
        discard;

    return LineColor;
}
