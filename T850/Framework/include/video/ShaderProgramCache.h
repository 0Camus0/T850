#pragma once

#include <cstddef>
#include <cstdint>
#include <thread>
#include <unordered_map>

namespace t850 {

class ShaderBase;

struct ShaderFamilyId {
  uint64_t vertex = 0;
  uint64_t fragment = 0;
  bool operator==(const ShaderFamilyId&) const = default;
};

enum class ShaderProgramFlow : uint64_t {
  Default = 0,
  D3D12Dxc,
  D3D12LegacyHlsl,
  WebGPUAuto,
  WebGPUWgsl,
  WebGPUSpirv
};

struct ShaderProgramKey {
  ShaderFamilyId family;
  uint64_t permutation = 0;
  ShaderProgramFlow flow = ShaderProgramFlow::Default;
  bool operator==(const ShaderProgramKey&) const = default;
};

struct ShaderProgramKeyHash {
  size_t operator()(const ShaderProgramKey& key) const noexcept;
};

class ShaderProgramCache {
public:
  // Render-thread-affine. The cache stores non-owning shader pointers whose
  // lifetime is controlled by the owning BaseDriver.
  ShaderBase* Find(const ShaderProgramKey& key) const;
  bool Insert(const ShaderProgramKey& key, ShaderBase* shader);
  bool Erase(const ShaderProgramKey& key);
  void Clear();
  size_t Size() const;

private:
  void AssertOwnerThread() const;
  std::thread::id m_ownerThread = std::this_thread::get_id();
  std::unordered_map<ShaderProgramKey, ShaderBase*, ShaderProgramKeyHash> m_programs;
};

} // namespace t850