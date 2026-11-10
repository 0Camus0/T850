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
    float  ProjScaleX;          // Projection.m[0][0]
    float3 Color1;
    float  Color1Pad;
    float3 Color2;
    float  Color2Pad;
    float3 Color3;
    float  Color3Pad;

    float3 EmitterPosition;
    float  Anisotropic;         // 1 = read the ellipsoids from `Anisotropy`

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

// Three float4 per particle from CS_PbfAnisotropy: [0] smoothed centre + bounding radius
// (0 = unset: sphere fallback; negative = settled droplet: skip), [1] quadric M00 M01 M02 M11,
// [2] M12 M22 neighbours spray. World space.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
StructuredBuffer<float4> Anisotropy : register(t2);

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

// Linear view depth of the opaque scene at a pixel (reversed-Z projection terms).
float SceneLinearDepth(int x, int y)
{
    const float rawDepth = SceneDepth.Load(int3(x, int(T850_IMAGE_Y(y, OutputHeight)), 0));
    // Projection terms, not view-projection rows (see diagnosis-3 F10).
    return rawDepth > 0.0f && abs(rawDepth - ProjA) > 1e-9f ? ProjB / (rawDepth - ProjA) : 1.0e30f;
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

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount) return;

    const float4 p = Projected[i];
    if (p.z <= 0.0f) return;   // culled by the stamp (dead particle or behind the camera)

    const bool surface = ScreenSpace > 0.5f;
    const float2 centre = p.xy * float2(float(OutputWidth), float(OutputHeight));
    // Cap the per-particle disc so a particle close to the camera cannot monopolise a
    // dispatch. The depth/thickness radii stay consistent with the projected radius.
    const float rPx = min(p.z, 64.0f);
    const float worldThick = max(Radius, 1.0e-4f);
    const int2 screenMax = int2(int(OutputWidth) - 1, int(OutputHeight) - 1);

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
                if (zFront >= SceneLinearDepth(x, y)) continue;   // behind opaque geometry
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

    // Thickness: isotropic spheres of radius spacing at the simulated position.
    if (rPx >= 0.5f) {
        const int2 lo = max(int2(floor(centre - rPx)), int2(0, 0));
        const int2 hi = min(int2(ceil(centre + rPx)), screenMax);
        const float r2 = max(rPx * rPx, 1.0e-6f);
        for (int y = lo.y; y <= hi.y; ++y) {
            for (int x = lo.x; x <= hi.x; ++x) {
                const float2 delta = float2(float(x) + 0.5f, float(y) + 0.5f) - centre;
                const float d2 = dot(delta, delta);
                if (d2 > r2) continue;
                const float h = sqrt(max(1.0f - d2 / r2, 0.0f));
                if (p.w - h * worldThick >= SceneLinearDepth(x, y)) continue;
                InterlockedAdd(ThicknessBits[uint(x) + uint(y) * OutputWidth],
                               uint(2.0f * h * worldThick * InvPackingFactor * 1024.0f));
            }
        }
    }

    // Depth. Focal lengths in pixels per metre at view depth 1.
    const float focalY = 0.5f * float(OutputHeight) * max(ProjScaleY, 1.0e-4f);
    const float focalX = 0.5f * float(OutputWidth) * max(ProjScaleX, 1.0e-4f);
    const float2 halfSize = 0.5f * float2(float(OutputWidth), float(OutputHeight));

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
                if (zFront <= 0.05f || zFront >= SceneLinearDepth(x, y)) continue;
                const uint pix = uint(x) + uint(y) * OutputWidth;
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
            if (zFront >= SceneLinearDepth(x, y)) continue;
            InterlockedMin(DepthBits[uint(x) + uint(y) * OutputWidth], asuint(zFront));
        }
    }
}
