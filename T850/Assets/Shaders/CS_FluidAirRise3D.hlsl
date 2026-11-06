// T850 pool fluid: entrained air rises out of the water (W-29).
//
// The solver is single-phase and has no bubble buoyancy, so air a splash pushes into the
// water stayed there for good: bulk cells below 98 % alpha grew from 798 to 4212 over
// 300 s and the surface kernel drew every pocket as a fixed bump. This is a vertical
// drift flux: across each face between a lower and an upper open cell, water moves down
// and air up by
//
//   F = c * (1 - alpha_lower) * alpha_upper,   c = RiseSpeed * dt / h, clamped to 1.
//
// Both cells of a face read the same two alphas, so the column total telescopes and is
// conserved exactly. With c <= 1 a cell gains at most c * (1 - alpha) and loses at most
// c * alpha, so the result stays in [0, 1] without a limiter. A fully wet or fully dry
// column, and a sharp interface (water below air), carry no flux; a smeared interface is
// pulled back to a step, which never changes a column's water total or its surface.

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
    // Reused as the rise speed in m/s for this kernel.
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
Texture3D<float4> AlphaIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_r32f]]
#endif
RWTexture3D<float> AlphaOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
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

// Water crossing the face above `lower` downwards; air crosses it upwards. Air under a
// solid (the floor of a step, a floating body) stays there.
float DownFlux(int3 lower)
{
    const int3 upper = lower + int3(0, 1, 0);
    if (IsSolidCell(lower) || IsSolidCell(upper))
        return 0.0f;
    const float c = saturate(Padding0 * DeltaTime / CellSize);
    const float alphaLower = saturate(AlphaIn.Load(int4(lower, 0)).x);
    const float alphaUpper = saturate(AlphaIn.Load(int4(upper, 0)).x);
    return c * (1.0f - alphaLower) * alphaUpper;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    const int3 cell = int3(id);
    if (IsSolidCell(cell))
    {
        AlphaOut[id] = 0.0f;
        return;
    }
    const float alpha = saturate(AlphaIn.Load(int4(cell, 0)).x);
    AlphaOut[id] = saturate(alpha + DownFlux(cell) - DownFlux(cell - int3(0, 1, 0)));
}
