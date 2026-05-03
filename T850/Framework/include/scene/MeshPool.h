/*********************************************************
 * VertexPool / IndexPool — Tier 1 storage pools for the
 * per-format / per-IB-width sharing of GPU buffers across
 * MeshAssets.
 *
 * Phase A.5 step 1: pools are populated by RenderMesh::Create
 * but not yet bound by RenderMesh::Draw. Memory roughly
 * doubles temporarily until step 2 retires the per-asset
 * buffers.
 *
 * Pools own a CPU staging vector that bump-allocates on
 * Suballocate; the GPU buffer is created/recreated on the
 * next EnsureUploaded() call (lazy upload). This works
 * cleanly for the load-once lifecycle today; runtime
 * additions trigger a re-upload.
 *********************************************************/

#ifndef T850_MESH_POOL_H
#define T850_MESH_POOL_H

#include <Config.h>
#include <video/BaseDriver.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace t850 {

  class VertexPool {
  public:
    VertexPool(uint64_t formatHash, uint32_t vertexStride);
    ~VertexPool();

    uint64_t FormatHash()    const { return m_formatHash; }
    uint32_t VertexStride()  const { return m_vertexStride; }
    uint32_t VertexCount()   const { return static_cast<uint32_t>(m_cpuStaging.size() / m_vertexStride); }
    uint32_t UsedBytes()     const { return static_cast<uint32_t>(m_cpuStaging.size()); }
    VertexBuffer* GetGPUBuffer();      // lazy upload

    // Append `byteCount` bytes from `data` to the pool. Returns the
    // vertex offset (in vertices) where this block starts.
    uint32_t Suballocate(const void* data, uint32_t byteCount);

    void EnsureUploaded();

  private:
    uint64_t              m_formatHash;
    uint32_t              m_vertexStride;
    std::vector<uint8_t>  m_cpuStaging;
    VertexBuffer*         m_gpuVB = nullptr;
    bool                  m_dirty = false;
  };

  class IndexPool {
  public:
    explicit IndexPool(bool ib32Bit);
    ~IndexPool();

    bool     Is32Bit()    const { return m_ib32Bit; }
    uint32_t IndexCount() const { return static_cast<uint32_t>(m_cpuStaging.size() / IndexStride()); }
    uint32_t UsedBytes()  const { return static_cast<uint32_t>(m_cpuStaging.size()); }
    uint32_t IndexStride() const { return m_ib32Bit ? sizeof(uint32_t) : sizeof(uint16_t); }
    IndexBuffer* GetGPUBuffer();

    // Append `indexCount` indices (of the configured width) from `data`.
    // Returns the index offset (in indices) where this block starts.
    uint32_t Suballocate(const void* data, uint32_t indexCount);

    void EnsureUploaded();

  private:
    bool                  m_ib32Bit;
    std::vector<uint8_t>  m_cpuStaging;
    IndexBuffer*          m_gpuIB = nullptr;
    bool                  m_dirty = false;
  };

}

#endif // T850_MESH_POOL_H
