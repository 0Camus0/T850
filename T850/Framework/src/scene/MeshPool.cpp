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
      return;
    }
    BufferDesc desc;
    desc.byteWidth = static_cast<int>(m_cpuStaging.size());
    desc.usage = BufferUsage::DEFAULT;
    m_gpuVB = (VertexBuffer*)T8Device->CreateBuffer(BufferType::VERTEX, desc, m_cpuStaging.data());
    m_dirty = false;
  }

  VertexBuffer* VertexPool::GetGPUBuffer() {
    EnsureUploaded();
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
      return;
    }
    BufferDesc desc;
    desc.byteWidth = static_cast<int>(m_cpuStaging.size());
    desc.usage = BufferUsage::DEFAULT;
    m_gpuIB = (IndexBuffer*)T8Device->CreateBuffer(BufferType::INDEX, desc, m_cpuStaging.data());
    m_dirty = false;
  }

  IndexBuffer* IndexPool::GetGPUBuffer() {
    EnsureUploaded();
    return m_gpuIB;
  }

}
