// T850 volumetric compute diagnostic: validates a 3D storage texture written by
// CS_VolumePatternWrite and accumulates mismatch and visited-voxel counts.

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
Texture3D<float4> Input : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
RWStructuredBuffer<uint2> Result : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID) {
  if (id.x >= Width || id.y >= Height || id.z >= Depth)
    return;
  uint4 value = uint4(Input.Load(int4(id, 0)));
  uint expected = (id.x * 3u + id.y * 11u + id.z * 131u + Seed) % 64u;
  uint mismatched = value.x != expected || value.y != id.z || value.z != id.y || value.w != id.x ? 1u : 0u;
  InterlockedAdd(Result[0].x, mismatched);
  InterlockedAdd(Result[0].y, 1u);
}
