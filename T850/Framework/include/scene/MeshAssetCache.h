/*********************************************************
 * MeshAssetCache — path-based dedup for MeshAsset.
 *
 * Phase A scaffolding: process-wide singleton keyed by
 * normalized source path. Acquire() returns an existing
 * asset or creates a new (empty) one and bumps refcount.
 * Release() decrements; the asset and its GPU resources
 * are destroyed at zero.
 *
 * In Phase A step 1 this is created but not yet called by
 * RenderMesh. Step 2 hooks RenderMesh::Load to populate
 * the cache and step 3 makes RenderMesh borrow VB/IB from
 * the cached asset rather than owning its own.
 *********************************************************/

#ifndef T850_MESH_ASSET_CACHE_H
#define T850_MESH_ASSET_CACHE_H

#include <scene/MeshAsset.h>
#include <scene/MeshPool.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace t850 {
  struct MeshPreprocessCacheSettings {
    uint32_t minTrianglesForClustering = 0;
    uint32_t targetTrianglesPerCluster = 0;
    uint32_t flags = 0;
  };

  struct MeshPreprocessCacheData {
    uint64_t vertexAttribMask = 0;
    uint32_t vertexStride = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    AABB rootAABB;
    std::vector<Submesh> submeshes;
    std::vector<SubmeshCluster> clusters;
  };

  class MeshAssetCache {
  public:
    static MeshAssetCache& Get();

    // Returns the cached asset for `sourcePath` if present, otherwise
    // inserts a new empty MeshAsset and returns it. The caller is
    // expected to populate fields on first acquisition (refCount==1).
    // `outCreated` is set to true when a new entry was inserted.
    MeshAsset* Acquire(const std::string& sourcePath, bool* outCreated = nullptr);

    // Look up without bumping refcount. Returns nullptr if not present.
    MeshAsset* Find(const std::string& sourcePath);

    // Decrement refcount. When it reaches zero the asset and its GPU
    // resources are destroyed and the entry removed.
    void Release(MeshAsset* asset);

    // ── Phase A.5 pool API ───────────────────────────────────────────
    // Look up (or create) the pool that holds vertices of the given
    // (formatHash, vertexStride) combination. Returned pointer remains
    // valid for the life of the cache. `outPoolId` is the index used
    // by Submesh::vbAlloc.poolId.
    VertexPool* GetOrCreateVertexPool(uint64_t formatHash, uint32_t vertexStride, uint32_t* outPoolId);
    IndexPool*  GetOrCreateIndexPool(bool ib32Bit, uint32_t* outPoolId);

    VertexPool* GetVertexPool(uint32_t poolId);
    IndexPool*  GetIndexPool(uint32_t poolId);

    // Upload every dirty mesh pool explicitly after asset population.
    // Returns the number of pools that were dirty at the start of the pass.
    std::size_t UploadDirtyPools();

    // Diagnostics.
    std::size_t Size() const;
    void DumpToLog() const;

    bool LoadPreprocessCache(const std::string& sourcePath,
                             const MeshPreprocessCacheSettings& settings,
                             MeshPreprocessCacheData& outData) const;
    bool SavePreprocessCache(const std::string& sourcePath,
                             const MeshPreprocessCacheSettings& settings,
                             const MeshAsset& asset) const;

    // Test/teardown only — destroy all assets unconditionally.
    void Clear();

  private:
    MeshAssetCache() = default;
    ~MeshAssetCache();
    MeshAssetCache(const MeshAssetCache&) = delete;
    MeshAssetCache& operator=(const MeshAssetCache&) = delete;

    static std::string Normalize(const std::string& path);
    void DestroyAsset(MeshAsset* asset);

    mutable std::mutex                                                m_mutex;
    std::unordered_map<std::string, std::unique_ptr<MeshAsset>>       m_assets;

    // Pools are append-only; vertexPoolKey() builds the dedup key.
    static uint64_t VertexPoolKey(uint64_t formatHash, uint32_t vertexStride);
    std::vector<std::unique_ptr<VertexPool>>                          m_vertexPools;
    std::unordered_map<uint64_t, uint32_t>                            m_vertexPoolIndex;  // key → poolId
    std::vector<std::unique_ptr<IndexPool>>                           m_indexPools;        // [0]=16-bit, [1]=32-bit (lazy)
    int32_t                                                           m_indexPool16 = -1;
    int32_t                                                           m_indexPool32 = -1;
  };
}

#endif // T850_MESH_ASSET_CACHE_H
