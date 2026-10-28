// T850 pool fluid: reduce the per-column obstacle bookkeeping to two totals.
//
// Pass 1 writes, per column, how much water the obstacle removed and how much
// room that column's surface has to take it back. Pass 2 needs the sums of both.
// Doing the reduction on the GPU keeps it off the CPU: a readback here was a hard
// pipeline stall every frame, and it also came back zeroed, which silently made
// the whole displacement a no-op.
//
// Deliberately one thread: summing ~5000 floats serially is microseconds, and it
// avoids needing an atomic accumulator that would have to be zeroed every frame
// (and this engine's ComputeBuffer has no update path to zero it with).

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

// Per column: .x = volume removed by the body (cells), .y = surface free capacity.
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
StructuredBuffer<float2> Removed : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<float2> Totals : register(u0);

[numthreads(1, 1, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    const uint columns = Width * Depth;
    float removed = 0.0f;
    float freeCapacity = 0.0f;
    for (uint column = 0; column < columns; ++column) {
        const float2 value = Removed[column];
        removed += value.x;
        freeCapacity += value.y;
    }
    Totals[0] = float2(removed, freeCapacity);
}
