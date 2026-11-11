// T850 screen-space fluid: splat the secondary (whitewater) particles.
//
// The solver's secondary pool (CS_PbfWhitewater emits, CS_PbfDiffuse moves and classifies)
// is drawn into four full-resolution channels per pixel, which CS_ParticleShade reads and
// clears in the same frame:
//
//   [0] spray    droplets in the air, in front of the water surface (coverage x 4096)
//   [1] foam     floating at the water surface                       (coverage x 4096)
//   [2] bubbles  under the surface, dimmed by the water above them   (coverage x 4096)
//   [3] sparkle  brightest single bubble on the pixel                (max, x 65535)
//
// A particle is a soft disc of its class radius, at least about a pixel wide; a sub-pixel
// droplet contributes its fractional coverage instead of vanishing. Opaque geometry in front
// of it hides it (full-resolution scene depth); the water surface decides its class's
// visibility (the screen-space depth, possibly at a lower resolution).
//
// One thread per pool slot; free slots (life <= 0) return at once.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer WhitewaterSplatConstants : register(b0)
{
    uint  OutputWidth;
    uint  OutputHeight;
    uint  DiffuseCapacity;
    uint  Enabled;

    float ProjScaleX;           // Projection.m[0][0]
    float ProjScaleY;           // Projection.m[1][1]
    float ProjA;                // Projection.m[2][2]
    float ProjB;                // Projection.m[3][2]

    float SprayRadius;          // metres
    float FoamRadius;
    float BubbleRadius;
    float WaterAbsorption;      // per metre, dims bubbles seen through the water

    float4 ViewProjection[4];   // row-vector view-projection
};

// Two float4 per particle: [0] position, life; [1] velocity, class (0 spray, 1 foam, 2 bubble).
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Diffuse : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float> SceneDepth : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
Texture2D<float> FluidDepth : register(t2);    // smoothed view depth of the water, 0 = none

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<uint> WhitewaterBits : register(u0);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

float SceneLinearDepth(int x, int y)
{
    const float raw = SceneDepth.Load(int3(x, int(T850_IMAGE_Y(y, OutputHeight)), 0));
    return raw > 0.0f && abs(raw - ProjA) > 1e-9f ? ProjB / (raw - ProjA) : 1.0e30f;
}

float WaterDepth(int x, int y, uint2 fluidSize)
{
    const int2 t = min(int2(float2(x, y) * float2(fluidSize) / float2(float(OutputWidth), float(OutputHeight))),
                       int2(fluidSize) - 1);
    return FluidDepth.Load(int3(t.x, int(T850_IMAGE_Y(t.y, fluidSize.y)), 0));
}

float3 ToViewPoint(float3 p)
{
    const float4 clip = p.x * ViewProjection[0] + p.y * ViewProjection[1] +
                        p.z * ViewProjection[2] + ViewProjection[3];
    return float3(clip.x / max(ProjScaleX, 1.0e-6f), clip.y / max(ProjScaleY, 1.0e-6f), clip.w);
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint k = id.x;
    if (Enabled == 0u || k >= DiffuseCapacity) return;
    const float4 state = Diffuse[k * 2u + 0u];
    if (state.w <= 0.0f) return;
    const float kind = Diffuse[k * 2u + 1u].w;

    const float3 c = ToViewPoint(state.xyz);
    if (c.z < 0.1f) return;
    const float focalX = 0.5f * float(OutputWidth) * max(ProjScaleX, 1.0e-4f);
    const float focalY = 0.5f * float(OutputHeight) * max(ProjScaleY, 1.0e-4f);
    const float2 centre = float2(c.x * focalX / c.z, -c.y * focalY / c.z) +
                          0.5f * float2(float(OutputWidth), float(OutputHeight));
    const float radius = kind < 0.5f ? SprayRadius : (kind < 1.5f ? FoamRadius : BubbleRadius);
    const float radiusPx = radius * focalY / c.z;
    const float drawPx = clamp(radiusPx, 0.75f, 6.0f);
    // Sub-pixel particles keep their area: a disc drawn larger than it is carries less weight.
    const float coverage = saturate((radiusPx * radiusPx) / (drawPx * drawPx)) * saturate(state.w / 0.4f);
    if (coverage <= 1.0e-3f) return;

    uint2 fluidSize;
    FluidDepth.GetDimensions(fluidSize.x, fluidSize.y);
    const int2 lo = max(int2(floor(centre - drawPx)), int2(0, 0));
    const int2 hi = min(int2(ceil(centre + drawPx)), int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    const float r2 = drawPx * drawPx;
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const float2 delta = float2(float(x) + 0.5f, float(y) + 0.5f) - centre;
            const float d2 = dot(delta, delta);
            if (d2 > r2) continue;
            if (c.z >= SceneLinearDepth(x, y) - 0.01f) continue;     // behind opaque geometry
            const float profile = (1.0f - d2 / r2) * (1.0f - d2 / r2);
            const float weight = profile * coverage;
            const float water = WaterDepth(x, y, fluidSize);
            const uint base = (uint(x) + uint(y) * OutputWidth) * 4u;
            if (kind < 0.5f) {
                if (water <= 0.0f || c.z < water + 0.02f)
                    InterlockedAdd(WhitewaterBits[base + 0u], uint(weight * 4096.0f));
            } else if (kind < 1.5f) {
                if (water <= 0.0f || abs(c.z - water) < 0.4f)
                    InterlockedAdd(WhitewaterBits[base + 1u], uint(weight * 4096.0f));
            } else if (water > 0.0f && c.z > water) {
                const float seen = weight * exp(-(c.z - water) * WaterAbsorption);
                InterlockedAdd(WhitewaterBits[base + 2u], uint(seen * 4096.0f));
                InterlockedMax(WhitewaterBits[base + 3u], uint(saturate(seen) * 65535.0f));
            }
        }
    }
}
