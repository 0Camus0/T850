// T850 pool fluid: flux-form volume-fraction advection on a staggered MAC grid.
//
// Conservative flux form: every face flux leaving one cell enters its neighbour,
// so summing alpha over the domain telescopes to boundary fluxes only. Face
// velocities are the single stored MAC values, shared by both cells, keeping the
// scheme antisymmetric. A compression term (Russo & Smereka) sharpens the front.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Depth;
    uint Iteration;
    float DeltaTime;
    float CellSize;
    float Viscosity;
    float Gravity;
    float WindX;
    float WindY;
    float WindZ;
    float Damping;
    float SurfaceLevel;
    float TimeValue;
    float PressureScale;
    // Reused as the compression strength for this kernel.
    float Padding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> VelocityIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> AlphaOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool IsSolidCell(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

float AlphaAt(int3 cell)
{
    if (IsSolidCell(cell))
        return 0.0f;
    return saturate(AlphaIn.Load(int4(cell, 0)).x);
}

// Stored MAC face velocity for component c at texel n; out-of-range along c is 0.
float FaceVelocity(int3 n, int c)
{
    int3 dim = int3(Width, Height, Depth);
    if (c == 0)
    {
        if (n.x < 0 || n.x > dim.x - 1)
            return 0.0f;
        n.y = clamp(n.y, 0, dim.y - 1);
        n.z = clamp(n.z, 0, dim.z - 1);
        return VelocityIn.Load(int4(n, 0)).x;
    }
    if (c == 1)
    {
        if (n.y < 0 || n.y > dim.y - 1)
            return 0.0f;
        n.x = clamp(n.x, 0, dim.x - 1);
        n.z = clamp(n.z, 0, dim.z - 1);
        return VelocityIn.Load(int4(n, 0)).y;
    }
    if (n.z < 0 || n.z > dim.z - 1)
        return 0.0f;
    n.x = clamp(n.x, 0, dim.x - 1);
    n.y = clamp(n.y, 0, dim.y - 1);
    return VelocityIn.Load(int4(n, 0)).z;
}

// Signed volume flux LEAVING `cell` across one face, in alpha-cells per second.
// Both cells sharing the face read the same stored U, so the mirror flux is its
// exact negation and the domain sum telescopes.
float FaceFluxOut(int3 cell, int3 offset, int axis, float outwardSign)
{
    const int3 neighbour = cell + offset;
    const float alphaCell = AlphaAt(cell);
    const float alphaNeighbour = AlphaAt(neighbour);

    // Stored MAC face velocity: + face uses texel(cell), - face uses texel(cell - e_c).
    float u;
    if (outwardSign > 0.0f)
        u = FaceVelocity(cell, axis);
    else
        u = FaceVelocity(cell + offset, axis);

    const float s = outwardSign * u;

    if (IsSolidCell(neighbour))
        return 0.0f;

    float flux = s > 0.0f ? s * alphaCell : s * alphaNeighbour;
    // Interface sharpening (OpenFOAM-style): moves water toward the fuller side,
    // scaled by a(1-a); the ramp keeps near-equal bulk cells from being amplified.
    const float faceAlpha = 0.5f * (alphaCell + alphaNeighbour);
    const float direction = clamp((alphaNeighbour - alphaCell) * 4.0f, -1.0f, 1.0f);
    flux += Padding0 * abs(s) * faceAlpha * (1.0f - faceAlpha) * direction;
    return flux;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);

    if (IsSolidCell(cell))
    {
        AlphaOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float center = AlphaAt(cell);
    const float dtOverH = DeltaTime / CellSize;

    const float fluxOut =
        FaceFluxOut(cell, int3( 1, 0, 0), 0,  1.0f) +
        FaceFluxOut(cell, int3(-1, 0, 0), 0, -1.0f) +
        FaceFluxOut(cell, int3( 0, 1, 0), 1,  1.0f) +
        FaceFluxOut(cell, int3( 0,-1, 0), 1, -1.0f) +
        FaceFluxOut(cell, int3( 0, 0, 1), 2,  1.0f) +
        FaceFluxOut(cell, int3( 0, 0,-1), 2, -1.0f);

    float alpha = saturate(center - dtOverH * fluxOut);
    AlphaOut[id] = float4(alpha, 0.0f, 0.0f, 0.0f);
}
