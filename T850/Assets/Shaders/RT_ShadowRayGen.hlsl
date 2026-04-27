/*
 * RT_ShadowRayGen.hlsl — Ray generation shader for hard shadows
 *
 * For each G-buffer pixel:
 *   1. Reconstruct world-space position and normal from G-buffer.
 *   2. Shoot a shadow ray toward the directional light.
 *   3. Write visibility (0=shadow, 1=lit) to the output UAV.
 */

#include "RT_Common.hlsl"

struct ShadowPayload
{
    float shadowed;  // 1.0 = occluded, 0.0 = lit
    float pad[3];
};

[shader("raygeneration")]
void RayGenShader()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    float3 worldPos    = GetWorldPos(launchIndex);
    float3 worldNormal = GetWorldNormal(launchIndex);

    // Skip skybox / empty pixels (worldPos == 0)
    if (dot(worldPos, worldPos) < 0.0001)
    {
        g_Output[launchIndex] = float4(1.0, 1.0, 1.0, 1.0); // fully lit
        return;
    }

    // Offset origin along normal to avoid self-intersection
    float3 origin    = worldPos + worldNormal * g_RayTMin;
    float3 direction = normalize(-g_LightDir.xyz); // toward the light

    // Dot product with normal: back-face check
    if (dot(worldNormal, direction) <= 0.0)
    {
        g_Output[launchIndex] = float4(0.0, 0.0, 0.0, 1.0); // in shadow
        return;
    }

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = g_RayTMin;
    ray.TMax      = g_RayTMax;

    ShadowPayload payload;
    payload.shadowed = 1.0; // assume hit (shadow) unless Miss fires

    TraceRay(
        g_TLAS,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF,         // instance mask
        0,            // ray contribution to hit group index
        0,            // geometry contribution to hit group multiplier
        0,            // miss shader index
        ray,
        payload
    );

    float visibility = 1.0 - payload.shadowed;
    g_Output[launchIndex] = float4(visibility, visibility, visibility, 1.0);
}
