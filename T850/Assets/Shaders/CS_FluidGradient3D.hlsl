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

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool Outside(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t2);

#define T8_AIR_THRESHOLD 0.05f

bool IsAir(int3 cell)
{
    if (Outside(cell))
        return false;
    return AlphaIn.Load(int4(cell, 0)).x < T8_AIR_THRESHOLD;
}

float PressureAt(int3 cell, float fallback)
{
    if (Outside(cell))
        return fallback;
    // Dirichlet p = 0 at the free surface.
    if (IsAir(cell))
        return 0.0f;
    return PressureIn.Load(int4(cell, 0)).x;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);
    float4 velocity = VelocityIn.Load(int4(id, 0));
    // Only solid carries no flow. Air must NOT be zeroed: doing so makes the free
    // surface behave like a solid lid, so water pushed down has nowhere to rise
    // into and alpha clamping destroys mass (measured -27.7% in 600 frames).
    // Air keeps the transported velocity and is simply skipped by the pressure
    // solve, which is the Dirichlet p = 0 boundary condition.
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
    // No-penetration at the wall face: a component pointing into a solid
    // neighbour is forced to zero, so the boundary condition holds at the face
    // rather than only inside the solid voxel.
    if (Outside(cell + int3(1, 0, 0)) && velocity.x > 0.0f) velocity.x = 0.0f;
    if (Outside(cell - int3(1, 0, 0)) && velocity.x < 0.0f) velocity.x = 0.0f;
    if (Outside(cell + int3(0, 1, 0)) && velocity.y > 0.0f) velocity.y = 0.0f;
    if (Outside(cell - int3(0, 1, 0)) && velocity.y < 0.0f) velocity.y = 0.0f;
    if (Outside(cell + int3(0, 0, 1)) && velocity.z > 0.0f) velocity.z = 0.0f;
    if (Outside(cell - int3(0, 0, 1)) && velocity.z < 0.0f) velocity.z = 0.0f;
    velocity.xyz = clamp(velocity.xyz, -12.0f, 12.0f);
    VelocityOut[id] = velocity;
}
