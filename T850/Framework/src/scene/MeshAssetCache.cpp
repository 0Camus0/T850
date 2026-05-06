#include <pch.h>

#include <scene/MeshAssetCache.h>
#include <video/BaseDriver.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace t850 {

  namespace {
    constexpr std::array<char, 8> kMeshPreprocessCacheMagic = { 'T', '8', 'M', 'C', 'A', 'C', 'H', 'E' };
    constexpr uint32_t kMeshPreprocessCacheVersion = 2;
    constexpr uint32_t kMeshPreprocessCacheHeaderSize = 108;
    constexpr uint32_t kMaxCachedSubmeshes = 1000000;
    constexpr uint32_t kMaxCachedClusters = 10000000;

    template <typename T>
    bool ReadPod(std::ifstream& file, T& value) {
      file.read(reinterpret_cast<char*>(&value), sizeof(T));
      return file.good();
    }

    template <typename T>
    void WritePod(std::ofstream& file, const T& value) {
      file.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    uint64_t HashValue(uint64_t hash, uint64_t value) {
      for (int i = 0; i < 8; ++i) {
        hash ^= static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
        hash *= 0x100000001b3ull;
      }
      return hash;
    }

    uint64_t HashString(uint64_t hash, const std::string& value) {
      for (unsigned char c : value) {
        hash ^= c;
        hash *= 0x100000001b3ull;
      }
      return hash;
    }

    uint64_t BuildPreprocessTopologyHash(uint64_t vertexAttribMask,
                                         uint32_t vertexStride,
                                         uint32_t vertexCount,
                                         uint32_t indexCount,
                                         const std::vector<Submesh>& submeshes) {
      uint64_t hash = 0xcbf29ce484222325ull;
      hash = HashValue(hash, vertexAttribMask);
      hash = HashValue(hash, vertexStride);
      hash = HashValue(hash, vertexCount);
      hash = HashValue(hash, indexCount);
      hash = HashValue(hash, static_cast<uint64_t>(submeshes.size()));
      for (const Submesh& submesh : submeshes) {
        hash = HashValue(hash, submesh.vertexCount);
        hash = HashValue(hash, submesh.triangleCount);
        hash = HashValue(hash, submesh.ib32Bit ? 1u : 0u);
        hash = HashValue(hash, submesh.vertexAttribKey.bits & ShaderKey::VERTEX_ATTRIB_MASK);
      }
      return hash;
    }

    uint64_t BuildPreprocessTopologyHash(const MeshAsset& asset) {
      return BuildPreprocessTopologyHash(asset.vertexAttribMask,
                                         asset.vertexStride,
                                         asset.vertexCount,
                                         asset.indexCount,
                                         asset.submeshes);
    }

    bool SourceSignature(const std::string& sourcePath, uint64_t& outSize, int64_t& outWriteTicks) {
      std::error_code ec;
      const std::filesystem::path path(sourcePath);
      if (!std::filesystem::exists(path, ec))
        return false;

      ec.clear();
      const auto fileSize = std::filesystem::file_size(path, ec);
      if (ec)
        return false;

      ec.clear();
      const auto writeTime = std::filesystem::last_write_time(path, ec);
      if (ec)
        return false;

      outSize = static_cast<uint64_t>(fileSize);
      outWriteTicks = static_cast<int64_t>(writeTime.time_since_epoch().count());
      return true;
    }

    uint64_t BuildPreprocessCacheKey(const std::string& normalizedPath,
                                     const MeshPreprocessCacheSettings& settings,
                                     uint64_t sourceSize,
                                     int64_t sourceWriteTicks) {
      uint64_t hash = 0xcbf29ce484222325ull;
      hash = HashValue(hash, kMeshPreprocessCacheVersion);
      hash = HashValue(hash, settings.minTrianglesForClustering);
      hash = HashValue(hash, settings.targetTrianglesPerCluster);
      hash = HashValue(hash, settings.flags);
      hash = HashValue(hash, sourceSize);
      hash = HashValue(hash, static_cast<uint64_t>(sourceWriteTicks));
      hash = HashString(hash, normalizedPath);
      return hash;
    }

    std::filesystem::path PreprocessCachePath(const std::string& sourcePath,
                                              const std::string& normalizedPath,
                                              const MeshPreprocessCacheSettings& settings,
                                              uint64_t sourceSize,
                                              int64_t sourceWriteTicks) {
      const std::filesystem::path source(sourcePath);
      std::filesystem::path parent = source.parent_path();
      if (parent.empty())
        parent = ".";

      std::string stem = source.stem().string();
      if (stem.empty())
        stem = "model";

      const uint64_t key = BuildPreprocessCacheKey(normalizedPath, settings, sourceSize, sourceWriteTicks);
      std::ostringstream name;
      name << stem << "_v" << kMeshPreprocessCacheVersion << "_"
           << std::hex << std::setw(16) << std::setfill('0') << key << ".t8mesh";
      return parent / ".t8cache" / name.str();
    }

    void WriteAABB(std::ofstream& file, const AABB& aabb) {
      WritePod(file, aabb.vMin.x);
      WritePod(file, aabb.vMin.y);
      WritePod(file, aabb.vMin.z);
      WritePod(file, aabb.vMax.x);
      WritePod(file, aabb.vMax.y);
      WritePod(file, aabb.vMax.z);
    }

    bool ReadAABB(std::ifstream& file, AABB& aabb) {
      return ReadPod(file, aabb.vMin.x)
        && ReadPod(file, aabb.vMin.y)
        && ReadPod(file, aabb.vMin.z)
        && ReadPod(file, aabb.vMax.x)
        && ReadPod(file, aabb.vMax.y)
        && ReadPod(file, aabb.vMax.z);
    }

    void WriteSubmesh(std::ofstream& file, const Submesh& submesh) {
      const uint32_t ib32Bit = submesh.ib32Bit ? 1u : 0u;
      WritePod(file, submesh.vertexStart);
      WritePod(file, submesh.vertexCount);
      WritePod(file, submesh.indexStart);
      WritePod(file, submesh.triangleCount);
      WritePod(file, submesh.materialSlot);
      WritePod(file, ib32Bit);
      WritePod(file, submesh.vertexAttribKey.bits);
      WritePod(file, submesh.firstCluster);
      WritePod(file, submesh.clusterCount);
      WriteAABB(file, submesh.localAABB);
    }

    bool ReadSubmesh(std::ifstream& file, Submesh& submesh) {
      uint32_t ib32Bit = 0;
      submesh = Submesh{};
      if (!ReadPod(file, submesh.vertexStart)
          || !ReadPod(file, submesh.vertexCount)
          || !ReadPod(file, submesh.indexStart)
          || !ReadPod(file, submesh.triangleCount)
          || !ReadPod(file, submesh.materialSlot)
          || !ReadPod(file, ib32Bit)
          || !ReadPod(file, submesh.vertexAttribKey.bits)
          || !ReadPod(file, submesh.firstCluster)
          || !ReadPod(file, submesh.clusterCount)
          || !ReadAABB(file, submesh.localAABB)) {
        return false;
      }
      submesh.ib32Bit = ib32Bit != 0;
      return true;
    }

    void WriteCluster(std::ofstream& file, const SubmeshCluster& cluster) {
      WritePod(file, cluster.submeshIndex);
      WritePod(file, cluster.indexOffset);
      WritePod(file, cluster.indexCount);
      WriteAABB(file, cluster.localAABB);
    }

    bool ReadCluster(std::ifstream& file, SubmeshCluster& cluster) {
      cluster = SubmeshCluster{};
      return ReadPod(file, cluster.submeshIndex)
        && ReadPod(file, cluster.indexOffset)
        && ReadPod(file, cluster.indexCount)
        && ReadAABB(file, cluster.localAABB);
    }

    bool ValidateCacheRanges(const MeshPreprocessCacheData& data) {
      if (!data.rootAABB.IsValid() || data.submeshes.empty())
        return false;
      for (const Submesh& submesh : data.submeshes) {
        if (!submesh.localAABB.IsValid())
          return false;
        if (submesh.firstCluster > data.clusters.size())
          return false;
        if (submesh.clusterCount > data.clusters.size() - submesh.firstCluster)
          return false;
      }
      for (const SubmeshCluster& cluster : data.clusters) {
        if (cluster.submeshIndex >= data.submeshes.size())
          return false;
        if (cluster.indexCount == 0 || !cluster.localAABB.IsValid())
          return false;
      }
      return true;
    }
  }

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

  bool MeshAssetCache::LoadPreprocessCache(const std::string& sourcePath,
                                           const MeshPreprocessCacheSettings& settings,
                                           MeshPreprocessCacheData& outData) const {
      outData = MeshPreprocessCacheData{};

      uint64_t sourceSize = 0;
      int64_t sourceWriteTicks = 0;
      if (!SourceSignature(sourcePath, sourceSize, sourceWriteTicks))
        return false;

      const std::string normalizedPath = Normalize(sourcePath);
      const std::filesystem::path path = PreprocessCachePath(sourcePath, normalizedPath, settings, sourceSize, sourceWriteTicks);

      std::error_code existsError;
      if (!std::filesystem::exists(path, existsError))
        return false;

      std::ifstream file(path, std::ios::binary);
      if (!file.is_open()) {
        T8_LOG_INFO("[MeshAssetCache] Cannot open mesh preprocess cache '%s'", path.string().c_str());
        return false;
      }

      std::array<char, 8> magic = {};
      file.read(magic.data(), magic.size());

      uint32_t version = 0;
      uint32_t headerSize = 0;
      uint64_t cachedSourceSize = 0;
      int64_t cachedSourceWriteTicks = 0;
      uint32_t cachedMinTriangles = 0;
      uint32_t cachedTargetTriangles = 0;
      uint32_t cachedFlags = 0;
      uint32_t reserved = 0;
      uint32_t submeshCount = 0;
      uint32_t clusterCount = 0;

      const bool headerOk = file.good()
        && magic == kMeshPreprocessCacheMagic
        && ReadPod(file, version)
        && ReadPod(file, headerSize)
        && ReadPod(file, cachedSourceSize)
        && ReadPod(file, cachedSourceWriteTicks)
        && ReadPod(file, cachedMinTriangles)
        && ReadPod(file, cachedTargetTriangles)
        && ReadPod(file, cachedFlags)
        && ReadPod(file, reserved)
        && ReadPod(file, outData.vertexAttribMask)
        && ReadPod(file, outData.topologyHash)
        && ReadPod(file, outData.vertexStride)
        && ReadPod(file, outData.vertexCount)
        && ReadPod(file, outData.indexCount)
        && ReadPod(file, submeshCount)
        && ReadPod(file, clusterCount)
        && ReadAABB(file, outData.rootAABB);

      if (!headerOk
          || version != kMeshPreprocessCacheVersion
          || headerSize != kMeshPreprocessCacheHeaderSize
          || cachedSourceSize != sourceSize
          || cachedSourceWriteTicks != sourceWriteTicks
          || cachedMinTriangles != settings.minTrianglesForClustering
          || cachedTargetTriangles != settings.targetTrianglesPerCluster
          || cachedFlags != settings.flags
          || submeshCount == 0
          || submeshCount > kMaxCachedSubmeshes
          || clusterCount > kMaxCachedClusters) {
        T8_LOG_INFO("[MeshAssetCache] Ignoring stale mesh preprocess cache '%s'", path.string().c_str());
        return false;
      }

      outData.submeshes.resize(submeshCount);
      for (Submesh& submesh : outData.submeshes) {
        if (!ReadSubmesh(file, submesh)) {
          outData = MeshPreprocessCacheData{};
          T8_LOG_INFO("[MeshAssetCache] Ignoring truncated mesh preprocess cache '%s'", path.string().c_str());
          return false;
        }
      }

      outData.clusters.resize(clusterCount);
      for (SubmeshCluster& cluster : outData.clusters) {
        if (!ReadCluster(file, cluster)) {
          outData = MeshPreprocessCacheData{};
          T8_LOG_INFO("[MeshAssetCache] Ignoring truncated mesh preprocess cache '%s'", path.string().c_str());
          return false;
        }
      }

      if (!ValidateCacheRanges(outData)) {
        outData = MeshPreprocessCacheData{};
        T8_LOG_INFO("[MeshAssetCache] Ignoring invalid mesh preprocess cache '%s'", path.string().c_str());
        return false;
      }

      T8_LOG_INFO("[MeshAssetCache] Loaded mesh preprocess cache '%s' (%zu submesh(es), %zu cluster(s))",
                  path.string().c_str(), outData.submeshes.size(), outData.clusters.size());
      return true;
    }

  bool MeshAssetCache::SavePreprocessCache(const std::string& sourcePath,
                                           const MeshPreprocessCacheSettings& settings,
                                           const MeshAsset& asset) const {
      if (asset.submeshes.empty() || !asset.rootAABB.IsValid())
        return false;

      uint64_t sourceSize = 0;
      int64_t sourceWriteTicks = 0;
      if (!SourceSignature(sourcePath, sourceSize, sourceWriteTicks))
        return false;

      if (asset.submeshes.size() > kMaxCachedSubmeshes || asset.clusters.size() > kMaxCachedClusters) {
        T8_LOG_INFO("[MeshAssetCache] Mesh preprocess cache skipped for '%s': metadata is too large",
                    sourcePath.c_str());
        return false;
      }

      const std::string normalizedPath = Normalize(sourcePath);
      const std::filesystem::path path = PreprocessCachePath(sourcePath, normalizedPath, settings, sourceSize, sourceWriteTicks);

      std::error_code ec;
      std::filesystem::create_directories(path.parent_path(), ec);
      if (ec) {
        T8_LOG_INFO("[MeshAssetCache] Cannot create mesh preprocess cache dir '%s': %s",
                    path.parent_path().string().c_str(), ec.message().c_str());
        return false;
      }

      std::filesystem::path tmpPath = path;
      tmpPath += ".tmp";

      std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
      if (!file.is_open()) {
        T8_LOG_INFO("[MeshAssetCache] Cannot write mesh preprocess cache '%s'", tmpPath.string().c_str());
        return false;
      }

      const uint32_t version = kMeshPreprocessCacheVersion;
      const uint32_t headerSize = kMeshPreprocessCacheHeaderSize;
      const uint32_t reserved = 0;
      const uint32_t submeshCount = static_cast<uint32_t>(asset.submeshes.size());
      const uint32_t clusterCount = static_cast<uint32_t>(asset.clusters.size());
      const uint64_t topologyHash = BuildPreprocessTopologyHash(asset);

      file.write(kMeshPreprocessCacheMagic.data(), kMeshPreprocessCacheMagic.size());
      WritePod(file, version);
      WritePod(file, headerSize);
      WritePod(file, sourceSize);
      WritePod(file, sourceWriteTicks);
      WritePod(file, settings.minTrianglesForClustering);
      WritePod(file, settings.targetTrianglesPerCluster);
      WritePod(file, settings.flags);
      WritePod(file, reserved);
      WritePod(file, asset.vertexAttribMask);
      WritePod(file, topologyHash);
      WritePod(file, asset.vertexStride);
      WritePod(file, asset.vertexCount);
      WritePod(file, asset.indexCount);
      WritePod(file, submeshCount);
      WritePod(file, clusterCount);
      WriteAABB(file, asset.rootAABB);

      for (const Submesh& submesh : asset.submeshes)
        WriteSubmesh(file, submesh);
      for (const SubmeshCluster& cluster : asset.clusters)
        WriteCluster(file, cluster);

      file.close();
      if (!file.good()) {
        T8_LOG_INFO("[MeshAssetCache] Failed while writing mesh preprocess cache '%s'", tmpPath.string().c_str());
        std::filesystem::remove(tmpPath, ec);
        return false;
      }

      std::filesystem::remove(path, ec);
      ec.clear();
      std::filesystem::rename(tmpPath, path, ec);
      if (ec) {
        T8_LOG_INFO("[MeshAssetCache] Cannot finalize mesh preprocess cache '%s': %s",
                    path.string().c_str(), ec.message().c_str());
        std::filesystem::remove(tmpPath, ec);
        return false;
      }

      T8_LOG_INFO("[MeshAssetCache] Wrote mesh preprocess cache '%s' (%zu submesh(es), %zu cluster(s))",
                  path.string().c_str(), asset.submeshes.size(), asset.clusters.size());
      return true;
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

  std::size_t MeshAssetCache::UploadDirtyPools() {
    std::vector<VertexPool*> dirtyVertexPools;
    std::vector<IndexPool*> dirtyIndexPools;
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      dirtyVertexPools.reserve(m_vertexPools.size());
      for (auto& pool : m_vertexPools) {
        if (pool && pool->IsDirty()) dirtyVertexPools.push_back(pool.get());
      }
      dirtyIndexPools.reserve(m_indexPools.size());
      for (auto& pool : m_indexPools) {
        if (pool && pool->IsDirty()) dirtyIndexPools.push_back(pool.get());
      }
    }

    const std::size_t dirtyCount = dirtyVertexPools.size() + dirtyIndexPools.size();
    if (dirtyCount == 0) return 0;

    if (g_pBaseDriver) g_pBaseDriver->BeginResourceUploadBatch();
    for (VertexPool* pool : dirtyVertexPools) pool->EnsureUploaded();
    for (IndexPool* pool : dirtyIndexPools) pool->EnsureUploaded();
    if (g_pBaseDriver) g_pBaseDriver->EndResourceUploadBatch();

    T8_LOG_INFO("[MeshAssetCache] Uploaded %zu dirty mesh pool(s) (%zu vertex, %zu index)",
                dirtyCount, dirtyVertexPools.size(), dirtyIndexPools.size());
    return dirtyCount;
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
