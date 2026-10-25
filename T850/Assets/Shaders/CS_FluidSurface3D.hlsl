// T850 pool fluid: derives a renderable 2D surface from the 3D velocity
// volume. One output texel per water column: height, foam, slope X/Z.

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
Texture2D<float4> SurfaceIn : register(t1);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(3, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> SurfaceOut : register(u0);

float SurfaceHeight(int x, int z)
{
    if (x < 0 || z < 0 || x >= int(Width) || z >= int(Depth))
        return 0.0f;
    return SurfaceIn.Load(int3(x, z, 0)).x;
}

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.z >= Depth)
        return;

    const int surfaceY = clamp(int(SurfaceLevel * float(Height)), 1, int(Height) - 2);
    const float vertical = VelocityIn.Load(int4(int3(id.x, surfaceY, id.z), 0)).y;
    const float below = VelocityIn.Load(int4(int3(id.x, surfaceY - 1, id.z), 0)).y;

    // Wave equation driven by the 3D vertical velocity, damped and coupled to
    // the neighbouring columns so disturbances spread across the pool.
    const float center = SurfaceIn.Load(int3(id.x, id.z, 0)).x;
    const float laplacian =
        SurfaceHeight(int(id.x) + 1, int(id.z)) + SurfaceHeight(int(id.x) - 1, int(id.z)) +
        SurfaceHeight(int(id.x), int(id.z) + 1) + SurfaceHeight(int(id.x), int(id.z) - 1) -
        4.0f * center;

    float height = center + (vertical + below * 0.5f) * DeltaTime + laplacian * 0.18f;
    height *= saturate(1.0f - 1.6f * DeltaTime);
    height = clamp(height, -0.35f, 0.35f);

    const float slopeX = (SurfaceHeight(int(id.x) + 1, int(id.z)) - SurfaceHeight(int(id.x) - 1, int(id.z))) * 0.5f;
    const float slopeZ = (SurfaceHeight(int(id.x), int(id.z) + 1) - SurfaceHeight(int(id.x), int(id.z) - 1)) * 0.5f;
    const float foam = saturate(abs(vertical) * 1.5f + abs(height) * 4.0f);
    SurfaceOut[id.xy] = float4(height, foam, slopeX, slopeZ);
}
