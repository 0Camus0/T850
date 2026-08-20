#include <pch.h>

#include <terrain/VoxelChunk.h>

#include <algorithm>
#include <stdexcept>

namespace t850::terrain {

VoxelChunk::VoxelChunk(ChunkKey key, ChunkDimensions dimensions)
    : m_key(key), m_dimensions(dimensions) {
  if (dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0) {
    throw std::invalid_argument("voxel chunk dimensions must be positive");
  }
  m_blocks.resize(
      static_cast<std::size_t>(dimensions.x) * dimensions.y * dimensions.z,
      kAirBlock);
}

bool VoxelChunk::InBounds(int x, int y, int z) const {
  return x >= 0 && y >= 0 && z >= 0 &&
      x < m_dimensions.x && y < m_dimensions.y && z < m_dimensions.z;
}

std::size_t VoxelChunk::Index(int x, int y, int z) const {
  return static_cast<std::size_t>(x) + static_cast<std::size_t>(m_dimensions.x) *
      (static_cast<std::size_t>(z) + static_cast<std::size_t>(m_dimensions.z) * y);
}

BlockId VoxelChunk::Get(int x, int y, int z) const {
  return InBounds(x, y, z) ? m_blocks[Index(x, y, z)] : kAirBlock;
}

bool VoxelChunk::Set(int x, int y, int z, BlockId block) {
  if (!InBounds(x, y, z)) return false;
  BlockId& current = m_blocks[Index(x, y, z)];
  if (current == block) return false;
  current = block;
  ++m_version;
  if (m_version == 0) m_version = 1;
  return true;
}

void VoxelChunk::Fill(BlockId block) {
  if (std::all_of(m_blocks.begin(), m_blocks.end(), [block](BlockId current) { return current == block; })) {
    return;
  }
  std::fill(m_blocks.begin(), m_blocks.end(), block);
  ++m_version;
  if (m_version == 0) m_version = 1;
}

} // namespace t850::terrain
