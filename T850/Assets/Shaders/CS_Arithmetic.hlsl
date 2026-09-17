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

[numthreads(64, 1, 1)]
void CS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= ElementCount)
        return;

    const uint value = (dispatchId.x + Addend) * Multiplier;
    Output[dispatchId.x] = value ^ XorMask;
}
