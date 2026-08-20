#pragma once

#include <terrain/BlockRegistry.h>

#include <cstdint>
#include <vector>

namespace t850::terrain {

struct ChunkKey {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
  friend bool operator==(const ChunkKey&, const ChunkKey&) = default;
};

struct ChunkDimensions {
  int x = 16;
  int y = 64;
  int z = 16;
};

class VoxelChunk {
public:
  explicit VoxelChunk(ChunkKey key = {}, ChunkDimensions dimensions = {});

  const ChunkKey& Key() const { return m_key; }
  const ChunkDimensions& Dimensions() const { return m_dimensions; }
  uint64_t Version() const { return m_version; }
  std::size_t BlockCount() const { return m_blocks.size(); }
  const std::vector<BlockId>& Blocks() const { return m_blocks; }

  bool InBounds(int x, int y, int z) const;
  BlockId Get(int x, int y, int z) const;
  bool Set(int x, int y, int z, BlockId block);
  void Fill(BlockId block);

private:
  std::size_t Index(int x, int y, int z) const;

  ChunkKey m_key;
  ChunkDimensions m_dimensions;
  std::vector<BlockId> m_blocks;
  uint64_t m_version = 1;
};

} // namespace t850::terrain
