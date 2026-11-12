// T850 screen-space fluid: edge-aware a-trous depth smoothing.
//
// Each pass is a 5x5 B3-spline kernel whose taps are `Spacing` pixels apart (the a-trous
// "holes"). The graph's passes use spacings 1, 2, 4, 8, ...; a pixel stops taking part once the
// spacing exceeds half its projected radius, so near water is smoothed over more pixels than far
// water and the result approximates a Gaussian of a fixed WORLD radius. The spacings must stay
// powers of two: rounded in-between spacings aliased the creases between particle caps into
// rings.
//
// The range test keeps separate fluid layers apart, but it is measured against the local PLANE,
// not against the centre depth: at a grazing angle a flat pool changes depth by several
// centimetres per pixel, and a centre-depth test rejected the far taps of a perfectly smooth
// surface. A neighbour that deviates from the plane by more than `Tolerance` metres belongs to
// another surface and is ignored.
//
// Taps are taken in symmetric pairs (o, -o) and a pair is used only when both ends are valid.
// A one-sided average of a sloped surface is biased toward the side that has samples, which bent
// every pixel next to an occluder (the near coping, the pool walls) and drew bright lines along
// the water's edge. With both ends of every pair the plane terms cancel and edges stay unbiased.
//
// Empty pixels (depth <= 0) are never written and never contribute, so the silhouette stays where
// the particles are. Spacing 0 copies (passes beyond the configured count).

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleSmoothConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    float Spacing;              // a-trous tap spacing in pixels, 0 = copy
    float WorldRadius;          // filter radius in metres
    float Strength;
    float Tolerance;            // layer threshold in metres
    float RadiusPx;             // upper bound for the projected radius
    float ProjScaleY;           // Projection.m[1][1]
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float> Source : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_r32f]]
#endif
RWTexture2D<float> Destination : register(u0);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

float LoadDepth(int2 coordinate)
{
    if (coordinate.x < 0 || coordinate.y < 0 ||
        coordinate.x >= int(OutputWidth) || coordinate.y >= int(OutputHeight))
        return 0.0f;
    return Source.Load(int3(coordinate.x, int(T850_IMAGE_Y(coordinate.y, OutputHeight)), 0));
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    const int2 base = int2(id.xy);
    const float z = LoadDepth(base);
    if (z <= 0.0f || Strength <= 0.0f || Spacing <= 0.0f) {
        Destination[imagePixel] = z;
        return;
    }

    // Pixels per metre at depth z: (H / 2) * P11 / z.
    const float focal = 0.5f * float(OutputHeight) * max(ProjScaleY, 1.0e-4f);
    const float radius = clamp(WorldRadius * focal / z, 1.0f, max(RadiusPx, 1.0f));
    if (Spacing > 0.5f * radius && Spacing > 1.0f) {
        Destination[imagePixel] = z;
        return;
    }
    const int spacing = int(Spacing + 0.5f);
    const float range = max(Tolerance, 1.0e-4f);
    const float rangeFalloff = -1.0f / (range * range);

    // Local depth gradient at this spacing (per pixel). Central difference when both sides lie
    // on this surface, one-sided next to an edge, zero when isolated.
    const float zl = LoadDepth(base - int2(spacing, 0));
    const float zr = LoadDepth(base + int2(spacing, 0));
    const float zu = LoadDepth(base - int2(0, spacing));
    const float zd = LoadDepth(base + int2(0, spacing));
    const float edge = 2.0f * range;
    const bool l = zl > 0.0f && abs(zl - z) < edge;
    const bool r = zr > 0.0f && abs(zr - z) < edge;
    const bool u = zu > 0.0f && abs(zu - z) < edge;
    const bool d = zd > 0.0f && abs(zd - z) < edge;
    const float s = float(spacing);
    const float gx = (l && r) ? (zr - zl) / (2.0f * s) : (r ? (zr - z) / s : (l ? (z - zl) / s : 0.0f));
    const float gy = (u && d) ? (zd - zu) / (2.0f * s) : (d ? (zd - z) / s : (u ? (z - zu) / s : 0.0f));

    // 5x5 B3-spline in symmetric pairs: the centre plus the 12 taps of one half, each mirrored.
    const float centreWeight = 0.375f * 0.375f;
    float sum = z * centreWeight;
    float weightSum = centreWeight;
    [unroll]
    for (int k = 0; k < 12; ++k) {
        // Half of the 5x5 neighbourhood: rows j = 1, 2 entirely, and i = 1, 2 on row 0.
        const int i = k < 2 ? k + 1 : ((k - 2) % 5) - 2;
        const int j = k < 2 ? 0 : (k < 7 ? 1 : 2);
        const float wx = i == 0 ? 0.375f : (abs(i) == 1 ? 0.25f : 0.0625f);
        const float wy = j == 0 ? 0.375f : (abs(j) == 1 ? 0.25f : 0.0625f);
        const int2 offset = int2(i, j) * spacing;
        const float a = LoadDepth(base + offset);
        const float b = LoadDepth(base - offset);
        if (a <= 0.0f || b <= 0.0f) continue;
        const float slope = gx * float(offset.x) + gy * float(offset.y);
        const float da = a - (z + slope);
        const float db = b - (z - slope);
        if (abs(da) > range || abs(db) > range) continue;   // another layer
        const float w = wx * wy * exp((da * da + db * db) * rangeFalloff);
        sum += (a + b) * w;
        weightSum += 2.0f * w;
    }

    Destination[imagePixel] = lerp(z, sum / max(weightSum, 1.0e-6f), saturate(Strength));
}
