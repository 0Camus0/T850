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
