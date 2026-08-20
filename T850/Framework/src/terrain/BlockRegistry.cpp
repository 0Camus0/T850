#include <pch.h>

#include <terrain/BlockRegistry.h>

#include <limits>

namespace t850::terrain {

BlockRegistry::BlockRegistry() {
  BlockDefinition air;
  air.name = "air";
  air.color = XVECTOR3(0.0f, 0.0f, 0.0f, 0.0f);
  air.renderable = false;
  air.occludes = false;
  air.collidable = false;
  m_definitions.push_back(air);
  m_nameToId.emplace(air.name, kAirBlock);
}

BlockId BlockRegistry::Register(BlockDefinition definition) {
  if (definition.name.empty()) return kAirBlock;
  const auto existing = m_nameToId.find(definition.name);
  if (existing != m_nameToId.end()) return existing->second;
  if (m_definitions.size() >= static_cast<std::size_t>((std::numeric_limits<BlockId>::max)())) {
    return kAirBlock;
  }
  const BlockId id = static_cast<BlockId>(m_definitions.size());
  m_nameToId.emplace(definition.name, id);
  m_definitions.push_back(std::move(definition));
  return id;
}

const BlockDefinition& BlockRegistry::Get(BlockId id) const {
  return id < m_definitions.size() ? m_definitions[id] : m_definitions[kAirBlock];
}

BlockId BlockRegistry::Find(std::string_view name) const {
  const auto found = m_nameToId.find(std::string(name));
  return found == m_nameToId.end() ? kAirBlock : found->second;
}

} // namespace t850::terrain
