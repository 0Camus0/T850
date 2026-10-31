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
    // 1 when the rows above the domain are open air, 0 for a closed ceiling.
    float OpenCeiling;
    float OpenCeilingPad0;
    float OpenCeilingPad1;
    float OpenCeilingPad2;
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
[[spv::format_r32f]]
#endif
RWTexture3D<float> AlphaOut : register(u0);

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
// Both cells sharing the face read the same stored U and the same three alphas, so the
// mirror flux is its exact negation and the domain sum telescopes.
//
// W-03: the donated amount is not `cfl * alpha_donor` (the cell average, which smears the
// front by construction) but `cfl * a_face`, where a_face is a van Leer / minmod limited
// reconstruction of the alpha at the upwind side of the face:
//
//   d1 = alpha_donor - alpha_far,  d2 = alpha_receiver - alpha_donor
//   slope = minmod(d1, d2)                      (0 when the three are not monotone)
//   a_face = clamp(alpha_donor + 0.5 * slope * (1 - cfl), 0, 1)
//
// For a sharp front (1, 1, 0) the slope is 0 and the full cell value advects, so the front
// moves at exactly the flow speed. For a smeared front (1, 0.5, 0.25) the slope is negative
// and the reconstruction is *below* the cell average, so less than the average leaves and
// the profile steepens instead of spreading. That is the anti-diffusive behaviour this
// stage needs; the minmod keeps it monotone, so no new maxima appear.
//
// A two-branch SLIC / Hirt-Nichols donor-acceptor amount was tried first and rejected: its
// far-side branch sends nothing when cfl < 1 - alpha, so every smeared cell froze and the
// band of 0.5 cells widened (bulk 0.9637, interface 7.43 cells, 22 133 cells below 0.98).
float FaceFluxOut(int3 cell, int3 offset, int axis, float outwardSign)
{
    const int3 step = (axis == 0) ? int3(1, 0, 0) : ((axis == 1) ? int3(0, 1, 0) : int3(0, 0, 1));
    // The face's canonical owner is the lower cell, so both cells compute identical values.
    const bool plusFace = outwardSign > 0.0f;
    const int3 lower = plusFace ? cell : cell - step;
    const int3 upper = lower + step;
    if (IsSolidCell(lower) || IsSolidCell(upper))
        return 0.0f;

    const float u = FaceVelocity(lower, axis);
    const float alphaLower = AlphaAt(lower);
    const float alphaUpper = AlphaAt(upper);

    float flux = 0.0f;
    if (u != 0.0f)
    {
        const float cfl = min(abs(u) * DeltaTime / CellSize, 1.0f);
        const bool forward = u > 0.0f;
        const float alphaDonor = forward ? alphaLower : alphaUpper;
        const float alphaReceiver = forward ? alphaUpper : alphaLower;
        const int3 farCell = forward ? (lower - step) : (upper + step);
        const float alphaFar = AlphaAt(farCell);

        const float d1 = alphaDonor - alphaFar;
        const float d2 = alphaReceiver - alphaDonor;
        // minmod: only a monotone stencil contributes a slope.
        const float slope = (d1 * d2 > 0.0f) ? (abs(d1) < abs(d2) ? d1 : d2) : 0.0f;
        const float faceAlpha = clamp(alphaDonor + 0.5f * slope * (1.0f - cfl), 0.0f, 1.0f);
        const float amount = min(cfl * faceAlpha, alphaDonor);
        // Amount per step -> rate, signed in the +axis direction.
        const float rate = amount * CellSize / max(DeltaTime, 1e-8f);
        flux = forward ? rate : -rate;
    }

    // Interface sharpening (OpenFOAM-style): moves water toward the fuller side, scaled by
    // a(1-a); the ramp keeps near-equal bulk cells from being amplified. Antisymmetric
    // between the two cells, so it conserves.
    const float s = abs(u);
    const float faceAverage = 0.5f * (alphaLower + alphaUpper);
    const float direction = clamp((alphaUpper - alphaLower) * 4.0f, -1.0f, 1.0f);
    flux += Padding0 * s * faceAverage * (1.0f - faceAverage) * direction;

    // Convert to "leaving `cell`": the canonical flux is positive in +axis, and a face on
    // the -axis side of the cell is crossed in the opposite sense.
    return plusFace ? flux : -flux;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);

    if (IsSolidCell(cell))
    {
        AlphaOut[id] = 0.0f;
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
    AlphaOut[id] = alpha;
}
