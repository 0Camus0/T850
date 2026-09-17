#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform BlurConstants {
    uvec4 OutputSizeKernelDirection;
    vec4 RadiusAndPadding;
    vec4 Weights[6];
};

layout(binding = 1) uniform sampler2D InputTexture;
layout(rgba8, binding = 3) writeonly uniform image2D OutputTexture;

float GetWeight(uint index)
{
    return Weights[index >> 2u][index & 3u];
}

void main()
{
    uvec2 dispatchId = gl_GlobalInvocationID.xy;
    uvec2 outputSize = OutputSizeKernelDirection.xy;
    if (any(greaterThanEqual(dispatchId, outputSize)))
        return;

    uint kernelSize = OutputSizeKernelDirection.z;
    uint directionIndex = OutputSizeKernelDirection.w;
    vec2 uv = (vec2(dispatchId) + vec2(0.5)) / vec2(outputSize);
    vec2 texelStep = RadiusAndPadding.x / vec2(max(textureSize(InputTexture, 0), ivec2(1)));
    vec2 direction = directionIndex == 0u ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    float origin = -((float(kernelSize) - 1.0) * 0.5);

    vec3 sum = vec3(0.0);
    for (uint index = 0u; index < kernelSize; ++index) {
        float offset = origin + float(index);
        sum += GetWeight(index) * textureLod(InputTexture, uv + direction * offset * texelStep, 0.0).rgb;
    }
    imageStore(OutputTexture, ivec2(dispatchId), vec4(sum, 1.0));
}