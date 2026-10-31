// T850 pool fluid: divergence of the MAC velocity field.
//
// Non-LIQUID cells write 0. A LIQUID cell sums the six face fluxes that bound it:
// the +X face is texel (i,j,k).x, the -X face is texel (i-1,j,k).x, and likewise
// for Y and Z. A texel index of -1 on the face's own axis is the domain wall and
// reads 0. Solid faces already hold their boundary values from the boundary
// kernel, so they are used as stored.

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
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_r32f]]
#endif
RWTexture3D<float> DivergenceOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t2);

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

bool IsLiquid(int3 cell)
{
    if (IsSolid(cell))
        return false;
    return saturate(AlphaIn.Load(int4(cell, 0)).x) >= T8_LIQUID_THRESHOLD;
}

// Component `axis` of the face stored in texel `cell`. A texel index of -1 on
// the face's own axis is the domain wall and reads 0.
float FaceComponent(int3 cell, int axis)
{
    if (axis == 0)
    {
        if (cell.x < 0)
            return 0.0f;
        return VelocityIn.Load(int4(cell, 0)).x;
    }
    if (axis == 1)
    {
        if (cell.y < 0)
            return 0.0f;
        return VelocityIn.Load(int4(cell, 0)).y;
    }
    if (cell.z < 0)
        return 0.0f;
    return VelocityIn.Load(int4(cell, 0)).z;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;

    int3 cell = int3(id);
    if (!IsLiquid(cell))
    {
        DivergenceOut[id] = 0.0f;
        return;
    }

    const float uPlus  = FaceComponent(cell, 0);
    const float uMinus = FaceComponent(cell + int3(-1, 0, 0), 0);
    const float vPlus  = FaceComponent(cell, 1);
    const float vMinus = FaceComponent(cell + int3(0, -1, 0), 1);
    const float wPlus  = FaceComponent(cell, 2);
    const float wMinus = FaceComponent(cell + int3(0, 0, -1), 2);

    const float divergence = (uPlus - uMinus + vPlus - vMinus + wPlus - wMinus) / CellSize;
    DivergenceOut[id] = divergence;
}
