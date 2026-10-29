// T850 pool fluid: one red-black Gauss-Seidel pressure pass with SOR and a
// ghost-fluid free surface.
//
// Non-LIQUID cells write 0. LIQUID cells of the inactive colour copy their
// PressureIn value. LIQUID cells of the active colour accumulate the six
// neighbours: LIQUID contributes p_N and 1 to the diagonal, AIR contributes
// 1/theta to the diagonal only (the ghost value is folded in), SOLID contributes
// nothing (Neumann). The target is (sum - div*PressureScale)/diag, relaxed by
// SOR omega = Padding0.

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer FluidConstants : register(b0)
{
    uint Width;
    uint Height;
    uint Depth;
    // Packed as sweep * 2 + parity (nothing else reads this field).
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
    // Successive over-relaxation factor for this pass.
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
Texture3D<float4> PressureIn : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
Texture3D<float4> DivergenceIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> PressureOut : register(u0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
StructuredBuffer<uint> WallMask : register(t8);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(5, 0)]]
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

// The ceiling is the only open boundary, and only when the solver is told the rows
// above the domain are air. Cells directly above the top row are then air, not a lid;
// every other out-of-range direction is a genuine pool wall.
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

float AlphaAt(int3 cell)
{
    // Above the ceiling alpha is air by definition; HLSL Load() out of bounds is
    // undefined on Vulkan, so return explicitly instead of relying on it.
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 ||
        cell.x >= int(Width) || cell.y >= int(Height) || cell.z >= int(Depth))
        return 0.0f;
    return saturate(AlphaIn.Load(int4(cell, 0)).x);
}

// theta for the interface between liquid cell L and air cell N.
float Theta(float aL, float aN)
{
    return clamp((aL - 0.5f) / max(aL - aN, 1e-4f), 0.1f, 1.0f);
}

// Accumulate one neighbour into sum/diag. Returns nothing; mutates by reference.
void AccumulateNeighbour(int3 n, float aC, inout float sum, inout float diag)
{
    // Open ceiling: a Dirichlet p = 0 boundary. theta is forced to 1 rather than
    // derived from the alpha gradient, because the cell above the domain has no
    // stored alpha -- when the top row is fully wet (alpha = 1) there is no air
    // cell to compute theta against, and the 0.1 clamp would over-soften the
    // diagonal and produce a checkerboard at the lid. diag += 1 also anchors the
    // matrix, so it stays non-singular even with an entirely wet top row.
    if (OpenCeiling > 0.5f && IsAboveTop(n))
    {
        diag += 1.0f;
        return;
    }
    if (IsSolid(n))
        return; // Neumann: face not part of the stencil
    if (IsLiquid(n))
    {
        sum += PressureIn.Load(int4(n, 0)).x;
        diag += 1.0f;
        return;
    }
    // AIR neighbour: ghost value folded into the diagonal.
    const float theta = Theta(aC, AlphaAt(n));
    diag += 1.0f / theta;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;

    int3 cell = int3(id);
    if (!IsLiquid(cell))
    {
        PressureOut[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float center = PressureIn.Load(int4(cell, 0)).x;

    const uint parity = Iteration & 1u;
    if (((cell.x + cell.y + cell.z) & 1) != int(parity))
    {
        PressureOut[id] = float4(center, 0.0f, 0.0f, 0.0f);
        return;
    }

    const float aC = AlphaAt(cell);
    float sum = 0.0f;
    float diag = 0.0f;

    AccumulateNeighbour(cell + int3(1, 0, 0), aC, sum, diag);
    AccumulateNeighbour(cell + int3(-1, 0, 0), aC, sum, diag);
    AccumulateNeighbour(cell + int3(0, 1, 0), aC, sum, diag);
    AccumulateNeighbour(cell + int3(0, -1, 0), aC, sum, diag);
    AccumulateNeighbour(cell + int3(0, 0, 1), aC, sum, diag);
    AccumulateNeighbour(cell + int3(0, 0, -1), aC, sum, diag);

    const float divergence = DivergenceIn.Load(int4(cell, 0)).x;
    const float target = diag > 0.0f ? (sum - divergence * PressureScale) / diag : 0.0f;
    const float omega = clamp(Padding0, 0.05f, 1.99f);
    const float pressure = lerp(center, target, omega);
    PressureOut[id] = float4(pressure, 0.0f, 0.0f, 0.0f);
}
