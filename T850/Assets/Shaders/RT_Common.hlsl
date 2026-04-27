/*
 * RT_Common.hlsl — Shared structures and bindings for all RT shaders
 *
 * Global root signature layout (set 0):
 *   t0  — RaytracingAccelerationStructure (TLAS)
 *   u0  — RWTexture2D<float4>             (output UAV)
 *   t1  — Texture2D<float4>               (G-buffer position / depth reconstructed)
 *   t2  — Texture2D<float4>               (G-buffer normal)
 *   t3  — Texture2D<float4>               (G-buffer albedo / material)
 *   b0  — CBuffer                         (camera + light params)
 */

#ifndef RT_COMMON_HLSL
#define RT_COMMON_HLSL

// ── Resource bindings ──────────────────────────────────
RaytracingAccelerationStructure g_TLAS : register(t0);
RWTexture2D<float4>             g_Output : register(u0);
Texture2D<float4>               g_GBuffer0 : register(t1);  // world-space position (rgb) + depth (a)
Texture2D<float4>               g_GBuffer1 : register(t2);  // world-space normal   (rgb) + roughness (a)
Texture2D<float4>               g_GBuffer2 : register(t3);  // albedo (rgb) + material ID (a)

cbuffer RTParams : register(b0)
{
    float4x4 g_InvViewProj;
    float4   g_CamPos;          // xyz = world-space camera position
    float4   g_LightDir;        // xyz = world-space light direction, w = intensity
    float4   g_LightColor;
    float2   g_Resolution;
    float    g_Time;
    float    g_RayTMin;         // near clip for shadow / AO rays (avoids self-intersection)
    float    g_RayTMax;         // far range
    float    g_AORadius;        // ambient occlusion hemisphere radius
    uint     g_AOSampleCount;   // number of AO rays per pixel
    float    g_Pad;
};

// ── Common helpers ─────────────────────────────────────

// Reconstruct world-space position from the G-buffer stored in GBuffer0.
float3 GetWorldPos(uint2 pixelCoord)
{
    return g_GBuffer0[pixelCoord].xyz;
}

float3 GetWorldNormal(uint2 pixelCoord)
{
    return normalize(g_GBuffer1[pixelCoord].xyz * 2.0 - 1.0);
}

// Build an orthonormal tangent frame around N.
void BuildTangentFrame(float3 N, out float3 T, out float3 B)
{
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

// Low-discrepancy sequence for AO ray directions (Hammersley-on-hemisphere).
float2 Hammersley(uint i, uint N)
{
    uint bits = i;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    float vdc = float(bits) * 2.3283064365386963e-10f; // / 0x100000000
    return float2(float(i) / float(N), vdc);
}

float3 CosineWeightedHemisphere(float2 xi)
{
    float phi = 2.0 * 3.14159265 * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

#endif // RT_COMMON_HLSL
