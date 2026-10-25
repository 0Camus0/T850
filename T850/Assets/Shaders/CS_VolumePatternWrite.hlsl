// T850 volumetric compute diagnostic: writes a position-derived pattern into a
// 3D storage texture. Values are small integers so RGBA16F storage is exact.

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
[[spv::format_rgba16f]]
#endif
RWTexture3D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID) {
  if (id.x >= Width || id.y >= Height || id.z >= Depth)
    return;
  uint pattern = (id.x * 3u + id.y * 11u + id.z * 131u + Seed) % 64u;
  Output[id] = float4(float(pattern), float(id.z), float(id.y), float(id.x));
}