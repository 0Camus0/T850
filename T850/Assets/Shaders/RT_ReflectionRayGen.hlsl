/*
 * RT_ReflectionRayGen.hlsl — Ray generation shader for screen-space reflections
 *
 * For each G-buffer pixel:
 *   1. Reconstruct world position and normal.
 *   2. Compute a reflection direction from the view vector and the normal.
 *   3. Shoot a reflection ray; the closest-hit shader shades the hit point.
 *   4. Write the resulting radiance to the output UAV.
 */

#include "RT_Common.hlsl"

struct ReflectionPayload
{
    float3 radiance;
    float  roughness;  // carries surface roughness to the hit shader
};

[shader("raygeneration")]
void RayGenShader()
{
    uint2 launchIndex = DispatchRaysIndex().xy;

    float3 worldPos    = GetWorldPos(launchIndex);
    float3 worldNormal = GetWorldNormal(launchIndex);
    float  roughness   = g_GBuffer1[launchIndex].a;

    if (dot(worldPos, worldPos) < 0.0001)
    {
        g_Output[launchIndex] = float4(0, 0, 0, 0);
        return;
    }

    float3 viewDir   = normalize(worldPos - g_CamPos.xyz);
    float3 reflDir   = reflect(viewDir, worldNormal);
    float3 origin    = worldPos + worldNormal * g_RayTMin;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = reflDir;
    ray.TMin      = g_RayTMin;
    ray.TMax      = g_RayTMax;

    ReflectionPayload payload;
    payload.radiance  = float3(0, 0, 0);
    payload.roughness = roughness;

    TraceRay(
        g_TLAS,
        RAY_FLAG_NONE,
        0xFF,
        0, 0, 0,   // hit group 0, miss shader 0
        ray,
        payload
    );

    // Modulate by roughness: rough surfaces get dimmer reflections
    float fade = saturate(1.0 - roughness);
    g_Output[launchIndex] = float4(payload.radiance * fade, 1.0);
}
