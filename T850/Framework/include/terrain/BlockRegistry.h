#pragma once

#include <scene/MutableMeshData.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace t850::terrain {

using BlockId = uint16_t;
inline constexpr BlockId kAirBlock = 0;

struct BlockDefinition {
  std::string name;
  XVECTOR3 color = XVECTOR3(0.5f, 0.5f, 0.5f, 1.0f);
  MutableMeshAlphaMode alphaMode = MutableMeshAlphaMode::Opaque;
  float metallic = 0.0f;
  float roughness = 0.8f;
  float alphaCutoff = 0.5f;
  float atlasU0 = 0.0f;
  float atlasV0 = 0.0f;
  float atlasU1 = 1.0f;
  float atlasV1 = 1.0f;
  // Optional per-face atlas rectangles. When hasPerFaceTextures is true the
  // mesher selects the top/side/bottom rect by face normal instead of the
  // single atlasU0/V0/U1/V1 rect. Defaults mirror the single-rect values.
  float topU0 = 0.0f, topV0 = 0.0f, topU1 = 1.0f, topV1 = 1.0f;
  float sideU0 = 0.0f, sideV0 = 0.0f, sideU1 = 1.0f, sideV1 = 1.0f;
  float bottomU0 = 0.0f, bottomV0 = 0.0f, bottomU1 = 1.0f, bottomV1 = 1.0f;
  bool hasPerFaceTextures = false;
  bool usesBaseColorTexture = false;
  bool renderable = true;
  bool occludes = true;
  bool collidable = true;
  bool doubleSided = false;
};

class BlockRegistry {
public:
  BlockRegistry();

  BlockId Register(BlockDefinition definition);
  const BlockDefinition& Get(BlockId id) const;
  BlockId Find(std::string_view name) const;
  std::size_t Count() const { return m_definitions.size(); }

private:
  std::vector<BlockDefinition> m_definitions;
  std::unordered_map<std::string, BlockId> m_nameToId;
};

} // namespace t850::terrain
