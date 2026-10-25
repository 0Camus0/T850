// T850 pool fluid: divergence of the velocity volume with solid-wall clamping.

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
Texture3D<float4> VelocityIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> DivergenceOut : register(u0);

bool Outside(int3 cell)
{
    return cell.x <= 0 || cell.y <= 0 || cell.z <= 0 ||
           cell.x >= int(Width) - 1 || cell.y >= int(Height) - 1 || cell.z >= int(Depth) - 1;
}

float Component(int3 cell, int axis)
{
    if (Outside(cell))
        return 0.0f;
    float4 value = VelocityIn.Load(int4(cell, 0));
    return axis == 0 ? value.x : (axis == 1 ? value.y : value.z);
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);
    const float scale = 0.5f / CellSize;
    const float divergence =
        (Component(cell + int3(1, 0, 0), 0) - Component(cell - int3(1, 0, 0), 0)) * scale +
        (Component(cell + int3(0, 1, 0), 1) - Component(cell - int3(0, 1, 0), 1)) * scale +
        (Component(cell + int3(0, 0, 1), 2) - Component(cell - int3(0, 0, 1), 2)) * scale;
    DivergenceOut[id] = float4(divergence, 0.0f, 0.0f, 0.0f);
}
