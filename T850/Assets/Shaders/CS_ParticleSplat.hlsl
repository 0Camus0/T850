// T850 generic particle splat.
//
// Draws one ParticleSystem into a screen-space RGBA16F target. The system says where the
// particles are (procedural emitter, or an external position buffer) and how they look
// (shape, size, colour ramp, opacity, blend); this kernel does the projection and splatting
// and nothing else. No simulation happens here.
//
// Two passes, selected by `Iteration`:
//   MODE_STAMP    one thread per particle, project it and mark its 8x8 pixel tile.
//   MODE_RESOLVE  one thread per pixel, gather only particles whose splat reaches it.
//
// A single kernel cannot do both: there is no grid-wide barrier in a compute dispatch.
//
// This replaces the per-scene splat hack (which hardcoded a fluid's radius and colour in a
// scene-specific kernel) and generalises CS_TorchParticles' procedural model so that fire,
// smoke, sparks and fluid all go through one renderer.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleConstants : register(b0)
{
    uint  ParticleCount;
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Iteration;            // MODE_STAMP | MODE_RESOLVE

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
    uint  Shape;                // 0 sphere, 1 sprite, 2 streak

    uint  Blend;                // 0 additive, 1 alpha
    uint  Source;               // 0 procedural, 1 buffer
    float ScreenSpace;          // 1 = screen-space fluid: the splat resolve can stand down
    float TileCountX;

    float TileCountY;
    uint  FrameIndex;           // stamped into TileMasks so stale tiles from earlier frames are ignored
    float ProjScaleY;           // Projection.m[1][1] = cot(fov/2); converts metres to pixels
    float Style;                // 0 shaded, 1 wireframe, 2 flat
    float DepthRadiusScale;     // depth sphere radius / particle spacing
    float InvPackingFactor;     // 1 / lattice packing factor
    float ProjA;                // Projection.m[2][2]
    float ProjB;                // Projection.m[3][2]

    // Each float3 is padded to a 16-byte slot so the HLSL layout is unambiguous.
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

    // Row-major view-projection. In the cbuffer rather than a buffer so the graph does not
    // need a per-frame buffer upload API (the engine only supports upload at creation).
    float4 ViewProjection[4];

    uint   ThicknessShift;   // WO-P1: 0 = depth grid, 1 = half grid
    float ThicknessPad0; // WO-P1: pad to 68 DWORDs
    float ThicknessPad1;
    float ThicknessPad2;
};

// Scene depth for soft-particle occlusion (R4).
//
// Loaded with texelFetch (no sampler): the render graph binds GBuffer:DEPTH as a sampled
// resource. The engine's projection is reversed-Z, so the stored value d maps back to
// linear view depth as  z = b / (d - a)  with a = VP[2][2], b = VP[3][2]. Deriving a and b
// from the view-projection avoids two extra constants (and D3D12 root DWORDs).
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
Texture2D<float> SceneDepth : register(t2);

// OpenGL writes storage images with a bottom-left origin, while the engine's render targets
// and UV convention are top-left. Flip only in the GL twin; D3D/Vulkan are unaffected.
// (The GL graphics path compensates with a flipped viewport; compute has no viewport.)
#if defined(T850_GL)
  #define T850_IMAGE_Y(y, h) ((uint)(h) - 1u - (uint)(y))
#else
  #define T850_IMAGE_Y(y, h) (uint)(y)
#endif

#define TILE 8u

#define SHAPE_SPHERE 0u
#define SHAPE_SPRITE 1u
#define SHAPE_STREAK 2u

#define MODE_STAMP   0u
#define MODE_RESOLVE 1u

// External positions for a Buffer-source system.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
// float4, matching the 16-byte stride the host creates this buffer with. Declaring
// `float` here and indexing `i*4` only works on backends that ignore the view stride;
// D3D11 builds the SRV from the buffer's 16-byte stride, so the read was 4x out of range.
StructuredBuffer<float4> Positions : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float4> Projected : register(u0);   // xy uv, z radius px, w depth

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<uint> TileMasks : register(u1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> OutputTexture : register(u3);

// Camera::VP is a ROW-VECTOR matrix (p' = p * M). XMatPerspectiveLH sets m[2][3] = 1, so
// clip.w must come out as view-space z, which only happens with p * M. The engine's vertex
// shaders use mul(WVP, position), which is the same product under HLSL's row-major packing.
//
// ViewProjection[k] holds row k. Therefore clip_i = p.x*row0[i] + p.y*row1[i] + ... , i.e.
// the combination below -- NOT dot(row_i, p), which computes the transpose and projected the
// water to the wrong place at about a third of its size.
float4 Transform(float3 p)
{
    return p.x * ViewProjection[0] + p.y * ViewProjection[1] +
           p.z * ViewProjection[2] + ViewProjection[3];
}

// Deterministic hash, so a procedural system is stable frame to frame.
float Hash(uint n)
{
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / 2147483647.0;
}

// Life of a procedural particle in 0..1.
float ProceduralAge(uint i)
{
    if (Lifetime <= 1e-4f) return 0.0f;
    const float phase = Hash(i * 9781u + 17u);
    return frac(TimeSeconds / Lifetime + phase);
}

// Colour ramp: four stops evenly spaced over age.
float3 RampColor(float t)
{
    t = saturate(t);
    const float s = t * 3.0f;
    const uint k = min((uint)floor(s), 2u);
    const float f = s - float(k);
    const float3 a = k == 0u ? Color0 : (k == 1u ? Color1 : Color2);
    const float3 b = k == 0u ? Color1 : (k == 1u ? Color2 : Color3);
    return lerp(a, b, f);
}

void ProceduralParticle(uint i, out float3 position, out float3 velocity, out float age)
{
    age = ProceduralAge(i);
    const float angle = Hash(i * 6271u + 43u) * 6.28318530718f;
    const float radialSeed = Hash(i * 3253u + 91u);
    const float radialDistance = Spread * radialSeed * (0.2f + age);
    const float wobble = sin(TimeSeconds * (1.0f + Hash(i * 1877u + 7u) * 2.0f) + angle) * Spread * 0.25f;

    const float3 local = float3(cos(angle) * radialDistance + wobble,
                                age * Rise,
                                sin(angle) * radialDistance - wobble * 0.5f);
    position = EmitterPosition + local;
    const float riseSpeed = Rise / max(Lifetime, 1e-4f);
    velocity = float3(-sin(angle) * radialDistance * 2.0f,
                      riseSpeed,
                      cos(angle) * radialDistance * 2.0f);
}

// Both modes run as a normal 2D dispatch (one group per 64x1 threads). The particle passes
// used to be forced into a 1D grid over W*H threads, which exceeds the 65535-groups-per-
// dimension limit above 4.2 MP (3440x1440, 3840x2160) and silently drew nothing.
//
// `flat` recovers a unique 1D index from the 2D grid: the thread grid is
// ceil(W/64)*64 wide and H tall, not W wide. Resolve uses id.xy directly.
[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint threadGridWidth = ((OutputWidth + 63u) / 64u) * 64u;
    const uint flat = id.x + id.y * threadGridWidth;
    const uint i = flat;

    if (Iteration == MODE_STAMP) {
        if (i >= ParticleCount) return;

        float3 position;
        float3 velocity;
        float age = 1.0f;
        if (Source == 0u) {
            ProceduralParticle(i, position, velocity, age);
        } else {
            // w = alive flag. Retired particles are parked at y = -1000; drawing them would
            // put phantom splats in the frame (and, before the frame-stamped tile mask,
            // keep their old tiles alive forever).
            if (Positions[i].w < 0.5f) {
                Projected[i] = float4(0.0f, 0.0f, -1.0f, 0.0f);
                return;
            }
            position = Positions[i].xyz;
            velocity = float3(0.0f, 0.0f, 0.0f);
            age = 1.0f;
        }

        const float4 clip = Transform(position);
        if (clip.w <= 1e-4f) {
            Projected[i] = float4(0.0f, 0.0f, -1.0f, 0.0f);   // negative radius = culled
            return;
        }

        const float2 ndc = clip.xy / clip.w;
        const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        const float sizeScale = lerp(SizeScaleMin, SizeScaleMax, age);
        // Projected radius in pixels: R * P11 * (H/2) / w, with P11 = cot(fov/2). The old
        // form dropped the P11*0.5 factor, so the water was drawn ~15% too small at fov 0.8
        // and wrong by more at wider fields of view.
        const float radiusPx = Radius * sizeScale * ProjScaleY * 0.5f *
                               float(OutputHeight) / max(clip.w, 1e-3f);
        Projected[i] = float4(uv, radiusPx, clip.w);

        // Stamp the frame index into every tile the splat radius covers. InterlockedMax
        // makes this idempotent within a frame; the resolve ignores any tile whose value is
        // not the current FrameIndex, so tiles never need a per-frame clear and stale tiles
        // from earlier frames stop costing work (they used to accumulate forever).
        const float2 centrePx = uv * float2(float(OutputWidth), float(OutputHeight));
        const float reach = radiusPx;
        const int tx0 = (int)floor((centrePx.x - reach) / float(TILE));
        const int tx1 = (int)floor((centrePx.x + reach) / float(TILE));
        const int ty0 = (int)floor((centrePx.y - reach) / float(TILE));
        const int ty1 = (int)floor((centrePx.y + reach) / float(TILE));
        const int maxTx = (int)max(TileCountX - 1.0f, 0.0f);
        const int maxTy = (int)max(TileCountY - 1.0f, 0.0f);
        [loop]
        for (int ty = max(ty0, 0); ty <= min(ty1, maxTy); ++ty)
        [loop]
        for (int tx = max(tx0, 0); tx <= min(tx1, maxTx); ++tx)
            InterlockedMax(TileMasks[tx + int(TileCountX) * ty], FrameIndex);
        return;
    }

    // MODE_RESOLVE: one thread per pixel. The dispatch is 2D, so use id.xy and reject the
    // padding threads of the last group in X.
    //
    // The splat resolve stays active in screen-space mode. The screen-space shade pass
    // writes over it where it has surface coverage; keeping the splat underneath means a
    // backend whose screen-space chain produces no depth still shows the fluid instead of
    // an empty pool, and the soft splat edge is a better fallback than a hard cut-out.
    if (id.x >= OutputWidth || id.y >= OutputHeight) return;
    // Capture the dispatch coordinates once, as scalars, before anything else. FXC
    // (D3D11) lost `id`/`pixel` after the tile-mask indexing: `float2(pixel)` evaluated
    // to (0,0), so the splat distance was measured from the top-left corner and every
    // shape was zero. DXC (D3D12) kept it correctly. Scalars captured first are stable
    // on both compilers.
    const uint pixelX = id.x;
    const uint pixelY = id.y;
    const uint2 pixel = uint2(pixelX, pixelY);
    const uint2 imagePixel = uint2(pixelX, T850_IMAGE_Y(pixelY, OutputHeight));
    const float2 pixelCentre = float2(float(pixelX) + 0.5f, float(pixelY) + 0.5f);
    const uint tileX = pixelX / TILE;
    const uint tileY = pixelY / TILE;
    const uint tile = tileX + uint(TileCountX) * tileY;
    if (TileMasks[tile] != FrameIndex) {
        OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Linear view depth of the opaque scene at this pixel. GBuffer:DEPTH is F32 and the
    // projection is reversed-Z: d = a + b/z, so z = b/(d - a). Background (d == 0) gives z
    // == far; clamp below to keep the fade finite.
    // Projection terms, not the view-projection rows: VP[2].z includes the view matrix's
    // z row, which made scene depth 2.7% too near and cut fluid off before walls.
    const float rawDepth = SceneDepth.Load(int3(int(pixelX), int(T850_IMAGE_Y(pixelY, OutputHeight)), 0));
    const float sceneZ = rawDepth > 0.0f && abs(rawDepth - ProjA) > 1e-9f
        ? ProjB / (rawDepth - ProjA) : 1.0e9f;

    const float2 tileMin = float2(float(tileX * TILE), float(tileY * TILE));
    const float2 tileMax = tileMin + float2(float(TILE), float(TILE));
    // Normalised weighted average, NOT additive accumulation.
    //
    // A fluid is heavily overdrawn: 26k particles of ~16 px radius over a sphere that covers
    // ~20k pixels is ~43 hits per pixel. With per-hit alpha (0.35) the coverage saturates
    // after three hits, so the whole body rendered as a flat opaque white blob. Tracking the
    // total weight and dividing gives a proper volumetric average: dense interiors read
    // opaque-ish, thin edges stay soft, and the silhouette is the particle cloud's own.
    float3 accumColor = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;

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
        // smoothstep takes edge0 < edge1. The old code passed (r, r*(1-Softness)), i.e. the
        // edges reversed, so every pixel OUTSIDE the disc got weight 1 and the centre got 0.
        // Inside a marked tile that filled whole 8x8 blocks -- the "staircase" was never a
        // tile-marking bug. Compute a distance, then fade 1 -> 0 from the inner radius out.
        const float inner = r * (1.0f - saturate(Softness));
        float d;
        if (Shape == SHAPE_SPRITE) {
            d = max(abs(delta.x), abs(delta.y));
        } else if (Shape == SHAPE_STREAK) {
            // Elongate along Y in screen space; a full velocity-oriented streak needs the
            // per-particle velocity in the resolve pass, which is a later refinement.
            d = length(delta * float2(1.0f, 0.25f));
        } else {
            d = length(delta);
        }
        float shape;
        if (Style >= 1.5f) {
            // Flat: hard filled disc (debug / stylised particles).
            shape = d <= r ? 1.0f : 0.0f;
        } else if (Style >= 0.5f) {
            // Wireframe: a thin ring at the splat edge. This exposes particle placement,
            // coverage and the lattice without changing the simulation.
            const float ring = r * (1.0f - 0.12f);
            const float width = max(1.0f, r * 0.10f);
            shape = 1.0f - smoothstep(0.0f, width, abs(d - ring));
        } else {
            shape = 1.0f - smoothstep(inner, max(r, inner + 1e-4f), d);
        }
        if (shape <= 0.0f) continue;

        // Soft-particle occlusion: compare the splat's FRONT surface with the scene's
        // linear depth. DepthBias is the fade band in metres (0 disables the test).
        // Without this the particles drew over the coping, the deck and the house.
        float occlusion = 1.0f;
        if (DepthBias > 0.0f) {
            const float frontZ = proj.w - Radius;
            occlusion = saturate((sceneZ - frontZ) / DepthBias);
            if (occlusion <= 0.0f) continue;
        }

        // The colour ramp maps particle AGE. A buffer-source particle has no life cycle --
        // it is just a position the owner supplied -- so age=1 evaluated the ramp's LAST
        // stop, which is the near-white end. Every fluid particle came out white.
        // Buffer particles sample the ramp around its base instead, with a small
        // per-particle offset so a dense body is not one flat colour.
        const float variation = Hash(j * 2654435761u) * 0.35f;
        const float age = Source == 0u ? ProceduralAge(j) : variation;
        const float3 tint = RampColor(age);

        if (Blend == 0u) {
            // Additive: light adds up. Fire/sparks rely on this.
            accumColor += tint * shape * occlusion * Intensity;
        } else {
            // Alpha blend: weighted average of the contributing particles.
            const float weight = shape * occlusion;
            accumColor += tint * weight;
            weightSum += weight;
        }
    }

    float accumAlpha = 0.0f;
    if (Blend != 0u && weightSum > 1e-5f) {
        accumColor /= weightSum;
        // Coverage from how many particles actually landed here: one hit is faint, a filled
        // interior approaches Opacity. The 1 - exp form saturates smoothly instead of
        // clipping hard at a few hits.
        accumAlpha = Opacity * (1.0f - exp(-weightSum * 0.35f));
    }

    if (Blend == 0u) {
        if (dot(accumColor, accumColor) <= 1e-6f) {
            OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            return;
        }
        OutputTexture[imagePixel] = float4(accumColor, 1.0f);
        return;
    }
    if (accumAlpha <= 0.001f) {
        OutputTexture[imagePixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // accumColor is ALREADY the weighted average (divided by weightSum above). Dividing by
    // accumAlpha again multiplied it by ~3 at these densities and clipped the whole body to
    // white. Emit the average colour and the coverage separately.
    OutputTexture[imagePixel] = float4(accumColor, accumAlpha);
}
