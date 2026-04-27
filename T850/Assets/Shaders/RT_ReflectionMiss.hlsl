/*
 * RT_ReflectionMiss.hlsl — Miss shader for reflection rays
 *
 * Ray missed all geometry → sample the environment cubemap as the background.
 */

#include "RT_Common.hlsl"

TextureCube g_EnvMap : register(t4);
SamplerState g_Sampler : register(s0);

struct ReflectionPayload
{
    float3 radiance;
    float  roughness;
};

[shader("miss")]
void MissShader(inout ReflectionPayload payload)
{
    float mip = payload.roughness * 8.0;
    payload.radiance = g_EnvMap.SampleLevel(g_Sampler, WorldRayDirection(), mip).rgb;
}
