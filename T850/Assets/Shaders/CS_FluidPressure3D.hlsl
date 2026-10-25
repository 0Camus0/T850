// T850 pool fluid: one Jacobi pressure iteration.
// Reads the previous pressure and the divergence volume, writes the next
// pressure approximation.

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
    const float neighbours =
        PressureAt(cell + int3(1, 0, 0), center) + PressureAt(cell - int3(1, 0, 0), center) +
        PressureAt(cell + int3(0, 1, 0), center) + PressureAt(cell - int3(0, 1, 0), center) +
        PressureAt(cell + int3(0, 0, 1), center) + PressureAt(cell - int3(0, 0, 1), center);
    const float divergence = DivergenceIn.Load(int4(cell, 0)).x;
    const float pressure = (neighbours - divergence * PressureScale) / 6.0f;
    PressureOut[id] = float4(pressure, 0.0f, 0.0f, 0.0f);
}
