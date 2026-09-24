#pragma once

#include <cstddef>
#include <cstdint>
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
  ShaderBase* Find(const ShaderProgramKey& key) const;
  bool Insert(const ShaderProgramKey& key, ShaderBase* shader);
  bool Erase(const ShaderProgramKey& key);
  void Clear();
  size_t Size() const { return m_programs.size(); }

private:
  std::unordered_map<ShaderProgramKey, ShaderBase*, ShaderProgramKeyHash> m_programs;
};

} // namespace t850