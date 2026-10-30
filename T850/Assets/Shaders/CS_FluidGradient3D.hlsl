// T850 pool fluid: subtracts the pressure gradient on the MAC grid.
//
// Per texel and component c, the face between A (this texel's cell) and
// B = A + e_c is classified. SOLID faces take their boundary value (body
// velocity component if the solid cell is a BODY cell, else 0). AIR faces copy
// the input. LIQUID faces subtract (pB - pA)/CellSize, where an AIR cell uses the
// ghost value p_ghost = p_L * (1 - 1/theta). Each component is clamped to
// [-12, 12].

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

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t2);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(6, 0)]]
#endif
cbuffer ObjectConstants : register(b1)
{
    // W-22: a body list. The wall mask encodes which body owns a cell as 2u + k, so the
    // kernels need no centre or radius - the stamp already carries the footprint - and
    // only each body's velocity is needed here. 4 DWORDs of header plus 4 per body keeps
    // the D3D12 root signature inside its 64-DWORD budget for kMaxBodies = 8.
    uint BodyCount;
    uint ObjectPad0;
    uint ObjectPad1;
    uint ObjectPad2;
    float4 BodyVelocity[8];
};

#define T8_LIQUID_THRESHOLD 0.5f

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

// The ceiling is the only open boundary. Every other out-of-range direction is a
// genuine pool wall, but the rows ABOVE the domain are air, not a lid. Treating
// them as solid made the top row's +Y face a no-flux wall, so the pressure solve
// resisted any rise and a strong splash piled up against the ceiling instead of
// leaving the surface free. Only x/z-valid cells directly above the top row are
// open; the corner cells beyond that stay solid.
bool IsAboveTop(int3 cell)
{
    return cell.y >= int(Height) &&
           cell.x >= 0 && cell.x < int(Width) &&
           cell.z >= 0 && cell.z < int(Depth);
}

bool IsLiquid(int3 cell)
{
    if (IsSolid(cell))
        return false;
    return saturate(AlphaIn.Load(int4(cell, 0)).x) >= T8_LIQUID_THRESHOLD;
}

// A cell stamped with 2u + k belongs to body k; static solids are 1u. The index is
// clamped and only used when the mask really is a body value (R1: HLSL does not
// short-circuit, so the comparison is done before the lookup).
bool IsBodyCell(int3 cell, out uint bodyIndex)
{
    bodyIndex = 0u;
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return false;
    const uint mask = WallMask[CellIndex(cell)];
    if (mask < 2u)
        return false;
    bodyIndex = min(mask - 2u, BodyCount - 1u);
    return true;
}
float AlphaAt(int3 cell)
{
    // Guarded: cells above the ceiling are air by definition. HLSL Load() beyond
    // the texture returns 0 on D3D12 but is undefined on Vulkan, so never rely on
    // it -- R1 applies here as much as to the wall-mask buffer.
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return 0.0f;
    return saturate(AlphaIn.Load(int4(cell, 0)).x);
}

float Theta(float aL, float aAir)
{
    return clamp((aL - 0.5f) / max(aL - aAir, 1e-4f), 0.1f, 1.0f);
}

// Pressure at a cell, using the ghost value for AIR cells. `liquidCell` is the
// LIQUID cell of the face; `otherCell` is the cell being queried.
float PressureForFace(int3 liquidCell, int3 otherCell)
{
    if (IsLiquid(otherCell))
        return PressureIn.Load(int4(otherCell, 0)).x;
    // AIR: ghost value p_L * (1 - 1/theta).
    const float pL = PressureIn.Load(int4(liquidCell, 0)).x;
    const float theta = Theta(AlphaAt(liquidCell), AlphaAt(otherCell));
    return pL * (1.0f - 1.0f / theta);
}

float SolidBoundaryValue(int3 solidCell, int axis)
{
    uint bodyIndex = 0u;
    if (IsBodyCell(solidCell, bodyIndex))
        return BodyVelocity[bodyIndex][axis];
    return 0.0f;
}

// Gradient-corrected value for component `axis` on the face between A and B.
float FaceValue(int3 a, int3 b, int axis, float inputValue)
{
    if (IsSolid(a))
        return SolidBoundaryValue(a, axis);
    // An AIR neighbour above the ceiling must NOT take the solid branch: that
    // branch zeroes the face velocity, which is exactly the lid this change
    // removes. Falling through turns it into an ordinary liquid/air face, whose
    // ghost pressure drives the free surface upward.
    if (IsSolid(b) && !(OpenCeiling > 0.5f && IsAboveTop(b)))
        return SolidBoundaryValue(b, axis);

    const bool aLiquid = IsLiquid(a);
    const bool bLiquid = IsLiquid(b);
    if (!aLiquid && !bLiquid)
        return inputValue; // AIR face: copy input

    // LIQUID face: at least one side is liquid.
    float pA;
    float pB;
    if (aLiquid)
        pA = PressureIn.Load(int4(a, 0)).x;
    else
        pA = PressureForFace(b, a);
    if (bLiquid)
        pB = PressureIn.Load(int4(b, 0)).x;
    else if (OpenCeiling > 0.5f && IsAboveTop(b))
        pB = 0.0f; // Must match the pressure kernel, which solves this face with theta = 1.
    else
        pB = PressureForFace(a, b);

    return inputValue - DeltaTime * (pB - pA) / CellSize;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;

    int3 cell = int3(id);
    float4 velocity = VelocityIn.Load(int4(cell, 0));

    float u = FaceValue(cell, cell + int3(1, 0, 0), 0, velocity.x);
    float v = FaceValue(cell, cell + int3(0, 1, 0), 1, velocity.y);
    float w = FaceValue(cell, cell + int3(0, 0, 1), 2, velocity.z);

    u = clamp(u, -12.0f, 12.0f);
    v = clamp(v, -12.0f, 12.0f);
    w = clamp(w, -12.0f, 12.0f);

    VelocityOut[id] = float4(u, v, w, 0.0f);
}
