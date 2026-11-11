// T850 screen-space fluid: scatter the particle depth and thickness.
//
// One thread per particle. For every pixel inside the particle's depth footprint, take an atomic
// minimum of the front surface; for every pixel inside the thickness disc, add the chord length
// through the sphere. Positive view depth is order-preserving as uint, so the minimum is an
// InterlockedMin on the float bits. This replaces the per-pixel gather over every particle (the
// old CS_ParticleDepth), whose cost was screen coverage times particle count.
//
// Depth (ScreenSpace = 1). With an anisotropy buffer (CS_PbfAnisotropy, Yu and Turk 2013) each
// particle is an ellipsoid around its smoothed centre and the pixel ray is intersected with it
// exactly. Free-surface particles are discs lying in the surface and sheets become films, so the
// front surface is no longer a field of domes. Without the buffer (or for a particle it marks
// as unset) the particle is a sphere of radius spacing * DepthRadiusScale.
//
// Thickness is always the isotropic chord sum of spacing-sized spheres at the simulated
// positions, divided by the packing factor, so it measures the metres of water on the ray.
//
// With ScreenSpace = 0 the same scatter feeds the particle renderer instead: it keeps the
// nearest sphere per pixel as a packed (depth, index) key, and still sums the thickness.
//
// Whitewater (AuxEnabled): the solver's per-particle foam, aeration and agitation
// (CS_PbfWhitewater) and the anisotropy's spray flag go into four channels per pixel:
// foam as a maximum (it is a surface layer), the others as chord-weighted sums the shade
// divides by the thickness.
//
// Surfels (Iteration == 2, screen-space only, active while SurfelBand > 0): instead of writing
// the front depth, each ellipsoid's front hit is blended into five per-pixel sums (SurfelBits,
// CS_FluidResolve reads and clears them) when it lies within SurfelBand metres of the pixel's
// front surface: the weight is the ellipsoid's centre weight (1 - s^2, the ray's distance from
// the centre line in ellipsoid units) raised to SurfelSharpness, times a frontness term that
// peaks at the front surface. The normal blends the ellipsoid's own quadric normal with a
// single per-particle disc axis (the shortest quadric axis from a short power iteration on the
// view-space quadric, oriented toward the camera) so that particles lying in the surface are
// flat discs; only isolated spray (the anisotropy's spray flag) keeps its dome.
// CS_FluidResolve (stage 2) turns the sums into the blended depth and the normal map, so the
// particle renderer keeps the whole screen-space shade (CS_ParticleShade, Mode 2) with a
// smooth surface. The depth and thickness scatter, the whitewater and the sphere fallback
// all stand down in this pass: the particle renderer draws the surface through the fluid
// targets, so nothing of the splat path must be written twice.
//
// Resolution: the output may be smaller than the scene (the screen-space renderer splats at
// half resolution); projected radii come from the full-resolution stamp and are scaled. The
// screen-space mode does not test the opaque depth at all: CS_ParticleShade re-tests every
// pixel against the full-resolution depth, and water hidden under the coping continues the
// visible surface, which is what the smoothing should see. (Testing the farthest of the 2x2
// full-resolution texels per pixel cost as much as the 4x fewer pixels saved.) The particle
// renderer (ScreenSpace = 0) keeps its per-pixel test.

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
    float  ProjScaleX;          // Projection.m[0][0]
    float3 Color1;
    float  AuxEnabled;          // 1 = accumulate the whitewater channels
    float3 Color2;
    float  SurfelBand;          // metres behind the front surface to blend; 0 = surfel pass off
    float3 Color3;
    float  SurfelSharpness;     // exponent on the ellipsoid's centre weight

    float3 EmitterPosition;
    float  Anisotropic;         // 1 = read the ellipsoids from `Anisotropy`

    float4 ViewProjection[4];

    uint   ThicknessShift;   // WO-P1: 0 = depth grid, 1 = half grid
    float ThicknessPad0; // WO-P1: pad to 68 DWORDs
    float ThicknessPad1;
    float ThicknessPad2;
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

// Three float4 per particle from CS_PbfAnisotropy: [0] smoothed centre + bounding radius
// (0 = unset: sphere fallback; negative = settled droplet: skip), [1] quadric M00 M01 M02 M11,
// [2] M12 M22 neighbours spray. World space.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<float4> Anisotropy : register(t2);

// Per particle from CS_PbfWhitewater: foam, aeration, agitation, trapped air.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
StructuredBuffer<float4> Whitewater : register(t3);

// Four per output pixel: foam (max, 1/65535), aeration, agitation, spray (sum of value x chord,
// 1/1024 m). Cleared by CS_FluidClear.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(7, 0)]]
#endif
RWStructuredBuffer<uint> AuxBits : register(u2);

// Five per output pixel, written by the surfel pass (Iteration == 2) and read and cleared by
// CS_FluidResolve: [0] sum w, [1] sum w * dz / SurfelBand, [2..4] sum w * n (view space,
// two's complement), all in units of 1/4096.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(8, 0)]]
#endif
RWStructuredBuffer<uint> SurfelBits : register(u3);
#define SURFEL_SCALE 4096.0f

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

// Linear view depth of the opaque scene at a full-resolution texel (reversed-Z projection
// terms, not view-projection rows; see diagnosis-3 F10).
float SceneTexelDepth(int x, int y, uint sceneHeight)
{
    const float rawDepth = SceneDepth.Load(int3(x, int(T850_IMAGE_Y(y, sceneHeight)), 0));
    return rawDepth > 0.0f && abs(rawDepth - ProjA) > 1e-9f ? ProjB / (rawDepth - ProjA) : 1.0e30f;
}

// Opaque depth for an OUTPUT pixel: the texel itself at full resolution, otherwise the
// farthest texel of the (up to 3x3) block the output pixel covers.
float SceneLinearDepth(int x, int y, uint2 sceneSize)
{
    if (sceneSize.x == OutputWidth && sceneSize.y == OutputHeight)
        return SceneTexelDepth(x, y, sceneSize.y);
    const float2 scale = float2(sceneSize) / float2(float(OutputWidth), float(OutputHeight));
    const int2 lo = int2(floor(float2(x, y) * scale));
    const int2 hi = min(int2(ceil(float2(x + 1, y + 1) * scale)) - 1, int2(sceneSize) - 1);
    float farthest = 0.0f;
    [loop]
    for (int sy = lo.y; sy <= min(hi.y, lo.y + 2); ++sy)
        [loop]
        for (int sx = lo.x; sx <= min(hi.x, lo.x + 2); ++sx)
            farthest = max(farthest, SceneTexelDepth(sx, sy, sceneSize.y));
    return farthest;
}

// World point/direction to view space through the row-vector view-projection: clip.x =
// x_view * P00, clip.y = y_view * P11 and clip.w = z_view, so the view coordinates come back
// without a separate view matrix in the constants.
float3 ViewFromClip(float4 clip)
{
    return float3(clip.x / max(ProjScaleX, 1.0e-6f), clip.y / max(ProjScaleY, 1.0e-6f), clip.w);
}

float3 ToViewPoint(float3 p)
{
    return ViewFromClip(p.x * ViewProjection[0] + p.y * ViewProjection[1] +
                        p.z * ViewProjection[2] + ViewProjection[3]);
}

// Inverse of a symmetric 3x3 matrix (adjugate over determinant).
float3x3 InverseSymmetric(float3x3 m)
{
    const float a = m[0][0], b = m[0][1], c = m[0][2];
    const float d = m[1][1], e = m[1][2], f = m[2][2];
    const float c00 = d * f - e * e;
    const float c01 = c * e - b * f;
    const float c02 = b * e - c * d;
    const float c11 = a * f - c * c;
    const float c12 = b * c - a * e;
    const float c22 = a * d - b * b;
    const float inv = 1.0f / (a * c00 + b * c01 + c * c02);
    return float3x3(c00, c01, c02, c01, c11, c12, c02, c12, c22) * inv;
}

// Surfel pass: blend this ellipsoid's front hit into the pixel when it lies in the front
// layer, weighted toward the ellipsoid's centre line and toward the front.
// `centreWeight` = 1 - s^2, s = the ray's distance from the centre in ellipsoid units;
// `surfelNormal` is the particle's disc axis, and `flatness` blends the per-pixel quadric
// normal toward it (1 = flat disc, 0 = the particle's own dome for spray).
void AccumulateSurfel(uint pix, float3 r, float zFront, float3 c, float3x3 viewM, float3 surfelNormal, float flatness, float centreWeight)
{
    const uint bits = DepthBits[pix];
    if (bits >= 0x7F800000u) return;            // empty (cleared) pixel
    const float dz = max(zFront - asfloat(bits), 0.0f);
    if (dz > SurfelBand) return;                 // behind the front layer
    const float3 hit = r * zFront;               // view space (r.z == 1)
    float3 q = mul(viewM, hit - c);              // quadric gradient: the ellipsoid's own normal
    const float qLen = length(q);
    q = qLen > 1.0e-6f ? q / qLen : surfelNormal;
    if (dot(q, hit) > 0.0f) q = -q;              // face the camera
    const float3 n = normalize(lerp(q, surfelNormal, flatness));
    const float front = 1.0f - dz / SurfelBand;
    const float w = pow(saturate(centreWeight), max(SurfelSharpness, 0.25f)) * front * front;
    if (w < 1.0e-3f) return;
    const uint base = pix * 5u;
    InterlockedAdd(SurfelBits[base + 0u], uint(w * SURFEL_SCALE + 0.5f));
    InterlockedAdd(SurfelBits[base + 1u], uint(w * (dz / SurfelBand) * SURFEL_SCALE + 0.5f));
    InterlockedAdd(SurfelBits[base + 2u], asuint(int(round(w * n.x * SURFEL_SCALE))));
    InterlockedAdd(SurfelBits[base + 3u], asuint(int(round(w * n.y * SURFEL_SCALE))));
    InterlockedAdd(SurfelBits[base + 4u], asuint(int(round(w * n.z * SURFEL_SCALE))));
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount) return;

    const float4 p = Projected[i];
    if (p.z <= 0.0f) return;   // culled by the stamp (dead particle or behind the camera)

    uint2 sceneSize;
    SceneDepth.GetDimensions(sceneSize.x, sceneSize.y);
    const bool surface = ScreenSpace > 0.5f;
    const float2 centre = p.xy * float2(float(OutputWidth), float(OutputHeight));
    // Cap the per-particle disc so a particle close to the camera cannot monopolise a
    // dispatch. The stamp measured the radius in full-resolution pixels.
    const float rPx = min(p.z * float(OutputHeight) / float(max(sceneSize.y, 1u)), 64.0f);
    const float worldThick = max(Radius, 1.0e-4f);
    const int2 screenMax = int2(int(OutputWidth) - 1, int(OutputHeight) - 1);
    // WO-P1: the thickness/whitewater sums live on a grid half the depth grid when
    // ThicknessShift == 1 (both axes); the 64-px cap above was measured on the depth
    // grid, so it divides with the centre and radius below. The sphere renderer
    // (!surface) always splats its thickness at full resolution.
    const uint shift = ScreenSpace > 0.5f ? min(ThicknessShift, 1u) : 0u;   // spheres: always full
    const float downsample = 1.0f / float(1u << shift);

    if (!surface) {
        // ScreenSpace = 0: nearest-sphere key plus thickness for the particle renderer (full
        // particle radius for both).
        if (rPx < 0.5f) return;
        const int2 lo = max(int2(floor(centre - rPx)), int2(0, 0));
        const int2 hi = min(int2(ceil(centre + rPx)), screenMax);
        if (lo.x > hi.x || lo.y > hi.y) return;
        const float r2 = max(rPx * rPx, 1.0e-6f);
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) {
                const float2 delta = float2(float(x) + 0.5f, float(y) + 0.5f) - centre;
                const float d2 = dot(delta, delta);
                if (d2 > r2) continue;
                const float h = sqrt(max(1.0f - d2 / r2, 0.0f));
                const float zFront = p.w - h * worldThick;
                if (zFront >= SceneLinearDepth(x, y, sceneSize)) continue;   // behind opaque geometry
                const uint pix = uint(x) + uint(y) * OutputWidth;
                InterlockedMin(DepthBits[pix], ParticleKey(zFront, i));
                InterlockedAdd(ThicknessBits[pix], uint(2.0f * h * worldThick * InvPackingFactor * 1024.0f));
            }
        }
        return;
    }

    // A settled lone droplet (negative bound from CS_PbfAnisotropy) draws nothing at all.
    const float4 shape0 = Anisotropic > 0.5f ? Anisotropy[i * 3u + 0u] : float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (shape0.w < 0.0f) return;

    // Surfel pass: screen-space only, and only while the surfel band is on. The depth
    // (stage 0/1) writes nothing in it: CS_FluidResolve (stage 2) rebuilds the surface from
    // the surfel sums.
    const bool surfelPass = Iteration == 2u;
    if (surfelPass && SurfelBand <= 0.0f) return;

    // Whitewater carried by this particle (zero when the solver has none).
    const float4 whitewater = AuxEnabled > 0.5f ? Whitewater[i] : float4(0.0f, 0.0f, 0.0f, 0.0f);
    const float spray = AuxEnabled > 0.5f && Anisotropic > 0.5f ? Anisotropy[i * 3u + 2u].w : 0.0f;
    const bool hasFoam = whitewater.x > 0.02f;
    const bool hasAir = whitewater.y > 0.02f;
    const bool hasAgitation = whitewater.z > 0.02f;
    const bool hasSpray = spray > 0.02f;
    const bool anyWhitewater = hasFoam || hasAir || hasAgitation || hasSpray;

    // Thickness: isotropic spheres of radius spacing at the simulated position. The surfel
    // pass skips it: the scatter must not add the chord sum (and the whitewater channels)
    // twice, and the surfel surface is what the particle renderer shades.
    if (!surfelPass && rPx >= 0.5f) {
        const float2 centreT = centre * downsample;
        // A particle that just passed rPx >= 0.5 on the depth grid would be 0.25 of a
        // coarse texel wide after the 2x downscale: too small to cover a texel centre,
        // so distant water would lose its chords. The splat keeps a 0.75 coarse-texel
        // minimum instead (the chord value still comes from the coarse disc).
        const float rPxT = shift > 0u ? max(rPx * downsample, 0.75f) : rPx;
        const uint widthT = (OutputWidth + (1u << shift) - 1u) >> shift;
        const uint heightT = (OutputHeight + (1u << shift) - 1u) >> shift;
        const int2 maxT = int2(int(widthT) - 1, int(heightT) - 1);
        const int2 lo = max(int2(floor(centreT - rPxT)), int2(0, 0));
        const int2 hi = min(int2(ceil(centreT + rPxT)), maxT);
        const float r2 = max(rPxT * rPxT, 1.0e-6f);
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) {
                const float2 delta = float2(float(x) + 0.5f, float(y) + 0.5f) - centreT;
                const float d2 = dot(delta, delta);
                if (d2 > r2) continue;
                const float h = sqrt(max(1.0f - d2 / r2, 0.0f));
                const uint pix = uint(x) + uint(y) * widthT;
                const float chord = 2.0f * h * worldThick * InvPackingFactor;
                InterlockedAdd(ThicknessBits[pix], uint(chord * 1024.0f));
                if (anyWhitewater) {
                    if (hasFoam) InterlockedMax(AuxBits[pix * 4u + 0u], uint(saturate(whitewater.x * h) * 65535.0f));
                    if (hasAir) InterlockedAdd(AuxBits[pix * 4u + 1u], uint(min(whitewater.y, 2.0f) * chord * 1024.0f));
                    if (hasAgitation) InterlockedAdd(AuxBits[pix * 4u + 2u], uint(whitewater.z * chord * 1024.0f));
                    if (hasSpray) InterlockedAdd(AuxBits[pix * 4u + 3u], uint(spray * chord * 1024.0f));
                }
            }
        }
    }

    // Depth. Focal lengths in pixels per metre at view depth 1.
    const float focalY = 0.5f * float(OutputHeight) * max(ProjScaleY, 1.0e-4f);
    const float focalX = 0.5f * float(OutputWidth) * max(ProjScaleX, 1.0e-4f);
    const float2 halfSize = 0.5f * float2(float(OutputWidth), float(OutputHeight));

    // The sphere fallback has no quadric normal, so the surfel pass takes the front hit with a
    // zero normal: the depth normal (InterlockedMin) is what those pixels keep, and CS_ParticleShade
    // rebuilds its macro normal from the depth when the surfel normal is empty.
    if (surfelPass && shape0.w <= 0.0f) return;

    if (shape0.w > 0.0f) {
        // Ellipsoid: (x - c)^T M (x - c) <= 1 in view space, with M_view = R M_world R^T and R
        // the world-to-view rotation whose columns are the view images of the world axes.
        const float4 shape1 = Anisotropy[i * 3u + 1u];
        const float4 shape2 = Anisotropy[i * 3u + 2u];
        const float3x3 worldM = float3x3(shape1.x, shape1.y, shape1.z,
                                         shape1.y, shape1.w, shape2.x,
                                         shape1.z, shape2.x, shape2.y);
        const float3 ax = ViewFromClip(ViewProjection[0]);
        const float3 ay = ViewFromClip(ViewProjection[1]);
        const float3 az = ViewFromClip(ViewProjection[2]);
        const float3x3 rotation = float3x3(ax.x, ay.x, az.x,
                                           ax.y, ay.y, az.y,
                                           ax.z, ay.z, az.z);
        const float3x3 viewM = mul(mul(rotation, worldM), transpose(rotation));
        const float3 c = ToViewPoint(shape0.xyz);
        const float bound = shape0.w;
        if (c.z - bound <= 0.05f) return;   // crosses the near plane: not worth splatting
        const float2 cPx = float2(c.x * focalX / c.z, -c.y * focalY / c.z) + halfSize;
        const float bPx = min(bound * focalY / (c.z - bound), 64.0f);
        if (bPx < 0.5f) return;
        // Tight screen rectangle from the dual quadric (Sigg et al. 2006). A plane x = s z
        // through the eye is tangent to the ellipsoid when pi^T Q* pi = 0, with
        // Q* = [S - c c^T, -c; -c^T, -1] and S = M^-1; the same for y. A flattened surface disc
        // seen at a grazing angle covers a fraction of its longest axis' bounding square.
        // The eye is outside the bounding sphere (near test above), so q22 < 0.
        const float3x3 shapeS = InverseSymmetric(viewM);
        const float q00 = shapeS[0][0] - c.x * c.x;
        const float q11 = shapeS[1][1] - c.y * c.y;
        const float q22 = shapeS[2][2] - c.z * c.z;
        const float q02 = shapeS[0][2] - c.x * c.z;
        const float q12 = shapeS[1][2] - c.y * c.z;
        const float rootX = sqrt(max(q02 * q02 - q00 * q22, 0.0f));
        const float rootY = sqrt(max(q12 * q12 - q11 * q22, 0.0f));
        const float2 slopeX = float2(q02 + rootX, q02 - rootX) / q22;
        const float2 slopeY = float2(q12 + rootY, q12 - rootY) / q22;
        const float2 tightLo = float2(min(slopeX.x, slopeX.y) * focalX,
                                      -max(slopeY.x, slopeY.y) * focalY) + halfSize;
        const float2 tightHi = float2(max(slopeX.x, slopeX.y) * focalX,
                                      -min(slopeY.x, slopeY.y) * focalY) + halfSize;
        const int2 lo = max(max(int2(floor(cPx - bPx)), int2(floor(tightLo))), int2(0, 0));
        const int2 hi = min(min(int2(ceil(cPx + bPx)), int2(ceil(tightHi))), screenMax);
        if (lo.x > hi.x || lo.y > hi.y) return;
        // Surfel early out: tap the front depth at the centre pixel and the four corners of
        // [lo, hi]. If even the FARTHEST front surface of these taps is more than SurfelBand
        // behind this ellipsoid's nearest point (c.z - bound), then no pixel of the rectangle
        // has a front surface within the band of any of this ellipsoid's hits, so skip the whole
        // loop. An empty tap (bits >= 0x7F800000u) disables the test: part of the rectangle saw
        // nothing before this particle, so the front-surface bound cannot be trusted and the
        // particle must be scattered (its front layer there is exactly what the surfel pass
        // exists to build).
        if (surfelPass) {
            const int2 cp = max(min(int2(floor(cPx)), screenMax), int2(0, 0));
            const uint2 taps[5] = { cp,
                                    uint2(lo.x, lo.y),
                                    uint2(hi.x, lo.y),
                                    uint2(lo.x, hi.y),
                                    uint2(hi.x, hi.y) };
            bool trusted = true;
            float farthest = 0.0f;
            [unroll]
            for (int k = 0; k < 5; ++k) {
                const uint bits = DepthBits[taps[k].x + taps[k].y * OutputWidth];
                if (bits >= 0x7F800000u) { trusted = false; break; }
                farthest = max(farthest, asfloat(bits));
            }
            if (trusted && c.z - bound > farthest + SurfelBand) return;
        }
        // Disc axis for the surfel normal: six power-iteration steps of the view-space quadric
        // from world up. It converges to the largest eigenvalue's eigenvector, the shortest
        // quadric axis, which is the disc normal of a surface particle; a level surface
        // converges at once. Computed after the early out, so skipped particles pay nothing.
        float3 surfelNormal = float3(0.0f, 0.0f, -1.0f);
        float surfelFlatness = 0.0f;
        if (surfelPass) {
            float3 v = ViewFromClip(ViewProjection[1]);
            [unroll]
            for (int it = 0; it < 6; ++it) v = mul(viewM, v / max(length(v), 1.0e-20f));
            const float len = length(v);
            if (len > 1.0e-20f) surfelNormal = v / len;
            if (dot(surfelNormal, c) > 0.0f) surfelNormal = -surfelNormal;   // face the camera
            // CS_PbfAnisotropy's spray flag: an isolated droplet is a sphere (its own dome
            // normal); every other particle is a disc in the surface (the axis normal).
            surfelFlatness = 1.0f - saturate(shape2.w);
        }
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) {
                // Ray through the pixel centre, z = 1. delta = point of the ray at the centre's
                // depth minus the centre, so the quadratic is solved about the particle and no
                // large terms cancel: (delta + u r)^T M (delta + u r) = 1, z_front = c.z + u.
                const float3 r = float3((float(x) + 0.5f - halfSize.x) / focalX,
                                        -(float(y) + 0.5f - halfSize.y) / focalY, 1.0f);
                const float3 delta = c.z * r - c;
                const float3 mr = mul(viewM, r);
                const float a = dot(r, mr);
                const float b = dot(delta, mr);
                const float cc = dot(delta, mul(viewM, delta)) - 1.0f;
                const float disc = b * b - a * cc;
                if (disc < 0.0f || a <= 1.0e-12f) continue;
                const float zFront = c.z + (-b - sqrt(disc)) / a;
                if (zFront <= 0.05f) continue;
                const uint pix = uint(x) + uint(y) * OutputWidth;
                if (surfelPass) {
                    // disc = b^2 - a * cc, and s^2 = cc / a: disc / a is exactly 1 - s^2,
                    // the ray's squared distance from the centre line in ellipsoid units.
                    AccumulateSurfel(pix, r, zFront, c, viewM, surfelNormal, surfelFlatness, disc / a);
                    continue;
                }
                // Depths only decrease, so a plain read that is already nearer skips the
                // atomic: most interior particles lie behind the surface layer.
                InterlockedMin(DepthBits[pix], asuint(zFront));
            }
        }
        return;
    }

    // Sphere fallback.
    const float depthR = rPx * DepthRadiusScale;
    if (depthR < 0.5f) return;
    const float worldDepth = max(Radius * DepthRadiusScale, 1.0e-4f);
    const int2 lo = max(int2(floor(centre - depthR)), int2(0, 0));
    const int2 hi = min(int2(ceil(centre + depthR)), screenMax);
    const float depthR2 = depthR * depthR;
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const float2 delta = float2(float(x) + 0.5f, float(y) + 0.5f) - centre;
            const float d2 = dot(delta, delta);
            if (d2 > depthR2) continue;
            const float zFront = p.w - sqrt(max(1.0f - d2 / depthR2, 0.0f)) * worldDepth;
            InterlockedMin(DepthBits[uint(x) + uint(y) * OutputWidth], asuint(zFront));
        }
    }
}
