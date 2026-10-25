// T850 pool fluid: summarizes the velocity volume into a small structured
// buffer so the CPU can assert the solver stays finite and bounded.

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
RWStructuredBuffer<uint4> Result : register(u0);

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    float3 velocity = VelocityIn.Load(int4(id, 0)).xyz;
    const bool finite = all(isfinite(velocity));
    const float speed = finite ? length(velocity) : 0.0f;
    const uint fixed = (uint)min(speed * 1000.0f, 65535.0f);
    // Bias +X so a signed sum stays non-negative for the atomic accumulator.
    const uint fixedX = (uint)clamp((velocity.x + 8.0f) * 1000.0f, 0.0f, 16000.0f);
    InterlockedAdd(Result[0].x, 1u);
    InterlockedAdd(Result[0].y, fixed);
    InterlockedMax(Result[0].z, fixed);
    if (!finite)
        InterlockedAdd(Result[0].w, 1u);
    InterlockedAdd(Result[1].x, fixedX);
}
