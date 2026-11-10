// T850 screen-space fluid: resolve the scatter buffers into the shading inputs.
//
//   FluidDepthA      F32  view-space front surface, 0 = empty
//   FluidThickness   RGBA16F  r = metres of water, a = coverage
//
// Keeping depth in R32F matters: RGBA16F has 10 mantissa bits, so at 7 m a quantisation step
// is ~11 mm of surface - about one pixel of tangent - and normals terrace even after good
// smoothing. Thickness and coverage do not need that precision.

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
    float  Color2Pad;
    float3 Color3;
    float  Color3Pad;

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
    const float z = bits < 0x7F800000u ? asfloat(bits) : 0.0f;

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

    const uint2 imagePixel = uint2(id.x, T850_IMAGE_Y(id.y, OutputHeight));
    DepthOut[imagePixel] = z;
    ThicknessOut[imagePixel] = float4(thickness, 0.0f, 0.0f, coverage);
}
