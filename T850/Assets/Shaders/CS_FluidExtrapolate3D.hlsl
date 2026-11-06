// T850 pool fluid: one-layer velocity extrapolation into the air on a staggered
// MAC grid. Solid and liquid faces copy their input; air faces average the same
// component over neighbouring texels whose own component-c face is liquid.

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
Texture3D<float4> AlphaIn : register(t1);

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

#define T8_LIQUID_THRESHOLD 0.5f

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool InRange(int3 cell)
{
    return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 &&
           cell.x < int(Width) && cell.y < int(Height) && cell.z < int(Depth);
}

bool IsSolidCell(int3 cell)
{
    if (!InRange(cell))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

bool IsLiquidCell(int3 cell)
{
    if (!InRange(cell))
        return false;
    if (WallMask[CellIndex(cell)] != 0u)
        return false;
    return saturate(AlphaIn.Load(int4(cell, 0)).x) >= T8_LIQUID_THRESHOLD;
}

bool IsSolidFace(int3 a, int axis)
{
    if (IsSolidCell(a))
        return true;
    int3 b = a;
    if (axis == 0) b.x += 1;
    else if (axis == 1) b.y += 1;
    else b.z += 1;
    return IsSolidCell(b);
}

bool IsLiquidFace(int3 a, int axis)
{
    if (IsSolidFace(a, axis))
        return false;
    if (IsLiquidCell(a))
        return true;
    int3 b = a;
    if (axis == 0) b.x += 1;
    else if (axis == 1) b.y += 1;
    else b.z += 1;
    return IsLiquidCell(b);
}

float FetchComponent(int3 n, int c)
{
    int3 dim = int3(Width, Height, Depth);
    if (c == 0)
    {
        if (n.x < 0 || n.x > dim.x - 1)
            return 0.0f;
        n.y = clamp(n.y, 0, dim.y - 1);
        n.z = clamp(n.z, 0, dim.z - 1);
        return VelocityIn.Load(int4(n, 0)).x;
    }
    if (c == 1)
    {
        if (n.y < 0 || n.y > dim.y - 1)
            return 0.0f;
        n.x = clamp(n.x, 0, dim.x - 1);
        n.z = clamp(n.z, 0, dim.z - 1);
        return VelocityIn.Load(int4(n, 0)).y;
    }
    if (n.z < 0 || n.z > dim.z - 1)
        return 0.0f;
    n.x = clamp(n.x, 0, dim.x - 1);
    n.y = clamp(n.y, 0, dim.y - 1);
    return VelocityIn.Load(int4(n, 0)).z;
}

void Accumulate(int3 n, int c, inout float sum, inout uint count)
{
    if (!InRange(n))
        return;
    if (!IsLiquidFace(n, c))
        return;
    sum += FetchComponent(n, c);
    ++count;
}

float ExtrapolateComponent(int3 cell, int c)
{
    float sum = 0.0f;
    uint count = 0u;
    Accumulate(cell + int3(1, 0, 0), c, sum, count);
    Accumulate(cell - int3(1, 0, 0), c, sum, count);
    Accumulate(cell + int3(0, 1, 0), c, sum, count);
    Accumulate(cell - int3(0, 1, 0), c, sum, count);
    Accumulate(cell + int3(0, 0, 1), c, sum, count);
    Accumulate(cell - int3(0, 0, 1), c, sum, count);
    if (count > 0u)
        return sum / float(count);
    return 0.0f;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);

    float4 input = VelocityIn.Load(int4(cell, 0));
    float u = input.x;
    float v = input.y;
    float w = input.z;

    if (!IsSolidFace(cell, 0) && !IsLiquidFace(cell, 0))
        u = ExtrapolateComponent(cell, 0);
    if (!IsSolidFace(cell, 1) && !IsLiquidFace(cell, 1))
        v = ExtrapolateComponent(cell, 1);
    if (!IsSolidFace(cell, 2) && !IsLiquidFace(cell, 2))
        w = ExtrapolateComponent(cell, 2);

    VelocityOut[id] = float4(u, v, w, 0.0f);
}
