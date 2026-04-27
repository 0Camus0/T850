/*
 * RT_AOMiss.hlsl — Miss shader for AO rays
 * Ray reached its max range without hitting anything → not occluded.
 */

#include "RT_Common.hlsl"

struct AOPayload
{
    float hit;
    float pad[3];
};

[shader("miss")]
void MissShader(inout AOPayload payload)
{
    payload.hit = 0.0;
}
