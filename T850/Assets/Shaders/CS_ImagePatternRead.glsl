#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform ImagePatternConstants {
    uint Width;
    uint Height;
    uint Seed;
    uint Padding;
};

layout(binding = 1) uniform sampler2D InputTexture;
layout(std430, binding = 2) buffer ImagePatternOutput {
    uint Output[];
};

uint PackBytes(uvec4 value)
{
    return value.x | (value.y << 8u) | (value.z << 16u) | (value.w << 24u);
}

void main()
{
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x >= Width || pixel.y >= Height)
        return;
    uint index = pixel.y * Width + pixel.x;
    uvec4 bytes = uvec4(round(clamp(texelFetch(InputTexture, ivec2(pixel), 0), 0.0, 1.0) * 255.0));
    Output[index] = PackBytes(bytes);
}
