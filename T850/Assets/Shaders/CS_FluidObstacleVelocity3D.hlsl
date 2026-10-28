// T850 pool fluid: MAC-grid boundary kernel.
//
// Runs every substep after forces and before divergence. For each texel and each
// component c it inspects the face between cell A (this texel) and B = A + e_c.
// A SOLID face is overwritten with its boundary value: the body's velocity
// component if the solid cell of the face is a BODY cell, otherwise 0. Every
// other face is copied through unchanged. On a staggered grid the solid faces
// carry the body velocity exactly, so no smoothing band is needed.

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
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
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

bool IsBodyCell(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return false;
    if (ObjectEnabled < 0.5f || ObjectRadius <= 0.0f)
        return false;
    const float3 centre = float3(WorldOriginX + float(cell.x) * CellSize,
                                 BasinMinY + (float(cell.y) + 0.5f) * CellSize,
                                 WorldOriginZ + float(cell.z) * CellSize);
    const float3 d = centre - float3(ObjectCentreX, ObjectCentreY, ObjectCentreZ);
    return dot(d, d) <= ObjectRadius * ObjectRadius;
}

// Boundary value for a solid face whose solid cell is `solidCell`.
float SolidBoundaryValue(int3 solidCell, int axis)
{
    if (IsBodyCell(solidCell))
        return axis == 0 ? ObjectVelX : (axis == 1 ? ObjectVelY : ObjectVelZ);
    return 0.0f;
}

// Face value for component `axis` between A and B = A + e_axis.
float FaceValue(int3 a, int3 b, int axis, float inputValue)
{
    if (IsSolid(a))
        return SolidBoundaryValue(a, axis);
    if (IsSolid(b))
        return SolidBoundaryValue(b, axis);
    return inputValue;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;

    int3 cell = int3(id);
    float4 velocity = VelocityIn.Load(int4(cell, 0));

    const float u = FaceValue(cell, cell + int3(1, 0, 0), 0, velocity.x);
    const float v = FaceValue(cell, cell + int3(0, 1, 0), 1, velocity.y);
    const float w = FaceValue(cell, cell + int3(0, 0, 1), 2, velocity.z);

    VelocityOut[id] = float4(u, v, w, 0.0f);
}
