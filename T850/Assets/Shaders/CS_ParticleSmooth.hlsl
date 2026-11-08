// T850 particle screen-space depth smoothing.
//
// One thread per pixel. Each dispatch reads one RGBA16F surface-depth target and writes the
// other (the graph ping-pongs four times). Section 3.2 of van der Laan et al. formulates
// this as mean-curvature flow, dz/dt = H. A stable explicit step of the same flow is the
// Laplacian update used here:
//
//   z' = z + strength * (average(neighbours) - z)
//
// The depth discontinuity guard is the paper's boundary condition: neighbours whose view
// depth differs by more than `tolerance` (silhouettes) do not contribute, so separate
// patches of fluid never blend into one another.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleSmoothConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Stage;
    uint  Pad0;
    float Strength;
    float Tolerance;
    float RadiusPx;
    float Pad1;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture2D<float4> Source : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> Destination : register(u0);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

float4 LoadDepth(int2 coordinate)
{
    const int2 clamped = clamp(coordinate, int2(0, 0),
                               int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    return Source.Load(int3(clamped.x, int(T850_IMAGE_Y(clamped.y, OutputHeight)), 0));
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint2 pixel = uint2(id.x, id.y);
    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    const float4 centre = LoadDepth(int2(id.xy));
    if (Strength <= 0.0f || centre.r <= 0.0f) {
        Destination[imagePixel] = centre;
        return;
    }

    const int2 base = int2(id.xy);
    const float z = centre.r;
    // A 3x3 Laplacian is far too small for a particle splat: one splat is ~10-20 pixels
    // across, so the depth field keeps the lattice and the reconstructed normals look like
    // noise. Use a configurable radius with a radial weight; four passes at radius 8 cover
    // the splat scale. The `r == 0` test rejects empty pixels and the tolerance test rejects
    // silhouette jumps, so separate patches still never blend.
    const int radius = clamp(int(RadiusPx + 0.5f), 1, 16);
    float sum = 0.0f;
    float weightSum = 0.0f;
    [loop]
    for (int dy = -radius; dy <= radius; ++dy) {
        [loop]
        for (int dx = -radius; dx <= radius; ++dx) {
            const float4 neighbour = LoadDepth(base + int2(dx, dy));
            if (neighbour.r <= 0.0f || abs(neighbour.r - z) > Tolerance) continue;
            const float distance = sqrt(float(dx * dx + dy * dy));
            const float weight = max(0.0f, 1.0f - distance / float(radius + 1));
            sum += neighbour.r * weight;
            weightSum += weight;
        }
    }
    float smoothed = z;
    if (weightSum > 1e-5f) {
        smoothed = lerp(z, sum / weightSum, saturate(Strength));
    }
    Destination[imagePixel] = float4(smoothed, centre.g, centre.b, centre.a);
}
