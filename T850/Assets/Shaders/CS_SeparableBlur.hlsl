cbuffer BlurConstants : register(b0) {
    uint width;
    uint height;
    int directionX;
    int directionY;
};

Texture2D<float4> inputTexture : register(t0);
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_rgba16f]]
#endif
RWTexture2D<float4> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 threadId : SV_DispatchThreadID) {
    if (threadId.x >= width || threadId.y >= height) return;
    int2 pixel = int2(threadId.xy);
    int2 direction = int2(directionX, directionY);
    int2 maximum = int2(width, height) - 1;
    float4 result = inputTexture.Load(int3(clamp(pixel - 2 * direction, int2(0, 0), maximum), 0));
    result += 4.0 * inputTexture.Load(int3(clamp(pixel - direction, int2(0, 0), maximum), 0));
    result += 6.0 * inputTexture.Load(int3(pixel, 0));
    result += 4.0 * inputTexture.Load(int3(clamp(pixel + direction, int2(0, 0), maximum), 0));
    result += inputTexture.Load(int3(clamp(pixel + 2 * direction, int2(0, 0), maximum), 0));
    outputTexture[pixel] = result / 16.0;
}