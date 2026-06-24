#include <pch.h>

#include <scene/RenderQueue.h>
#include <video/BaseDriver.h>

#include <algorithm>
#include <cstring>

namespace t850 {

  void RenderQueue::Sort() {
    std::sort(m_items.begin(), m_items.end(),
              [](const DrawItem& a, const DrawItem& b) {
                return a.sortKey < b.sortKey;
              });
  }

  // ── MeshDrawStateTracker ────────────────────────────────────────

  MeshDrawStateTracker& MeshDrawStateTracker::Get() {
    static MeshDrawStateTracker instance;
    return instance;
  }

  void MeshDrawStateTracker::Begin() {
    Reset();
    m_passActive = true;
  }

  void MeshDrawStateTracker::End() {
    m_passActive = false;
    Reset();
  }

  void MeshDrawStateTracker::Reset() {
    m_lastShader = nullptr;
    for (int i = 0; i < kMaxTrackedSlots; ++i) m_lastTex[i] = nullptr;
    for (int i = 0; i < kMaxTrackedSlots; ++i) m_lastCB[i] = nullptr;
    m_lastEnv = nullptr;
    m_lastVB = nullptr;
    m_lastVBStride = 0;
    m_lastVBOffset = 0;
    m_lastVBSet = false;
    m_lastIB = nullptr;
    m_lastIBFmtSet = false;
    m_lastTopology = Topology::TRIANLE_LIST;
    m_lastTopologySet = false;
  }

  void MeshDrawStateTracker::OnShaderChanged(ShaderBase* s) {
    if (s != m_lastShader) {
      // D3D12 invariant: per-shader rootParam map differs; texture
      // and CB caches must be dropped on shader change.
      for (int i = 0; i < kMaxTrackedSlots; ++i) m_lastTex[i] = nullptr;
      for (int i = 0; i < kMaxTrackedSlots; ++i) m_lastCB[i] = nullptr;
      m_lastEnv = nullptr;
      m_lastShader = s;
    }
  }

  bool MeshDrawStateTracker::ShouldBindTexture(int slot, Texture* t) {
    if (slot < 0 || slot >= kMaxTrackedSlots) return true;
    if (m_lastTex[slot] == t) return false;
    m_lastTex[slot] = t;
    return true;
  }

  bool MeshDrawStateTracker::ShouldBindEnvMap(Texture* env) {
    if (m_lastEnv == env) return false;
    m_lastEnv = env;
    return true;
  }

  bool MeshDrawStateTracker::ShouldBindIB(IndexBuffer* ib, IndexBufferFormat::E fmt) {
    if (m_lastIBFmtSet && m_lastIB == ib && m_lastIBFmt == fmt) return false;
    m_lastIB = ib;
    m_lastIBFmt = fmt;
    m_lastIBFmtSet = true;
    return true;
  }

  bool MeshDrawStateTracker::ShouldSetTopology(Topology::E topology) {
    if (m_lastTopologySet && m_lastTopology == topology) return false;
    m_lastTopology = topology;
    m_lastTopologySet = true;
    return true;
  }

  bool MeshDrawStateTracker::ShouldBindVertexBuffer(VertexBuffer* vb,
                                                    unsigned stride,
                                                    unsigned offset) {
    if (m_lastVBSet && m_lastVB == vb &&
        m_lastVBStride == stride && m_lastVBOffset == offset)
      return false;
    m_lastVB = vb;
    m_lastVBStride = stride;
    m_lastVBOffset = offset;
    m_lastVBSet = true;
    return true;
  }

  unsigned MeshDrawStateTracker::BindIndexedGeometry(DeviceContext& deviceContext,
                                                     VertexBuffer* vb,
                                                     unsigned stride,
                                                     unsigned offset,
                                                     IndexBuffer* ib,
                                                     IndexBufferFormat::E fmt,
                                                     Topology::E topology) {
    if (!vb || !ib)
      return 0;

    if (!m_passActive)
      Reset();

    unsigned changes = 0;
    if (ShouldSetTopology(topology)) {
      deviceContext.SetPrimitiveTopology(topology);
      ++changes;
    }
    if (ShouldBindVertexBuffer(vb, stride, offset)) {
      vb->Set(deviceContext, stride, offset);
      ++changes;
    }
    if (ShouldBindIB(ib, fmt)) {
      ib->Set(deviceContext, 0, fmt);
      ++changes;
    }
    return changes;
  }

  bool MeshDrawStateTracker::UpdateAndBindConstantBuffer(const DeviceContext& deviceContext,
                                                         ConstantBuffer* cb,
                                                         unsigned int slot,
                                                         const void* data,
                                                         std::size_t byteSize) {
    if (!cb || !data || byteSize == 0)
      return false;

    const bool contentsChanged = cb->sysMemCpy.size() != byteSize ||
      (byteSize > 0 && std::memcmp(cb->sysMemCpy.data(), data, byteSize) != 0);

    if (contentsChanged)
      cb->UpdateFromBuffer(deviceContext, data);

    const bool slotTracked = slot < static_cast<unsigned int>(kMaxTrackedSlots);
    const bool bindChanged = !slotTracked || m_lastCB[slot] != cb;
    if (contentsChanged || bindChanged) {
      cb->Set(deviceContext, slot);
      if (slotTracked)
        m_lastCB[slot] = cb;
      return true;
    }

    return false;
  }

}
