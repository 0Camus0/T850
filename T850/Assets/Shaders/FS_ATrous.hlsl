/*
 * FS_ATrous.hlsl — Spatially-Guided A-Trous Wavelet Denoiser
 *
 * A bilateral filter that weights samples by depth and normal similarity.
 * Run this pass (with increasing step sizes 1,2,4,8,…) after RT dispatch
 * to reduce variance in shadow / AO signals without blurring geometry edges.
 *
 * Inputs (bound via the render graph):
 *   t0 — noisy RT output (shadow / AO / reflection, RGBA16F or R8)
 *   t1 — G-buffer normal
 *   t2 — G-buffer depth (R32F)
 *
 * Output:
 *   Filtered signal written to the current render target.
 *
 * Constant buffer b0:
 *   cbuffer ATrousParams { int g_StepSize; float g_SigmaL; float g_SigmaN; float g_SigmaD; };
 */

Texture2D<float4> g_NoisyInput : register(t0);
Texture2D<float4> g_GBufNormal : register(t1);
Texture2D<float>  g_GBufDepth  : register(t2);

SamplerState g_PointSampler : register(s0);

cbuffer ATrousParams : register(b0)
{
    int   g_StepSize;  // current wavelet step (1, 2, 4, 8, 16 …)
    float g_SigmaL;    // luminance weight (higher = more blur)
    float g_SigmaN;    // normal weight
    float g_SigmaD;    // depth weight
    float2 g_InvRes;   // 1 / resolution
    float2 pad;
};

struct VS_OUT
{
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
};

// ── A-Trous 3×3 kernel weights (h-values) ──
static const float kKernel[3][3] =
{
    { 1.0/16.0, 2.0/16.0, 1.0/16.0 },
    { 2.0/16.0, 4.0/16.0, 2.0/16.0 },
    { 1.0/16.0, 2.0/16.0, 1.0/16.0 },
};

float4 FS(VS_OUT input) : SV_Target
{
    float2 uv      = input.uv;
    float4 cColor  = g_NoisyInput.Sample(g_PointSampler, uv);
    float3 cNormal = g_GBufNormal.Sample(g_PointSampler, uv).xyz * 2.0 - 1.0;
    float  cDepth  = g_GBufDepth.Sample(g_PointSampler, uv).r;

    float4 sum     = float4(0, 0, 0, 0);
    float  wSum    = 0.0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            float2 offset = float2(dx, dy) * (float)g_StepSize * g_InvRes;
            float2 sampleUV = uv + offset;

            float4 sColor  = g_NoisyInput.Sample(g_PointSampler, sampleUV);
            float3 sNormal = g_GBufNormal.Sample(g_PointSampler, sampleUV).xyz * 2.0 - 1.0;
            float  sDepth  = g_GBufDepth.Sample(g_PointSampler, sampleUV).r;

            // Luminance weight
            float3 dColor = cColor.rgb - sColor.rgb;
            float  wL = exp(-dot(dColor, dColor) / (g_SigmaL * g_SigmaL + 1e-6));

            // Normal weight
            float  nDot = saturate(dot(cNormal, sNormal));
            float  wN   = pow(nDot, g_SigmaN);

            // Depth weight
            float  dDepth = abs(cDepth - sDepth);
            float  wD = exp(-(dDepth * dDepth) / (g_SigmaD * g_SigmaD + 1e-6));

            float w = kKernel[dy + 1][dx + 1] * wL * wN * wD;
            sum  += sColor * w;
            wSum += w;
        }
    }

    return (wSum > 0.0) ? (sum / wSum) : cColor;
}
