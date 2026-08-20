#pragma once

#include <terrain/VoxelChunk.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace t850::terrain {

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey& key) const noexcept;
};

struct VoxelRayHit {
  bool hit = false;
  int blockX = 0;
  int blockY = 0;
  int blockZ = 0;
  int previousX = 0;
  int previousY = 0;
  int previousZ = 0;
  BlockId block = kAirBlock;
  float distance = 0.0f;
};

class VoxelWorld {
public:
  explicit VoxelWorld(ChunkDimensions dimensions = {});

  const ChunkDimensions& Dimensions() const { return m_dimensions; }
  std::size_t ChunkCount() const { return m_chunks.size(); }
  VoxelChunk& EnsureChunk(ChunkKey key);
  bool AdoptChunk(std::unique_ptr<VoxelChunk> chunk);
  VoxelChunk* FindChunk(ChunkKey key);
  const VoxelChunk* FindChunk(ChunkKey key) const;
  bool RemoveChunk(ChunkKey key);
  void Clear();
  std::vector<ChunkKey> LoadedChunkKeys() const;

  BlockId GetBlock(int worldX, int worldY, int worldZ) const;
  bool SetBlock(int worldX, int worldY, int worldZ, BlockId block, bool createChunk = true);
  bool Raycast(const XVECTOR3& origin,
               const XVECTOR3& direction,
               float maxDistance,
               const BlockRegistry& registry,
               VoxelRayHit& hit) const;

  ChunkKey WorldToChunk(int worldX, int worldY, int worldZ) const;
  void WorldToLocal(int worldX, int worldY, int worldZ, int& localX, int& localY, int& localZ) const;

private:
  static int FloorDiv(int value, int divisor);
  static int FloorMod(int value, int divisor);

  ChunkDimensions m_dimensions;
  std::unordered_map<ChunkKey, std::unique_ptr<VoxelChunk>, ChunkKeyHash> m_chunks;
};

} // namespace t850::terrain
