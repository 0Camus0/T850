#include <pch.h>
#include <video/ShaderProgramCache.h>

namespace t850 {

size_t ShaderProgramKeyHash::operator()(const ShaderProgramKey& key) const noexcept {
  size_t hash = static_cast<size_t>(key.family.vertex);
  const auto combine = [&](uint64_t value) {
    hash ^= static_cast<size_t>(value) + static_cast<size_t>(0x9e3779b97f4a7c15ull) +
            (hash << 6) + (hash >> 2);
  };
  combine(key.family.fragment);
  combine(key.permutation);
  combine(static_cast<uint64_t>(key.flow));
  return hash;
}

ShaderBase* ShaderProgramCache::Find(const ShaderProgramKey& key) const {
  const auto it = m_programs.find(key);
  return it != m_programs.end() ? it->second : nullptr;
}

bool ShaderProgramCache::Insert(const ShaderProgramKey& key, ShaderBase* shader) {
  return shader && m_programs.emplace(key, shader).second;
}

bool ShaderProgramCache::Erase(const ShaderProgramKey& key) {
  return m_programs.erase(key) != 0;
}

void ShaderProgramCache::Clear() {
  m_programs.clear();
}

} // namespace t850