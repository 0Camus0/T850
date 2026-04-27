/*
 * RT_AORayGen.hlsl — Ray generation shader for ambient occlusion
 *
 * For each G-buffer pixel:
 *   1. Reconstruct world position and normal.
 *   2. Shoot g_AOSampleCount cosine-weighted hemisphere rays.
 *   3. Average the visibility and write to the output UAV (single-channel float).
 */

#include "RT_Common.hlsl"

struct AOPayload
{
    float hit; // 1.0 = hit (occluded), 0.0 = miss (lit)
    float pad[3];
};

[shader("raygeneration")]
void RayGenShader()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

    float3 worldPos    = GetWorldPos(launchIndex);
    float3 worldNormal = GetWorldNormal(launchIndex);

    if (dot(worldPos, worldPos) < 0.0001)
    {
        g_Output[launchIndex] = float4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    float3 T, B;
    BuildTangentFrame(worldNormal, T, B);

    float3 origin = worldPos + worldNormal * g_RayTMin;
    float occlusion = 0.0;
    uint sampleCount = max(1u, g_AOSampleCount);

    for (uint i = 0; i < sampleCount; i++)
    {
        float2 xi = Hammersley(i, sampleCount);
        float3 localDir = CosineWeightedHemisphere(xi);
        float3 worldDir = localDir.x * T + localDir.y * B + localDir.z * worldNormal;

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = normalize(worldDir);
        ray.TMin      = g_RayTMin;
        ray.TMax      = g_AORadius;

        AOPayload payload;
        payload.hit = 1.0;

        TraceRay(
            g_TLAS,
            RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
            0xFF,
            0, 0, 0,
            ray,
            payload
        );

        occlusion += payload.hit;
    }

    float ao = 1.0 - (occlusion / float(sampleCount));
    g_Output[launchIndex] = float4(ao, ao, ao, 1.0);
}
