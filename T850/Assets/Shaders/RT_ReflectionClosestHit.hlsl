/*
 * RT_ReflectionClosestHit.hlsl — Closest hit shader for reflection rays
 *
 * Simple environment sample fallback: use the hit-point normal to look up
 * the pre-convolved environment cubemap.  For a full PBR implementation,
 * this shader would perform deferred shading at the hit point.
 */

#include "RT_Common.hlsl"

TextureCube g_EnvMap : register(t4);
SamplerState g_Sampler : register(s0);

struct ReflectionPayload
{
    float3 radiance;
    float  roughness;
};

[shader("closesthit")]
void ClosestHitShader(inout ReflectionPayload payload,
                      BuiltInTriangleIntersectionAttributes attrib)
{
    // Use the world-space ray direction at the hit point as a cheap env lookup.
    // The environment map is pre-filtered by roughness level (not yet implemented here;
    // using a fixed LOD for now).
    float mip = payload.roughness * 8.0; // assume 8-mip env map
    payload.radiance = g_EnvMap.SampleLevel(g_Sampler, WorldRayDirection(), mip).rgb;
}
