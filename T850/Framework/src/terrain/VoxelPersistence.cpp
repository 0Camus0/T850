#include <pch.h>

#include <terrain/VoxelPersistence.h>

#include <utils/ResourceLocator.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

namespace t850::terrain {
namespace {

constexpr std::array<unsigned char, 8> kMagic = {'T', '8', 'V', 'O', 'X', 'D', 'L', '1'};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxEdits = 16u * 1024u * 1024u;
constexpr std::size_t kHeaderSize = 24;
constexpr std::size_t kRecordSize = 16;

void SetError(std::string* error, std::string message) {
  if (error) *error = std::move(message);
}

void AppendU16(std::vector<unsigned char>& bytes, uint16_t value) {
  bytes.push_back(static_cast<unsigned char>(value & 0xffu));
  bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xffu));
}

void AppendU32(std::vector<unsigned char>& bytes, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8)
    bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xffu));
}

void AppendU64(std::vector<unsigned char>& bytes, uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8)
    bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xffu));
}

bool ReadU16(std::span<const unsigned char> bytes, std::size_t& offset, uint16_t& value) {
  if (offset + 2 > bytes.size()) return false;
  value = static_cast<uint16_t>(bytes[offset]) |
      static_cast<uint16_t>(bytes[offset + 1] << 8);
  offset += 2;
  return true;
}

bool ReadU32(std::span<const unsigned char> bytes, std::size_t& offset, uint32_t& value) {
  if (offset + 4 > bytes.size()) return false;
  value = 0;
  for (int shift = 0; shift < 32; shift += 8)
    value |= static_cast<uint32_t>(bytes[offset++]) << shift;
  return true;
}

bool ReadU64(std::span<const unsigned char> bytes, std::size_t& offset, uint64_t& value) {
  if (offset + 8 > bytes.size()) return false;
  value = 0;
  for (int shift = 0; shift < 64; shift += 8)
    value |= static_cast<uint64_t>(bytes[offset++]) << shift;
  return true;
}

uint64_t HashBytes(std::span<const unsigned char> bytes) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

} // namespace

std::size_t VoxelEditKeyHash::operator()(const VoxelEditKey& key) const noexcept {
  std::size_t hash = static_cast<uint32_t>(key.x) * 0x9e3779b1u;
  hash ^= static_cast<uint32_t>(key.y) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  hash ^= static_cast<uint32_t>(key.z) + 0x85ebca6bu + (hash << 6) + (hash >> 2);
  return hash;
}

bool VoxelDeltaStore::Record(int32_t x, int32_t y, int32_t z, BlockId block) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const VoxelEditKey key{x, y, z};
  const auto found = m_edits.find(key);
  if (found != m_edits.end() && found->second == block) return false;
  m_edits.insert_or_assign(key, block);
  return true;
}

bool VoxelDeltaStore::Find(int32_t x, int32_t y, int32_t z, BlockId& block) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto found = m_edits.find(VoxelEditKey{x, y, z});
  if (found == m_edits.end()) return false;
  block = found->second;
  return true;
}

void VoxelDeltaStore::ApplyToChunk(VoxelChunk& chunk) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  const ChunkDimensions dimensions = chunk.Dimensions();
  const ChunkKey key = chunk.Key();
  const int64_t originX = static_cast<int64_t>(key.x) * dimensions.x;
  const int64_t originY = static_cast<int64_t>(key.y) * dimensions.y;
  const int64_t originZ = static_cast<int64_t>(key.z) * dimensions.z;
  for (const auto& [edit, block] : m_edits) {
    const int64_t localX = static_cast<int64_t>(edit.x) - originX;
    const int64_t localY = static_cast<int64_t>(edit.y) - originY;
    const int64_t localZ = static_cast<int64_t>(edit.z) - originZ;
    if (localX >= 0 && localY >= 0 && localZ >= 0 &&
        localX < dimensions.x && localY < dimensions.y && localZ < dimensions.z) {
      chunk.Set(static_cast<int>(localX), static_cast<int>(localY), static_cast<int>(localZ), block);
    }
  }
}

void VoxelDeltaStore::Clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_edits.clear();
}

std::size_t VoxelDeltaStore::Count() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_edits.size();
}

bool VoxelDeltaStore::Save(const std::string& path, std::string* error) const {
  std::vector<std::pair<VoxelEditKey, BlockId>> edits;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    edits.assign(m_edits.begin(), m_edits.end());
  }
  std::sort(edits.begin(), edits.end(), [](const auto& left, const auto& right) {
    if (left.first.x != right.first.x) return left.first.x < right.first.x;
    if (left.first.y != right.first.y) return left.first.y < right.first.y;
    return left.first.z < right.first.z;
  });
  if (edits.size() > kMaxEdits) {
    SetError(error, "voxel delta store exceeds the supported edit count");
    return false;
  }

  std::vector<unsigned char> payload;
  payload.reserve(edits.size() * kRecordSize);
  for (const auto& [key, block] : edits) {
    AppendU32(payload, static_cast<uint32_t>(key.x));
    AppendU32(payload, static_cast<uint32_t>(key.y));
    AppendU32(payload, static_cast<uint32_t>(key.z));
    AppendU16(payload, block);
    AppendU16(payload, 0);
  }
  std::vector<unsigned char> bytes;
  bytes.reserve(kHeaderSize + payload.size());
  bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
  AppendU32(bytes, kVersion);
  AppendU32(bytes, static_cast<uint32_t>(edits.size()));
  AppendU64(bytes, HashBytes(payload));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  if (!ResourceLocator::Instance().WriteBinaryAtomic(path, bytes)) {
    SetError(error, "failed to atomically write voxel delta store: " + path);
    return false;
  }
  return true;
}

bool VoxelDeltaStore::Load(const std::string& path, std::string* error) {
  std::vector<unsigned char> bytes;
  if (!ResourceLocator::Instance().ReadBinary(path, bytes)) {
    SetError(error, "voxel delta store is unreadable: " + path);
    return false;
  }
  if (bytes.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    SetError(error, "voxel delta store has an invalid header");
    return false;
  }
  std::size_t offset = kMagic.size();
  uint32_t version = 0;
  uint32_t count = 0;
  uint64_t expectedHash = 0;
  if (!ReadU32(bytes, offset, version) || !ReadU32(bytes, offset, count) ||
      !ReadU64(bytes, offset, expectedHash) || version != kVersion || count > kMaxEdits) {
    SetError(error, "voxel delta store has an unsupported version or count");
    return false;
  }
  const std::size_t expectedSize = kHeaderSize + static_cast<std::size_t>(count) * kRecordSize;
  if (bytes.size() != expectedSize || HashBytes(std::span(bytes).subspan(kHeaderSize)) != expectedHash) {
    SetError(error, "voxel delta store is truncated or corrupt");
    return false;
  }

  std::unordered_map<VoxelEditKey, BlockId, VoxelEditKeyHash> loaded;
  loaded.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    uint16_t block = 0;
    uint16_t reserved = 0;
    if (!ReadU32(bytes, offset, x) || !ReadU32(bytes, offset, y) || !ReadU32(bytes, offset, z) ||
        !ReadU16(bytes, offset, block) || !ReadU16(bytes, offset, reserved)) {
      SetError(error, "voxel delta store ended inside a record");
      return false;
    }
    loaded.insert_or_assign(
        VoxelEditKey{static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)},
        static_cast<BlockId>(block));
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_edits = std::move(loaded);
  }
  return true;
}

} // namespace t850::terrain
