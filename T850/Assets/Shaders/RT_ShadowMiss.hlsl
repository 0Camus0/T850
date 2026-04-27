/*
 * RT_ShadowMiss.hlsl — Miss shader for shadow rays
 *
 * Ray didn't hit any geometry → the pixel is fully lit.
 */

#include "RT_Common.hlsl"

struct ShadowPayload
{
    float shadowed;
    float pad[3];
};

[shader("miss")]
void MissShader(inout ShadowPayload payload)
{
    payload.shadowed = 0.0; // no hit → fully lit
}
