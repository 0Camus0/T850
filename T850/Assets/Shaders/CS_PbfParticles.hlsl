// T850 Position Based Fluids: render the particle cloud as a screen-space fluid body.
//
// WHY THIS EXISTS
//
// The pool renderer (VS_Mesh/FS_Mesh FLUID_SURFACE) displaces a flat XZ plane by ONE height
// per (x,z) column. A heightfield can only describe a surface seen from directly above, so a
// sphere of water in mid-air, droplets, or water curling over the coping (all of which have
// many surfaces stacked in one column) force the mesh to stretch between them. That is the
// "curtains" / "stretched triangles" artefact, and it was never a solver problem.
//
// Particles are the right primitive and the solver is already particles, so this kernel
// splats them straight into a screen-space target, following CS_TorchParticles:
//
//   pass 0 (MODE_STAMP):   one thread per particle, project it and OR its tile bit.
//   pass 1 (MODE_RESOLVE): one thread per pixel, walk only the tiles near it.
//
// The host runs it as two dispatches and swaps `Iteration` between them. Doing it in one
// kernel would need a grid-wide barrier, which compute shaders do not have.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ParticleConstants : register(b0)
{
    uint  ParticleCount;
    uint  OutputWidth;
    uint  OutputHeight;
    uint  Iteration;          // MODE_STAMP or MODE_RESOLVE
    float ParticleRadius;     // world-space splat radius
    float DepthBias;
    float Opacity;
    float Pad0;
    float3 CameraPosition;
    float Pad1;
    float3 FluidColor;
    float TileCountX;
    float TileCountY;
    float Pad2;
    float Pad3;
};

// 8x8 pixel tiles, matching [numthreads(8,8,1)] in the stamp pass.
#define TILE 8u

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Positions : register(t0);  // xyz + alive/pad

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<float4> Velocities : register(t1);

// Screen-space projection of every particle, filled by the stamp pass.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<float4> Projected : register(u0);   // xy = uv, z = radius px, w = view depth

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
RWStructuredBuffer<uint> TileMasks : register(u1);     // one mask per tile

// Camera view-projection, supplied as a 4x4 by the host.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<float> ViewProjection : register(t2);  // 16 floats, row-major

#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16float]]
#endif
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
RWTexture2D<float4> OutputTexture : register(u2);

#define MODE_STAMP   0u
#define MODE_RESOLVE 1u

float4 Transform(float3 p)
{
    return float4(p.x * ViewProjection[0] + p.y * ViewProjection[4] + p.z * ViewProjection[8] + ViewProjection[12],
                  p.x * ViewProjection[1] + p.y * ViewProjection[5] + p.z * ViewProjection[9] + ViewProjection[13],
                  p.x * ViewProjection[2] + p.y * ViewProjection[6] + p.z * ViewProjection[10] + ViewProjection[14],
                  p.x * ViewProjection[3] + p.y * ViewProjection[7] + p.z * ViewProjection[11] + ViewProjection[15]);
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;

    if (Iteration == MODE_STAMP) {
        if (i >= ParticleCount) return;
        const float3 p = Positions[i].xyz;
        const float4 clip = Transform(p);
        if (clip.w <= 1e-4f) {
            Projected[i] = float4(0.0f, 0.0f, -1.0f, 0.0f);   // negative radius = cull marker
            return;
        }
        const float2 ndc = clip.xy / clip.w;
        const float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
        const float radiusPx = ParticleRadius * (float)OutputHeight / max(clip.w, 1e-3f);
        Projected[i] = float4(uv, radiusPx, clip.w);

        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f) return;
        const uint tx = min((uint)(uv.x * (float)(OutputWidth / TILE)), (uint)max(TileCountX - 1.0f, 0.0f));
        const uint ty = min((uint)(uv.y * (float)(OutputHeight / TILE)), (uint)max(TileCountY - 1.0f, 0.0f));
        const uint tile = tx + (uint)TileCountX * ty;
        InterlockedOr(TileMasks[tile], 1u);
        return;
    }

    // MODE_RESOLVE: one thread per pixel.
    const uint2 pixel = uint2(id.x % OutputWidth, id.x / OutputWidth);
    if (id.x >= OutputWidth * OutputHeight) return;
    const uint tileX = pixel.x / TILE;
    const uint tileY = pixel.y / TILE;
    const uint tile = tileX + (uint)TileCountX * tileY;
    if (TileMasks[tile] == 0u) {
        OutputTexture[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Gather particles that project into this tile. This is O(tile particles) per pixel,
    // which is what keeps it affordable: only tiles that contain water are scanned.
    const float2 pixelCentre = float2(pixel) + 0.5f;
    float3 accumColor = float3(0.0f, 0.0f, 0.0f);
    float accumAlpha = 0.0f;
    float nearestDepth = 1e30f;

    [loop]
    for (uint j = 0u; j < ParticleCount; ++j) {
        const float4 proj = Projected[j];
        if (proj.z <= 0.0f) continue;
        const float2 centrePx = proj.xy * float2((float)OutputWidth, (float)OutputHeight);
        const float2 tileMin = float2((float)(tileX * TILE), (float)(tileY * TILE));
        const float2 tileMax = tileMin + float2((float)TILE, (float)TILE);
        const float r = proj.z;
        if (centrePx.x + r < tileMin.x || centrePx.x - r > tileMax.x ||
            centrePx.y + r < tileMin.y || centrePx.y - r > tileMax.y)
            continue;

        const float2 delta = pixelCentre - centrePx;
        const float dist = length(delta);
        if (dist > r) continue;

        const float edge = saturate(1.0f - dist / max(r, 1e-3f));
        const float shape = smoothstep(0.0f, 0.35f, edge);

        const float3 v = Velocities[j].xyz;
        const float speed = length(v);
        const float3 tint = lerp(FluidColor, float3(1.0f, 1.0f, 1.0f), saturate(speed / 8.0f) * 0.35f);

        const float weight = shape * Opacity * saturate(1.0f - accumAlpha);
        accumColor += tint * weight;
        accumAlpha += weight;
        nearestDepth = min(nearestDepth, proj.w);
    }

    if (accumAlpha <= 0.001f) {
        OutputTexture[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    OutputTexture[pixel] = float4(accumColor / max(accumAlpha, 1e-4f), saturate(accumAlpha));
}
