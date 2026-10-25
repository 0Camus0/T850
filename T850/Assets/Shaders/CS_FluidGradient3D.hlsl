// T850 pool fluid: subtracts the pressure gradient so the velocity field
// becomes divergence free.

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
Texture3D<float4> PressureIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> VelocityOut : register(u0);

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
    float4 velocity = VelocityIn.Load(int4(id, 0));
    if (Outside(cell))
    {
        VelocityOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float center = PressureIn.Load(int4(cell, 0)).x;
    const float scale = 0.5f / CellSize;
    const float3 gradient = float3(
        PressureAt(cell + int3(1, 0, 0), center) - PressureAt(cell - int3(1, 0, 0), center),
        PressureAt(cell + int3(0, 1, 0), center) - PressureAt(cell - int3(0, 1, 0), center),
        PressureAt(cell + int3(0, 0, 1), center) - PressureAt(cell - int3(0, 0, 1), center)) * scale;
    velocity.xyz -= gradient;
    velocity.xyz = clamp(velocity.xyz, -12.0f, 12.0f);
    VelocityOut[id] = velocity;
}
