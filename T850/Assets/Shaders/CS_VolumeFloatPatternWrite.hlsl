// T850 volumetric compute diagnostic: writes a position-derived pattern into an R32F
// 3D storage texture. The fractional part is deliberately not representable in fp16, so
// a backend that quietly stores as RGBA16F fails the reader's tolerance check instead of
// passing with rounded values.

struct VolumeConstants {
  uint width;
  uint height;
  uint depth;
  uint seed;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(0, 0)]]
#endif
cbuffer VolumeConstantsBuffer : register(b0)
{
  uint Width;
  uint Height;
  uint Depth;
  uint Seed;
};

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(1, 0)]]
#endif
#ifdef T850_SPIRV
[[spv::nonreadable]]
[[spv::format_r32f]]
#endif
RWTexture3D<float> Output : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID) {
  if (id.x >= Width || id.y >= Height || id.z >= Depth)
    return;
  uint pattern = (id.x * 3u + id.y * 11u + id.z * 131u + Seed) % 64u;
  // 1/3 in single precision: fp16 would round it to ~0.33325 at best, and much worse
  // once added to a value near 64.
  Output[id] = float(pattern) + 0.3333333432674408f;
}
