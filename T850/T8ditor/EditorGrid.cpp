/*********************************************************
* T8ditor — XZ-plane reference grid. See header.
*********************************************************/

#include "EditorGrid.h"

#include <utils/Log.h>
#include <utils/xMaths.h>

#include <vector>

namespace t8ditor {

bool EditorGrid::Create(int halfExtent, float spacing) {
  if (m_minorVB) return true; // already created

  if (halfExtent < 1) halfExtent = 1;
  if (spacing  <= 0.0f) spacing = 1.0f;

  const float extent = halfExtent * spacing;

  // ── Minor grid (skip the two axis lines — the axes pass draws those) ──
  std::vector<float>          verts; // xyzw per vertex
  std::vector<unsigned short> idx;
  verts.reserve((halfExtent * 2 + 1) * 4 * 4);
  idx.reserve((halfExtent * 2 + 1) * 4);

  for (int i = -halfExtent; i <= halfExtent; ++i) {
    if (i == 0) continue; // axis lines are drawn separately, in colour

    const float p = i * spacing;

    // Line parallel to X (varies in X, fixed Z)
    unsigned short base = (unsigned short)(verts.size() / 4);
    verts.push_back(-extent); verts.push_back(0.0f); verts.push_back(p); verts.push_back(1.0f);
    verts.push_back( extent); verts.push_back(0.0f); verts.push_back(p); verts.push_back(1.0f);
    idx.push_back(base);
    idx.push_back((unsigned short)(base + 1));

    // Line parallel to Z (varies in Z, fixed X)
    base = (unsigned short)(verts.size() / 4);
    verts.push_back(p); verts.push_back(0.0f); verts.push_back(-extent); verts.push_back(1.0f);
    verts.push_back(p); verts.push_back(0.0f); verts.push_back( extent); verts.push_back(1.0f);
    idx.push_back(base);
    idx.push_back((unsigned short)(base + 1));
  }

  m_minorVB = EditorLineRenderer::CreatePositionVB(verts.data(),
                                                   (unsigned)(verts.size() / 4));
  m_minorIB = EditorLineRenderer::CreateIndexBuffer16(idx.data(), (unsigned)idx.size());
  m_minorIndexCount = (unsigned)idx.size();

  // ── Principal axes (X red, Z blue) — separate IBs so each axis gets
  //    its own colour. They share one VB with the four endpoints.
  const float axesVerts[] = {
    -extent, 0.0f, 0.0f, 1.0f,   // 0: X-
     extent, 0.0f, 0.0f, 1.0f,   // 1: X+
     0.0f, 0.0f, -extent, 1.0f,  // 2: Z-
     0.0f, 0.0f,  extent, 1.0f,  // 3: Z+
  };
  const unsigned short xAxisIdx[] = { 0, 1 };
  const unsigned short zAxisIdx[] = { 2, 3 };
  m_axesVB = EditorLineRenderer::CreatePositionVB(axesVerts, 4);
  m_xAxisIB = EditorLineRenderer::CreateIndexBuffer16(xAxisIdx, 2);
  m_zAxisIB = EditorLineRenderer::CreateIndexBuffer16(zAxisIdx, 2);

  if (!m_minorVB || !m_minorIB || !m_axesVB || !m_xAxisIB || !m_zAxisIB) {
    T8_LOG_ERROR("[T8ditor] EditorGrid: VB/IB creation failed");
    Destroy();
    return false;
  }

  T8_LOG_INFO("[T8ditor] EditorGrid ready (halfExtent=%d, spacing=%.2f)",
              halfExtent, spacing);
  return true;
}

void EditorGrid::Destroy() {
  // Buffer ownership is held by the driver; reset our handles so subsequent
  // Create() calls can rebuild without leaking references.
  m_minorVB = nullptr;
  m_minorIB = nullptr;
  m_axesVB  = nullptr;
  m_xAxisIB = nullptr;
  m_zAxisIB = nullptr;
  m_minorIndexCount = 0;
}

void EditorGrid::Draw(EditorLineRenderer& lines, const XMATRIX44& vp) {
  if (!lines.IsReady()) return;

  XMATRIX44 world;
  XMatIdentity(world);

  if (m_minorVB && m_minorIB && m_minorIndexCount) {
    lines.DrawLines(world, vp, MinorColor, m_minorVB, m_minorIB,
                    m_minorIndexCount, /*stride=*/16);
  }
  if (m_axesVB && m_xAxisIB) {
    lines.DrawLines(world, vp, XAxisColor, m_axesVB, m_xAxisIB, 2, /*stride=*/16);
  }
  if (m_axesVB && m_zAxisIB) {
    lines.DrawLines(world, vp, ZAxisColor, m_axesVB, m_zAxisIB, 2, /*stride=*/16);
  }
}

} // namespace t8ditor
