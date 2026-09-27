#include <pch.h>
#include <video/ShaderProgramCache.h>

#include <cassert>

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
  AssertOwnerThread();
  const auto it = m_programs.find(key);
  return it != m_programs.end() ? it->second : nullptr;
}

bool ShaderProgramCache::Insert(const ShaderProgramKey& key, ShaderBase* shader) {
  AssertOwnerThread();
  return shader && m_programs.emplace(key, shader).second;
}

bool ShaderProgramCache::Erase(const ShaderProgramKey& key) {
  AssertOwnerThread();
  return m_programs.erase(key) != 0;
}

void ShaderProgramCache::Clear() {
  AssertOwnerThread();
  m_programs.clear();
}

size_t ShaderProgramCache::Size() const {
  AssertOwnerThread();
  return m_programs.size();
}

void ShaderProgramCache::AssertOwnerThread() const {
  assert(m_ownerThread == std::this_thread::get_id() &&
         "ShaderProgramCache must be accessed from its owning render thread");
}

} // namespace t850