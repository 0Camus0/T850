// T850 particle screen-space surface depth.
//
// One thread per pixel. Reuses the projected particle table and the 8x8 tile mask that
// CS_ParticleSplat's stamp pass produced, so this pass is tile-culled exactly like the
// splat resolve. Writes, per pixel:
//
//   r = view-space depth of the front surface of the particle cloud (0 = no fluid)
//   g = accumulated fluid thickness along the view ray, in metres
//   b = accumulated kernel weight (diagnostic)
//   a = coverage in [0,1]
//
// This is section 3.1/3.3 of van der Laan et al., "Screen Space Fluid Rendering with
// Curvature Flow": the nearest sphere surface plus the amount of fluid behind it.

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
    float ScreenSpace;          // 1 = screen-space fluid: the splat resolve can stand down
    float TileCountX;

    float TileCountY;
    uint  FrameIndex;
    float ProjScaleY;
    float Style;                // 0 shaded, 1 wireframe, 2 flat

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
StructuredBuffer<uint> TileMasks : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
Texture2D<float> SceneDepth : register(t2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceDepth : register(u0);

#define TILE 8u

// OpenGL writes storage images with a bottom-left origin, while the engine's render targets
// and UV convention are top-left. Flip only in the GL twin; D3D/Vulkan are unaffected.
#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;

    const uint pixelX = id.x;
    const uint pixelY = id.y;
    const uint2 pixel = uint2(pixelX, pixelY);
    const uint2 imagePixel = uint2(pixelX, T850_IMAGE_Y(pixelY, OutputHeight));
    const uint tileX = pixelX / TILE;
    const uint tileY = pixelY / TILE;
    const uint tile = tileX + uint(TileCountX) * tileY;
    if (ScreenSpace < 0.5f) {
        // Splat mode: the screen-space chain is not the active renderer. Write an empty
        // surface so the smoothing passes copy zeroes and cost nothing.
        SurfaceDepth[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    if (TileMasks[tile] != FrameIndex) {
        SurfaceDepth[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float projA = ViewProjection[2].z;
    const float projB = ViewProjection[3].z;
    const float rawDepth = SceneDepth.Load(int3(int(pixelX), int(T850_IMAGE_Y(pixelY, OutputHeight)), 0));
    const float sceneZ = abs(rawDepth - projA) > 1e-9f ? projB / (rawDepth - projA) : 1.0e9f;

    const float2 tileMin = float2(float(tileX * TILE), float(tileY * TILE));
    const float2 tileMax = tileMin + float2(float(TILE), float(TILE));
    const float2 pixelCentre = float2(float(pixelX) + 0.5f, float(pixelY) + 0.5f);

    float frontZ = 1.0e9f;
    float thickness = 0.0f;
    float weightSum = 0.0f;
    const float radius2 = Radius * Radius;

    [loop]
    for (uint j = 0u; j < ParticleCount; ++j) {
        const float4 proj = Projected[j];
        if (proj.z <= 0.0f) continue;
        const float2 centrePx = proj.xy * float2(float(OutputWidth), float(OutputHeight));
        const float r = proj.z;
        if (centrePx.x + r < tileMin.x || centrePx.x - r > tileMax.x ||
            centrePx.y + r < tileMin.y || centrePx.y - r > tileMax.y)
            continue;

        const float2 delta = pixelCentre - centrePx;
        const float inner = r * (1.0f - saturate(Softness));
        float d;
        if (Shape == 1u) {
            d = max(abs(delta.x), abs(delta.y));
        } else if (Shape == 2u) {
            d = length(delta * float2(1.0f, 0.25f));
        } else {
            d = length(delta);
        }
        float shape;
        if (Style >= 1.5f) {
            shape = d <= r ? 1.0f : 0.0f;
        } else if (Style >= 0.5f) {
            const float ring = r * (1.0f - 0.12f);
            const float width = max(1.0f, r * 0.10f);
            shape = 1.0f - smoothstep(0.0f, width, abs(d - ring));
        } else {
            shape = 1.0f - smoothstep(inner, max(r, inner + 1e-4f), d);
        }
        if (shape <= 0.0f) continue;

        // Sphere front surface: the pixel-ray intersection, not the flat sprite centre.
        const float metresPerPixel = Radius / max(r, 1e-3f);
        const float radial = min(d * metresPerPixel, Radius);
        const float halfChord = sqrt(max(radius2 - radial * radial, 0.0f));
        const float front = proj.w - halfChord;

        float occlusion = 1.0f;
        if (DepthBias > 0.0f) {
            occlusion = saturate((sceneZ - front) / DepthBias);
            if (occlusion <= 0.0f) continue;
        }

        const float weight = shape * occlusion;
        frontZ = min(frontZ, front);
        thickness += weight * 2.0f * halfChord;
        weightSum += weight;
    }

    if (frontZ >= 1.0e8f || weightSum <= 1e-5f) {
        SurfaceDepth[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float coverage = saturate(1.0f - exp(-thickness * 2.0f));
    SurfaceDepth[imagePixel] = float4(frontZ, thickness, weightSum, coverage);
}
