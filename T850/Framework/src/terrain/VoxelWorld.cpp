#include <pch.h>

#include <terrain/VoxelWorld.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace t850::terrain {

std::size_t ChunkKeyHash::operator()(const ChunkKey& key) const noexcept {
  std::size_t hash = static_cast<uint32_t>(key.x) * 0x9e3779b1u;
  hash ^= static_cast<uint32_t>(key.y) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  hash ^= static_cast<uint32_t>(key.z) + 0x85ebca6bu + (hash << 6) + (hash >> 2);
  return hash;
}

VoxelWorld::VoxelWorld(ChunkDimensions dimensions) : m_dimensions(dimensions) {
  if (dimensions.x <= 0 || dimensions.y <= 0 || dimensions.z <= 0) {
    throw std::invalid_argument("voxel world chunk dimensions must be positive");
  }
}

VoxelChunk& VoxelWorld::EnsureChunk(ChunkKey key) {
  auto [iterator, inserted] = m_chunks.try_emplace(key);
  if (inserted) iterator->second = std::make_unique<VoxelChunk>(key, m_dimensions);
  return *iterator->second;
}

bool VoxelWorld::AdoptChunk(std::unique_ptr<VoxelChunk> chunk) {
  if (!chunk || chunk->Dimensions().x != m_dimensions.x ||
      chunk->Dimensions().y != m_dimensions.y || chunk->Dimensions().z != m_dimensions.z) {
    return false;
  }
  const ChunkKey key = chunk->Key();
  m_chunks.insert_or_assign(key, std::move(chunk));
  return true;
}

VoxelChunk* VoxelWorld::FindChunk(ChunkKey key) {
  const auto found = m_chunks.find(key);
  return found == m_chunks.end() ? nullptr : found->second.get();
}

const VoxelChunk* VoxelWorld::FindChunk(ChunkKey key) const {
  const auto found = m_chunks.find(key);
  return found == m_chunks.end() ? nullptr : found->second.get();
}

bool VoxelWorld::RemoveChunk(ChunkKey key) {
  return m_chunks.erase(key) != 0;
}

void VoxelWorld::Clear() {
  m_chunks.clear();
}

std::vector<ChunkKey> VoxelWorld::LoadedChunkKeys() const {
  std::vector<ChunkKey> keys;
  keys.reserve(m_chunks.size());
  for (const auto& [key, chunk] : m_chunks) {
    (void)chunk;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end(), [](const ChunkKey& left, const ChunkKey& right) {
    if (left.x != right.x) return left.x < right.x;
    if (left.y != right.y) return left.y < right.y;
    return left.z < right.z;
  });
  return keys;
}

int VoxelWorld::FloorDiv(int value, int divisor) {
  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder < 0) --quotient;
  return quotient;
}

int VoxelWorld::FloorMod(int value, int divisor) {
  const int remainder = value % divisor;
  return remainder < 0 ? remainder + divisor : remainder;
}

ChunkKey VoxelWorld::WorldToChunk(int worldX, int worldY, int worldZ) const {
  return ChunkKey{
      FloorDiv(worldX, m_dimensions.x),
      FloorDiv(worldY, m_dimensions.y),
      FloorDiv(worldZ, m_dimensions.z)};
}

void VoxelWorld::WorldToLocal(
    int worldX, int worldY, int worldZ, int& localX, int& localY, int& localZ) const {
  localX = FloorMod(worldX, m_dimensions.x);
  localY = FloorMod(worldY, m_dimensions.y);
  localZ = FloorMod(worldZ, m_dimensions.z);
}

BlockId VoxelWorld::GetBlock(int worldX, int worldY, int worldZ) const {
  const VoxelChunk* chunk = FindChunk(WorldToChunk(worldX, worldY, worldZ));
  if (!chunk) return kAirBlock;
  int localX = 0;
  int localY = 0;
  int localZ = 0;
  WorldToLocal(worldX, worldY, worldZ, localX, localY, localZ);
  return chunk->Get(localX, localY, localZ);
}

bool VoxelWorld::SetBlock(
    int worldX, int worldY, int worldZ, BlockId block, bool createChunk) {
  const ChunkKey key = WorldToChunk(worldX, worldY, worldZ);
  VoxelChunk* chunk = FindChunk(key);
  if (!chunk) {
    if (!createChunk || block == kAirBlock) return false;
    chunk = &EnsureChunk(key);
  }
  int localX = 0;
  int localY = 0;
  int localZ = 0;
  WorldToLocal(worldX, worldY, worldZ, localX, localY, localZ);
  return chunk->Set(localX, localY, localZ, block);
}

bool VoxelWorld::Raycast(const XVECTOR3& origin,
                         const XVECTOR3& direction,
                         float maxDistance,
                         const BlockRegistry& registry,
                         VoxelRayHit& hit) const {
  hit = VoxelRayHit{};
  if (!std::isfinite(maxDistance) || maxDistance < 0.0f) return false;
  XVECTOR3 rayDirection = direction;
  rayDirection.w = 0.0f;
  const float length = rayDirection.Length();
  if (!std::isfinite(length) || length <= 0.000001f) return false;
  rayDirection /= length;

  int x = static_cast<int>(std::floor(origin.x));
  int y = static_cast<int>(std::floor(origin.y));
  int z = static_cast<int>(std::floor(origin.z));
  int previousX = x;
  int previousY = y;
  int previousZ = z;
  const int stepX = rayDirection.x > 0.0f ? 1 : (rayDirection.x < 0.0f ? -1 : 0);
  const int stepY = rayDirection.y > 0.0f ? 1 : (rayDirection.y < 0.0f ? -1 : 0);
  const int stepZ = rayDirection.z > 0.0f ? 1 : (rayDirection.z < 0.0f ? -1 : 0);
  const float infinity = (std::numeric_limits<float>::infinity)();
  const float deltaX = stepX == 0 ? infinity : std::abs(1.0f / rayDirection.x);
  const float deltaY = stepY == 0 ? infinity : std::abs(1.0f / rayDirection.y);
  const float deltaZ = stepZ == 0 ? infinity : std::abs(1.0f / rayDirection.z);
  float maxX = stepX > 0 ? (static_cast<float>(x + 1) - origin.x) * deltaX
                         : (stepX < 0 ? (origin.x - static_cast<float>(x)) * deltaX : infinity);
  float maxY = stepY > 0 ? (static_cast<float>(y + 1) - origin.y) * deltaY
                         : (stepY < 0 ? (origin.y - static_cast<float>(y)) * deltaY : infinity);
  float maxZ = stepZ > 0 ? (static_cast<float>(z + 1) - origin.z) * deltaZ
                         : (stepZ < 0 ? (origin.z - static_cast<float>(z)) * deltaZ : infinity);
  float distance = 0.0f;

  for (;;) {
    const BlockId block = GetBlock(x, y, z);
    if (registry.Get(block).collidable) {
      hit.hit = true;
      hit.blockX = x;
      hit.blockY = y;
      hit.blockZ = z;
      hit.previousX = previousX;
      hit.previousY = previousY;
      hit.previousZ = previousZ;
      hit.block = block;
      hit.distance = distance;
      return true;
    }
    previousX = x;
    previousY = y;
    previousZ = z;
    if (maxX <= maxY && maxX <= maxZ) {
      distance = maxX;
      maxX += deltaX;
      x += stepX;
    } else if (maxY <= maxZ) {
      distance = maxY;
      maxY += deltaY;
      y += stepY;
    } else {
      distance = maxZ;
      maxZ += deltaZ;
      z += stepZ;
    }
    if (distance > maxDistance) return false;
  }
}

} // namespace t850::terrain
