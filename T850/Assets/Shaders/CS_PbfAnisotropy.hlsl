// T850 PBF: anisotropic particle shapes for screen-space fluid rendering.
//
// Yu and Turk, "Reconstructing Surfaces of Particle-Based Fluids Using Anisotropic Kernels"
// (ACM TOG 32(1), 2013); the PBF paper renders its fluid the same way. A particle drawn as a
// sphere makes every free surface a field of domes and every thin sheet a row of balls. Here each
// particle becomes an ellipsoid shaped by its neighbourhood:
//
//   1. Smoothed centre. The centre moves toward the weighted mean of its neighbours by
//      `Smoothing` (Laplacian smoothing), which removes the particle-scale jitter of the surface.
//   2. Shape. The weighted covariance of the neighbour offsets is decomposed (Jacobi). An
//      interior particle has an isotropic neighbourhood and stays a sphere; a free-surface
//      particle has no neighbours above it, so its covariance is flat along the surface normal
//      and the particle becomes a disc lying in the surface; a sheet of water becomes a film.
//   3. Isolated particles (few neighbours) cannot define a surface and become small spheres,
//      which the shade draws as spray. Once such a particle comes to rest it is water the
//      particle resolution cannot represent (a couple of litres stranded on a step would be a
//      film millimetres thick, not a 13 cm ball), so it shrinks away as it settles.
//
// Output, three float4 per particle:
//   [0] smoothed centre xyz (world), bounding radius in metres (0 = dead; negative = settled
//       droplet, draw nothing)
//   [1] M00, M01, M02, M11      ellipsoid quadric: (x - c)^T M (x - c) <= 1, world space
//   [2] M12, M22, neighbours, spray (0 = part of the surface, 1 = isolated droplet)
//
// One thread per particle. The neighbour grid must have been built from the same positions.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer PbfAnisotropyConstants : register(b0)
{
    uint  ParticleCount;
    uint  GridX;
    uint  GridY;
    uint  GridZ;

    float GridOriginX;
    float GridOriginY;
    float GridOriginZ;
    float CellSize;          // neighbour radius R; equals the grid cell (the solver's h)

    float Spacing;           // particle spacing, metres
    float Smoothing;         // 0..1: how far the centre moves toward the neighbour mean
    float MaxRatio;          // longest axis / shortest axis limit (Yu and Turk's k_r)
    float RadiusScale;       // interior particle radius, in spacings

    float SparseScale;       // isolated particle radius, in spacings
    float FullNeighbours;    // at or above: full anisotropic shape
    float SparseNeighbours;  // at or below: isolated droplet
    float MaxStretch;        // longest axis cap, relative to the interior radius
};

#define PBFCellCapacity 64u

// WO-P2: buried classification. A particle sits at least two spacings inside the fluid when its
// neighbourhood is full and its weighted neighbour mean points nowhere: every ellipsoid around
// it then covers it from every direction, so the splat may skip it in the depth and surfel
// passes. The splat reads Anisotropy[i*3+2].z as -neighbours when buried (it reads .x, .y and
// .w of components 0..2, never .z), so no output changes. Thickness keeps every particle.
#define BURIED_NEIGHBOURS 24.0f
#define BURIED_OFFSET 0.1f       // length(weighted mean offset) <= this fraction of R

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float4> Positions : register(t0);   // xyz, w = alive

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<uint> GridBuffer : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
RWStructuredBuffer<float4> Anisotropy : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<float4> Velocities : register(t2);

// One classical Jacobi rotation (Numerical Recipes) that zeroes a_pq of a symmetric 3x3
// matrix. r is the third index; vp and vq are the eigenvector columns being accumulated.
void JacobiRotate(inout float app, inout float aqq, inout float apq,
                  inout float arp, inout float arq, inout float3 vp, inout float3 vq)
{
    if (abs(apq) < 1.0e-14f) return;
    const float theta = 0.5f * (aqq - app) / apq;
    const float t = (theta >= 0.0f ? 1.0f : -1.0f) / (abs(theta) + sqrt(theta * theta + 1.0f));
    const float c = rsqrt(t * t + 1.0f);
    const float s = t * c;
    app -= t * apq;
    aqq += t * apq;
    apq = 0.0f;
    const float rp = arp;
    const float rq = arq;
    arp = c * rp - s * rq;
    arq = s * rp + c * rq;
    const float3 p = vp;
    const float3 q = vq;
    vp = c * p - s * q;
    vq = s * p + c * q;
}

[numthreads(64, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    if (i >= ParticleCount)
        return;
    const float4 self = Positions[i];
    if (self.w < 0.5f) {
        Anisotropy[i * 3u + 0u] = float4(0.0f, -1000.0f, 0.0f, 0.0f);
        Anisotropy[i * 3u + 1u] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        Anisotropy[i * 3u + 2u] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    const float3 xi = self.xyz;
    const uint cells = GridX * GridY * GridZ;
    const float radius = max(CellSize, 1.0e-4f);
    const float inv = 1.0f / radius;
    const int cx = clamp((int)floor((xi.x - GridOriginX) * inv), 0, (int)GridX - 1);
    const int cy = clamp((int)floor((xi.y - GridOriginY) * inv), 0, (int)GridY - 1);
    const int cz = clamp((int)floor((xi.z - GridOriginZ) * inv), 0, (int)GridZ - 1);

    // Weighted first and second moments of the neighbour offsets d = x_j - x_i, with Yu and
    // Turk's weight w = 1 - (|d| / R)^3. Offsets keep the sums small and well conditioned.
    float weightSum = 0.0f;
    float3 mean = float3(0.0f, 0.0f, 0.0f);
    float sxx = 0.0f, sxy = 0.0f, sxz = 0.0f, syy = 0.0f, syz = 0.0f, szz = 0.0f;
    float neighbours = 0.0f;
    const float r2Max = radius * radius;

    [loop]
    for (int dz = -1; dz <= 1; ++dz)
    [loop]
    for (int dy = -1; dy <= 1; ++dy)
    [loop]
    for (int dx = -1; dx <= 1; ++dx) {
        const int gx = cx + dx, gy = cy + dy, gz = cz + dz;
        if (gx < 0 || gy < 0 || gz < 0 || gx >= (int)GridX || gy >= (int)GridY || gz >= (int)GridZ)
            continue;
        const uint cell = (uint)gx + GridX * ((uint)gy + GridY * (uint)gz);
        const uint count = min(GridBuffer[cell], PBFCellCapacity);
        [loop]
        for (uint k = 0u; k < count; ++k) {
            const uint j = GridBuffer[cells * PBFCellCapacity + cell * PBFCellCapacity + k];
            if (j >= ParticleCount) continue;
            const float3 d = Positions[j].xyz - xi;
            const float r2 = dot(d, d);
            if (r2 >= r2Max) continue;
            const float q = sqrt(r2) * inv;
            const float w = 1.0f - q * q * q;
            weightSum += w;
            mean += w * d;
            sxx += w * d.x * d.x;
            sxy += w * d.x * d.y;
            sxz += w * d.x * d.z;
            syy += w * d.y * d.y;
            syz += w * d.y * d.z;
            szz += w * d.z * d.z;
            neighbours += 1.0f;
        }
    }

    const float invW = 1.0f / max(weightSum, 1.0e-6f);
    mean *= invW;
    // Covariance about the weighted mean.
    float a00 = sxx * invW - mean.x * mean.x;
    float a01 = sxy * invW - mean.x * mean.y;
    float a02 = sxz * invW - mean.x * mean.z;
    float a11 = syy * invW - mean.y * mean.y;
    float a12 = syz * invW - mean.y * mean.z;
    float a22 = szz * invW - mean.z * mean.z;

    float3 v0 = float3(1.0f, 0.0f, 0.0f);
    float3 v1 = float3(0.0f, 1.0f, 0.0f);
    float3 v2 = float3(0.0f, 0.0f, 1.0f);
    [unroll]
    for (int sweep = 0; sweep < 5; ++sweep) {
        JacobiRotate(a00, a11, a01, a02, a12, v0, v1);
        JacobiRotate(a00, a22, a02, a01, a12, v0, v2);
        JacobiRotate(a11, a22, a12, a01, a02, v1, v2);
    }

    // A full, uniform neighbourhood has variance 0.15 R^2 per axis with this weight (the
    // continuum value; measured 0.0100 vs 0.0101 m^2 on the settled pool), so an interior
    // particle maps to stretch 1 on every axis.
    const float referenceVariance = 0.15f * radius * radius;
    float3 stretch = sqrt(max(float3(a00, a11, a22), 0.0f) / referenceVariance);
    const float longest = clamp(max(stretch.x, max(stretch.y, stretch.z)), 1.0e-3f, max(MaxStretch, 1.0f));
    stretch = clamp(stretch, longest / max(MaxRatio, 1.0f), longest);

    // Sparse neighbourhoods give an unreliable covariance and are not part of a surface: they
    // blend toward a small isotropic droplet (Yu and Turk's k_n * I).
    const float full = saturate((neighbours - SparseNeighbours) /
                                max(FullNeighbours - SparseNeighbours, 1.0f));
    const float interiorRadius = max(RadiusScale, 0.05f) * Spacing;
    const float sparseRadius = max(SparseScale, 0.05f) * Spacing;
    float3 axes = lerp(float3(sparseRadius, sparseRadius, sparseRadius),
                       stretch * interiorRadius, full);

    // A lone particle (at most two above the sparse count) that has come to rest shrinks away;
    // flying spray keeps its size.
    const float lone = 1.0f - saturate((neighbours - SparseNeighbours) * 0.5f);
    const float speed = length(Velocities[i].xyz);
    const float keep = 1.0f - lone * (1.0f - smoothstep(0.3f, 1.2f, speed));
    if (keep < 0.1f) {
        Anisotropy[i * 3u + 0u] = float4(xi, -1.0f);
        Anisotropy[i * 3u + 1u] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        Anisotropy[i * 3u + 2u] = float4(0.0f, 0.0f, neighbours, 1.0f);
        return;
    }
    axes *= keep;

    const float3 centre = xi + Smoothing * full * mean;
    const float3 invA2 = 1.0f / max(axes * axes, 1.0e-8f);
    // M = sum_k v_k v_k^T / a_k^2
    const float m00 = invA2.x * v0.x * v0.x + invA2.y * v1.x * v1.x + invA2.z * v2.x * v2.x;
    const float m01 = invA2.x * v0.x * v0.y + invA2.y * v1.x * v1.y + invA2.z * v2.x * v2.y;
    const float m02 = invA2.x * v0.x * v0.z + invA2.y * v1.x * v1.z + invA2.z * v2.x * v2.z;
    const float m11 = invA2.x * v0.y * v0.y + invA2.y * v1.y * v1.y + invA2.z * v2.y * v2.y;
    const float m12 = invA2.x * v0.y * v0.z + invA2.y * v1.y * v1.z + invA2.z * v2.y * v2.z;
    const float m22 = invA2.x * v0.z * v0.z + invA2.y * v1.z * v1.z + invA2.z * v2.z * v2.z;

    const float bound = max(axes.x, max(axes.y, axes.z));
    // Buried: full, balanced neighbourhood. Marked as a negative neighbour count so the splat
    // passes can skip it without a second buffer.
    const bool buried = neighbours >= BURIED_NEIGHBOURS &&
                        length(mean) <= BURIED_OFFSET * radius;
    const float storedNeighbours = buried ? -neighbours : neighbours;
    Anisotropy[i * 3u + 0u] = float4(centre, bound);
    Anisotropy[i * 3u + 1u] = float4(m00, m01, m02, m11);
    Anisotropy[i * 3u + 2u] = float4(m12, m22, storedNeighbours, 1.0f - full);
}
