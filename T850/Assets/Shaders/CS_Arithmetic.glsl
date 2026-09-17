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

void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= ElementCount)
        return;

    Output[index] = ((index + Addend) * Multiplier) ^ XorMask;
}