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
// Scalar elements rather than uint4: Tint's SPIR-V reader crashes on InterlockedAdd applied to a
// vector component (vector_element_pointer.cc) and WebGPU has no fallback, so every atomic
// accumulator in the solver is a flat uint array. Word order is the host's Diagnostics layout:
// 0 samples, 1 sum speed, 2 max speed, 3 non-finite, 4 sum of (x + 8).
RWStructuredBuffer<uint> Result : register(u0);

// Sentinel in the constants' Iteration slot: clear the accumulator instead of measuring.
// The buffer is persistent (no per-call allocation), and there is no buffer-write API, so the
// kernel clears its own accumulator before each accumulation.
#define T8_DIAGNOSTICS_RESET 0xFFFFFFFFu
// Not a loop: NVIDIA's GLSL compiler rejects a writable unsized array indexed by a loop induction
// variable ("error C5025: lvalue in array access too complex"), and the OpenGL twin for this
// kernel is generated straight from the HLSL by scripts/shaders/generate_gl_compute_twins.ps1.
#define T8_DIAGNOSTICS_WORDS 8u

[numthreads(4, 4, 4)]
void CS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height || id.z >= Depth)
        return;
    if (Iteration == T8_DIAGNOSTICS_RESET) {
        // One thread per buffer: every thread in the grid would race otherwise.
        if (id.x == 0u && id.y == 0u && id.z == 0u) {
            Result[0] = 0u;
            Result[1] = 0u;
            Result[2] = 0u;
            Result[3] = 0u;
            Result[4] = 0u;
            Result[5] = 0u;
            Result[6] = 0u;
            Result[7] = 0u;
        }
        return;
    }
    float3 velocity = VelocityIn.Load(int4(id, 0)).xyz;
    // `isfinite`, `isnan` and `isinf` all trip Tint's SPIR-V reader on WebGPU (a vector-element
    // pointer ICE that aborts the process), so finiteness is tested on the bit pattern instead:
    // a NaN or an infinity has the all-ones exponent.
    const uint3 bits = asuint(velocity);
    const bool finite = (bits.x & 0x7F800000u) != 0x7F800000u &&
                        (bits.y & 0x7F800000u) != 0x7F800000u &&
                        (bits.z & 0x7F800000u) != 0x7F800000u;
    const float speed = finite ? length(velocity) : 0.0f;
    const uint fixed = (uint)min(speed * 1000.0f, 65535.0f);
    // Bias +X so a signed sum stays non-negative for the atomic accumulator.
    const uint fixedX = (uint)clamp((velocity.x + 8.0f) * 1000.0f, 0.0f, 16000.0f);
    InterlockedAdd(Result[0], 1u);
    InterlockedAdd(Result[1], fixed);
    InterlockedMax(Result[2], fixed);
    if (!finite)
        InterlockedAdd(Result[3], 1u);
    InterlockedAdd(Result[4], fixedX);
}
