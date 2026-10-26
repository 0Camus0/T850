// T850 pool fluid: one red-black Gauss-Seidel pressure pass with SOR.
// Reads the previous pressure and the divergence volume, writes the next
// pressure approximation. Only cells matching the current parity are relaxed;
// the rest are copied through so the ping-pong target stays a complete field.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Depth;
    // Packed as sweep * 2 + parity (nothing else reads this field).
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
    // Successive over-relaxation factor for this pass.
    float Padding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
Texture3D<float4> PressureIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture3D<float4> DivergenceIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> PressureOut : register(u0);

bool Outside(int3 cell)
{
    return cell.x <= 0 || cell.y <= 0 || cell.z <= 0 ||
           cell.x >= int(Width) - 1 || cell.y >= int(Height) - 1 || cell.z >= int(Depth) - 1;
}

float PressureAt(int3 cell, float fallback)
{
    if (Outside(cell))
        return fallback;
    return PressureIn.Load(int4(cell, 0)).x;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);
    const float center = PressureIn.Load(int4(cell, 0)).x;

    // Red-black ordering. Cells of the other colour are copied through; the next
    // pass relaxes them against these freshly updated neighbours, which is what
    // makes this Gauss-Seidel rather than Jacobi.
    const uint parity = Iteration & 1u;
    if (((cell.x + cell.y + cell.z) & 1) != int(parity)) {
        PressureOut[id] = float4(center, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float neighbours =
        PressureAt(cell + int3(1, 0, 0), center) + PressureAt(cell - int3(1, 0, 0), center) +
        PressureAt(cell + int3(0, 1, 0), center) + PressureAt(cell - int3(0, 1, 0), center) +
        PressureAt(cell + int3(0, 0, 1), center) + PressureAt(cell - int3(0, 0, 1), center);
    const float divergence = DivergenceIn.Load(int4(cell, 0)).x;
    const float gaussSeidelTarget = (neighbours - divergence * PressureScale) / 6.0f;
    // SOR: omega > 1 overshoots toward the target and converges markedly faster
    // than plain Gauss-Seidel for a 7-point Laplacian. 1.0 degenerates to plain
    // Gauss-Seidel, so the slider is usable as a comparison.
    const float omega = clamp(Padding0, 0.05f, 1.99f);
    const float pressure = lerp(center, gaussSeidelTarget, omega);
    PressureOut[id] = float4(pressure, 0.0f, 0.0f, 0.0f);
}
