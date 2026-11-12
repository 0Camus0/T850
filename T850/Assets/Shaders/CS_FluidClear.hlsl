// T850 screen-space fluid: clear the scatter accumulators.
//
// The render graph creates compute buffers once and cannot re-upload them per frame, so the
// scatter targets are cleared by a dispatch. One thread per screen pixel.

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
    float Style;          // particle look style (0..3); the fluid kernels ignore it
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

    uint   ThicknessShift;   // WO-P1: 0 = depth grid, 1 = half grid
    float ThicknessPad0; // WO-P1: pad to 68 DWORDs
    float ThicknessPad1;
    float ThicknessPad2;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
RWStructuredBuffer<uint> DepthBits : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<uint> ThicknessBits : register(u1);

// Four per pixel: foam (max), aeration, agitation and spray (chord-weighted sums), written by
// CS_FluidSplat from the solver's per-particle whitewater and read by CS_ParticleShade.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<uint> AuxBits : register(u2);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;
    const uint pixel = id.x + id.y * OutputWidth;
    // All ones sorts above every positive float's bits and every packed particle key, so one
    // sentinel serves both the screen-space depth and the particle-impostor key.
    DepthBits[pixel] = 0xFFFFFFFFu;
    // WO-P1: the thickness and aux accumulators may live on a grid half the depth grid;
    // clear exactly the entries the splat can write (the first Wt*Ht, aux 4x that).
    const uint shift = min(ThicknessShift, 1u);
    const uint widthT = (OutputWidth + (1u << shift) - 1u) >> shift;
    ThicknessBits[pixel] = 0u;
    const uint tIndex = (id.x >> shift) + (id.y >> shift) * widthT;
    const uint a = tIndex * 4u;
    AuxBits[a + 0u] = 0u;
    AuxBits[a + 1u] = 0u;
    AuxBits[a + 2u] = 0u;
    AuxBits[a + 3u] = 0u;
    // Threads that share a coarse cell (a 2x2 block when shift is 1) clear the same entries;
    // the write is idempotent, so the duplicates are harmless.
}
