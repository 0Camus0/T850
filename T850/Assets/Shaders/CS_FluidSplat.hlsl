// T850 screen-space fluid: scatter the particle depth and thickness.
//
// One thread per particle. For every pixel inside the particle's depth disc, take an atomic
// minimum of the front sphere surface; for every pixel inside the thickness disc, add the
// chord length through the sphere. Positive view depth is order-preserving as uint, so the
// minimum is an InterlockedMin on the float bits. This replaces the per-pixel gather over
// every particle (the old CS_ParticleDepth), whose cost was screen coverage times particle
// count.
//
// The two discs are deliberately different sizes:
//   depth  radius = spacing * DepthRadiusScale (about 0.7) so the surface sits on the fluid
//   thickness radius = spacing                      so the chord sum measures the actual path
//
// With ScreenSpace = 0 the same scatter feeds the particle renderer instead: it keeps the
// nearest sphere per pixel as a packed (depth, index) key, and still sums the thickness.

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
StructuredBuffer<float4> Projected : register(t0);   // xy uv, z radius px, w view depth

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture2D<float> SceneDepth : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<uint> DepthBits : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<uint> ThicknessBits : register(u1);

#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

// Particle-impostor key: log-spaced front depth in the high bits, particle index in the low
// bits. The minimum is the nearest sphere; CS_FluidSphereShade recomputes the exact depth and
// normal from the index, so 15 depth bits only have to order spheres (~2 mm at 10 m).
#define KEY_INDEX_BITS 17u
#define KEY_INDEX_MASK ((1u << KEY_INDEX_BITS) - 1u)
#define KEY_DEPTH_MAX 32766.0f

uint ParticleKey(float zFront, uint index)
{
    const float t = saturate((log2(max(zFront, 0.05f)) + 4.321928f) / 14.287712f);   // 0.05..1000 m
    return (uint(t * KEY_DEPTH_MAX) << KEY_INDEX_BITS) | (index & KEY_INDEX_MASK);
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount) return;

    const float4 p = Projected[i];
    if (p.z <= 0.0f) return;   // culled by the stamp (dead particle or behind the camera)

    // ScreenSpace = 1: surface depth + thickness for SSFR. ScreenSpace = 0: nearest-sphere key
    // plus thickness for the particle renderer (full particle radius for both).
    const bool surface = ScreenSpace > 0.5f;
    const float2 centre = p.xy * float2(float(OutputWidth), float(OutputHeight));
    // Cap the per-particle disc so a particle close to the camera cannot monopolise a
    // dispatch. The depth/thickness radii stay consistent with the projected radius.
    const float rPx = min(p.z, 64.0f);
    const float depthScale = surface ? DepthRadiusScale : 1.0f;
    const float depthR = rPx * depthScale;
    const float thickR = rPx;
    const float maxR = max(depthR, thickR);
    if (maxR < 0.5f) return;

    const int2 lo = max(int2(floor(centre - maxR)), int2(0, 0));
    const int2 hi = min(int2(ceil(centre + maxR)),
                        int2(int(OutputWidth) - 1, int(OutputHeight) - 1));
    if (lo.x > hi.x || lo.y > hi.y) return;

    const float worldDepth = max(Radius * depthScale, 1.0e-4f);
    const float worldThick = max(Radius, 1.0e-4f);
    const float depthR2 = depthR * depthR;
    const float thickR2 = max(thickR * thickR, 1.0e-6f);

    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const float2 delta = float2(float(x) + 0.5f, float(y) + 0.5f) - centre;
            const float d2 = dot(delta, delta);
            const bool inDepth = d2 <= depthR2;
            const bool inThick = d2 <= thickR2;
            if (!inDepth && !inThick) continue;

            const float halfDepth = inDepth
                ? sqrt(max(1.0f - d2 / depthR2, 0.0f)) * worldDepth
                : worldDepth;
            const float zFront = p.w - halfDepth;

            const float rawDepth = SceneDepth.Load(
                int3(x, int(T850_IMAGE_Y(y, OutputHeight)), 0));
            // Projection terms, not view-projection rows (see diagnosis-3 F10).
            const float sceneZ = rawDepth > 0.0f && abs(rawDepth - ProjA) > 1e-9f
                ? ProjB / (rawDepth - ProjA) : 1.0e30f;
            if (zFront >= sceneZ) continue;   // behind opaque geometry

            const uint pix = uint(x) + uint(y) * OutputWidth;
            if (inDepth) {
                if (surface) InterlockedMin(DepthBits[pix], asuint(zFront));
                else InterlockedMin(DepthBits[pix], ParticleKey(zFront, i));
            }
            if (inThick) {
                const float chord = 2.0f * sqrt(max(1.0f - d2 / thickR2, 0.0f)) *
                                    worldThick * InvPackingFactor;
                InterlockedAdd(ThicknessBits[pix], uint(chord * 1024.0f));
            }
        }
    }
}
