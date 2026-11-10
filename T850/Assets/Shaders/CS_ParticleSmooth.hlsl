// T850 screen-space fluid: edge-aware a-trous depth smoothing.
//
// Each pass is a 5x5 B3-spline kernel whose taps are `Spacing` pixels apart (the a-trous
// "holes"). The graph's passes use spacings 1, 2, 4, 8, ...; a pixel stops taking part once the
// spacing exceeds half its projected radius, so near water is smoothed over more pixels than far
// water and the result approximates a Gaussian of a fixed WORLD radius. The spacings must stay
// powers of two: rounded in-between spacings aliased the creases between particle caps into
// rings.
//
// The range test keeps separate fluid layers apart: a neighbour more than `Tolerance` metres
// nearer or farther belongs to another surface and is ignored. A plain Gaussian averaged the
// falling sphere into the pool behind it, and separable (horizontal then vertical) passes turned
// every layer edge the first axis cut into a vertical streak on the second.
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
    const float rangeFalloff = -2.0f / (range * range);

    float sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int j = -2; j <= 2; ++j) {
        const float wy = j == 0 ? 0.375f : (abs(j) == 1 ? 0.25f : 0.0625f);
        [unroll]
        for (int i = -2; i <= 2; ++i) {
            const float wx = i == 0 ? 0.375f : (abs(i) == 1 ? 0.25f : 0.0625f);
            const float neighbour = LoadDepth(base + int2(i, j) * spacing);
            const float dz = neighbour - z;
            if (neighbour <= 0.0f || abs(dz) > range) continue;   // empty or another layer
            const float w = wx * wy * exp(dz * dz * rangeFalloff);
            sum += neighbour * w;
            weightSum += w;
        }
    }

    Destination[imagePixel] = lerp(z, sum / max(weightSum, 1.0e-6f), saturate(Strength));
}
