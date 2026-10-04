#version 430 core

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(std140, binding = 0) uniform ImagePatternConstants {
    uint Width;
    uint Height;
    uint Seed;
    uint Padding;
};

layout(rgba8, binding = 1) writeonly uniform image2D OutputTexture;

uvec4 PatternBytes(uvec2 pixel)
{
    uint linearIndex = pixel.y * Width + pixel.x;
    return uvec4(
        (linearIndex * 17u + Seed) & 255u,
        (pixel.x * 29u + pixel.y * 11u + Seed * 3u) & 255u,
        (pixel.x * 7u + pixel.y * 31u + Seed * 5u) & 255u,
        255u);
}

void main()
{
    uvec2 pixel = gl_GlobalInvocationID.xy;
    if (pixel.x >= Width || pixel.y >= Height)
        return;
    imageStore(OutputTexture, ivec2(pixel), vec4(PatternBytes(pixel)) / 255.0);
}
