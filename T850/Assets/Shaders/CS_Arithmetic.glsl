#version 430 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std140, binding = 0) uniform ArithmeticConstants {
    uint ElementCount;
    uint Addend;
    uint Multiplier;
    uint XorMask;
};

layout(std430, binding = 1) buffer ArithmeticOutput {
    uint Output[];
};

#ifdef COMPUTE_READ_INPUT
layout(std430, binding = 2) readonly buffer ArithmeticInput {
    uint Input[];
};
#endif

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= ElementCount)
        return;

#ifdef COMPUTE_READ_INPUT
    Output[index] = ((Input[index] + Addend) * Multiplier) ^ XorMask;
#else
    Output[index] = ((index + Addend) * Multiplier) ^ XorMask;
#endif
}