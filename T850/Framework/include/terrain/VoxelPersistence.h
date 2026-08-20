#pragma once

#include <terrain/VoxelChunk.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace t850::terrain {

struct VoxelEditKey {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
  friend bool operator==(const VoxelEditKey&, const VoxelEditKey&) = default;
};

struct VoxelEditKeyHash {
  std::size_t operator()(const VoxelEditKey& key) const noexcept;
};

class VoxelDeltaStore {
public:
  bool Record(int32_t x, int32_t y, int32_t z, BlockId block);
  bool Find(int32_t x, int32_t y, int32_t z, BlockId& block) const;
  void ApplyToChunk(VoxelChunk& chunk) const;
  void Clear();
  std::size_t Count() const;

  bool Save(const std::string& path, std::string* error = nullptr) const;
  bool Load(const std::string& path, std::string* error = nullptr);

private:
  mutable std::mutex m_mutex;
  std::unordered_map<VoxelEditKey, BlockId, VoxelEditKeyHash> m_edits;
};

} // namespace t850::terrain
