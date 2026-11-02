// T850 volumetric compute diagnostic: reads the R32F 3D storage texture written by
// CS_VolumeFloatPatternWrite and accumulates mismatch and visited-voxel counts. The
// value must survive to fp32 precision, which is what an R32F binding is for.

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
Texture3D<float> Input : register(t0);

#if defined(T850_VULKAN) || defined(T850_SPIRV)
[[vk::binding(2, 0)]]
#endif
// Scalar elements: Tint's SPIR-V reader crashes on InterlockedAdd applied to a vector
// component, and WebGPU has no fallback for that shader.
RWStructuredBuffer<uint> Result : register(u0);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID) {
  if (id.x >= Width || id.y >= Height || id.z >= Depth)
    return;
  float value = Input.Load(int4(id, 0));
  uint expected = (id.x * 3u + id.y * 11u + id.z * 131u + Seed) % 64u;
  float expectedValue = float(expected) + 0.3333333432674408f;
  // fp16 storage rounds by at least 1/64 near 64, so 1e-4 separates the two formats.
  uint mismatched = abs(value - expectedValue) > 1e-4f ? 1u : 0u;
  InterlockedAdd(Result[0], mismatched);
  InterlockedAdd(Result[1], 1u);
}
