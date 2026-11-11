// T850 screen-space fluid: resolve the scatter buffers into the shading inputs.
//
//   FluidDepthA      F32  view-space front surface, 0 = empty
//   FluidThickness   RGBA16F  r = metres of water, a = coverage
//   FluidNormal      RGBA16F  view-space front-layer normal, a = confidence (Iteration == 2)
//
// Keeping depth in R32F matters: RGBA16F has 10 mantissa bits, so at 7 m a quantisation step
// is ~11 mm of surface - about one pixel of tangent - and normals terrace even after good
// smoothing. Thickness and coverage do not need that precision.
//
// Surfels (Iteration == 2, the particle renderer's surface reconstruction): the front-most
// depth is the front layer's blended depth - the front hit plus the weighted mean offset of
// the ellipsoids within SurfelBand of it - and the normal is the normalised weighted sum of
// the ellipsoids' quadric normals, with the weight sum as confidence. The five per-pixel
// surfel sums (SurfelBits) are read and cleared here every frame, so the buffer can be
// created and left alone between frames. In stage 0 the normal output is zero.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleConstants : register(b0)
{
    uint  ParticleCount;
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Iteration;

    float Radius;
    float Opacity;
    float Intensity;
    float Softness;

    float DepthBias;
    float Lifetime;
    float Spread;
    float Rise;

    float TimeSeconds;
    float SizeScaleMin;
    float SizeScaleMax;
    uint  Shape;

    uint  Blend;
    uint  Source;
    float ScreenSpace;
    float TileCountX;

    float TileCountY;
    uint  FrameIndex;
    float ProjScaleY;
    float Style;
    float DepthRadiusScale;
    float InvPackingFactor;
    float ProjA;
    float ProjB;

    float3 Color0;
    float  Color0Pad;
    float3 Color1;
    float  Color1Pad;
    float3 Color2;
    float  SurfelBand;          // metres behind the front surface the surfel pass blends; 0 = off
    float3 Color3;
    float  SurfelSharpness;

    float3 EmitterPosition;
    float  EmitterPad;

    float4 ViewProjection[4];
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<uint> DepthBits : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<uint> ThicknessBits : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_r32f]]
#endif
RWTexture2D<float> DepthOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> ThicknessOut : register(u1);

// Five per output pixel: the surfel sums CS_FluidSplat (Iteration == 2) accumulates and this
// pass reads and clears: [0] sum w, [1] sum w * dz / SurfelBand, [2..4] sum w * n (view
// space, two's complement), all in units of 1/4096.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
RWStructuredBuffer<uint> SurfelBits : register(u2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> NormalOut : register(u3);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint pix = id.x + id.y * OutputWidth;
    const uint bits = DepthBits[pix];
    // The clear writes all ones (a NaN pattern); anything non-finite is an empty pixel.
    float z = bits < 0x7F800000u ? asfloat(bits) : 0.0f;

    // 5x5 binomial blur of the chord sum. Rays that pass between lattice columns see fewer
    // chords than rays through them, so the raw sum carries radial moire streaks.
    float sum = 0.0f;
    float weightSum = 0.0f;
    [unroll]
    for (int dy = -2; dy <= 2; ++dy) {
        const int y = int(id.y) + dy;
        const float wy = dy == 0 ? 6.0f : (abs(dy) == 1 ? 4.0f : 1.0f);
        [unroll]
        for (int dx = -2; dx <= 2; ++dx) {
            const int x = int(id.x) + dx;
            if (x < 0 || y < 0 || x >= int(OutputWidth) || y >= int(OutputHeight)) continue;
            const float w = wy * (dx == 0 ? 6.0f : (abs(dx) == 1 ? 4.0f : 1.0f));
            sum += float(ThicknessBits[uint(x) + uint(y) * OutputWidth]) * w;
            weightSum += w;
        }
    }
    const float thickness = sum / max(weightSum, 1.0f) * (1.0f / 1024.0f);

    // Coverage is a silhouette anti-alias term, not opacity: it reaches 1 once the path is
    // about a third of one particle's centre chord, so thin water stays fully shaded.
    const float edgeThickness = max(0.7f * Radius * InvPackingFactor, 1.0e-4f);
    const float coverage = z > 0.0f ? smoothstep(0.0f, edgeThickness, thickness) : 0.0f;

    // Surfel resolve (Iteration == 2): the blended front layer. Depth = front-most hit plus the
    // weighted mean offset; normal = normalised weighted sum, w = confidence. Read and clear.
    float4 normal = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (Iteration == 2u) {
        const uint base = pix * 5u;
        const uint weightBits = SurfelBits[base + 0u];
        if (z > 0.0f && weightBits > 4u) {
            z += SurfelBand * float(SurfelBits[base + 1u]) / float(weightBits);
            const float3 sum = float3(float(asint(SurfelBits[base + 2u])),
                                      float(asint(SurfelBits[base + 3u])),
                                      float(asint(SurfelBits[base + 4u])));
            const float len = length(sum);
            if (len > 1.0f) normal = float4(sum / len, saturate(float(weightBits) / 4096.0f));
        }
        SurfelBits[base + 0u] = 0u;
        SurfelBits[base + 1u] = 0u;
        SurfelBits[base + 2u] = 0u;
        SurfelBits[base + 3u] = 0u;
        SurfelBits[base + 4u] = 0u;
    }

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    DepthOut[imagePixel] = z;
    ThicknessOut[imagePixel] = float4(thickness, 0.0f, 0.0f, coverage);
    NormalOut[imagePixel] = normal;
}
