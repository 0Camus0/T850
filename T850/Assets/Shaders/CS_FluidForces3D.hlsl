// T850 pool fluid: viscosity, gravity and optional surface forcing on a
// staggered MAC velocity volume. Each component diffuses against its own
// same-component neighbours; solid faces are zeroed and left to the boundary
// kernel.

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
    // Sustained surface forcing gain, from JSON.
    float ForcingAmplitude;
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

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(4, 0)]]
#endif
Texture3D<float4> AlphaIn : register(t2);

// Wind stress on the water surface, tau = rho_air * Cd * |U| U, with U the wind speed in m/s.
static const float AIR_DENSITY = 1.2f;
static const float WIND_DRAG_COEFFICIENT = 1.3e-3f;
static const float WATER_DENSITY = 1000.0f;

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

float AlphaAt(int3 cell)
{
    if (IsSolidCell(cell))
        return 0.0f;
    return saturate(AlphaIn.Load(int4(cell, 0)).x);
}

// The horizontal face between `cell` and `cell + across` carries the wind stress when it is
// wet and the face above it is not: one face per column, wherever the waterline is.
bool IsTopWetFace(int3 cell, int3 across)
{
    const int3 up = int3(0, 1, 0);
    const float wet = 0.5f * (AlphaAt(cell) + AlphaAt(cell + across));
    const float above = 0.5f * (AlphaAt(cell + up) + AlphaAt(cell + across + up));
    return wet >= 0.5f && above < 0.5f;
}

// Same-component neighbour value; out-of-range texel contributes the centre value.
float NeighbourComponent(int3 n, int c, float centre)
{
    int3 dim = int3(Width, Height, Depth);
    if (c == 0)
    {
        if (n.x < 0 || n.x > dim.x - 1)
            return centre;
        n.y = clamp(n.y, 0, dim.y - 1);
        n.z = clamp(n.z, 0, dim.z - 1);
        return VelocityIn.Load(int4(n, 0)).x;
    }
    if (c == 1)
    {
        if (n.y < 0 || n.y > dim.y - 1)
            return centre;
        n.x = clamp(n.x, 0, dim.x - 1);
        n.z = clamp(n.z, 0, dim.z - 1);
        return VelocityIn.Load(int4(n, 0)).y;
    }
    if (n.z < 0 || n.z > dim.z - 1)
        return centre;
    n.x = clamp(n.x, 0, dim.x - 1);
    n.y = clamp(n.y, 0, dim.y - 1);
    return VelocityIn.Load(int4(n, 0)).z;
}

float LaplacianComponent(int3 cell, int c, float centre)
{
    float sum = 0.0f;
    sum += NeighbourComponent(cell + int3(1, 0, 0), c, centre);
    sum += NeighbourComponent(cell - int3(1, 0, 0), c, centre);
    sum += NeighbourComponent(cell + int3(0, 1, 0), c, centre);
    sum += NeighbourComponent(cell - int3(0, 1, 0), c, centre);
    sum += NeighbourComponent(cell + int3(0, 0, 1), c, centre);
    sum += NeighbourComponent(cell - int3(0, 0, 1), c, centre);
    return sum - 6.0f * centre;
}

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    int3 cell = int3(id);

    float4 centre = VelocityIn.Load(int4(cell, 0));
    float u = 0.0f;
    float v = 0.0f;
    float w = 0.0f;

    // Explicit diffusion: nu*dt/h^2, capped at the 3D stability limit of 1/6.
    const float diffusion = min(Viscosity * DeltaTime / (CellSize * CellSize), 1.0f / 6.0f);

    if (!IsSolidFace(cell, 0))
    {
        u = centre.x + LaplacianComponent(cell, 0, centre.x) * diffusion;
    }
    if (!IsSolidFace(cell, 1))
    {
        v = centre.y + LaplacianComponent(cell, 1, centre.y) * diffusion;
    }
    if (!IsSolidFace(cell, 2))
    {
        w = centre.z + LaplacianComponent(cell, 2, centre.z) * diffusion;
    }

    // Gravity acts on the vertical face velocity.
    if (!IsSolidFace(cell, 1))
        v -= Gravity * DeltaTime;

    // Wind: surface stress on the top wet layer, as an acceleration of that one-cell layer.
    // It used to be scaled by ForcingAmplitude, which ships at 0, so wind did nothing (W-27).
    const float windSpeed = length(float2(WindX, WindZ));
    if (windSpeed > 0.0f)
    {
        const float windAccel = AIR_DENSITY * WIND_DRAG_COEFFICIENT * windSpeed /
                                (WATER_DENSITY * CellSize);
        if (!IsSolidFace(cell, 0) && IsTopWetFace(cell, int3(1, 0, 0)))
            u += windAccel * WindX * DeltaTime;
        if (!IsSolidFace(cell, 2) && IsTopWetFace(cell, int3(0, 0, 1)))
            w += windAccel * WindZ * DeltaTime;
    }

    // Optional synthetic stir of the top rows (ForcingAmplitude, 0 in the shipped scene).
    const float surfaceY = SurfaceLevel * float(Height);
    if (float(id.y) > surfaceY - 2.0f)
    {
        const float gain = max(ForcingAmplitude, 0.0f) * DeltaTime;
        const float stirX = sin(TimeValue * 0.9f + float(id.x) * 0.31f + float(id.z) * 0.17f);
        const float stirZ = cos(TimeValue * 0.7f + float(id.z) * 0.29f + float(id.x) * 0.13f);
        if (!IsSolidFace(cell, 0))
            u += stirX * 0.45f * gain;
        if (!IsSolidFace(cell, 2))
            w += stirZ * 0.45f * gain;
        if (!IsSolidFace(cell, 1))
            v += stirX * stirZ * 0.25f * gain;
    }

    const float damp = saturate(1.0f - Damping * DeltaTime);
    u *= damp;
    v *= damp;
    w *= damp;
    u = clamp(u, -12.0f, 12.0f);
    v = clamp(v, -12.0f, 12.0f);
    w = clamp(w, -12.0f, 12.0f);

    VelocityOut[id] = float4(u, v, w, 0.0f);
}
