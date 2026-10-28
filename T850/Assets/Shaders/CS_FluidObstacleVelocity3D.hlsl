// T850 pool fluid: couple a moving body to the velocity field.
//
// This is the physically essential half of obstacle interaction, and it was
// missing. The volume-fraction pass carved a hole where the body was but never
// touched velocity, so the solver had no divergence to react to: the pressure
// solve had nothing to push against, the surface just showed a static dimple that
// tracked the ball, and no wave ever radiated. Displacement has to enter the
// momentum equation as a VELOCITY SOURCE, not as a height-field patch.
//
// The constraint imposed is the no-penetration condition at a moving solid
// boundary:
//     u . n = u_body . n        on the body surface
// Only the normal component is touched; the tangential component is left alone,
// which is the free-slip convention the rest of the solver already uses at the
// pool walls.
//
// Runs AFTER forces/viscosity and BEFORE divergence, so the divergence the
// pressure solve sees includes the body's motion. Applying it after projection
// would simply be undone by the gradient subtraction.

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
Texture3D<float4> AlphaIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
cbuffer ObjectConstants : register(b1)
{
    float ObjectCentreX;
    float ObjectCentreY;
    float ObjectCentreZ;
    float ObjectRadius;

    float ObjectVelX;
    float ObjectVelY;
    float ObjectVelZ;
    float ObjectEnabled;

    float WorldOriginX;
    float WorldOriginZ;
    float BasinMinY;
    float ObjectPadding0;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> VelocityOut : register(u0);

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool IsSolid(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;

    int3 cell = int3(id);
    float4 velocity = VelocityIn.Load(int4(cell, 0));

    // Dry cells carry no momentum: imposing a boundary condition on air would
    // manufacture velocity out of nothing, and the gradient kernel deliberately
    // keeps air velocity untouched so the free surface is not a solid lid.
    const float alpha = saturate(AlphaIn.Load(int4(cell, 0)).x);
    if (IsSolid(cell) || alpha <= 0.05f || ObjectEnabled < 0.5f || ObjectRadius <= 0.0f) {
        VelocityOut[id] = velocity;
        return;
    }

    // World position of this cell's CENTRE.
    const float3 centre = float3(WorldOriginX + (float(cell.x) + 0.5f) * CellSize,
                                 BasinMinY + (float(cell.y) + 0.5f) * CellSize,
                                 WorldOriginZ + (float(cell.z) + 0.5f) * CellSize);
    const float3 offset = centre - float3(ObjectCentreX, ObjectCentreY, ObjectCentreZ);
    const float distance = length(offset);

    // Band one and a half cells wide: enough to catch every cell the body
    // actually sweeps through without dragging the whole pool along with it.
    const float band = 1.5f * CellSize;
    if (distance > ObjectRadius + band)
    {
        VelocityOut[id] = velocity;
        return;
    }

    const float3 normal = distance > 1e-5f ? offset / distance : float3(0.0f, 1.0f, 0.0f);
    const float3 bodyVelocity = float3(ObjectVelX, ObjectVelY, ObjectVelZ);

    // No-penetration: the fluid's normal velocity must match the body's.
    const float fluidNormal = dot(velocity.xyz, normal);
    const float bodyNormal = dot(bodyVelocity, normal);
    // Fully inside the body, fully enforced; ramping to zero across the band.
    const float weight = distance <= ObjectRadius
        ? 1.0f
        : saturate(1.0f - (distance - ObjectRadius) / band);

    velocity.xyz += (bodyNormal - fluidNormal) * weight * normal;
    VelocityOut[id] = velocity;
}
