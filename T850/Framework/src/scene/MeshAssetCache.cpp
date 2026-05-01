#include <pch.h>

#include <scene/MeshAssetCache.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <algorithm>
#include <cctype>

namespace t850 {

  MeshAssetCache& MeshAssetCache::Get() {
    static MeshAssetCache instance;
    return instance;
  }

  MeshAssetCache::~MeshAssetCache() {
    Clear();
  }

  std::string MeshAssetCache::Normalize(const std::string& path) {
    // Lower-case + forward-slashes. Identical assets referenced via
    // mixed case / mixed separators must collapse to one entry.
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
      if (c == '\\') c = '/';
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
  }

  MeshAsset* MeshAssetCache::Acquire(const std::string& sourcePath, bool* outCreated) {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::string key = Normalize(sourcePath);

    auto it = m_assets.find(key);
    if (it != m_assets.end()) {
      it->second->refCount++;
      if (outCreated) *outCreated = false;
      return it->second.get();
    }

    auto asset = std::make_unique<MeshAsset>();
    asset->sourcePath = sourcePath;
    asset->refCount = 1;
    MeshAsset* raw = asset.get();
    m_assets.emplace(std::move(key), std::move(asset));
    if (outCreated) *outCreated = true;
    return raw;
  }

  MeshAsset* MeshAssetCache::Find(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_assets.find(Normalize(sourcePath));
    return it == m_assets.end() ? nullptr : it->second.get();
  }

  void MeshAssetCache::Release(MeshAsset* asset) {
    if (!asset) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    if (asset->refCount == 0) {
      T8_LOG_ERROR("[MeshAssetCache] Release on asset '%s' with refCount==0",
                   asset->sourcePath.c_str());
      return;
    }
    asset->refCount--;
    if (asset->refCount > 0) return;

    auto it = m_assets.find(Normalize(asset->sourcePath));
    if (it == m_assets.end()) return;
    DestroyAsset(it->second.get());
    m_assets.erase(it);
  }

  void MeshAssetCache::DestroyAsset(MeshAsset* asset) {
    if (!asset) return;
    asset->submeshes.clear();
    // GPU buffers (VertexPool/IndexPool) live on the cache itself,
    // outliving any single asset. They are torn down in Clear().
  }

  std::size_t MeshAssetCache::Size() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_assets.size();
  }

  void MeshAssetCache::DumpToLog() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    T8_LOG_INFO("[MeshAssetCache] %zu asset(s):", m_assets.size());
    for (const auto& kv : m_assets) {
      const MeshAsset* a = kv.second.get();
      T8_LOG_INFO("  '%s' refs=%u submeshes=%zu verts=%u idx=%u stride=%u",
                  a->sourcePath.c_str(), a->refCount, a->submeshes.size(),
                  a->vertexCount, a->indexCount, a->vertexStride);
    }
    T8_LOG_INFO("[MeshAssetCache] %zu vertex pool(s), %zu index pool(s):",
                m_vertexPools.size(), m_indexPools.size());
    for (std::size_t i = 0; i < m_vertexPools.size(); ++i) {
      const VertexPool* p = m_vertexPools[i].get();
      T8_LOG_INFO("  VPool[%zu] format=0x%016llX stride=%u verts=%u (%u bytes)",
                  i,
                  static_cast<unsigned long long>(p->FormatHash()),
                  p->VertexStride(), p->VertexCount(), p->UsedBytes());
    }
    for (std::size_t i = 0; i < m_indexPools.size(); ++i) {
      const IndexPool* p = m_indexPools[i].get();
      T8_LOG_INFO("  IPool[%zu] %s indices=%u (%u bytes)",
                  i, p->Is32Bit() ? "32-bit" : "16-bit",
                  p->IndexCount(), p->UsedBytes());
    }
  }

  // ── Pool API ────────────────────────────────────────────────────────

  uint64_t MeshAssetCache::VertexPoolKey(uint64_t formatHash, uint32_t vertexStride) {
    // Combine attribute mask and stride into a single dedup key. Two
    // pools with identical mask but different strides (shouldn't
    // happen) get separate slots.
    return formatHash ^ (static_cast<uint64_t>(vertexStride) * 0x9E3779B97F4A7C15ull);
  }

  VertexPool* MeshAssetCache::GetOrCreateVertexPool(uint64_t formatHash, uint32_t vertexStride, uint32_t* outPoolId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    uint64_t key = VertexPoolKey(formatHash, vertexStride);
    auto it = m_vertexPoolIndex.find(key);
    if (it != m_vertexPoolIndex.end()) {
      if (outPoolId) *outPoolId = it->second;
      return m_vertexPools[it->second].get();
    }
    uint32_t newId = static_cast<uint32_t>(m_vertexPools.size());
    m_vertexPools.emplace_back(std::make_unique<VertexPool>(formatHash, vertexStride));
    m_vertexPoolIndex.emplace(key, newId);
    if (outPoolId) *outPoolId = newId;
    T8_LOG_INFO("[MeshAssetCache] Created VertexPool[%u] format=0x%016llX stride=%u",
                newId, static_cast<unsigned long long>(formatHash), vertexStride);
    return m_vertexPools.back().get();
  }

  IndexPool* MeshAssetCache::GetOrCreateIndexPool(bool ib32Bit, uint32_t* outPoolId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    int32_t& slot = ib32Bit ? m_indexPool32 : m_indexPool16;
    if (slot >= 0) {
      if (outPoolId) *outPoolId = static_cast<uint32_t>(slot);
      return m_indexPools[slot].get();
    }
    uint32_t newId = static_cast<uint32_t>(m_indexPools.size());
    m_indexPools.emplace_back(std::make_unique<IndexPool>(ib32Bit));
    slot = static_cast<int32_t>(newId);
    if (outPoolId) *outPoolId = newId;
    T8_LOG_INFO("[MeshAssetCache] Created IndexPool[%u] %s", newId, ib32Bit ? "32-bit" : "16-bit");
    return m_indexPools.back().get();
  }

  VertexPool* MeshAssetCache::GetVertexPool(uint32_t poolId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (poolId >= m_vertexPools.size()) return nullptr;
    return m_vertexPools[poolId].get();
  }

  IndexPool* MeshAssetCache::GetIndexPool(uint32_t poolId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (poolId >= m_indexPools.size()) return nullptr;
    return m_indexPools[poolId].get();
  }

  void MeshAssetCache::Clear() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& kv : m_assets) {
      DestroyAsset(kv.second.get());
    }
    m_assets.clear();
    m_vertexPools.clear();
    m_vertexPoolIndex.clear();
    m_indexPools.clear();
    m_indexPool16 = -1;
    m_indexPool32 = -1;
  }

}
