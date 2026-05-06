#include <pch.h>

#include <scene/MeshPool.h>
#include <utils/Log.h>

namespace t850 {

  extern Device* T8Device;

  VertexPool::VertexPool(uint64_t formatHash, uint32_t vertexStride)
    : m_formatHash(formatHash), m_vertexStride(vertexStride) {
  }

  VertexPool::~VertexPool() {
    if (m_gpuVB) m_gpuVB->release();
  }

  uint32_t VertexPool::Suballocate(const void* data, uint32_t byteCount) {
    uint32_t baseBytes = static_cast<uint32_t>(m_cpuStaging.size());
    m_cpuStaging.resize(baseBytes + byteCount);
    if (data && byteCount) std::memcpy(m_cpuStaging.data() + baseBytes, data, byteCount);
    m_dirty = true;
    m_dirtyAccessLogged = false;
    ++m_stagingVersion;
    return baseBytes / m_vertexStride;
  }

  void VertexPool::EnsureUploaded() {
    if (!m_dirty) return;
    if (m_gpuVB) {
      m_gpuVB->release();
      m_gpuVB = nullptr;
    }
    if (m_cpuStaging.empty()) {
      m_dirty = false;
      m_dirtyAccessLogged = false;
      m_uploadedVersion = m_stagingVersion;
      return;
    }
    BufferDesc desc;
    desc.byteWidth = static_cast<int>(m_cpuStaging.size());
    desc.usage = BufferUsage::DEFAULT;
    m_gpuVB = (VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, desc, m_cpuStaging.data());
    m_dirty = false;
    m_dirtyAccessLogged = false;
    m_uploadedVersion = m_stagingVersion;
  }

  VertexBuffer* VertexPool::GetGPUBuffer() {
    if (m_dirty) {
      if (!m_dirtyAccessLogged) {
        T8_LOG_ERROR("[MeshPool] VertexPool format=0x%016llX stride=%u accessed before UploadDirtyPools (stagingVersion=%llu uploadedVersion=%llu)",
                     static_cast<unsigned long long>(m_formatHash),
                     m_vertexStride,
                     static_cast<unsigned long long>(m_stagingVersion),
                     static_cast<unsigned long long>(m_uploadedVersion));
        m_dirtyAccessLogged = true;
      }
      return nullptr;
    }
    return m_gpuVB;
  }

  // ────────────────────────────────────────────────────────────────────

  IndexPool::IndexPool(bool ib32Bit) : m_ib32Bit(ib32Bit) {}

  IndexPool::~IndexPool() {
    if (m_gpuIB) m_gpuIB->release();
  }

  uint32_t IndexPool::Suballocate(const void* data, uint32_t indexCount) {
    uint32_t stride = IndexStride();
    uint32_t baseBytes = static_cast<uint32_t>(m_cpuStaging.size());
    m_cpuStaging.resize(baseBytes + indexCount * stride);
    if (data && indexCount) std::memcpy(m_cpuStaging.data() + baseBytes, data, indexCount * stride);
    m_dirty = true;
    m_dirtyAccessLogged = false;
    ++m_stagingVersion;
    return baseBytes / stride;
  }

  void IndexPool::EnsureUploaded() {
    if (!m_dirty) return;
    if (m_gpuIB) {
      m_gpuIB->release();
      m_gpuIB = nullptr;
    }
    if (m_cpuStaging.empty()) {
      m_dirty = false;
      m_dirtyAccessLogged = false;
      m_uploadedVersion = m_stagingVersion;
      return;
    }
    BufferDesc desc;
    desc.byteWidth = static_cast<int>(m_cpuStaging.size());
    desc.usage = BufferUsage::DEFAULT;
    m_gpuIB = (IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, desc, m_cpuStaging.data());
    m_dirty = false;
    m_dirtyAccessLogged = false;
    m_uploadedVersion = m_stagingVersion;
  }

  IndexBuffer* IndexPool::GetGPUBuffer() {
    if (m_dirty) {
      if (!m_dirtyAccessLogged) {
        T8_LOG_ERROR("[MeshPool] IndexPool %s accessed before UploadDirtyPools (stagingVersion=%llu uploadedVersion=%llu)",
                     m_ib32Bit ? "32-bit" : "16-bit",
                     static_cast<unsigned long long>(m_stagingVersion),
                     static_cast<unsigned long long>(m_uploadedVersion));
        m_dirtyAccessLogged = true;
      }
      return nullptr;
    }
    return m_gpuIB;
  }

}
