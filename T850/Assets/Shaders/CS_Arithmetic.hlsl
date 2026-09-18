#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer ArithmeticConstants : register(b0)
{
    uint ElementCount;
    uint Addend;
    uint Multiplier;
    uint XorMask;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
RWStructuredBuffer<uint> Output : register(u0);

#ifdef COMPUTE_READ_INPUT
#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
StructuredBuffer<uint> Input : register(t0);
#endif

[numthreads(64, 1, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= ElementCount)
        return;

#ifdef COMPUTE_READ_INPUT
    const uint value = (Input[dispatchId.x] + Addend) * Multiplier;
#else
    const uint value = (dispatchId.x + Addend) * Multiplier;
#endif
    Output[dispatchId.x] = value ^ XorMask;
}
