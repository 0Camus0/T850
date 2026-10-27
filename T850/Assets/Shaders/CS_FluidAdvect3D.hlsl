// T850 pool fluid: semi-Lagrangian advection of a 3D velocity volume.
// Reads the previous velocity volume as a sampled texture and writes the
// advected result. Storage images are write-only by engine contract.

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

float4 SampleVelocity(int3 cell)
{
    cell = clamp(cell, int3(0, 0, 0), int3(Width - 1, Height - 1, Depth - 1));
    // A backtrace that lands inside a wall samples no velocity rather than the
    // wall's stale contents, so advection cannot drag fluid out of the basin.
    if (WallMask[CellIndex(cell)] != 0u)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    return VelocityIn.Load(int4(cell, 0));
}

float4 SampleVelocityTrilinear(float3 positionCells)
{
    float3 base = floor(positionCells);
    float3 frac = positionCells - base;
    int3 i0 = int3(base);
    float4 c000 = SampleVelocity(i0);
    float4 c100 = SampleVelocity(i0 + int3(1, 0, 0));
    float4 c010 = SampleVelocity(i0 + int3(0, 1, 0));
    float4 c110 = SampleVelocity(i0 + int3(1, 1, 0));
    float4 c001 = SampleVelocity(i0 + int3(0, 0, 1));
    float4 c101 = SampleVelocity(i0 + int3(1, 0, 1));
    float4 c011 = SampleVelocity(i0 + int3(0, 1, 1));
    float4 c111 = SampleVelocity(i0 + int3(1, 1, 1));
    float4 x00 = lerp(c000, c100, frac.x);
    float4 x10 = lerp(c010, c110, frac.x);
    float4 x01 = lerp(c001, c101, frac.x);
    float4 x11 = lerp(c011, c111, frac.x);
    float4 y0 = lerp(x00, x10, frac.y);
    float4 y1 = lerp(x01, x11, frac.y);
    return lerp(y0, y1, frac.z);
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    float3 cellPos = float3(id) + 0.5;
    float4 velocity = VelocityIn.Load(int4(id, 0));
    // Solid cells stay exactly zero: the wall is not advected.
    if (WallMask[CellIndex(int3(id))] != 0u) {
        VelocityOut[id] = float4(0.0f, 0.0f, 0.0f, velocity.w);
        return;
    }
    float3 backPos = cellPos - velocity.xyz * (DeltaTime / CellSize);
    float4 advected = SampleVelocityTrilinear(backPos - 0.5);
    VelocityOut[id] = float4(advected.xyz, velocity.w);
}
