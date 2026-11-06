// T850 pool fluid: semi-Lagrangian advection of a staggered MAC velocity volume.
// Each texel stores the three positive-side face velocities; every component is
// backtraced from its own face position and resampled on its own staggered
// lattice. Solid faces are zeroed and left to the boundary kernel.

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
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> VelocityOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

uint CellIndex(int3 cell)
{
    return uint(cell.x) + Width * (uint(cell.y) + Height * uint(cell.z));
}

bool IsSolidCell(int3 cell)
{
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return true;
    return WallMask[CellIndex(cell)] != 0u;
}

// Face between A and B = A + e_c is solid if either cell is solid.
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

// Staggered texel lookup for component c at integer index n.
// Along axis c an out-of-range index is a wall face and returns 0;
// the other two axes clamp into range.
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

float3 ComponentOffset(int c)
{
    if (c == 0) return float3(1.0f, 0.5f, 0.5f);
    if (c == 1) return float3(0.5f, 1.0f, 0.5f);
    return float3(0.5f, 0.5f, 1.0f);
}

// Trilinear sample of component c at arbitrary position P (cell units).
float SampleComponent(float3 p, int c)
{
    float3 l = p - ComponentOffset(c);
    float3 base = floor(l);
    float3 f = l - base;
    int3 i0 = int3(base);
    float c000 = FetchComponent(i0 + int3(0, 0, 0), c);
    float c100 = FetchComponent(i0 + int3(1, 0, 0), c);
    float c010 = FetchComponent(i0 + int3(0, 1, 0), c);
    float c110 = FetchComponent(i0 + int3(1, 1, 0), c);
    float c001 = FetchComponent(i0 + int3(0, 0, 1), c);
    float c101 = FetchComponent(i0 + int3(1, 0, 1), c);
    float c011 = FetchComponent(i0 + int3(0, 1, 1), c);
    float c111 = FetchComponent(i0 + int3(1, 1, 1), c);
    float x00 = lerp(c000, c100, f.x);
    float x10 = lerp(c010, c110, f.x);
    float x01 = lerp(c001, c101, f.x);
    float x11 = lerp(c011, c111, f.x);
    float y0 = lerp(x00, x10, f.y);
    float y1 = lerp(x01, x11, f.y);
    return lerp(y0, y1, f.z);
}

float3 SampleVelocity(float3 p)
{
    return float3(SampleComponent(p, 0), SampleComponent(p, 1), SampleComponent(p, 2));
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);

    float u = 0.0f;
    float v = 0.0f;
    float w = 0.0f;

    if (!IsSolidFace(cell, 0))
    {
        float3 x = float3(float(cell.x) + 1.0f, float(cell.y) + 0.5f, float(cell.z) + 0.5f);
        float3 vel = SampleVelocity(x);
        float3 back = x - vel * (DeltaTime / CellSize);
        u = SampleComponent(back, 0);
    }
    if (!IsSolidFace(cell, 1))
    {
        float3 x = float3(float(cell.x) + 0.5f, float(cell.y) + 1.0f, float(cell.z) + 0.5f);
        float3 vel = SampleVelocity(x);
        float3 back = x - vel * (DeltaTime / CellSize);
        v = SampleComponent(back, 1);
    }
    if (!IsSolidFace(cell, 2))
    {
        float3 x = float3(float(cell.x) + 0.5f, float(cell.y) + 0.5f, float(cell.z) + 1.0f);
        float3 vel = SampleVelocity(x);
        float3 back = x - vel * (DeltaTime / CellSize);
        w = SampleComponent(back, 2);
    }

    VelocityOut[id] = float4(u, v, w, 0.0f);
}
