#include <pch.h>

#include <scene/RenderQueue.h>

#include <algorithm>

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
    m_lastEnv = nullptr;
    m_lastIB = nullptr;
    m_lastIBFmtSet = false;
  }

  void MeshDrawStateTracker::OnShaderChanged(ShaderBase* s) {
    if (s != m_lastShader) {
      // D3D12 invariant: per-shader rootParam map differs; texture
      // cache must be dropped on shader change.
      for (int i = 0; i < kMaxTrackedSlots; ++i) m_lastTex[i] = nullptr;
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

}
