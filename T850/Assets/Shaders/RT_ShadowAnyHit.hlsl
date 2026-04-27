/*
 * RT_ShadowAnyHit.hlsl — Any-hit shader (trivial closest-hit / placeholder)
 *
 * Used by shadow and AO passes.  No additional shading is needed — we only
 * care whether a hit occurred (handled by RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
 * in the ray gen shader).  This file also acts as a no-op closest-hit shader
 * for the hit group when no real shading is required.
 */

#include "RT_Common.hlsl"

struct ShadowPayload
{
    float shadowed;
    float pad[3];
};

[shader("closesthit")]
void ClosestHitShader(inout ShadowPayload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    // Shadow ray hit: mark as occluded.  The miss shader clears this flag.
    payload.shadowed = 1.0;
}
