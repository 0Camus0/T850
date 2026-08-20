#pragma once

#include <utils/Picking.h>

#include <cstdint>
#include <string>
#include <vector>

namespace t850 {

struct MutableMeshVertex {
  XVECTOR3 position = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  XVECTOR3 normal = XVECTOR3(0.0f, 1.0f, 0.0f, 0.0f);
  float u = 0.0f;
  float v = 0.0f;
};

enum class MutableMeshAlphaMode : uint8_t {
  Opaque,
  Mask,
  Blend
};

struct MutableMeshMaterial {
  XVECTOR3 baseColor = XVECTOR3(0.5f, 0.5f, 0.5f, 1.0f);
  XVECTOR3 emissiveColor = XVECTOR3(0.0f, 0.0f, 0.0f, 1.0f);
  float metallic = 0.0f;
  float roughness = 0.8f;
  float alphaCutoff = 0.5f;
  MutableMeshAlphaMode alphaMode = MutableMeshAlphaMode::Opaque;
  bool doubleSided = false;
  bool usesBaseColorTexture = false;
};

struct MutableMeshSection {
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
  uint32_t materialIndex = 0;
};

struct MutableMeshSnapshot {
  uint64_t version = 0;
  std::vector<MutableMeshVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<MutableMeshSection> sections;
  std::vector<MutableMeshMaterial> materials;
  AABB localBounds;

  bool Empty() const { return vertices.empty() || indices.empty(); }
};

void RecalculateMutableMeshBounds(MutableMeshSnapshot& snapshot);
bool ValidateMutableMeshSnapshot(const MutableMeshSnapshot& snapshot, std::string* error = nullptr);

} // namespace t850
